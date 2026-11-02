// openkal.debug and openkal.prof for a program of this environment (0.23):
// the fault reported by what ends the program, an address named, and samples
// taken (SPEC.md clause 11, entry 56).
//
// EVERYTHING HERE THAT RUNS IN A SIGNAL HANDLER ASKS THE KERNEL DIRECTLY AND
// ALLOCATES NOTHING: a program that faulted may have done so inside an
// allocator, and one being sampled may be anywhere at all. Memory another
// context could have left inconsistent is never read; memory that may not be
// mapped --- a frame pointer of a broken chain --- is read through
// `process_vm_readv', which answers EFAULT where a load would fault again.
#include "sys.h"
#include "rights.h"
#include <openkal/debug.h>
#include <openkal/exec.h>
#include <openkal/prof.h>
#include <openkal/task.h>

// The return from a handler: this kernel requires the restorer of a handler
// installed without a C library to be named.
#if defined(__x86_64__)
extern "C" void okl_signal_return();
__asm__(".text\n.globl okl_signal_return\n.type okl_signal_return,@function\nokl_signal_return:\n"
        "  mov $15, %eax\n  syscall\n  hlt\n");
#elif defined(__aarch64__)
extern "C" void okl_signal_return();
__asm__(".text\n.globl okl_signal_return\n.type okl_signal_return,@function\nokl_signal_return:\n"
        "  mov x8, #139\n  svc #0\n");
#endif
extern "C" void okl_fault_handler(int signo, void* info, void* uc);
extern "C" void okl_sample_handler(int signo, void* info, void* uc);

