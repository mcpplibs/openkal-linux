#include "sys.h"
#include <openkal/time.h>

namespace okl { okl_ulong auxval(okl_ulong key); }

namespace {
kal_duration nanoseconds(const okl::ktimespec& t) {
    return static_cast<kal_duration>(t.sec) * 1000000000u
         + static_cast<kal_duration>(t.nsec);
}

// THE CLOCK THE KERNEL PLACES IN EVERY PROGRAM (openkal-linux 0.21).
//
// The kernel maps a small shared object into each program it starts --- the
// vDSO, found through the auxiliary vector (AT_SYSINFO_EHDR) --- whose
// `clock_gettime' reads the clock from a page the kernel keeps current, with no
// system call. A C library calls it; this implementation called the system call
// instead, and a compiler run as an image above it made 23 thousand of them in
// one warm start that its own build made none of. The entry is found once, by
// the object's dynamic symbol table; where there is no such object (a kernel
// started without it, an environment that hides the vector) or no such entry,
// the system call remains, as does an answer the entry declines.
using vdso_clock = int (*)(long, okl::ktimespec*);

struct elf_ehdr { unsigned char ident[16]; unsigned short type, machine; unsigned version;
                  okl_u64 entry, phoff, shoff; unsigned flags;
                  unsigned short ehsize, phentsize, phnum, shentsize, shnum, shstrndx; };
struct elf_phdr { unsigned type, flags; okl_u64 offset, vaddr, paddr, filesz, memsz, align; };
struct elf_dyn  { long long tag; okl_u64 val; };
struct elf_sym  { unsigned name; unsigned char info, other; unsigned short shndx; okl_u64 value, size; };

#if defined(__x86_64__)
constexpr const char kClockName[] = "__vdso_clock_gettime";
#else
constexpr const char kClockName[] = "__kernel_clock_gettime";
#endif

bool same_name(const char* a, const char* b) {
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}

// Symbols in a table hashed in the GNU form: the highest index any chain
// reaches, plus one, is the count; the form states no count of its own.
okl_u64 gnu_symbol_count(const unsigned* h) {
    const unsigned nbuckets = h[0], symoffset = h[1], bloom = h[2];
    const auto* buckets = h + 4 + bloom * (sizeof(okl_u64) / sizeof(unsigned));
    const unsigned* chain = buckets + nbuckets;
    unsigned last = 0;
    for (unsigned i = 0; i < nbuckets; ++i) if (buckets[i] > last) last = buckets[i];
    if (last < symoffset) return symoffset;
    while ((chain[last - symoffset] & 1u) == 0) ++last;
    return static_cast<okl_u64>(last) + 1;
}

vdso_clock find_vdso_clock() {
    const okl_ulong base = okl::auxval(33 /* AT_SYSINFO_EHDR */);
    if (base == 0) return nullptr;
    const auto* eh = reinterpret_cast<const elf_ehdr*>(base);
    if (eh->ident[0] != 0x7f || eh->ident[1] != 'E' || eh->ident[4] != 2 /* ELFCLASS64 */) return nullptr;
    const auto* ph = reinterpret_cast<const elf_phdr*>(base + eh->phoff);
    okl_u64 load = 0; bool have_load = false;
    const elf_dyn* dyn = nullptr;
    for (unsigned i = 0; i < eh->phnum; ++i) {
        if (ph[i].type == 1 /* PT_LOAD */ && !have_load) { load = base + ph[i].offset - ph[i].vaddr; have_load = true; }
        if (ph[i].type == 2 /* PT_DYNAMIC */) dyn = reinterpret_cast<const elf_dyn*>(base + ph[i].offset);
    }
    if (!have_load || dyn == nullptr) return nullptr;
    const char* strtab = nullptr; const elf_sym* symtab = nullptr;
    const unsigned* hash = nullptr; const unsigned* gnu_hash = nullptr;
    for (const elf_dyn* d = dyn; d->tag != 0; ++d) {
        switch (d->tag) {
            case 5:           strtab   = reinterpret_cast<const char*>(load + d->val); break;
            case 6:           symtab   = reinterpret_cast<const elf_sym*>(load + d->val); break;
            case 4:           hash     = reinterpret_cast<const unsigned*>(load + d->val); break;
            case 0x6ffffef5:  gnu_hash = reinterpret_cast<const unsigned*>(load + d->val); break;
            default: break;
        }
    }
    if (strtab == nullptr || symtab == nullptr || (hash == nullptr && gnu_hash == nullptr)) return nullptr;
    const okl_u64 count = hash ? hash[1] : gnu_symbol_count(gnu_hash);
    for (okl_u64 i = 0; i < count; ++i) {
        const elf_sym& s = symtab[i];
        if ((s.info & 0xf) != 2 /* STT_FUNC */ || s.shndx == 0) continue;
        if ((s.info >> 4) != 1 /* STB_GLOBAL */ && (s.info >> 4) != 2 /* STB_WEAK */) continue;
        if (same_name(strtab + s.name, kClockName)) return reinterpret_cast<vdso_clock>(load + s.value);
    }
    return nullptr;
}

// 0 not yet looked for; 1 looked for and absent; otherwise the entry.
okl_uptr g_vdso_clock = 0;

void read_clock(long clock, okl::ktimespec* t) {
    okl_uptr v = __atomic_load_n(&g_vdso_clock, __ATOMIC_ACQUIRE);
    if (v == 0) {
        const vdso_clock found = find_vdso_clock();
        v = found ? reinterpret_cast<okl_uptr>(found) : 1;
        __atomic_store_n(&g_vdso_clock, v, __ATOMIC_RELEASE);
    }
    if (v != 1 && reinterpret_cast<vdso_clock>(v)(clock, t) == 0) return;
    okl::sys(okl::nr_clock_gettime, clock, reinterpret_cast<okl_long>(t));
}
}  // namespace

extern "C" {

kal_duration kal_time_monotonic(void) {
    okl::ktimespec t{};
    read_clock(okl::clock_monotonic, &t);
    return nanoseconds(t);
}

kal_duration kal_time_wall(void) {
    okl::ktimespec t{};
    read_clock(okl::clock_realtime, &t);
    return nanoseconds(t);
}

kal_duration kal_time_monotonic_granularity(void) {
    okl::ktimespec t{};
    if (okl::failed(okl::sys(okl::nr_clock_getres, okl::clock_monotonic,
                             reinterpret_cast<okl_long>(&t)))) return 1;
    const kal_duration ns = nanoseconds(t);
    return ns == 0 ? 1 : ns;
}

void kal_time_sleep(kal_duration ns) {
    okl::ktimespec req{ static_cast<okl_i64>(ns / 1000000000u),
                        static_cast<okl_i64>(ns % 1000000000u) };
    // The specification requires that the call not return early, so an
    // interruption resumes the remainder rather than reporting it. The kernel
    // writes what is left into the same structure, which is why it is not
    // const.
    for (;;) {
        const okl_long r = okl::sys(okl::nr_nanosleep, reinterpret_cast<okl_long>(&req),
                                    reinterpret_cast<okl_long>(&req));
        if (!okl::interrupted(r)) return;
    }
}

// The monotonic clock of this kernel does not advance while the machine is
// suspended, which the corresponding property records.
kal_uintptr kal_time_props(void) {
    return KAL_TIME_PROP_WALL_AVAILABLE | KAL_TIME_PROP_MONOTONIC_SUSPENDS
         | KAL_TIME_PROP_SLEEP_PRECISE;
}

}
