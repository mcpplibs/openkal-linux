#include "sys.h"
#include "tls.h"
#include <openkal/task.h>
#include <openkal/memory.h>

// The vectors the kernel placed at inception, which is where the page size is
// stated. Defined in env.cpp, and declared here rather than taken from a
// header because it is this implementation's own arrangement rather than a
// name openkal has.
namespace okl { okl_ulong auxval(okl_ulong key); }

// Execution contexts come from one of two places, and which one is a property
// of the program rather than of this implementation.
//
// A program that already carries a runtime has that runtime's threads, and a
// context obtained from them may call anything the program can call. This is
// the ordinary arrangement and it is the default.
//
// A program that carries no such runtime --- because it supplies one itself, or
// because it has none --- has nothing to obtain threads from, and this
// implementation creates them. The feature named `standalone' selects that. It
// is not an optimisation: in that arrangement the choice is between creating
// contexts here and not providing openkal.task at all.

#ifdef OKL_STANDALONE

// The child of a clone begins on a stack of its own with no return address, so
// the transfer cannot be written in C. The sequence is the one every C library
// uses, and it is short enough to read: the arguments are moved into the
// positions the system call takes, the function and its argument are placed on
// the child's stack, and the child calls the one with the other and then ends.
#if defined(__x86_64__)
__asm__(
".text\n"
".globl __okl_clone\n"
".hidden __okl_clone\n"
".type __okl_clone,@function\n"
"__okl_clone:\n"          // rdi=fn rsi=stack rdx=flags rcx=arg r8=ptid r9=tls, 8(%rsp)=ctid
"  mov $56,%eax\n"        // SYS_clone
"  mov %rdi,%r11\n"
"  mov %rdx,%rdi\n"       // flags
"  mov %r8,%rdx\n"        // ptid
"  mov %r9,%r8\n"         // tls
"  mov 8(%rsp),%r10\n"    // ctid
"  mov %r11,%r9\n"        // fn kept out of the way
"  and $-16,%rsi\n"
"  sub $8,%rsi\n"
"  mov %rcx,(%rsi)\n"     // arg, for the child to pop
"  syscall\n"
"  test %eax,%eax\n"
"  jnz 1f\n"
"  xor %ebp,%ebp\n"
"  pop %rdi\n"
"  call *%r9\n"
"  mov %eax,%edi\n"
"  mov $60,%eax\n"        // SYS_exit, this context only
"  syscall\n"
"  hlt\n"
"1:ret\n"
".size __okl_clone,.-__okl_clone\n"
);
#elif defined(__aarch64__)
__asm__(
".text\n"
".globl __okl_clone\n"
".hidden __okl_clone\n"
".type __okl_clone,%function\n"
"__okl_clone:\n"          // x0=fn x1=stack x2=flags x3=arg x4=ptid x5=tls x6=ctid
"  and x1,x1,#-16\n"
"  stp x0,x3,[x1,#-16]!\n"
"  uxtw x0,w2\n"
"  mov x2,x4\n"
"  mov x3,x5\n"
"  mov x4,x6\n"
"  mov x8,#220\n"         // SYS_clone
"  svc #0\n"
"  cbz x0,1f\n"
"  ret\n"
"1:ldp x1,x0,[sp],#16\n"
"  blr x1\n"
"  mov x8,#93\n"          // SYS_exit, this context only
"  svc #0\n"
".size __okl_clone,.-__okl_clone\n"
);
#endif

extern "C" okl_long __okl_clone(int (*fn)(void*), void* stack, int flags, void* arg,
                                int* ptid, void* tls, int* ctid);

#else
// THE HOST'S OWN ENQUIRY, WHICH IS A NAME THIS IMPLEMENTATION MAY TAKE IN THIS
// ARRANGEMENT AND ONLY IN THIS ONE. A program that carries a runtime is a
// program whose runtime owns the stacks, and `pthread_getattr_np' is how that
// runtime states them; above a C library, taking the library's names is what
// this arrangement is. Where the program carries no runtime, the measurement
// above is used instead, because there is no library to ask and because a
// program that defines every ordinary name would have this call resolve back
// into itself.
//
// The macro is stated because the declaration is the GNU one on both C
// libraries this arrangement is built above.
#define _GNU_SOURCE 1
#include <pthread.h>
#include <sched.h>
#endif