namespace {

// --- reading memory that may not be there -----------------------------------
bool peek(okl_uptr at, void* out, okl_uptr n) {
    struct iov { void* base; okl_uptr len; };
    iov local{ out, n };
    iov remote{ reinterpret_cast<void*>(at), n };
    static okl_long self = 0;
    if (self == 0) self = okl::sys(okl::nr_getpid);
    const okl_long r = okl::sys(okl::nr_process_vm_readv, self, reinterpret_cast<okl_long>(&local), 1,
                                reinterpret_cast<okl_long>(&remote), 1, 0);
    return r == static_cast<okl_long>(n);
}

// --- writing without a C library ------------------------------------------
struct line {
    char b[512]; okl_uptr n = 0;
    line& s(const char* t) { while (*t && n + 1 < sizeof b) b[n++] = *t++; return *this; }
    line& s(const char* t, okl_uptr len) { for (okl_uptr i = 0; i < len && n + 1 < sizeof b; ++i) b[n++] = t[i]; return *this; }
    line& hex(okl_uptr v) {
        char d[18]; int k = 0;
        do { d[k++] = "0123456789abcdef"[v & 15]; v >>= 4; } while (v);
        s("0x");
        while (k) { if (n + 1 < sizeof b) b[n++] = d[--k]; else --k; }
        return *this;
    }
    line& dec(okl_uptr v) {
        char d[24]; int k = 0;
        do { d[k++] = static_cast<char>('0' + v % 10); v /= 10; } while (v);
        while (k) { if (n + 1 < sizeof b) b[n++] = d[--k]; else --k; }
        return *this;
    }
    void out(int fd) { okl::write_all(fd, b, n); n = 0; }
};

// --- where an address lies: /proc/self/maps ------------------------------------
//
// The mapping that holds `addr', its start, the offset in its file, and the
// file's name; false for memory no file backs.
struct where_t { okl_uptr start = 0, offset = 0; char file[512]; okl_uptr flen = 0; };

okl_uptr parse_hex(const char*& p) {
    okl_uptr v = 0;
    for (;;) {
        const char c = *p;
        if (c >= '0' && c <= '9') v = v * 16 + static_cast<okl_uptr>(c - '0');
        else if (c >= 'a' && c <= 'f') v = v * 16 + static_cast<okl_uptr>(c - 'a' + 10);
        else break;
        ++p;
    }
    return v;
}

bool locate(okl_uptr addr, where_t& w) {
    const okl_long fd = okl::sys(okl::nr_openat, okl::at_fdcwd, reinterpret_cast<okl_long>("/proc/self/maps"),
                                 okl::o_rdonly | okl::o_cloexec, 0);
    if (okl::failed(fd)) return false;
    char buf[4096];
    char ln[1024];
    okl_uptr ll = 0;
    bool found = false;
    for (;;) {
        const okl_long r = okl::sys(okl::nr_read, fd, reinterpret_cast<okl_long>(buf), sizeof buf);
        if (r == -okl::e_intr) continue;
        if (r <= 0) break;
        for (okl_long i = 0; i < r && !found; ++i) {
            if (buf[i] != '\n') { if (ll + 1 < sizeof ln) ln[ll++] = buf[i]; continue; }
            ln[ll] = '\0';
            const char* p = ln;
            const okl_uptr lo = parse_hex(p);
            if (*p == '-') ++p;
            const okl_uptr hi = parse_hex(p);
            if (addr >= lo && addr < hi) {
                // perms offset dev inode path
                while (*p == ' ') ++p; while (*p && *p != ' ') ++p; while (*p == ' ') ++p;
                const okl_uptr off = parse_hex(p);
                while (*p == ' ') ++p; while (*p && *p != ' ') ++p; while (*p == ' ') ++p;
                while (*p && *p != ' ') ++p; while (*p == ' ') ++p;
                w.start = lo; w.offset = off; w.flen = 0;
                if (*p == '/') { while (*p && w.flen + 1 < sizeof w.file) w.file[w.flen++] = *p++; }
                w.file[w.flen] = '\0';
                found = w.flen != 0;
                if (!found) break;
            }
            ll = 0;
        }
        if (found) break;
    }
    okl::sys(okl::nr_close, fd);
    return found;
}

// --- the symbol that covers an offset of an ELF file --------------------------
struct ehdr64 { unsigned char ident[16]; okl_u32 tv; okl_u32 version; okl_u64 entry, phoff, shoff;
                okl_u32 flags; unsigned short ehsize, phentsize, phnum, shentsize, shnum, shstrndx; };
struct phdr64 { okl_u32 type, flags; okl_u64 offset, vaddr, paddr, filesz, memsz, align; };
struct shdr64 { okl_u32 name, type; okl_u64 flags, addr, offset, size; okl_u32 link, info; okl_u64 align, entsize; };
struct sym64  { okl_u32 name; unsigned char info, other; unsigned short shndx; okl_u64 value, size; };

bool pread_all(int fd, void* out, okl_uptr n, okl_u64 at) {
    okl_uptr got = 0;
    while (got < n) {
        const okl_long r = okl::sys(okl::nr_pread64, fd, reinterpret_cast<okl_long>(static_cast<char*>(out) + got),
                                    static_cast<okl_long>(n - got), static_cast<okl_long>(at + got));
        if (r == -okl::e_intr) continue;
        if (r <= 0) return false;
        got += static_cast<okl_uptr>(r);
    }
    return true;
}

// The name of the symbol of `file' covering file offset `off', into `name',
// and the distance into it; false where the file has no such symbol.
bool symbol_at(const char* file, okl_uptr off, char* name, okl_uptr cap, okl_uptr& len, okl_uptr& delta,
               okl_uptr& sym_vaddr, okl_uptr& vaddr_out) {
    const okl_long fd = okl::sys(okl::nr_openat, okl::at_fdcwd, reinterpret_cast<okl_long>(file),
                                 okl::o_rdonly | okl::o_cloexec, 0);
    if (okl::failed(fd)) return false;
    bool ok = false;
    ehdr64 eh{};
    do {
        if (!pread_all(static_cast<int>(fd), &eh, sizeof eh, 0)) break;
        if (eh.ident[0] != 0x7f || eh.ident[1] != 'E' || eh.ident[4] != 2) break;
        // The offset as an address of the file: the loadable segment holding it.
        okl_uptr vaddr = ~okl_uptr{0};
        for (unsigned i = 0; i < eh.phnum; ++i) {
            phdr64 ph{};
            if (!pread_all(static_cast<int>(fd), &ph, sizeof ph, eh.phoff + i * sizeof ph)) break;
            if (ph.type == 1 && off >= ph.offset && off < ph.offset + ph.filesz) { vaddr = off - ph.offset + ph.vaddr; break; }
        }
        if (vaddr == ~okl_uptr{0}) break;
        vaddr_out = vaddr;
        // The symbol table, or the dynamic one where the other was removed.
        shdr64 table{}, strings{};
        bool have = false;
        static const int kinds[2] = { 2 /* SHT_SYMTAB */, 11 /* SHT_DYNSYM */ };
        for (int want : kinds) {
            for (unsigned i = 0; i < eh.shnum && !have; ++i) {
                shdr64 sh{};
                if (!pread_all(static_cast<int>(fd), &sh, sizeof sh, eh.shoff + i * sizeof sh)) break;
                if (static_cast<int>(sh.type) != want) continue;
                if (!pread_all(static_cast<int>(fd), &strings, sizeof strings, eh.shoff + sh.link * sizeof sh)) break;
                table = sh; have = true;
            }
            if (have) break;
        }
        if (!have || table.entsize != sizeof(sym64)) break;
        okl_u64 best = 0; okl_u32 best_name = 0; bool any = false;
        sym64 chunk[256];
        const okl_u64 count = table.size / sizeof(sym64);
        for (okl_u64 at = 0; at < count; at += 256) {
            const okl_u64 n = count - at < 256 ? count - at : 256;
            if (!pread_all(static_cast<int>(fd), chunk, n * sizeof(sym64), table.offset + at * sizeof(sym64))) break;
            for (okl_u64 k = 0; k < n; ++k) {
                const sym64& sy = chunk[k];
                if ((sy.info & 15) != 2 /* STT_FUNC */ && (sy.info & 15) != 1 /* STT_OBJECT */) continue;
                if (sy.shndx == 0 || sy.value > vaddr) continue;
                if (sy.size != 0 && vaddr >= sy.value + sy.size) continue;
                if (!any || sy.value > best) { best = sy.value; best_name = sy.name; any = true; }
            }
        }
        if (!any) break;
        okl_uptr n = 0;
        char c = 1;
        while (n + 1 < cap && pread_all(static_cast<int>(fd), &c, 1, strings.offset + best_name + n) && c) name[n++] = c;
        name[n] = '\0';
        len = n; delta = vaddr - best; sym_vaddr = best;
        ok = n != 0;
    } while (false);
    okl::sys(okl::nr_close, fd);
    return ok;
}

// --- the fault report ------------------------------------------------------------
struct frame_regs { okl_uptr pc, fp, sp; };

frame_regs from_context(void* uc) {
    frame_regs r{};
    auto* b = static_cast<unsigned char*>(uc);
#if defined(__x86_64__)
    // struct ucontext: flags, link, stack (24), then mcontext's gregs; RIP is
    // gregs[16], RBP gregs[10], RSP gregs[15].
    okl::copy(&r.pc, b + 40 + 16 * 8, 8);
    okl::copy(&r.fp, b + 40 + 10 * 8, 8);
    okl::copy(&r.sp, b + 40 + 15 * 8, 8);
#elif defined(__aarch64__)
    // flags, link, stack (24), the signal mask with its reserve (128), then
    // the sigcontext at a sixteen-byte boundary: fault address, x0..x30, sp, pc.
    constexpr okl_uptr mc = 176;
    okl::copy(&r.fp, b + mc + 8 + 29 * 8, 8);
    okl::copy(&r.sp, b + mc + 8 + 31 * 8, 8);
    okl::copy(&r.pc, b + mc + 8 + 32 * 8, 8);
#endif
    return r;
}

// The return addresses of a chain of frame pointers.
int walk(frame_regs r, okl_uptr* pcs, int max) {
    int n = 0;
    if (r.pc && n < max) pcs[n++] = r.pc;
    okl_uptr fp = r.fp;
    for (int i = 0; i < 256 && n < max; ++i) {
        if (fp == 0 || (fp & 7) != 0 || fp < r.sp) break;
        okl_uptr pair[2];
        if (!peek(fp, pair, sizeof pair)) break;
        if (pair[1] == 0) break;
        pcs[n++] = pair[1];
        if (pair[0] <= fp) break;      // a chain runs toward older frames
        fp = pair[0];
    }
    return n;
}

void describe_frame(line& l, okl_uptr pc) {
    where_t w;
    if (!locate(pc, w)) return;
    const okl_uptr off = pc - w.start + w.offset;
    l.s(" ").s(w.file, w.flen).s("+").hex(off);
    char name[256]; okl_uptr nlen = 0, delta = 0, sv = 0, va = 0;
    if (symbol_at(w.file, off, name, sizeof name, nlen, delta, sv, va))
        l.s(" ").s(name, nlen).s("+").hex(delta);
}

const char* fault_name(int signo, int code) {
    switch (signo) {
        case 11: return code == 2 ? "an access the program may not make (SIGSEGV, protection)"
                                  : "an access to memory that is not there (SIGSEGV)";
        case 7:  return "an access the memory cannot serve (SIGBUS)";
        case 4:  return "an instruction the program may not execute (SIGILL)";
        case 8:  return "an arithmetic fault (SIGFPE)";
        default: return "a fault";
    }
}

struct siginfo_head { int signo, errnum, code, pad; okl_uptr addr; };

void report(int signo, void* info, void* uc) {
    const auto* si = static_cast<const siginfo_head*>(info);
    line l;
    l.s("openkal: fault: ").s(fault_name(signo, si ? si->code : 0)).s(" at ").hex(si ? si->addr : 0).s("\n");
    l.out(2);
    okl_uptr pcs[64];
    const int n = walk(from_context(uc), pcs, 64);
    for (int i = 0; i < n; ++i) {
        l.s("openkal:   #").dec(static_cast<okl_uptr>(i)).s(" ").hex(pcs[i]);
        describe_frame(l, i == 0 ? pcs[i] : pcs[i] - 1);
        l.s("\n");
        l.out(2);
    }
}



struct k_sigaction { void* handler; unsigned long flags; void* restorer; unsigned long mask; };
constexpr unsigned long sa_siginfo = 4, sa_onstack = 0x08000000, sa_restorer = 0x04000000,
                        sa_resethand = 0x80000000, sa_restart = 0x10000000;

alignas(16) unsigned char g_alt_stack[256 * 1024];

// INSTALLED AT INCEPTION, for every program of this implementation, on a stack
// of its own so that a program that exhausted its stack is still reported.
// Having reported, the handler gives the signal its default disposition again,
// so the fault repeats and the program ends as the kernel ends it, its core
// kept. It is not reset on entry (SA_RESETHAND): a fault that is a touch being
// recorded (below) is the handler's to resolve, and the program continues.
[[gnu::constructor(103)]] void install_fault_report() {
    struct { void* sp; int flags; okl_uptr size; } ss{ g_alt_stack, 0, sizeof g_alt_stack };
    okl::sys(okl::nr_sigaltstack, reinterpret_cast<okl_long>(&ss), 0);
    static const int faults[4] = { 11, 7, 4, 8 };
    for (int signo : faults) {
        k_sigaction old{};
        if (okl::failed(okl::sys(okl::nr_rt_sigaction, signo, 0, reinterpret_cast<okl_long>(&old), sizeof old.mask)))
            continue;
        // A disposition the program was started with, other than the default,
        // is the program's to keep.
        if (old.handler != nullptr) continue;
        k_sigaction act{ reinterpret_cast<void*>(&okl_fault_handler),
                         sa_siginfo | sa_onstack | sa_restorer,
                         reinterpret_cast<void*>(&okl_signal_return), 0 };
        okl::sys(okl::nr_rt_sigaction, signo, reinterpret_cast<okl_long>(&act), 0, sizeof act.mask);
    }
}

// --- touches (0.23) ------------------------------------------------------------------
//
// A part being recorded is made readable and not executable: its first
// execution of a page is a fault the handler resolves --- the page made
// executable again, its address recorded once --- and the instruction is
// carried out. Only executing faults: a read of the part, the kernel's
// included, proceeds as it did.
struct touched_part {
    okl_uptr from = 0, to = 0;
    int prot = 0;
    volatile unsigned char* seen = nullptr;   // a bit for each page
    okl_uptr seen_bytes = 0;
};
constexpr int kTouchedParts = 16;
touched_part g_parts[kTouchedParts];
volatile int g_part_count = 0;
volatile int g_touching = 0;
okl_uptr g_touch_page = 4096;

struct touch_ring {
    okl_uptr* pages = nullptr;
    volatile okl_uptr* ready = nullptr;
    okl_uptr cap = 0;
    volatile okl_uptr written = 0, taken = 0;
};
touch_ring g_touches;

// The disposition of SIGSEGV before touches were recorded, where it was not
// this handler's: a fault that is not a touch is the program's.
k_sigaction g_segv_before{};
bool g_segv_replaced = false;

// The protection of the pages of [from, to) from /proc/self/maps: one for all
// of them, or -1.
int protection_of(okl_uptr from, okl_uptr to) {
    const okl_long fd = okl::sys(okl::nr_openat, okl::at_fdcwd, reinterpret_cast<okl_long>("/proc/self/maps"),
                                 okl::o_rdonly | okl::o_cloexec, 0);
    if (okl::failed(fd)) return -1;
    char buf[4096], ln[1024];
    okl_uptr ll = 0, covered = from;
    int prot = -2;
    bool done = false;
    for (;;) {
        const okl_long r = okl::sys(okl::nr_read, fd, reinterpret_cast<okl_long>(buf), sizeof buf);
        if (r == -okl::e_intr) continue;
        if (r <= 0) break;
        for (okl_long i = 0; i < r && !done; ++i) {
            if (buf[i] != '\n') { if (ll + 1 < sizeof ln) ln[ll++] = buf[i]; continue; }
            ln[ll] = '\0';
            ll = 0;
            const char* p = ln;
            const okl_uptr lo = parse_hex(p);
            if (*p == '-') ++p;
            const okl_uptr hi = parse_hex(p);
            if (hi <= covered || lo >= to) continue;
            if (lo > covered) { prot = -1; done = true; break; }   // a hole
            while (*p == ' ') ++p;
            const int here = (p[0] == 'r' ? okl::prot_read : 0) | (p[1] == 'w' ? okl::prot_write : 0)
                           | (p[2] == 'x' ? okl::prot_exec : 0);
            if (prot == -2) prot = here; else if (prot != here) { prot = -1; done = true; break; }
            covered = hi;
            if (covered >= to) { done = true; break; }
        }
        if (done) break;
    }
    okl::sys(okl::nr_close, fd);
    return covered >= to ? prot : -1;
}

// A touch, from the handler: true when the fault was one and is resolved.
bool touched(int signo, const siginfo_head* si) {
    if (signo != 11 || !__atomic_load_n(&g_touching, __ATOMIC_ACQUIRE) || si == nullptr) return false;
    const okl_uptr addr = si->addr;
    const int n = __atomic_load_n(&g_part_count, __ATOMIC_ACQUIRE);
    for (int k = 0; k < n; ++k) {
        touched_part& t = g_parts[k];
        if (addr < t.from || addr >= t.to) continue;
        const okl_uptr page = addr & ~(g_touch_page - 1);
        const okl_uptr index = (page - t.from) / g_touch_page;
        const unsigned char bit = static_cast<unsigned char>(1u << (index & 7));
        const unsigned char was = __atomic_fetch_or(&t.seen[index >> 3], bit, __ATOMIC_ACQ_REL);
        // The page executable again before the instruction is retried; a
        // second context that met it at once finds the bit and only retries.
        okl::sys(okl::nr_mprotect, static_cast<okl_long>(page), static_cast<okl_long>(g_touch_page), t.prot);
        if (!(was & bit) && g_touches.pages) {
            const okl_uptr mine = __atomic_fetch_add(&g_touches.written, 1, __ATOMIC_ACQ_REL);
            if (mine - __atomic_load_n(&g_touches.taken, __ATOMIC_ACQUIRE) < g_touches.cap) {
                g_touches.pages[mine % g_touches.cap] = page;
                __atomic_store_n(&g_touches.ready[mine % g_touches.cap], mine + 1, __ATOMIC_RELEASE);
            }
        }
        return true;
    }
    return false;
}

void fault(int signo, void* info, void* uc) {
    if (touched(signo, static_cast<const siginfo_head*>(info))) return;
    // A fault that is not a touch, met while SIGSEGV was the program's: the
    // program's handler, as the program installed it.
    if (signo == 11 && g_segv_replaced && g_segv_before.handler != nullptr
        && g_segv_before.handler != reinterpret_cast<void*>(1)) {
        if (g_segv_before.flags & sa_siginfo)
            reinterpret_cast<void (*)(int, void*, void*)>(g_segv_before.handler)(signo, info, uc);
        else
            reinterpret_cast<void (*)(int)>(g_segv_before.handler)(signo);
        return;
    }
    report(signo, info, uc);
    // The default disposition, and the fault repeats as the kernel's.
    k_sigaction standard{};
    okl::sys(okl::nr_rt_sigaction, signo, reinterpret_cast<okl_long>(&standard), 0, sizeof standard.mask);
}

// --- samples -----------------------------------------------------------------------
struct ring {
    kal_prof_sample* slots = nullptr;
    volatile okl_uptr* ready = nullptr;   // the sequence number + 1 of the sample a slot holds
    okl_uptr cap = 0;
    volatile okl_uptr written = 0;    // reserved by the handler
    volatile okl_uptr taken = 0;
    volatile int on = 0;
};
ring g_ring;

okl_u64 monotonic() {
    struct { okl_long s, ns; } t{};
    okl::sys(okl::nr_clock_gettime, 1 /* CLOCK_MONOTONIC */, reinterpret_cast<okl_long>(&t));
    return static_cast<okl_u64>(t.s) * 1000000000ull + static_cast<okl_u64>(t.ns);
}

void sample(void* uc) {
    if (!__atomic_load_n(&g_ring.on, __ATOMIC_ACQUIRE)) return;
    // A slot is reserved before it is written; when the ring holds as many as
    // it can, the sample is not recorded.
    const okl_uptr at = __atomic_load_n(&g_ring.written, __ATOMIC_RELAXED);
    if (at - __atomic_load_n(&g_ring.taken, __ATOMIC_ACQUIRE) >= g_ring.cap) return;
    const okl_uptr mine = __atomic_fetch_add(&g_ring.written, 1, __ATOMIC_ACQ_REL);
    if (mine - __atomic_load_n(&g_ring.taken, __ATOMIC_ACQUIRE) >= g_ring.cap) return;
    kal_prof_sample& s = g_ring.slots[mine % g_ring.cap];
    okl_uptr pcs[KAL_PROF_SAMPLE_DEPTH];
    const int n = walk(from_context(uc), pcs, KAL_PROF_SAMPLE_DEPTH);
    s.time_ns = monotonic();
    s.context = kal_task_current();
    s.depth = static_cast<kal_u32>(n);
    s.reserved = 0;
    for (int i = 0; i < n; ++i) s.pc[i] = pcs[i];
    __atomic_store_n(&g_ring.ready[mine % g_ring.cap], mine + 1, __ATOMIC_RELEASE);
}

k_sigaction g_prof_before{};

}  // namespace

