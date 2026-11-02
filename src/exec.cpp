#include "sys.h"
#include "handle.h"
#include <openkal/exec.h>
#include <openkal/map.h>

namespace okl { okl_ulong auxval(okl_ulong key); }

// openkal.exec on this kernel.
//
// A mapping obtained writable and made executable afterwards. The kernel places
// no condition upon either step, so this implementation provides the interface
// unconditionally --- unlike one of the other systems, where the same interface
// is available only to a program produced in a particular way, which clause 6.5
// covers and which is not this system's case.
//
// The reservation is not made executable at the outset even though this kernel
// would permit it. Two of the three environments the specification targets
// refuse a mapping that is both, so an implementation that returned one here
// would be offering a program a shape it could not use elsewhere --- and the
// program would discover that only on the other system. The interface states
// the narrower contract and this implementation keeps to it.

namespace {

// The kernel's page, as it told this program at inception (AT_PAGESZ). Four
// kibibytes was written here, and an aarch64 kernel built for sixteen or
// sixty-four rounds every protection to its own page, so a part published at a
// four-kibibyte boundary would have taken its neighbour with it.
okl_uptr page() {
    static okl_uptr p = 0;
    if (p == 0) {
        const okl_ulong a = okl::auxval(6 /* AT_PAGESZ */);
        p = (a != 0 && (a & (a - 1)) == 0) ? static_cast<okl_uptr>(a) : 4096;
    }
    return p;
}

okl_uptr round_up(okl_uptr n, okl_uptr to) { return (n + to - 1) & ~(to - 1); }

}  // namespace

