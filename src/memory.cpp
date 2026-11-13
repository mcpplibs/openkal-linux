#include "sys.h"
#include <openkal/memory.h>

// Clause 7.3 requires that where the environment already provides an
// allocator, this one be built upon it and not beside it. The environment of
// this implementation is the kernel, and the kernel provides mappings rather
// than an allocator, so an allocator is what this file supplies.
//
// The hazard the clause names is nevertheless worth checking against rather
// than dismissing. It is two independent claimants upon one region, on a
// system whose heap grows by extending a single region --- that is, `brk'. No
// allocation here uses `brk'. A program that contains both this allocator and
// a C library's therefore has two allocators drawing on disjoint mappings, and
// neither can shorten the other's.
//
// Version 0.4 called the host C library's `malloc'. That is correct for a
// program above a C library and wrong for a program that is one: the ported
// library defines `malloc' too, so the call resolves to it, and it in turn
// calls this implementation. See src/sys.h.

namespace okl { okl_ulong auxval(okl_ulong key); }

namespace {


constexpr okl_uptr kPage      = 4096;
constexpr okl_uptr kMinBlock  = 16;
constexpr okl_uptr kMaxSmall  = 32768;
constexpr okl_uptr kChunk     = 1u << 20;
constexpr int      kClasses   = 12;   // 16, 32, ... 32768

// Contention is rare --- the allocations of a C library above this one are
// served by that library's own allocator, and this one sees page-granular
// requests --- so a lock that spins is adequate and needs no suspension
// primitive, which would make openkal.memory depend upon openkal.task.
struct spin {
    volatile int held = 0;
    void lock() {
        while (__atomic_exchange_n(&held, 1, __ATOMIC_ACQUIRE)) okl::sys(okl::nr_sched_yield);
    }
    void unlock() { __atomic_store_n(&held, 0, __ATOMIC_RELEASE); }
};

spin  g_lock;
void* g_free[kClasses];
// The part of each class's newest chunk no block has been carved from yet.
unsigned char* g_next[kClasses];
unsigned char* g_end[kClasses];

int class_of(okl_uptr n) {
    okl_uptr s = kMinBlock; int c = 0;
    while (s < n) { s <<= 1; ++c; }
    return c;
}
okl_uptr size_of(int c) { return kMinBlock << c; }

void* map(okl_uptr bytes) {
    const okl_long r = okl::mmap( 0, static_cast<okl_long>(bytes),
                                okl::prot_read | okl::prot_write,
                                okl::map_private | okl::map_anonymous, -1, 0);
    if (okl::failed(r)) return nullptr;
    return reinterpret_cast<void*>(r);
}

void unmap(void* p, okl_uptr bytes) {
    okl::sys(okl::nr_munmap, reinterpret_cast<okl_long>(p), static_cast<okl_long>(bytes));
}

okl_uptr round_up(okl_uptr n, okl_uptr to) { return (n + to - 1) & ~(to - 1); }

// A chunk is page-aligned and a block size is a power of two no larger than a
// chunk, so a block at a multiple of its size is aligned to its size or to the
// page, whichever is smaller. That is why an alignment request up to 4096 bytes
// (the smallest page among the environments) is satisfied by choosing a larger
// class rather than by padding: no metadata is needed, and none is kept. A
// wider alignment is mapped (below), as a block of a class would not have it.
//
// A CHUNK IS CARVED AS ITS BLOCKS ARE HANDED OUT, NOT WHEN IT IS MAPPED. A free
// list threaded through the whole chunk at once wrote a pointer into each of
// its blocks, so every page of every chunk was made resident before a block of
// it was used: measured, six classes and six mebibytes resident, 1 500 page
// faults, in each program an ok image's loader started --- three quarters of
// what such a program paid before its first instruction.
void* carve(int c) {
    const okl_uptr bs = size_of(c);
    if (g_next[c] == g_end[c]) {
        auto* base = static_cast<unsigned char*>(map(kChunk));
        if (!base) return nullptr;
        g_next[c] = base;
        g_end[c] = base + kChunk;
    }
    void* b = g_next[c];
    g_next[c] += bs;
    return b;
}

// --- what the environment can give (0.23) ---------------------------------

// The whole of a small file of the kernel's, into `b'; its length, or 0.
okl_uptr read_small(const char* path, char* b, okl_uptr cap) {
    const okl_long fd = okl::openat( okl::at_fdcwd, reinterpret_cast<okl_long>(path),
                                 okl::o_rdonly | okl::o_cloexec, 0);
    if (okl::failed(fd)) return 0;
    okl_uptr n = 0;
    while (n + 1 < cap) {
        const okl_long r = okl::sys(okl::nr_read, fd, reinterpret_cast<okl_long>(b + n),
                                    static_cast<okl_long>(cap - 1 - n));
        if (r == -okl::e_intr) continue;
        if (r <= 0) break;
        n += static_cast<okl_uptr>(r);
    }
    okl::sys(okl::nr_close, fd);
    b[n] = '\0';
    return n;
}

// The number after `key' in `b' (a line `key:   123 kB'), in bytes when the
// line says kB; false where it is absent.
bool field_bytes(const char* b, const char* key, okl_u64& out) {
    for (const char* p = b; *p; ) {
        const char* k = key; const char* q = p;
        while (*k && *q == *k) { ++q; ++k; }
        if (*k == '\0' && *q == ':') {
            ++q; while (*q == ' ' || *q == '\t') ++q;
            okl_u64 v = 0; bool any = false;
            while (*q >= '0' && *q <= '9') { v = v * 10 + static_cast<okl_u64>(*q++ - '0'); any = true; }
            while (*q == ' ') ++q;
            if (q[0] == 'k' && q[1] == 'B') v *= 1024;
            out = v;
            return any;
        }
        while (*p && *p != '\n') ++p;
        if (*p) ++p;
    }
    return false;
}

bool read_meminfo(okl_u64& total, okl_u64& available) {
    char b[4096];
    if (read_small("/proc/meminfo", b, sizeof b) == 0) return false;
    if (!field_bytes(b, "MemTotal", total)) return false;
    if (!field_bytes(b, "MemAvailable", available)) {
        okl_u64 free = 0, cached = 0;
        field_bytes(b, "MemFree", free);
        field_bytes(b, "Cached", cached);
        available = free + cached;
    }
    return true;
}

// The limit of the cgroup-v2 group this program is in, and its use; false
// where there is none (`max', no unified hierarchy, or it cannot be read).
bool group_limit(okl_u64& limit, okl_u64& used) {
    char cg[1024];
    const okl_uptr n = read_small("/proc/self/cgroup", cg, sizeof cg);
    if (n == 0) return false;
    // The unified hierarchy's line: `0::<path>'.
    const char* path = nullptr;
    for (const char* p = cg; *p; ) {
        if (p[0] == '0' && p[1] == ':' && p[2] == ':') { path = p + 3; break; }
        while (*p && *p != '\n') ++p;
        if (*p) ++p;
    }
    if (!path) return false;
    char file[1200] = "/sys/fs/cgroup";
    okl_uptr o = 14;
    for (const char* p = path; *p && *p != '\n' && o + 20 < sizeof file; ++p) file[o++] = *p;
    if (o > 0 && file[o - 1] == '/') --o;
    auto value = [&](const char* leaf, okl_u64& out) {
        okl_uptr k = o;
        for (const char* l = leaf; *l && k + 1 < sizeof file; ++l) file[k++] = *l;
        file[k] = '\0';
        char b[64];
        if (read_small(file, b, sizeof b) == 0 || b[0] < '0' || b[0] > '9') return false;
        okl_u64 v = 0;
        for (const char* q = b; *q >= '0' && *q <= '9'; ++q) v = v * 10 + static_cast<okl_u64>(*q - '0');
        out = v;
        return true;
    };
    if (!value("/memory.max", limit)) return false;
    if (!value("/memory.current", used)) used = 0;
    return true;
}

}  // namespace