extern "C" void okl_fault_handler(int signo, void* info, void* uc) { fault(signo, info, uc); }
extern "C" void okl_sample_handler(int, void*, void* uc) { sample(uc); }

extern "C" {

kal_uintptr kal_debug_props(void) { return KAL_DEBUG_PROP_FAULT_REPORT; }

// AN ADDRESS NAMED (0.23 for a program of this environment): the mapping that
// holds it, and the symbol table of its file --- the program's, a library's,
// or an image's, which is the same format.
kal_intptr kal_debug_symbolize(kal_uintptr address, char* out, kal_uintptr cap, kal_uintptr* start) {
    if (address == 0) return -static_cast<kal_intptr>(kal_err_not_found);
    where_t w;
    if (!locate(address, w)) return -static_cast<kal_intptr>(kal_err_not_found);
    const okl_uptr off = address - w.start + w.offset;
    char name[1024]; okl_uptr nlen = 0, delta = 0, sv = 0, va = 0;
    if (!symbol_at(w.file, off, name, sizeof name, nlen, delta, sv, va))
        return -static_cast<kal_intptr>(kal_err_not_found);
    if (out && cap) okl::copy(out, name, nlen < cap ? nlen : cap);
    if (start) *start = address - delta;
    return static_cast<kal_intptr>(nlen);
}

// THIS IMPLEMENTATION DOES NOT MEASURE A PROGRAM ITSELF: a loader that binds
// an image does (clause 12.3). So the program is not measured, a counter is
// accepted and never read, and a phase is accepted and recorded nowhere.
int  kal_prof_active(void) { return 0; }
int  kal_prof_counter(const char*, kal_uintptr, const volatile kal_u64*) { return kal_ok; }
void kal_prof_mark(const char*, kal_uintptr, int) {}

kal_uintptr kal_prof_props(void) { return KAL_PROF_PROP_SAMPLES | KAL_PROF_PROP_TOUCHES; }

// SAMPLES: the processor-time timer of the whole program (ITIMER_PROF), whose
// signal the kernel delivers to the context that is using the time, and the
// chain of that context's frames.
int kal_prof_sample_begin(kal_u32 per_second) {
    if (per_second == 0 || per_second > 10000) return kal_err_invalid;
    if (__atomic_load_n(&g_ring.on, __ATOMIC_ACQUIRE)) return kal_err_again;
    if (!g_ring.slots) {
        // Address space for a minute of samples at a thousand a second; a page is
        // resident once a sample is written to it.
        constexpr okl_uptr cap = 65536;
        const okl_uptr bytes = cap * sizeof(kal_prof_sample) + cap * sizeof(okl_uptr);
        const okl_long m = okl::sys(okl::nr_mmap, 0, static_cast<okl_long>(bytes),
                                    okl::prot_read | okl::prot_write, okl::map_private | okl::map_anonymous, -1, 0);
        if (okl::failed(m)) return kal_err_no_memory;
        g_ring.slots = reinterpret_cast<kal_prof_sample*>(m);
        g_ring.ready = reinterpret_cast<volatile okl_uptr*>(m + static_cast<okl_long>(cap * sizeof(kal_prof_sample)));
        g_ring.cap = cap;
    }
    k_sigaction act{ reinterpret_cast<void*>(&okl_sample_handler), sa_siginfo | sa_restart | sa_restorer,
                     reinterpret_cast<void*>(&okl_signal_return), 0 };
    if (okl::failed(okl::sys(okl::nr_rt_sigaction, 27 /* SIGPROF */, reinterpret_cast<okl_long>(&act),
                             reinterpret_cast<okl_long>(&g_prof_before), sizeof act.mask)))
        return kal_err_not_supported;
    __atomic_store_n(&g_ring.on, 1, __ATOMIC_RELEASE);
    const okl_long us = static_cast<okl_long>(1000000 / per_second);
    struct { okl_long is, ius, vs, vus; } it{ us / 1000000, us % 1000000, us / 1000000, us % 1000000 };
    if (okl::failed(okl::sys(okl::nr_setitimer, 2 /* ITIMER_PROF */, reinterpret_cast<okl_long>(&it), 0))) {
        __atomic_store_n(&g_ring.on, 0, __ATOMIC_RELEASE);
        okl::sys(okl::nr_rt_sigaction, 27, reinterpret_cast<okl_long>(&g_prof_before), 0, sizeof act.mask);
        return kal_err_not_supported;
    }
    return kal_ok;
}

kal_intptr kal_prof_sample_take(kal_prof_sample* out, kal_uintptr count) {
    if (out == nullptr && count != 0) return -static_cast<kal_intptr>(kal_err_invalid);
    if (!g_ring.slots) return 0;
    kal_uintptr n = 0;
    while (n < count) {
        const okl_uptr t = __atomic_load_n(&g_ring.taken, __ATOMIC_ACQUIRE);
        if (__atomic_load_n(&g_ring.ready[t % g_ring.cap], __ATOMIC_ACQUIRE) != t + 1) break;
        out[n++] = g_ring.slots[t % g_ring.cap];
        __atomic_store_n(&g_ring.taken, t + 1, __ATOMIC_RELEASE);
    }
    return static_cast<kal_intptr>(n);
}

int kal_prof_sample_end(void) {
    if (!__atomic_load_n(&g_ring.on, __ATOMIC_ACQUIRE)) return kal_err_invalid;
    struct { okl_long is, ius, vs, vus; } stop{};
    okl::sys(okl::nr_setitimer, 2, reinterpret_cast<okl_long>(&stop), 0);
    __atomic_store_n(&g_ring.on, 0, __ATOMIC_RELEASE);
    okl::sys(okl::nr_rt_sigaction, 27, reinterpret_cast<okl_long>(&g_prof_before), 0, sizeof g_prof_before.mask);
    return kal_ok;
}

// TOUCHES (0.23).
int kal_prof_touch_begin(const void* at, kal_uintptr size) {
    const okl_uptr g = kal_exec_granularity();
    const okl_uptr from = reinterpret_cast<okl_uptr>(at);
    if (size == 0 || (from & (g - 1)) != 0 || (size & (g - 1)) != 0) return kal_err_invalid;
    const int prot = protection_of(from, from + size);
    if (prot < 0 || !(prot & okl::prot_exec)) return kal_err_invalid;
    if (__atomic_load_n(&g_part_count, __ATOMIC_ACQUIRE) == kTouchedParts) return kal_err_no_memory;
    g_touch_page = g;
    if (!g_touches.pages) {
        constexpr okl_uptr cap = 65536;
        const okl_long m = okl::sys(okl::nr_mmap, 0, static_cast<okl_long>(cap * 2 * sizeof(okl_uptr)),
                                    okl::prot_read | okl::prot_write, okl::map_private | okl::map_anonymous, -1, 0);
        if (okl::failed(m)) return kal_err_no_memory;
        g_touches.pages = reinterpret_cast<okl_uptr*>(m);
        g_touches.ready = reinterpret_cast<volatile okl_uptr*>(m + static_cast<okl_long>(cap * sizeof(okl_uptr)));
        g_touches.cap = cap;
    }
    const okl_uptr bits = (size / g + 7) / 8;
    const okl_long b = okl::sys(okl::nr_mmap, 0, static_cast<okl_long>(bits), okl::prot_read | okl::prot_write,
                                okl::map_private | okl::map_anonymous, -1, 0);
    if (okl::failed(b)) return kal_err_no_memory;
    // The handler resolves the touches, whoever holds SIGSEGV.
    k_sigaction now{};
    okl::sys(okl::nr_rt_sigaction, 11, 0, reinterpret_cast<okl_long>(&now), sizeof now.mask);
    if (now.handler != reinterpret_cast<void*>(&okl_fault_handler)) {
        g_segv_before = now;
        g_segv_replaced = true;
        k_sigaction act{ reinterpret_cast<void*>(&okl_fault_handler), sa_siginfo | sa_onstack | sa_restorer,
                         reinterpret_cast<void*>(&okl_signal_return), 0 };
        okl::sys(okl::nr_rt_sigaction, 11, reinterpret_cast<okl_long>(&act), 0, sizeof act.mask);
    }
    const int k = __atomic_load_n(&g_part_count, __ATOMIC_ACQUIRE);
    g_parts[k] = touched_part{ from, from + size, prot, reinterpret_cast<volatile unsigned char*>(b), bits };
    __atomic_store_n(&g_part_count, k + 1, __ATOMIC_RELEASE);
    __atomic_store_n(&g_touching, 1, __ATOMIC_RELEASE);
    const okl_long r = okl::sys(okl::nr_mprotect, static_cast<okl_long>(from), static_cast<okl_long>(size),
                                prot & ~okl::prot_exec);
    if (okl::failed(r)) {
        __atomic_store_n(&g_part_count, k, __ATOMIC_RELEASE);
        return okl::translate(r);
    }
    return kal_ok;
}

kal_intptr kal_prof_touch_take(kal_uintptr* out, kal_uintptr count) {
    if (out == nullptr && count != 0) return -static_cast<kal_intptr>(kal_err_invalid);
    if (!g_touches.pages) return 0;
    kal_uintptr n = 0;
    while (n < count) {
        const okl_uptr t = __atomic_load_n(&g_touches.taken, __ATOMIC_ACQUIRE);
        if (__atomic_load_n(&g_touches.ready[t % g_touches.cap], __ATOMIC_ACQUIRE) != t + 1) break;
        out[n++] = g_touches.pages[t % g_touches.cap];
        __atomic_store_n(&g_touches.taken, t + 1, __ATOMIC_RELEASE);
    }
    return static_cast<kal_intptr>(n);
}

// Every part as it was; the pages' bits stay for the life of the program (a
// context may still be in the handler with them).
int kal_prof_touch_end(void) {
    const int n = __atomic_load_n(&g_part_count, __ATOMIC_ACQUIRE);
    if (n == 0) return kal_err_invalid;
    for (int k = 0; k < n; ++k)
        okl::sys(okl::nr_mprotect, static_cast<okl_long>(g_parts[k].from),
                 static_cast<okl_long>(g_parts[k].to - g_parts[k].from), g_parts[k].prot);
    __atomic_store_n(&g_touching, 0, __ATOMIC_RELEASE);
    __atomic_store_n(&g_part_count, 0, __ATOMIC_RELEASE);
    if (g_segv_replaced) {
        okl::sys(okl::nr_rt_sigaction, 11, reinterpret_cast<okl_long>(&g_segv_before), 0, sizeof g_segv_before.mask);
        g_segv_replaced = false;
    }
    return kal_ok;
}

}  // extern "C"