extern "C" {

void* kal_exec_alloc(kal_uintptr size) {
    if (size == 0) return nullptr;
    const okl_uptr bytes = round_up(static_cast<okl_uptr>(size), page());
    const okl_long r = okl::sys(okl::nr_mmap, 0, static_cast<okl_long>(bytes),
                                okl::prot_read | okl::prot_write,
                                okl::map_private | okl::map_anonymous, -1, 0);
    if (okl::failed(r)) return nullptr;
    return reinterpret_cast<void*>(r);
}

int kal_exec_publish(void* p, kal_uintptr size) {
    if (p == nullptr || size == 0) return kal_err_invalid;
    const okl_uptr bytes = round_up(static_cast<okl_uptr>(size), page());
    const okl_long r = okl::sys(okl::nr_mprotect, reinterpret_cast<okl_long>(p),
                                static_cast<okl_long>(bytes),
                                okl::prot_read | okl::prot_exec);
    if (okl::failed(r)) return okl::translate(r);

    // THE INSTRUCTION CACHE IS NOT INVALIDATED HERE, AND ONE OF THE THREE
    // IMPLEMENTATIONS DOES INVALIDATE IT. THE ASYMMETRY HAS A RULE.
    //
    // A processor with separate caches for data and instructions has just had
    // bytes written through the data path that it is about to fetch through the
    // instruction path, and nothing in the protection call makes the second path
    // observe the first's writes.
    //
    // THE SPECIFICATION PLACES THE MAINTENANCE UPON THE PROGRAM, and the
    // conformance suite performs it itself and says why: the program is the
    // party that knows which bytes it wrote. So an implementation that performs
    // it is being helpful rather than conforming, and one that does not is not
    // deficient.
    //
    // AND THE ONLY MEANS AVAILABLE HERE IS ONE THIS IMPLEMENTATION MAY NOT
    // USE. `__builtin___clear_cache' expands to nothing on x86_64 and becomes a
    // CALL into the compiler's support library on the other two architectures
    // --- `__riscv_flush_icache' on riscv64. This implementation is linked into
    // programs that carry no other runtime, so acquiring that dependency to
    // perform an operation the specification does not require of it is not a
    // trade worth making.
    //
    // MEASURED, AND NOT ON THIS SYSTEM. openkal-macos added the builtin on
    // the reading that aarch64 would expand it inline, and its own independence
    // check reported within the hour:
    //
    //     obj/exec.o references a symbol it must not: ___clear_cache
    //
    // ⇒ The three implementations share a rule rather than an accident:
    //
    //     an implementation performs the maintenance where its environment
    //     offers it as a CALL of the environment's own --- openkal-windows has
    //     `FlushInstructionCache' --- and does not where the only means is a
    //     compiler builtin that becomes a dependency upon the compiler's
    //     support library.
    //
    // All three agree about what the PROGRAM must do, which is what the
    // specification actually states.
    return kal_ok;
}

// PART OF A REGION (0.17): the kernel protects per page, so a part is a run of
// whole pages, and a part that is not is refused rather than rounded --- a
// rounded part would take writable data beside it out of reach.
int kal_exec_publish_part(void* region, kal_uintptr offset, kal_uintptr size) {
    const okl_uptr g = page();
    if (region == nullptr || size == 0 || (offset & (g - 1)) != 0 || (size & (g - 1)) != 0
        || (reinterpret_cast<okl_uptr>(region) & (g - 1)) != 0)
        return kal_err_invalid;
    const okl_long r = okl::sys(okl::nr_mprotect,
                                reinterpret_cast<okl_long>(static_cast<char*>(region) + offset),
                                static_cast<okl_long>(size), okl::prot_read | okl::prot_exec);
    return okl::failed(r) ? okl::translate(r) : kal_ok;
}

kal_uintptr kal_exec_granularity(void) { return page(); }

// A PART THAT IS A FILE'S (0.21): the file's pages mapped over the part,
// private, and executable where asked. MAP_FIXED replaces what the part held in
// one call, so there is no moment at which the addresses are free for another
// mapping to take. The arguments are checked first: a refused part leaves the
// region as it was.
int kal_exec_map_part(void* region, kal_uintptr offset, kal_uintptr size, kal_file f, kal_u64 at,
                      kal_uintptr how) {
    const okl_uptr g = page();
    const int fd = okl::unpack(f.h);
    if (region == nullptr || size == 0 || fd < 0 || (offset & (g - 1)) != 0 || (size & (g - 1)) != 0
        || (reinterpret_cast<okl_uptr>(region) & (g - 1)) != 0 || at % KAL_MAP_ALIGNMENT != 0
        || (how & ~KAL_EXEC_MAP_EXECUTE) != 0)
        return kal_err_invalid;
    okl::kstat st{};
    const okl_long s = okl::sys(okl::nr_fstat, fd, reinterpret_cast<okl_long>(&st));
    if (okl::failed(s)) return okl::translate(s);
    if ((st.mode & okl::s_ifmt) != okl::s_ifreg) return kal_err_not_supported;
    const kal_u64 length = st.size > 0 ? static_cast<kal_u64>(st.size) : 0;
    if (at > length || static_cast<kal_u64>(size) > length - at) return kal_err_invalid;
    const okl_long prot = okl::prot_read | ((how & KAL_EXEC_MAP_EXECUTE) ? okl::prot_exec : 0);
    const okl_long r = okl::sys(okl::nr_mmap,
                                reinterpret_cast<okl_long>(static_cast<char*>(region) + offset),
                                static_cast<okl_long>(size), prot, okl::map_private | okl::map_fixed, fd,
                                static_cast<okl_long>(at));
    if (okl::failed(r)) {
        // ENODEV: the file system does not map. EPERM: its files are not
        // executed (a volume mounted noexec). The resource refuses, not the
        // implementation (exec.h).
        if (-r == okl::e_nodev || -r == okl::e_perm) return kal_err_not_supported;
        return okl::translate(r);
    }
    return kal_ok;
}

void kal_exec_free(void* p, kal_uintptr size) {
    if (p == nullptr || size == 0) return;
    const okl_uptr bytes = round_up(static_cast<okl_uptr>(size), page());
    okl::sys(okl::nr_munmap, reinterpret_cast<okl_long>(p),
             static_cast<okl_long>(bytes));
}

// A published region may be reserved for writing again: this kernel's
// protection call is not one-way. The position is set accordingly, and a
// caller that must change published bytes need not abandon the region.
kal_uintptr kal_exec_props(void) {
    // Executable memory is available to every artifact on this kernel: nothing
    // here is granted only to a program produced in a particular way.
    return KAL_EXEC_PROP_REPUBLISH | KAL_EXEC_PROP_AVAILABLE | KAL_EXEC_PROP_FILE;
}

}  // extern "C"