namespace {


constexpr okl_uptr kStack = 256u * 1024u;

struct context {
    void (*entry)(void*);
    void*    arg;
    void*    stack;
    okl_uptr stack_bytes;
    okl::tls_block tls;
    volatile int tid;      // the kernel clears this when the context ends
#ifndef OKL_STANDALONE
    unsigned long thread;
#endif
};

// --- the region a context stands on ------------------------------------------
//
// RECORDED WHERE THIS IMPLEMENTATION CHOSE THE STACK, MEASURED WHERE IT DID
// NOT. Every context but the first runs on a region this implementation
// allocated, so its bounds are known by construction and nothing has to be
// asked. The context the program was started on has no such record, and its
// region is measured rather than derived.
//
// WHY THE LIMIT ALONE IS NOT THE ANSWER, WHICH IS THE DEFECT THE OPERATION
// EXISTS TO REMOVE. `RLIMIT_STACK' is a policy, and the floor the kernel
// enforces is `mapping_end - RLIMIT_STACK' --- the end of the MAPPING, which is
// not the address the program's first instruction sees. The kernel places the
// arguments at the top of the mapping and moves the mapping's start down by a
// random shift, so a region computed from a stack pointer alone begins below
// the real floor by that shift. Below the real floor is where this system puts
// the mappings of the program's own libraries, so the region would contain
// another mapping: a caller that placed a guard at its base, or measured the
// room above it, would be measuring a range the kernel will not grow the stack
// into.
//
// WHAT IS MEASURED, AND WHAT IS STATED RATHER THAN MEASURED. The mapping is
// found by asking the kernel which pages are mapped --- `mincore' answers that
// and nothing else --- and its floor is the higher of two bounds the kernel
// itself applies:
//
//   - `mapping_end - RLIMIT_STACK', which `prlimit64' states, and
//   - one guard gap above the nearest mapping below, because the kernel
//     refuses to grow the stack to within `stack_guard_gap' of another mapping.
//
// MEASURED ON 6.8.0, WITH THE CONTROL THAT SEPARATES THE TWO. A descending
// write stopped at exactly `mapping_end - RLIMIT_STACK'; with one page mapped
// by the test itself inside the reservation, the same write stopped at exactly
// that page's end plus 256 pages, which is the kernel's default gap.
//
// THE GAP IS A KERNEL GLOBAL THIS IMPLEMENTATION CANNOT READ, and the default
// is therefore what is applied. A kernel booted with a larger gap stops the
// stack above the base reported here, so the region is reported larger than the
// kernel will grow it --- the direction a caller must not be wrong in, and the
// reason the default is applied rather than a smaller number. A kernel booted
// with a smaller gap is reported a region smaller than it could have, which is
// the harmless direction.
//
// The region is computed once per context: a stack does not move, and asking
// again would measure the same mapping.
struct self_bounds {
    void*    base;
    okl_uptr size;
    int      state;   // 0 not yet asked, 1 answered
};

thread_local self_bounds g_self = { nullptr, 0, 0 };

#ifdef OKL_STANDALONE

// The kernel's page, read rather than assumed: `mincore' refuses an address
// that is not page-aligned, and 4 KiB is not every architecture's answer.
okl_uptr page_bytes() {
    static okl_uptr cached = 0;
    if (cached == 0) {
        const okl_uptr n = static_cast<okl_uptr>(okl::auxval(6 /* AT_PAGESZ */));
        cached = n != 0 ? n : 4096u;
    }
    return cached;
}

// The range one question covers. Bounded by the kernel's own workspace, which
// is one byte per page; 4 MiB is 1024 bytes where the page is 4 KiB and 64
// bytes where it is 64 KiB.
constexpr okl_uptr kChunkBytes = 4u * 1024u * 1024u;
constexpr okl_uptr kMaxVec     = 1024;

okl_uptr pages_per_chunk() {
    const okl_uptr n = kChunkBytes / page_bytes();
    return n < kMaxVec && n != 0 ? n : kMaxVec;
}

// The kernel's answer when it would not say which pages are mapped. It is kept
// apart from "not mapped" because the two lead to different reports: a range
// derived from a refusal is a range derived from nothing, and the caller is
// owed the refusal instead.
thread_local okl_long g_refusal = 0;

// Whether every page in the range is mapped. False with `mapped' unset means
// at least one is not; false with `g_refusal' set means the environment would
// not answer.
bool range_state(okl_uptr base, okl_uptr pages, unsigned char* vec, bool* mapped) {
    const okl_long r = okl::sys(okl::nr_mincore, static_cast<okl_long>(base),
                                static_cast<okl_long>(pages * page_bytes()),
                                reinterpret_cast<okl_long>(vec));
    if (r == 0) { *mapped = true; return true; }
    // A range that runs past the end of the address space is a range no page of
    // which is mapped, which is the answer the search is looking for.
    if (r == -okl::e_nomem || r == -okl::e_fault) { *mapped = false; return true; }
    g_refusal = r;
    return false;
}

// The end of the mapped run that contains `at': the first page at or above it
// that is not mapped. One test per 4 MiB, and a page-by-page walk of the one
// that contains the boundary.
bool run_end(okl_uptr at, okl_uptr* end) {
    const okl_uptr page = page_bytes();
    const okl_uptr per  = pages_per_chunk();
    const okl_uptr span = per * page;
    unsigned char  vec[kMaxVec];

    okl_uptr p = at & ~(page - 1);
    for (;;) {
        bool mapped = false;
        if (!range_state(p, per, vec, &mapped)) return false;
        if (!mapped) {
            for (okl_uptr q = p; q < p + span; q += page) {
                bool one = false;
                if (!range_state(q, 1, vec, &one)) return false;
                if (!one) { *end = q; return true; }
            }
        }
        p += span;
        if (p < at) { *end = ~static_cast<okl_uptr>(0) & ~(page - 1); return true; }
    }
}

// The lowest page of the mapped run that contains `at'.
bool run_start(okl_uptr at, okl_uptr* start) {
    const okl_uptr page = page_bytes();
    const okl_uptr per  = pages_per_chunk();
    const okl_uptr span = per * page;
    unsigned char  vec[kMaxVec];

    okl_uptr hi = at & ~(page - 1);
    for (;;) {
        const okl_uptr base = hi > span ? hi - span : 0;
        bool mapped = false;
        if (!range_state(base, (hi - base) / page, vec, &mapped)) return false;
        if (!mapped) {
            // The run begins inside this chunk. Bisect on "every page from here
            // up to the chunk's top is mapped", which is monotone in the
            // address and is exactly what the boundary is.
            okl_uptr lo = base, up = hi;
            while (up - lo > page) {
                const okl_uptr mid = lo + (((up - lo) / 2) & ~(page - 1));
                bool whole = false;
                if (mid == lo) break;
                if (!range_state(mid, (hi - mid) / page, vec, &whole)) return false;
                if (whole) up = mid; else lo = mid;
            }
            // `up' is the lowest page from which everything up to the chunk's
            // top is mapped, and the pages below it are not.
            *start = up;
            return true;
        }
        if (base == 0) { *start = 0; return true; }
        hi = base;
    }
}

// The highest mapped page below `low', searching no further down than `limit'.
// False when nothing is mapped in that window.
bool obstacle_below(okl_uptr low, okl_uptr limit, okl_uptr* found) {
    const okl_uptr page = page_bytes();
    const okl_uptr per  = pages_per_chunk();
    const okl_uptr span = per * page;
    unsigned char  vec[kMaxVec];

    okl_uptr hi = low;
    while (hi > limit) {
        const okl_uptr base = (hi - limit) > span ? hi - span : limit;
        bool mapped = false;
        if (!range_state(base, (hi - base) / page, vec, &mapped)) return false;
        if (!mapped) {
            // Something in this chunk is mapped. Bisect on "some page from here
            // up to the chunk's top is mapped", which is monotone in the
            // address; the highest address it holds at is the obstacle's top.
            okl_uptr lo = base, up = hi;
            while (up - lo > page) {
                const okl_uptr mid = lo + (((up - lo) / 2) & ~(page - 1));
                bool any = false;
                if (mid == lo) break;
                if (!range_state(mid, (hi - mid) / page, vec, &any)) return false;
                if (any) lo = mid; else up = mid;
            }
            *found = lo;
            return true;
        }
        hi = base;
    }
    return false;
}

// How far below the stack an unbounded limit is searched for the first mapping.
// The kernel states no floor in that arrangement, so the region reported is the
// part of the free space below the stack that was verified free, and this is
// what bounds the verification. A caller is told a region of this size or the
// whole free run, whichever is smaller, and never a region that was not
// measured.
constexpr okl_uptr kUnboundedSweep = 256u * 1024u * 1024u;

// Measures the region the calling context stands on. False with `g_refusal' set
// when the kernel would not answer.
bool measure_self(okl_uptr at, void** base, okl_uptr* size) {
    const okl_uptr page = page_bytes();
    okl_uptr top = 0, low = 0;
    if (!run_end(at, &top)) return false;
    if (!run_start(at, &low)) return false;

    // The limit the kernel enforces over the whole mapping, and the guard gap it
    // enforces against the nearest mapping below.
    okl_uptr rlim = 0;
    okl_uptr lim[2] = { 0, 0 };
    const okl_long r = okl::sys(okl::nr_prlimit64, 0, 3 /* RLIMIT_STACK */, 0,
                                reinterpret_cast<okl_long>(lim));
    if (r != 0) { g_refusal = r; return false; }
    const okl_uptr gap = 256u * page;
    if (lim[0] != ~static_cast<okl_uptr>(0) && lim[0] < top) rlim = lim[0];

    // The floor from the limit: the smallest page-aligned address whose distance
    // from the mapping's end does not exceed the limit the kernel applies.
    okl_uptr floor = rlim != 0 ? (top - rlim + page - 1) & ~(page - 1) : 0;

    // The floor from the mapping below. The window reaches one gap below the
    // limit's floor, because a mapping just below it stops the stack a gap above
    // itself --- which is above the limit's floor and is therefore the binding
    // bound.
    okl_uptr window = floor > gap ? floor - gap : 0;
    if (rlim == 0) {
        window = low > kUnboundedSweep ? low - kUnboundedSweep : 0;
    }
    if (window < low) {
        okl_uptr obstacle = 0;
        if (obstacle_below(low, window, &obstacle)) {
            const okl_uptr by_gap = obstacle + page + gap;
            if (by_gap > floor) floor = by_gap;
        } else if (rlim == 0) {
            // Nothing is mapped in the window, so the nearest mapping below is
            // at or below its floor and the stack may grow to one gap above it.
            floor = window + gap;
        }
    }
    // A limit lowered after the mapping grew leaves the mapping larger than the
    // limit; what is mapped is still the context's stack.
    if (floor > low) floor = low;

    *base = reinterpret_cast<void*>(floor);
    *size = top - floor;
    return *size != 0;
}

#endif  // OKL_STANDALONE

#ifdef OKL_STANDALONE

void* alloc_bridge(okl_uptr n, okl_uptr a) { return kal_alloc(n, a); }

int run(void* p) {
    auto* c = static_cast<context*>(p);
    // THE REGION IS RECORDED BEFORE ANYTHING ELSE RUNS IN THIS CONTEXT, because
    // this is the last point at which this implementation knows it and the first
    // at which the context can be asked. Nothing is measured: the region is the
    // one kal_task_start allocated, and the pair is exact.
    g_self.base = c->stack;
    g_self.size = c->stack_bytes;
    g_self.state = 1;
    c->entry(c->arg);
    return 0;
}

#else

void* run(void* p) {
    auto* c = static_cast<context*>(p);
    c->entry(c->arg);
    return nullptr;
}

int translate_posix(int e) {
    switch (e) {
        case okl::e_inval: case okl::e_fault: return kal_err_invalid;
        case okl::e_again:                    return kal_err_again;
        case okl::e_nomem:                    return kal_err_no_memory;
        case okl::e_perm:                     return kal_err_permission;
        default:                              return kal_err_io;
    }
}

#endif

}  // namespace

