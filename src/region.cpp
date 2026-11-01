// openkal.region (openkal 0.20): a range of the address space reserved first
// and backed as it is used.
//
// THIS KERNEL CHARGES BACKING WHEN A PAGE IS FIRST WRITTEN, UNLESS IT HAS BEEN
// TOLD NOT TO. Its default (vm.overcommit_memory 0 or 1) lets a private mapping
// be made larger than the memory it may ever have, and a page is found when it
// is touched; that is KAL_REGION_PROP_OVERCOMMIT, and a reservation is then a
// mapping that may be used at once, `commit' makes no call, and returning the
// backing is `madvise(MADV_DONTNEED)', after which the part reads as zero.
//
// Told not to (vm.overcommit_memory 2), the kernel charges a writable private
// mapping when it is made. A reservation is then a mapping that may not be
// accessed (PROT_NONE, which is not charged), backing is `mprotect' to
// read-write (which charges it), and returning it is a fresh inaccessible
// mapping placed over the part (which uncharges it). The mode is read once,
// from /proc, and the property word states which of the two this program met.
//
// Discarding is `madvise(MADV_FREE)': the kernel takes the pages back when it
// wants them and not before, and a part written meanwhile keeps what was
// written. A kernel before 4.5 refuses the advice, and the part's backing is
// returned instead, which is also a permitted outcome (the contents are not
// specified).
#include "sys.h"
#include <openkal/memory.h>
#include <openkal/region.h>

namespace {

// -1 not yet read; 0 the kernel overcommits; 1 it charges at mapping.
int g_strict = -1;

bool strict() {
    int v = __atomic_load_n(&g_strict, __ATOMIC_ACQUIRE);
    if (v >= 0) return v == 1;
    v = 0;
    const okl_long fd = okl::sys(okl::nr_openat, okl::at_fdcwd,
                                 reinterpret_cast<okl_long>("/proc/sys/vm/overcommit_memory"),
                                 okl::o_rdonly | okl::o_cloexec, 0);
    if (!okl::failed(fd)) {
        char c = 0;
        if (okl::sys(okl::nr_read, fd, reinterpret_cast<okl_long>(&c), 1) == 1 && c == '2') v = 1;
        okl::sys(okl::nr_close, fd);
    }
    __atomic_store_n(&g_strict, v, __ATOMIC_RELEASE);
    return v == 1;
}

okl_uptr granularity() { return static_cast<okl_uptr>(kal_memory_granularity()); }

bool multiple(okl_uptr v, okl_uptr g) { return (v & (g - 1)) == 0; }

okl_long map_at(void* at, okl_uptr len, int prot, int extra) {
    return okl::sys(okl::nr_mmap, reinterpret_cast<okl_long>(at), static_cast<okl_long>(len), prot,
                    okl::map_private | okl::map_anonymous | extra, -1, 0);
}

}  // namespace

extern "C" {

void* kal_region_reserve(kal_uintptr size, kal_uintptr align, kal_uintptr flags) {
    const okl_uptr g = granularity();
    if (size == 0 || !multiple(size, g) || align < g || (align & (align - 1)) != 0) return nullptr;
    if ((flags & ~KAL_REGION_COMMIT) != 0) return nullptr;
    const bool usable = !strict() || (flags & KAL_REGION_COMMIT) != 0;
    const int prot = usable ? (okl::prot_read | okl::prot_write) : okl::prot_none;
    const int extra = strict() ? 0 : okl::map_noreserve;
    // More than asked for, by the alignment less one granule, and the two ends
    // that are not needed released: the kernel places a mapping at its own
    // granularity and knows nothing of the caller's.
    const okl_uptr total = size + align - g;
    if (total < size) return nullptr;
    const okl_long r = map_at(nullptr, total, prot, extra);
    if (okl::failed(r)) return nullptr;
    const okl_uptr base = static_cast<okl_uptr>(r);
    const okl_uptr at = (base + align - 1) & ~(align - 1);
    if (at > base) okl::sys(okl::nr_munmap, static_cast<okl_long>(base), static_cast<okl_long>(at - base));
    const okl_uptr end = at + size, mapped_end = base + total;
    if (mapped_end > end) okl::sys(okl::nr_munmap, static_cast<okl_long>(end), static_cast<okl_long>(mapped_end - end));
    return reinterpret_cast<void*>(at);
}

int kal_region_commit(void* addr, kal_uintptr len) {
    const okl_uptr g = granularity(), a = reinterpret_cast<okl_uptr>(addr);
    if (addr == nullptr || !multiple(a, g) || !multiple(len, g)) return kal_err_invalid;
    if (!strict() || len == 0) return kal_ok;
    const okl_long r = okl::sys(okl::nr_mprotect, static_cast<okl_long>(a), static_cast<okl_long>(len),
                                okl::prot_read | okl::prot_write);
    return okl::failed(r) ? okl::translate(r) : kal_ok;
}

int kal_region_decommit(void* addr, kal_uintptr len) {
    const okl_uptr g = granularity(), a = reinterpret_cast<okl_uptr>(addr);
    if (addr == nullptr || !multiple(a, g) || !multiple(len, g)) return kal_err_invalid;
    if (len == 0) return kal_ok;
    if (strict()) {
        const okl_long r = map_at(addr, len, okl::prot_none, okl::map_fixed);
        return okl::failed(r) ? okl::translate(r) : kal_ok;
    }
    const okl_long r = okl::sys(okl::nr_madvise, static_cast<okl_long>(a), static_cast<okl_long>(len),
                                okl::madv_dontneed);
    return okl::failed(r) ? okl::translate(r) : kal_ok;
}

int kal_region_discard(void* addr, kal_uintptr len) {
    const okl_uptr g = granularity(), a = reinterpret_cast<okl_uptr>(addr);
    if (addr == nullptr || !multiple(a, g) || !multiple(len, g)) return kal_err_invalid;
    if (len == 0) return kal_ok;
    okl_long r = okl::sys(okl::nr_madvise, static_cast<okl_long>(a), static_cast<okl_long>(len),
                          okl::madv_free);
    if (r == -okl::e_inval)
        r = okl::sys(okl::nr_madvise, static_cast<okl_long>(a), static_cast<okl_long>(len),
                     okl::madv_dontneed);
    return okl::failed(r) ? okl::translate(r) : kal_ok;
}

void kal_region_release(void* addr, kal_uintptr size) {
    if (addr == nullptr || size == 0) return;
    okl::sys(okl::nr_munmap, reinterpret_cast<okl_long>(addr), static_cast<okl_long>(size));
}

kal_uintptr kal_region_props(void) {
    return strict() ? 0u : KAL_REGION_PROP_OVERCOMMIT;
}

}