extern "C" {

void* kal_alloc(kal_uintptr size, kal_uintptr align) {
    if (size == 0) return nullptr;
    if (align < kMinBlock) align = kMinBlock;

    okl_uptr want = size < align ? align : size;
    if (want <= kMaxSmall && align <= 4096) {
        const int c = class_of(want);
        g_lock.lock();
        void* b = g_free[c];
        if (b) g_free[c] = *reinterpret_cast<void**>(b);
        else b = carve(c);
        g_lock.unlock();
        return b;
    }

    const okl_uptr bytes = round_up(size, kPage);
    if (align <= kPage) return map(bytes);

    // An alignment wider than a page is satisfied by mapping more and
    // recording, immediately before the region returned, what must be
    // released. The record is reachable because the returned address is at
    // least sixteen bytes above the mapping's base by construction.
    const okl_uptr total = bytes + align;
    auto* base = static_cast<unsigned char*>(map(total));
    if (!base) return nullptr;
    auto  addr = reinterpret_cast<okl_uptr>(base) + 16;
    addr = (addr + align - 1) & ~(align - 1);
    auto* user = reinterpret_cast<unsigned char*>(addr);
    reinterpret_cast<okl_uptr*>(user)[-1] = total;
    reinterpret_cast<okl_uptr*>(user)[-2] = reinterpret_cast<okl_uptr>(base);
    return user;
}

// The size and alignment are those passed to the allocation. The interface
// carries them so that an implementation need keep no record of its own, and
// this one keeps none: the class is recomputed from the size, and only the
// over-aligned case --- where the returned address is not the mapping's ---
// records anything at all.
void kal_free(void* p, kal_uintptr size, kal_uintptr align) {
    if (!p || size == 0) return;
    if (align < kMinBlock) align = kMinBlock;

    const okl_uptr want = size < align ? align : size;
    if (want <= kMaxSmall && align <= 4096) {
        const int c = class_of(want);
        g_lock.lock();
        *reinterpret_cast<void**>(p) = g_free[c];
        g_free[c] = p;
        g_lock.unlock();
        return;
    }

    if (align <= kPage) { unmap(p, round_up(size, kPage)); return; }

    auto* user = static_cast<unsigned char*>(p);
    const okl_uptr total = reinterpret_cast<okl_uptr*>(user)[-1];
    auto*  base = reinterpret_cast<void*>(reinterpret_cast<okl_uptr*>(user)[-2]);
    unmap(base, total);
}


// The quantum this environment allocates and protects memory in.
//
// AN OPERATION AND NOT A CONSTANT, BECAUSE IT IS A PROPERTY OF THE MACHINE THE
// PROGRAM RUNS ON. A C library above this reports it as its own page size; one
// that fixed it when it was built is wrong on every machine whose quantum
// differs from the one it was built for, which is what a distributed binary
// meets --- sixteen kilobytes on one family of hardware, sixty-four on another.
//
// The kernel states it in the auxiliary vector it leaves at inception. Where
// that vector is absent --- a program whose entry did not record it --- the
// value falls back to the architecture's smallest page, which is the smallest
// quantum this kernel ever uses and is therefore never coarser than the truth.
//
// One number, and it is the coarsest that is always safe: this kernel allocates
// and protects in the same unit, so the two are the same value here. An
// implementation on a system where they differ reports the coarser.
kal_uintptr kal_memory_granularity(void) {
    const okl_ulong page = okl::auxval(6 /* AT_PAGESZ */);
    return page != 0 ? static_cast<kal_uintptr>(page) : kPage;
}

// WHAT THE ENVIRONMENT CAN GIVE (0.23): the machine's memory and what the
// kernel estimates it can give without taking any from another program
// (MemTotal, MemAvailable), each bounded by the limit of the control group the
// program is in, and what that group already uses.
int kal_memory_info(kal_memory_status* out) {
    if (out == nullptr || out->self_size < 2 * sizeof(kal_u32)) return kal_err_invalid;
    const kal_u32 self = out->self_size;
    kal_memory_status v{};
    v.self_size = self;
    okl_u64 total = 0, available = 0;
    if (read_meminfo(total, available)) v.present = KAL_MSTATUS_TOTAL | KAL_MSTATUS_AVAILABLE;
    okl_u64 limit = 0, used = 0;
    if (group_limit(limit, used)) {
        if (limit < total || total == 0) total = limit;
        const okl_u64 room = limit > used ? limit - used : 0;
        if (room < available || !(v.present & KAL_MSTATUS_AVAILABLE)) available = room;
        v.present = KAL_MSTATUS_TOTAL | KAL_MSTATUS_AVAILABLE;
    }
    v.total_bytes = total;
    v.available_bytes = available < total ? available : total;
    const kal_u32 n = self < sizeof v ? self : static_cast<kal_u32>(sizeof v);
    okl::copy(reinterpret_cast<char*>(out), reinterpret_cast<const char*>(&v), n);
    return kal_ok;
}

}