extern "C" {

int kal_task_start(void (*entry)(void*), void* arg, kal_task* out) {
    if (entry == nullptr || out == nullptr) return kal_err_invalid;
    auto* c = static_cast<context*>(kal_alloc(sizeof(context), alignof(context)));
    if (c == nullptr) return kal_err_no_memory;
    okl::fill(c, 0, sizeof(context));
    c->entry = entry; c->arg = arg;

#ifdef OKL_STANDALONE
    c->stack_bytes = kStack;
    c->stack = kal_alloc(kStack, 16);
    c->tls   = okl::make_tls(alloc_bridge);
    if (c->stack == nullptr || c->tls.tp == nullptr) {
        if (c->stack) kal_free(c->stack, kStack, 16);
        if (c->tls.base) kal_free(c->tls.base, c->tls.bytes, c->tls.align);
        kal_free(c, sizeof(context), alignof(context));
        return kal_err_no_memory;
    }
    // The context is a thread of this process: it shares the address space,
    // the descriptors and the file system view, it is reaped without a wait,
    // and the kernel clears `tid' and wakes anything suspended upon it when
    // the context ends --- which is what kal_task_join waits for.
    constexpr int flags = 0x00000100   // CLONE_VM
                        | 0x00000200   // CLONE_FS
                        | 0x00000400   // CLONE_FILES
                        | 0x00000800   // CLONE_SIGHAND
                        | 0x00010000   // CLONE_THREAD
                        | 0x00040000   // CLONE_SYSVSEM
                        | 0x00080000   // CLONE_SETTLS
                        | 0x00100000   // CLONE_PARENT_SETTID
                        | 0x00200000;  // CLONE_CHILD_CLEARTID
    auto* top = static_cast<unsigned char*>(c->stack) + kStack;
    const okl_long r = __okl_clone(run, top, flags, c,
                                   const_cast<int*>(&c->tid), c->tls.tp,
                                   const_cast<int*>(&c->tid));
    if (okl::failed(r)) {
        kal_free(c->stack, kStack, 16);
        kal_free(c->tls.base, c->tls.bytes, c->tls.align);
        kal_free(c, sizeof(context), alignof(context));
        return okl::translate(r);
    }
#else
    pthread_t id{};
    const int rc = ::pthread_create(&id, nullptr, run, c);
    if (rc != 0) { kal_free(c, sizeof(context), alignof(context)); return translate_posix(rc); }
    c->thread = static_cast<unsigned long>(id);
#endif

    *out = kal_task{ reinterpret_cast<kal_uintptr>(c) };
    return kal_ok;
}

int kal_task_join(kal_task h) {
    auto* c = reinterpret_cast<context*>(h.h);
    if (c == nullptr) return kal_err_invalid;
#ifdef OKL_STANDALONE
    // The kernel clears the word and wakes those suspended upon it after the
    // context has left user space, so releasing its stack afterwards is safe:
    // nothing in it can still be executing.
    for (;;) {
        const int t = __atomic_load_n(&c->tid, __ATOMIC_ACQUIRE);
        if (t == 0) break;
        okl::sys(okl::nr_futex, reinterpret_cast<okl_long>(&c->tid),
                 okl::futex_wait, t, 0, 0, 0);
    }
    kal_free(c->stack, c->stack_bytes, 16);
    kal_free(c->tls.base, c->tls.bytes, c->tls.align);
#else
    const int rc = ::pthread_join(static_cast<pthread_t>(c->thread), nullptr);
    if (rc != 0) return translate_posix(rc);
#endif
    kal_free(c, sizeof(context), alignof(context));
    return kal_ok;
}

void kal_task_yield(void) { okl::sys(okl::nr_sched_yield); }

kal_uintptr kal_task_current(void) {
    // The identity is the kernel's, read once per context. It is unique among
    // contexts running at the same moment and may be reused after one ends,
    // which is what the specification says of it.
    static thread_local int cached = 0;
    if (cached == 0) cached = static_cast<int>(okl::sys(okl::nr_gettid));
    return static_cast<kal_uintptr>(cached);
}

// The stack the calling context runs on. Version 0.15.
//
// THE TWO ARRANGEMENTS ANSWER FROM DIFFERENT SOURCES, and the difference is
// which party chose the stack. Where this implementation creates the context it
// also allocates the stack, so the pair was recorded when the context began and
// is returned as it stands. Where the program carries its own runtime, the
// stack is that runtime's and the runtime is asked --- which is the same
// enquiry a program above openkal would make for itself, and the only answer
// that agrees with what the environment will actually do.
int kal_task_stack(void** base, kal_uintptr* size) {
    if (base == nullptr || size == nullptr) return kal_err_invalid;

#ifdef OKL_STANDALONE
    if (g_self.state == 0) {
        char here = 0;
        void* b = nullptr;
        okl_uptr n = 0;
        if (!measure_self(reinterpret_cast<okl_uptr>(&here), &b, &n)) {
            // The kernel would not say which pages are mapped, so there is no
            // measured region to report. The condition is the kernel's, mapped
            // onto the closed set --- and it is not "unsupported", which clause
            // 6.1 forbids an implementation of a provided interface to report.
            return okl::translate(g_refusal);
        }
        g_self.base = b;
        g_self.size = n;
        g_self.state = 1;
    }
    *base = g_self.base;
    *size = static_cast<kal_uintptr>(g_self.size);
    return kal_ok;
#else
    // The host's own answer, in the form POSIX states it. `pthread_attr_getstack'
    // reports the usable region and not the guard page below it.
    pthread_attr_t attr;
    void* b = nullptr;
    size_t n = 0;
    if (::pthread_getattr_np(::pthread_self(), &attr) != 0) return kal_err_io;
    if (::pthread_attr_getstack(&attr, &b, &n) != 0) return kal_err_io;
    if (b == nullptr || n == 0) return kal_err_io;
    *base = b;
    *size = static_cast<kal_uintptr>(n);
    return kal_ok;
#endif
}

// How many contexts can run at the same moment. Version 0.10.
//
// ADDED BECAUSE ITS ABSENCE WAS A WRONG ANSWER RATHER THAN A REFUSAL.
// `KAL_TASK_PROP_PARALLEL' says WHETHER and not HOW MANY, so a C library above
// had nowhere to look and `hardware_concurrency()' answered 1 with no error ---
// a program sizing a pool of workers got one worker and no way to know.
// Measured: 1 through openkal-musl against 32 on the same machine.
//
// THE SET THIS CONTEXT MAY RUN ON, not the set the machine has. A program
// confined to two processors is asked to size itself against two; asking the
// machine would have it size against a number it cannot use.
kal_uintptr kal_task_parallelism(void) {
    // The kernel writes a bitmap and reports how many BYTES of it it wrote.
    unsigned long mask[128] = { 0 };   // 8192 processors, which is this kernel's own bound
    const okl_long n = okl::sys(okl::nr_sched_getaffinity, 0,
                                static_cast<okl_long>(sizeof mask),
                                reinterpret_cast<okl_long>(mask));
    if (okl::failed(n) || n <= 0) return 0;   // 0 is "cannot say", and is not 1

    kal_uintptr count = 0;
    const okl_long words = n / static_cast<okl_long>(sizeof(unsigned long));
    for (okl_long i = 0; i < words; ++i)
        for (unsigned long bit = mask[i]; bit; bit &= bit - 1) ++count;
    return count;
}

// The primitive. It is the operation a caller cannot construct: the comparison
// and the suspension occur without an intervening opportunity for the value to
// change unobserved, and only the environment can arrange that.
int kal_task_wait(const kal_u32* word, kal_u32 expected,
                  kal_u64 timeout_ns) {
    okl::ktimespec ts{};
    okl_long tp = 0;
    if (timeout_ns != 0) {
        ts.sec  = static_cast<okl_i64>(timeout_ns / 1000000000u);
        ts.nsec = static_cast<okl_i64>(timeout_ns % 1000000000u);
        tp = reinterpret_cast<okl_long>(&ts);
    }
    for (;;) {
        const okl_long r = okl::sys(okl::nr_futex, reinterpret_cast<okl_long>(word),
                                    okl::futex_wait | okl::futex_private,
                                    static_cast<okl_long>(expected), tp, 0, 0);
        if (!okl::failed(r)) return kal_ok;
        // The value had already changed, which is a successful outcome: the
        // caller's condition no longer holds and it should re-examine it.
        if (r == -okl::e_again)    return kal_ok;
        if (okl::interrupted(r))   continue;
        if (r == -okl::e_timedout) return kal_err_again;
        return okl::translate(r);
    }
}

int kal_task_wake(const kal_u32* word, kal_uintptr count, kal_uintptr* woken) {
    const okl_long r = okl::sys(okl::nr_futex, reinterpret_cast<okl_long>(word),
                                okl::futex_wake | okl::futex_private,
                                static_cast<okl_long>(count), 0, 0, 0);
    if (okl::failed(r)) return okl::translate(r);
    if (woken) *woken = static_cast<kal_uintptr>(r);
    return kal_ok;
}

// The thread-local position is reported in both configurations, and it is true
// in both for different reasons: the C library's threads establish the
// convention, and so does the block this implementation builds. Clause 7.10.
kal_uintptr kal_task_props(void) {
    return KAL_TASK_PROP_PREEMPTIVE | KAL_TASK_PROP_PARALLEL
         | KAL_TASK_PROP_WAIT_TIMEOUT | KAL_TASK_PROP_THREAD_LOCAL;
}

}
