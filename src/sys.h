// The system call interface of the Linux kernel, and nothing else.
//
// openkal is a contract and says nothing about what else a program contains.
// That silence is the reason for this file.
//
// An implementation is selected by the program, and the program may itself
// supply the facilities the implementation would otherwise borrow. If it does,
// and the names agree, there is one definition of each name in the program:
// the implementation's calls resolve to the program's, and the program's
// resolve back to the implementation. The recursion is unbounded and it is not
// visible in either side's source.
//
// Version 0.4 of this implementation borrowed the host's C library. That is a
// correct implementation of openkal for a program that borrows nothing, and it
// is wrong for a program that supplies its own --- which the specification
// permits and clause 1 names first among the consumers it expects. The
// specification says an implementation may be built upon a C library, beneath
// one, or without one; only an implementation that borrows nothing can be the
// second and third.
//
// This implementation therefore contains the kernel's calling convention and no
// reference to a facility any program might also define. The property is
// asserted by CI, which examines the undefined symbols of the produced objects.
//
// A second consequence is less obvious and equally binding. A structure passed
// to the kernel has the kernel's layout, which is not the layout of the
// same-named structure in any particular runtime: two C libraries' `struct
// stat' differ, and an implementation compiled against one and linked into a
// program carrying the other would read the wrong fields. The kernel's own
// layouts are therefore declared here.
#pragma once

using okl_long  = long;
using okl_ulong = unsigned long;
using okl_u64   = unsigned long long;
using okl_i64   = long long;
using okl_u32   = unsigned;
using okl_i32   = int;
using okl_uptr  = __UINTPTR_TYPE__;

namespace okl {

// --- the calling convention ------------------------------------------------

#if defined(__x86_64__)

// EVERY ARGUMENT REGISTER IS AN IN/OUT OPERAND (0.18). The kernel preserves
// them, and the operands used to say so; a system-call translator does not.
// termux's PRoot, in its default seccomp mode, rewrites the register that
// carries the path of `openat' and leaves it rewritten, so a value the compiler
// kept "in rsi" across the call was the translator's and not the program's
// --- measured: the `"/"' preopen lost its name and every program path was
// "outside every preopened directory". Declaring the registers written costs
// nothing the kernel's own convention did not already allow.
inline okl_long sys(okl_long n) {
    okl_ulong r;
    __asm__ __volatile__("syscall" : "=a"(r) : "a"(n) : "rcx", "r11", "memory");
    return static_cast<okl_long>(r);
}
inline okl_long sys(okl_long n, okl_long a) {
    okl_ulong r;
    __asm__ __volatile__("syscall" : "=a"(r), "+D"(a) : "a"(n) : "rcx", "r11", "memory");
    return static_cast<okl_long>(r);
}
inline okl_long sys(okl_long n, okl_long a, okl_long b) {
    okl_ulong r;
    __asm__ __volatile__("syscall" : "=a"(r), "+D"(a), "+S"(b) : "a"(n) : "rcx", "r11", "memory");
    return static_cast<okl_long>(r);
}
inline okl_long sys(okl_long n, okl_long a, okl_long b, okl_long c) {
    okl_ulong r;
    __asm__ __volatile__("syscall" : "=a"(r), "+D"(a), "+S"(b), "+d"(c) : "a"(n)
                         : "rcx", "r11", "memory");
    return static_cast<okl_long>(r);
}
inline okl_long sys(okl_long n, okl_long a, okl_long b, okl_long c, okl_long d) {
    okl_ulong r; register okl_long r10 __asm__("r10") = d;
    __asm__ __volatile__("syscall" : "=a"(r), "+D"(a), "+S"(b), "+d"(c), "+r"(r10) : "a"(n)
                         : "rcx", "r11", "memory");
    return static_cast<okl_long>(r);
}
inline okl_long sys(okl_long n, okl_long a, okl_long b, okl_long c, okl_long d, okl_long e) {
    okl_ulong r; register okl_long r10 __asm__("r10") = d; register okl_long r8 __asm__("r8") = e;
    __asm__ __volatile__("syscall" : "=a"(r), "+D"(a), "+S"(b), "+d"(c), "+r"(r10), "+r"(r8)
                         : "a"(n) : "rcx", "r11", "memory");
    return static_cast<okl_long>(r);
}
inline okl_long sys(okl_long n, okl_long a, okl_long b, okl_long c, okl_long d, okl_long e, okl_long f) {
    okl_ulong r; register okl_long r10 __asm__("r10") = d; register okl_long r8 __asm__("r8") = e;
    register okl_long r9 __asm__("r9") = f;
    __asm__ __volatile__("syscall"
                         : "=a"(r), "+D"(a), "+S"(b), "+d"(c), "+r"(r10), "+r"(r8), "+r"(r9)
                         : "a"(n) : "rcx", "r11", "memory");
    return static_cast<okl_long>(r);
}

enum : okl_long {
    nr_read = 0, nr_write = 1, nr_close = 3, nr_fstat = 5, nr_lseek = 8,
    nr_mmap = 9, nr_mprotect = 10, nr_munmap = 11, nr_ioctl = 16, nr_msync = 26,
    nr_readv = 19, nr_writev = 20, nr_sched_yield = 24, nr_nanosleep = 35,
    nr_getpid = 39, nr_clone = 56, nr_execve = 59, nr_exit = 60, nr_wait4 = 61,
    nr_kill = 62, nr_ftruncate = 77, nr_getcwd = 79, nr_fsync = 74,
    nr_fcntl = 72, nr_prctl = 157, nr_sched_getaffinity = 204, nr_getppid = 110,
    nr_arch_prctl = 158, nr_gettid = 186, nr_futex = 202,
    nr_getdents64 = 217, nr_set_tid_address = 218, nr_clock_gettime = 228,
    nr_clock_getres = 229, nr_exit_group = 231, nr_tgkill = 234,
    nr_openat = 257, nr_mkdirat = 258, nr_newfstatat = 262, nr_unlinkat = 263,
    nr_renameat = 264, nr_readlinkat = 267, nr_dup3 = 292, nr_execveat = 322,
    nr_close_range = 436, nr_prlimit64 = 302,
    nr_dup2 = 33, nr_utimensat = 280, nr_symlinkat = 266, nr_linkat = 265, nr_fstatfs = 138,
    nr_getrandom = 318, nr_sched_setscheduler = 144,
    // openkal 0.15: the region a context stands on is measured by asking the
    // kernel which pages are mapped. The floor comes from the limit the kernel
    // itself enforces, which `nr_prlimit64' above already names.
    nr_mincore = 27,
    // openkal 0.13: whether a node may be started
    nr_fchmodat = 268,
    // openkal 0.11: the directory a started program runs in, and the unit it joins
    nr_fchdir = 81, nr_setpgid = 109, nr_getpgid = 121, nr_rt_sigaction = 13,
    // openkal.net and openkal.datagram
    nr_socket = 41, nr_connect = 42, nr_accept = 43, nr_sendto = 44,
    nr_recvfrom = 45, nr_shutdown = 48, nr_bind = 49, nr_listen = 50,
    nr_getsockname = 51, nr_getpeername = 52, nr_accept4 = 288,
    nr_setsockopt = 54, nr_pipe2 = 293,
    // openkal.timeout. ppoll and not poll: the bound is stated in nanoseconds
    // and poll takes milliseconds, so poll could not express a bound finer than
    // the granularity this implementation reports.
    nr_ppoll = 271,
    // openkal 0.17: a rename that does not replace, a program found by its
    // number, how one ended, and the request reproduced.
    nr_renameat2 = 316, nr_pidfd_open = 434, nr_waitid = 247,
    nr_rt_sigprocmask = 14,
    // openkal 0.20: transfers at an offset, a range's backing returned, and
    // whether a file may be written by this program (a control group's).
    nr_pread64 = 17, nr_pwrite64 = 18, nr_madvise = 28, nr_faccessat = 269,
    // openkal 0.23: a part sealed, a listing with attributes, page faults
    // counted, a fault reported, samples taken, a program confined, and what
    // memory is available.
    nr_mseal = 462, nr_statx = 332, nr_getrusage = 98, nr_sigaltstack = 131,
    nr_setitimer = 38, nr_rt_sigreturn = 15, nr_process_vm_readv = 310,
    nr_landlock_create_ruleset = 444, nr_landlock_add_rule = 445,
    nr_landlock_restrict_self = 446, nr_sysinfo = 99,
};

#elif defined(__aarch64__)

#define OKL_SVC(...) \
    register okl_long x8 __asm__("x8") = n; \
    register okl_long x0 __asm__("x0") = a0; \
    __VA_ARGS__ \
    __asm__ __volatile__("svc 0" : "+r"(x0) : OKL_IN : "memory", "cc"); \
    return x0;

inline okl_long sys(okl_long n) {
    register okl_long x8 __asm__("x8") = n; register okl_long x0 __asm__("x0");
    __asm__ __volatile__("svc 0" : "=r"(x0) : "r"(x8) : "memory", "cc");
    return x0;
}
inline okl_long sys(okl_long n, okl_long a) {
    register okl_long x8 __asm__("x8") = n; register okl_long x0 __asm__("x0") = a;
    __asm__ __volatile__("svc 0" : "+r"(x0) : "r"(x8) : "memory", "cc");
    return x0;
}
inline okl_long sys(okl_long n, okl_long a, okl_long b) {
    register okl_long x8 __asm__("x8") = n; register okl_long x0 __asm__("x0") = a;
    register okl_long x1 __asm__("x1") = b;
    __asm__ __volatile__("svc 0" : "+r"(x0), "+r"(x1) : "r"(x8) : "memory", "cc");
    return x0;
}
inline okl_long sys(okl_long n, okl_long a, okl_long b, okl_long c) {
    register okl_long x8 __asm__("x8") = n; register okl_long x0 __asm__("x0") = a;
    register okl_long x1 __asm__("x1") = b; register okl_long x2 __asm__("x2") = c;
    __asm__ __volatile__("svc 0" : "+r"(x0), "+r"(x1), "+r"(x2) : "r"(x8) : "memory", "cc");
    return x0;
}
inline okl_long sys(okl_long n, okl_long a, okl_long b, okl_long c, okl_long d) {
    register okl_long x8 __asm__("x8") = n; register okl_long x0 __asm__("x0") = a;
    register okl_long x1 __asm__("x1") = b; register okl_long x2 __asm__("x2") = c;
    register okl_long x3 __asm__("x3") = d;
    __asm__ __volatile__("svc 0" : "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3) : "r"(x8)
                         : "memory", "cc");
    return x0;
}
inline okl_long sys(okl_long n, okl_long a, okl_long b, okl_long c, okl_long d, okl_long e) {
    register okl_long x8 __asm__("x8") = n; register okl_long x0 __asm__("x0") = a;
    register okl_long x1 __asm__("x1") = b; register okl_long x2 __asm__("x2") = c;
    register okl_long x3 __asm__("x3") = d; register okl_long x4 __asm__("x4") = e;
    __asm__ __volatile__("svc 0" : "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3), "+r"(x4)
                         : "r"(x8) : "memory", "cc");
    return x0;
}
inline okl_long sys(okl_long n, okl_long a, okl_long b, okl_long c, okl_long d,
                    okl_long e, okl_long f) {
    register okl_long x8 __asm__("x8") = n; register okl_long x0 __asm__("x0") = a;
    register okl_long x1 __asm__("x1") = b; register okl_long x2 __asm__("x2") = c;
    register okl_long x3 __asm__("x3") = d; register okl_long x4 __asm__("x4") = e;
    register okl_long x5 __asm__("x5") = f;
    __asm__ __volatile__("svc 0" : "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3), "+r"(x4), "+r"(x5)
                         : "r"(x8) : "memory", "cc");
    return x0;
}

enum : okl_long {
    nr_getcwd = 17, nr_ioctl = 29, nr_mkdirat = 34, nr_unlinkat = 35,
    nr_ftruncate = 46, nr_openat = 56, nr_close = 57, nr_getdents64 = 61,
    nr_lseek = 62, nr_read = 63, nr_write = 64, nr_readv = 65, nr_writev = 66,
    nr_readlinkat = 78, nr_newfstatat = 79, nr_fstat = 80, nr_fsync = 82,
    nr_exit = 93, nr_exit_group = 94, nr_set_tid_address = 96, nr_futex = 98,
    nr_nanosleep = 101, nr_clock_gettime = 113, nr_clock_getres = 114,
    nr_sched_yield = 124, nr_kill = 129, nr_tgkill = 131, nr_gettid = 178,
    nr_getpid = 172, nr_mmap = 222, nr_munmap = 215, nr_mprotect = 226, nr_msync = 227,
    nr_clone = 220, nr_execve = 221, nr_wait4 = 260, nr_renameat = 38,
    nr_dup3 = 24, nr_execveat = 281,
    nr_close_range = 436, nr_prlimit64 = 261, nr_dup2 = -1, nr_fcntl = 25,
    nr_prctl = 167, nr_sched_getaffinity = 123, nr_getppid = 173,
    nr_arch_prctl = -1, nr_utimensat = 88, nr_symlinkat = 36, nr_linkat = 37, nr_fstatfs = 44,
    nr_getrandom = 278, nr_sched_setscheduler = 119,
    // openkal 0.15: the region a context stands on is measured by asking the
    // kernel which pages are mapped. The floor comes from the limit the kernel
    // itself enforces, which `nr_prlimit64' above already names --- and the two
    // numbers differ from the other architecture's because this one takes the
    // architecture-independent table for everything it can.
    nr_mincore = 232,
    // openkal 0.13: whether a node may be started
    nr_fchmodat = 53,
    // openkal 0.11: the directory a started program runs in, and the unit it joins
    nr_fchdir = 50, nr_setpgid = 154, nr_getpgid = 155, nr_rt_sigaction = 134,
    // openkal.net and openkal.datagram
    nr_socket = 198, nr_connect = 203, nr_accept = 202, nr_sendto = 206,
    nr_recvfrom = 207, nr_shutdown = 210, nr_bind = 200, nr_listen = 201,
    nr_getsockname = 204, nr_getpeername = 205, nr_accept4 = 242,
    nr_setsockopt = 208, nr_pipe2 = 59,
    // openkal.timeout. This architecture has no `poll' at all, only `ppoll',
    // which is a second reason the bound is expressed through the latter.
    nr_ppoll = 73,
    // openkal 0.17, as on the other architecture.
    nr_renameat2 = 276, nr_pidfd_open = 434, nr_waitid = 95,
    nr_rt_sigprocmask = 135,
    // openkal 0.20, as on the other architecture.
    nr_pread64 = 67, nr_pwrite64 = 68, nr_madvise = 233, nr_faccessat = 48,
    // openkal 0.23, as on the other architecture.
    nr_mseal = 462, nr_statx = 291, nr_getrusage = 165, nr_sigaltstack = 132,
    nr_setitimer = 103, nr_rt_sigreturn = 139, nr_process_vm_readv = 270,
    nr_landlock_create_ruleset = 444, nr_landlock_add_rule = 445,
    nr_landlock_restrict_self = 446, nr_sysinfo = 179,
};

#elif defined(__i386__)

// THE GATE IS `int $0x80', AND THE SIXTH ARGUMENT IS IN THE FRAME POINTER. The
// kernel's i386 convention takes the number in eax and the arguments in ebx,
// ecx, edx, esi, edi and ebp. ebp cannot be named as an operand of an inline
// statement in a function that has a frame, so the sixth is pushed first and
// moved into ebp between saving and restoring it --- the sequence every C
// library for this architecture uses.
inline okl_long sys(okl_long n) {
    okl_long r;
    __asm__ __volatile__("int $0x80" : "=a"(r) : "a"(n) : "memory");
    return r;
}
inline okl_long sys(okl_long n, okl_long a) {
    okl_long r;
    __asm__ __volatile__("int $0x80" : "=a"(r) : "a"(n), "b"(a) : "memory");
    return r;
}
inline okl_long sys(okl_long n, okl_long a, okl_long b) {
    okl_long r;
    __asm__ __volatile__("int $0x80" : "=a"(r) : "a"(n), "b"(a), "c"(b) : "memory");
    return r;
}
inline okl_long sys(okl_long n, okl_long a, okl_long b, okl_long c) {
    okl_long r;
    __asm__ __volatile__("int $0x80" : "=a"(r) : "a"(n), "b"(a), "c"(b), "d"(c) : "memory");
    return r;
}
inline okl_long sys(okl_long n, okl_long a, okl_long b, okl_long c, okl_long d) {
    okl_long r;
    __asm__ __volatile__("int $0x80" : "=a"(r) : "a"(n), "b"(a), "c"(b), "d"(c), "S"(d) : "memory");
    return r;
}
inline okl_long sys(okl_long n, okl_long a, okl_long b, okl_long c, okl_long d, okl_long e) {
    okl_long r;
    __asm__ __volatile__("int $0x80" : "=a"(r) : "a"(n), "b"(a), "c"(b), "d"(c), "S"(d), "D"(e) : "memory");
    return r;
}
inline okl_long sys(okl_long n, okl_long a, okl_long b, okl_long c, okl_long d, okl_long e, okl_long f) {
    okl_long r;
    __asm__ __volatile__("pushl %7\n\tpush %%ebp\n\tmov 4(%%esp),%%ebp\n\tint $0x80\n\t"
                         "pop %%ebp\n\tadd $4,%%esp"
                         : "=a"(r)
                         : "a"(n), "b"(a), "c"(b), "d"(c), "S"(d), "D"(e), "g"(f)
                         : "memory");
    return r;
}

// THE NUMBERS ARE THIS ARCHITECTURE'S OWN TABLE (arch/x86/entry/syscalls/
// syscall_32.tbl), and where a call has a sixty-four bit time form the name
// below is that form (clause 5.5 of the specification): clock_gettime64,
// clock_getres_time64, ppoll_time64, futex_time64 and utimensat_time64 take the
// same arguments as the names they stand for, with the kernel's sixty-four bit
// timespec, which is `ktimespec'. The calls whose ARGUMENTS differ here --- an
// offset in two words, a record of another layout, another order --- have no
// number of the common name; they are reached through the functions below the
// tables, which every caller uses on every architecture.
enum : okl_long {
    nr_read = 3, nr_write = 4, nr_close = 6, nr_fstat = -1, nr_lseek = -1,
    nr_mmap = -1, nr_mprotect = 125, nr_munmap = 91, nr_ioctl = 54, nr_msync = 144,
    nr_readv = 145, nr_writev = 146, nr_sched_yield = 158, nr_nanosleep = -1,
    nr_getpid = 20, nr_clone = 120, nr_execve = 11, nr_exit = 1, nr_wait4 = 114,
    nr_kill = 37, nr_ftruncate = -1, nr_getcwd = 183, nr_fsync = 118,
    nr_fcntl = 221 /* fcntl64: the record locks' positions are sixty-four bits */,
    nr_prctl = 172, nr_sched_getaffinity = 242, nr_getppid = 64,
    nr_arch_prctl = -1, nr_gettid = 224, nr_futex = 422,
    nr_getdents64 = 220, nr_set_tid_address = 258, nr_clock_gettime = 403,
    nr_clock_getres = 406, nr_exit_group = 252, nr_tgkill = 270,
    nr_openat = 295, nr_mkdirat = 296, nr_newfstatat = -1, nr_unlinkat = 301,
    nr_renameat = 302, nr_readlinkat = 305, nr_dup3 = 330, nr_execveat = 358,
    nr_close_range = 436, nr_prlimit64 = 340,
    nr_dup2 = 63, nr_utimensat = 412, nr_symlinkat = 304, nr_linkat = 303, nr_fstatfs = -1,
    nr_getrandom = 355, nr_sched_setscheduler = 156,
    nr_mincore = 218,
    nr_fchmodat = 306,
    nr_fchdir = 133, nr_setpgid = 57, nr_getpgid = 132, nr_rt_sigaction = 174,
    // Each call of a socket has its own number from kernel 4.3; `accept' has
    // none here, and `accept4' with no flags is the same call.
    nr_socket = 359, nr_connect = 362, nr_accept = -1, nr_sendto = 369,
    nr_recvfrom = 371, nr_shutdown = 373, nr_bind = 361, nr_listen = 363,
    nr_getsockname = 367, nr_getpeername = 368, nr_accept4 = 364,
    nr_setsockopt = 366, nr_pipe2 = 331,
    nr_ppoll = 414,
    nr_renameat2 = 353, nr_pidfd_open = 434, nr_waitid = 284,
    nr_rt_sigprocmask = 175,
    nr_pread64 = -1, nr_pwrite64 = -1, nr_madvise = 219, nr_faccessat = 307,
    nr_mseal = 462, nr_statx = 383, nr_getrusage = 77, nr_sigaltstack = 186,
    nr_setitimer = 104, nr_rt_sigreturn = 173, nr_process_vm_readv = 347,
    nr_landlock_create_ruleset = 444, nr_landlock_add_rule = 445,
    nr_landlock_restrict_self = 446, nr_sysinfo = 116,
    // Only here: the forms the functions below reach, and the thread pointer's
    // segment.
    nr_llseek = 140, nr_mmap2 = 192, nr_ftruncate64 = 194, nr_pread64_32 = 180,
    nr_pwrite64_32 = 181, nr_clock_nanosleep_time64 = 407, nr_fstatfs64 = 269,
    nr_set_thread_area = 243,
};

#else
#error "openkal-linux supports x86_64, aarch64 and i386"
#endif

// --- error values, as the kernel returns them ------------------------------
//
// A failing system call returns the error negated, which is why the values
// appear here rather than being taken from a header: the header belongs to a
// C library and this implementation has none.
enum : int {
    e_perm = 1, e_noent = 2, e_intr = 4, e_io = 5, e_noexec = 8, e_badf = 9,
    e_child = 10,
    e_again = 11, e_nomem = 12, e_acces = 13, e_fault = 14, e_busy = 16,
    e_exist = 17, e_xdev = 18, e_nodev = 19, e_notdir = 20, e_isdir = 21,
    e_inval = 22, e_nfile = 23, e_mfile = 24, e_notty = 25, e_fbig = 27,
    e_nospc = 28, e_spipe = 29, e_rofs = 30, e_pipe = 32, e_range = 34,
    e_nametoolong = 36, e_nosys = 38, e_notempty = 39, e_loop = 40,
    e_timedout = 110, e_connreset = 104, e_dquot = 122,
};

// --- constants the kernel defines ------------------------------------------
//
// THREE OF THESE ARE NOT THE SAME NUMBER ON BOTH ARCHITECTURES, AND WERE
// WRITTEN AS THOUGH THEY WERE.
//
// `O_DIRECTORY', `O_NOFOLLOW' and `O_DIRECT' have one set of values on x86_64
// and another in the kernel's architecture-independent header, which is what
// aarch64 uses. The three values below were x86_64's:
//
//                     x86_64      aarch64 (asm-generic)
//     O_DIRECTORY     0200000     040000
//     O_NOFOLLOW      0400000     0100000
//     O_DIRECT        040000      0200000
//
// So on aarch64 this implementation asked for O_DIRECT where it meant
// O_DIRECTORY. The kernel refuses O_DIRECT on a directory, so EVERY DIRECTORY
// THIS IMPLEMENTATION TRIED TO OPEN FAILED --- including the two preopens it
// supplies at inception, which is every directory a program above it can reach.
//
// MEASURED, and the reading is unambiguous: on aarch64 `kal_fs_preopen_count'
// answered two and both entries reported `kal_err_permission' with a handle of
// zero, while the same program on x86_64 reported both directories and opened a
// file in one. A C library above it then had no directory to resolve a name
// against, so every `open' answered ENOENT and `getcwd' answered "/" --- which
// reads as a program started somewhere odd rather than as an implementation
// that opened nothing.
//
// Nothing caught it. The conformance suite is run on x86_64; this package is
// built for aarch64 and the build succeeds, because a wrong constant is a
// number and not a type error.
enum : okl_long {
    o_rdonly = 0, o_wronly = 1, o_rdwr = 2,
    o_creat = 0100, o_excl = 0200, o_trunc = 01000, o_append = 02000,
    o_cloexec = 02000000,

    // A descriptor that only NAMES a node, and the empty name `execveat' accepts
    // for it. See the start in `kal_process_spawn'.
    o_path = 010000000, at_empty_path = 0x1000,

    // The descriptor flag, set and cleared one descriptor at a time, and the
    // form of `close_range' that sets it on every descriptor of a range.
    f_setfd = 2, fd_cloexec = 1, close_range_cloexec = 4, rlimit_nofile = 7,

    // THE LOWEST FREE DESCRIPTOR AT OR ABOVE A BOUND, which is the one
    // primitive that moves a descriptor out of the way WITHOUT NAMING the
    // number it moves to --- and therefore without closing whatever a caller
    // already had there. `dup3' cannot do this: it is told the number, and it
    // closes what is on it.
    f_dupfd_cloexec = 1030,

    // THE OPEN-FILE FORM AND NOT THE PROCESS FORM, WHICH IS THE WHOLE
    // DIFFERENCE.
    //
    // This kernel's oldest record lock is held by the PROCESS, and it is
    // released as soon as that process closes ANY descriptor for the node ---
    // so a library that opened one file twice destroyed its own lock, and two
    // parts of one program could not exclude each other at all. openkal states
    // the holder as the `kal_file', which is exactly what these describe: the
    // lock belongs to the open file description and ends when the last
    // descriptor for it closes.
    f_ofd_getlk = 36, f_ofd_setlk = 37, f_ofd_setlkw = 38,

    // A lock's kind, and where a range begins.
    lock_read = 0, lock_write = 1, lock_unlock = 2,
    seek_set = 0,

    // "End this context when the one that started it ends", which is what
    // binding a started program's lifetime is expressed as here.
    pr_set_pdeathsig = 1,
#if defined(__x86_64__) || defined(__i386__)
    o_directory = 0200000, o_nofollow = 0400000,
#else
    o_directory = 040000,  o_nofollow = 0100000,
#endif
    at_fdcwd = -100, at_removedir = 0x200, at_symlink_nofollow = 0x100,
    prot_read = 1, prot_write = 2, prot_exec = 4, prot_none = 0,
    map_shared = 1, map_private = 2, map_anonymous = 0x20, map_stack = 0x20000,
    map_fixed = 0x10, map_noreserve = 0x4000,
    madv_dontneed = 4, madv_free = 8,
    ms_async = 1,
    clock_monotonic = 1, clock_realtime = 0,
    futex_wait = 0, futex_wake = 1, futex_private = 128,
};

// --- translation -----------------------------------------------------------
//
// The kernel's values are mapped onto the closed set the specification
// defines. A table preserves the naturalness clause 7.1 requires;
// reconstructing a foreign namespace would not.
inline int translate(okl_long r) {
    const int e = static_cast<int>(-r);
    switch (e) {
        case e_badf: case e_inval: case e_fault: case e_nametoolong:
        case e_loop: case e_spipe:                       return 1;  // kal_err_invalid
        case e_again:                                    return 2;  // kal_err_again
        case e_nomem:                                    return 4;  // kal_err_no_memory
        case e_nospc: case e_fbig: case e_dquot:         return 5;  // kal_err_no_space
        case e_acces: case e_perm: case e_rofs:          return 6;  // kal_err_permission
        case e_nosys:                                    return 7;  // kal_err_not_supported
        case e_pipe: case e_connreset:                   return 8;  // kal_err_closed
        case e_noent: case e_child: case e_nodev:        return 9;  // kal_err_not_found
        case e_exist: case e_busy:                       return 10; // kal_err_exists
        case e_notempty:                                 return 11; // kal_err_not_empty
        case e_isdir:                                    return 12; // kal_err_is_directory
        case e_notdir:                                   return 13; // kal_err_not_directory
        case e_noexec:                                   return 14; // kal_err_not_program
        default:                                         return 3;  // kal_err_io
    }
}

inline bool failed(okl_long r) {
    return r < 0 && r > -4096;
}

// A call the kernel interrupts is retried rather than reported. Clause 7.5: a
// caller cannot distinguish an interrupted call from a genuine failure without
// knowledge of the environment, and an implementation that reports it produces
// short transfers on any system that delivers asynchronous notifications.
inline bool interrupted(okl_long r) { return r == -e_intr; }

// --- the kernel's structure layouts ----------------------------------------

// What the kernel reports about the volume a descriptor is on. The layout is
// the kernel's own `struct statfs', which is one layout on every architecture
// this implementation supports because both are LP64.
// The kernel's own `struct flock'. One layout on both architectures this
// implementation builds for: two shorts, padded to eight, then two 64-bit
// positions and the identifier of a holder this implementation never asks about.
struct kflock {
    short    l_type;
    short    l_whence;
#if !defined(__i386__)
    int      l_pad;       // i386's `struct flock64' aligns the positions to four
#endif
    okl_i64  l_start;
    okl_i64  l_len;
    int      l_pid;
    int      l_pad2;
};

struct kstatfs {
    okl_long f_type;
    okl_long f_bsize;
    okl_u64  f_blocks;
    okl_u64  f_bfree;
    okl_u64  f_bavail;
    okl_u64  f_files;
    okl_u64  f_ffree;
    okl_u64  f_fsid;
    okl_long f_namelen;
    okl_long f_frsize;
    okl_long f_flags;
    okl_long f_spare[4];
};

// The magic numbers the kernel reports in `f_type', from its own uapi header.
// A property that varies between the RESOURCES of an interface is answered by
// an enquiry taking the resource, and on this kernel the resource's format is
// what the enquiry has to consult.
enum : okl_ulong {
    fs_ext234    = 0xEF53,
    fs_btrfs     = 0x9123683E,
    fs_xfs       = 0x58465342,
    fs_f2fs      = 0xF2F52010,
    fs_tmpfs     = 0x01021994,
    fs_overlay   = 0x794C7630,
    fs_zfs       = 0x2FC12FC1,
    fs_bcachefs  = 0xCA451A4E,
    fs_msdos     = 0x4D44,        // vfat, and every FAT before it
    fs_exfat     = 0x2011BAB0,
    fs_ntfs      = 0x5346544E,
    fs_ntfs3     = 0x7366746E,
    fs_iso9660   = 0x9660,
    fs_squashfs  = 0x73717368,
    fs_erofs     = 0xE0F5E1E2,
    fs_hfsplus   = 0x482B,
};

// On i386 this record is not the kernel's: its `stat64' carries thirty-two bit
// times, and clause 5.5 of the specification asks for the sixty-four bit form,
// so `fstat' and `fstatat' below fill it from `statx'. It has x86_64's order.
struct kstat {
    okl_u64 dev;
    okl_u64 ino;
#if defined(__x86_64__) || defined(__i386__)
    okl_u64 nlink;
    okl_u32 mode;
    okl_u32 uid;
    okl_u32 gid;
    okl_u32 pad0;
    okl_u64 rdev;
    okl_i64 size;
    okl_i64 blksize;
    okl_i64 blocks;
#else
    // THE FIELDS OF THIS ARCHITECTURE'S RECORD WERE IN THE WRONG ORDER, AND
    // THE BUILD COULD NOT SAY SO.
    //
    // The kernel's architecture-independent `struct stat' --- which aarch64 uses
    // --- places the mode immediately after the device and inode, and the size
    // after the device number and one word of padding. The order below was
    // neither: the mode was read from offset 60 where the kernel writes a
    // group, and the size from 32 where it writes a device number.
    //
    // MEASURED, the same program on both architectures:
    //
    //     x86_64    file: kind=1 size=10 writable=1   link: kind=3
    //     aarch64   file: kind=4 size=0  writable=0   link: kind=4
    //
    // Every node on aarch64 was "some other kind of thing", of length zero and
    // not writable. A C library above it reported that a file it had just
    // written ten bytes to was not a regular file --- and every operation that
    // decides upon a kind, which is most of `std::filesystem', decided wrongly.
    //
    // NOTHING IN THIS ECOSYSTEM COULD HAVE CAUGHT IT. The conformance suite
    // runs on the machine that builds it, and every hosted machine in this
    // ecosystem's continuous integration is x86_64 or an arm64 Mac --- which
    // uses openkal-macos and a different record again. The aarch64 leg of THIS
    // implementation is built and never run. The offsets are asserted below so
    // that the next such error is a build failure rather than a wrong answer.
    okl_u32 mode;
    okl_u32 nlink;
    okl_u32 uid;
    okl_u32 gid;
    okl_u64 rdev;
    okl_u64 pad1;
    okl_i64 size;
    okl_u32 blksize;
    okl_u32 pad2;
    okl_i64 blocks;
#endif
    okl_i64 atime_sec, atime_nsec;
    okl_i64 mtime_sec, mtime_nsec;
    okl_i64 ctime_sec, ctime_nsec;
    okl_i64 unused[3];
};

// The kernel writes this record; a field read from the wrong offset is a wrong
// answer and not a failure, so the offsets are stated here rather than trusted.
#if defined(__i386__)
// Not the kernel's record: nothing to state.
#elif defined(__x86_64__)
static_assert(__builtin_offsetof(kstat, mode)      == 24, "x86_64 struct stat");
static_assert(__builtin_offsetof(kstat, size)      == 48, "x86_64 struct stat");
static_assert(__builtin_offsetof(kstat, mtime_sec) == 88, "x86_64 struct stat");
#else
static_assert(__builtin_offsetof(kstat, mode)      == 16, "asm-generic struct stat");
static_assert(__builtin_offsetof(kstat, size)      == 48, "asm-generic struct stat");
static_assert(__builtin_offsetof(kstat, mtime_sec) == 88, "asm-generic struct stat");
#endif
static_assert(__builtin_offsetof(kstat, ino) == 8, "the inode follows the device");
#if !defined(__i386__)
static_assert(sizeof(kstat) >= 128, "the kernel writes at least this much");
#endif

enum : okl_u32 {
    s_ifmt = 0170000, s_ifreg = 0100000, s_ifdir = 0040000, s_iflnk = 0120000,
};

struct ktimespec { okl_i64 sec; okl_i64 nsec; };

// The kernel's `struct sigaction', as `rt_sigaction' takes it: the mask is the
// kernel's sixty-four bit set on every architecture this implementation
// supports --- two words on i386, where an `unsigned long' mask was half of it
// and the size the call is told (`sizeof mask') was refused. One declaration for
// every unit that installs a handler.
// The kernel's signal set: sixty-four bits on every architecture this
// implementation supports, two words on i386 --- where an `unsigned long' set
// was half of it and the size a call is told was refused (measured:
// `rt_sigprocmask(..., 4) = -EINVAL', and no program could be started).
using ksigset = okl_u64;

// THE KERNEL'S `siginfo_t' AS `waitid' WRITES IT: three ints (signal, error,
// code), then the union, aligned to a pointer --- at sixteen on a sixty-four bit
// word and twelve on i386, where reading at sixteen took the user for the
// identifier and an exit status of 33 was reported as 0 (measured). For a child:
// the identifier, the user, then the status.
constexpr okl_uptr si_code_at   = 8;
constexpr okl_uptr si_fields_at = sizeof(void*) == 8 ? 16 : 12;
constexpr okl_uptr si_pid_at    = si_fields_at;
constexpr okl_uptr si_status_at = si_fields_at + 8;
struct k_sigaction { void* handler; okl_ulong flags; void* restorer; ksigset mask; };
constexpr okl_ulong sa_restorer_flag = 0x04000000u;  // SA_RESTORER

}  // namespace okl
// The return from a handler this implementation installs, which a handler
// installed without a C library must name on x86_64 and i386 (observe.cpp).
extern "C" void okl_signal_return();
namespace okl {

// --- the calls whose arguments are not the same on every architecture ----------
//
// EVERY CALLER USES THESE, ON EVERY ARCHITECTURE. On x86_64 and aarch64 each is
// the call itself. On i386 a sixty-four bit position travels in two words
// (`_llseek', `mmap2' in pages, `pread64', `ftruncate64'), the status of a node
// comes from `statx' (the `stat64' record's times are thirty-two bits), and
// `nanosleep' is `clock_nanosleep_time64'. A caller that named the common
// number directly would compile on i386 and pass a truncated position: the
// numbers above are -1 there, so such a call fails rather than misbehaves.
#if defined(__i386__)
struct kstatx_time { okl_i64 sec; okl_u32 nsec; okl_i32 pad; };
struct kstatx {
    okl_u32 mask, blksize;
    okl_u64 attributes;
    okl_u32 nlink, uid, gid;
    unsigned short mode, pad0;
    okl_u64 ino, size, blocks, attributes_mask;
    kstatx_time atime, btime, ctime, mtime;
    okl_u32 rdev_major, rdev_minor, dev_major, dev_minor;
    okl_u64 spare[14];
};
static_assert(__builtin_offsetof(kstatx, mode) == 28 && __builtin_offsetof(kstatx, mtime) == 112 &&
              __builtin_offsetof(kstatx, dev_major) == 136 && sizeof(kstatx) == 256, "struct statx");
inline okl_u64 encode_dev(okl_u32 major, okl_u32 minor) {
    return (okl_u64{major & 0xfffff000u} << 32) | (okl_u64{major & 0xfffu} << 8) |
           (okl_u64{minor & 0xffffff00u} << 12) | okl_u64{minor & 0xffu};
}
inline okl_long statx_into(okl_long dirfd, const char* path, okl_long flags, kstat* st) {
    kstatx x{};
    const okl_long r = sys(nr_statx, dirfd, reinterpret_cast<okl_long>(path), flags,
                           0x7ff /* STATX_BASIC_STATS */, reinterpret_cast<okl_long>(&x));
    if (r < 0) return r;
    *st = kstat{};
    st->dev = encode_dev(x.dev_major, x.dev_minor);
    st->ino = x.ino;
    st->nlink = x.nlink;
    st->mode = x.mode;
    st->uid = x.uid;
    st->gid = x.gid;
    st->rdev = encode_dev(x.rdev_major, x.rdev_minor);
    st->size = static_cast<okl_i64>(x.size);
    st->blksize = x.blksize;
    st->blocks = static_cast<okl_i64>(x.blocks);
    st->atime_sec = x.atime.sec; st->atime_nsec = x.atime.nsec;
    st->mtime_sec = x.mtime.sec; st->mtime_nsec = x.mtime.nsec;
    st->ctime_sec = x.ctime.sec; st->ctime_nsec = x.ctime.nsec;
    return 0;
}
#endif

// A file opened by a thirty-two bit program is limited to two gibibytes unless
// it asks otherwise (EOVERFLOW); a sixty-four bit program is never limited.
inline okl_long openat(okl_long dirfd, okl_long path, okl_long flags, okl_long mode = 0) {
#if defined(__i386__)
    flags |= 0100000;  // O_LARGEFILE
#endif
    return sys(nr_openat, dirfd, path, flags, mode);
}

inline okl_long fstat(okl_long fd, kstat* st) {
#if defined(__i386__)
    return statx_into(fd, "", 0x1000 /* AT_EMPTY_PATH */, st);
#else
    return sys(nr_fstat, fd, reinterpret_cast<okl_long>(st));
#endif
}
inline okl_long fstatat(okl_long dirfd, const char* path, kstat* st, okl_long flags) {
#if defined(__i386__)
    return statx_into(dirfd, path, flags, st);
#else
    return sys(nr_newfstatat, dirfd, reinterpret_cast<okl_long>(path), reinterpret_cast<okl_long>(st), flags);
#endif
}
inline okl_long mmap(okl_long addr, okl_long len, okl_long prot, okl_long flags, okl_long fd, okl_i64 off) {
#if defined(__i386__)
    if (off & 4095) return -22;  // EINVAL: mmap2 counts pages
    return sys(nr_mmap2, addr, len, prot, flags, fd, static_cast<okl_long>(off >> 12));
#else
    return sys(nr_mmap, addr, len, prot, flags, fd, static_cast<okl_long>(off));
#endif
}
inline okl_i64 lseek(okl_long fd, okl_i64 off, okl_long whence) {
#if defined(__i386__)
    okl_i64 at = 0;
    const okl_long r = sys(nr_llseek, fd, static_cast<okl_long>(static_cast<okl_u64>(off) >> 32),
                           static_cast<okl_long>(static_cast<okl_u64>(off) & 0xffffffffu),
                           reinterpret_cast<okl_long>(&at), whence);
    return r < 0 ? okl_i64{r} : at;
#else
    return sys(nr_lseek, fd, static_cast<okl_long>(off), whence);
#endif
}
inline okl_long pread(okl_long fd, void* buf, okl_uptr n, okl_i64 off) {
#if defined(__i386__)
    return sys(nr_pread64_32, fd, reinterpret_cast<okl_long>(buf), static_cast<okl_long>(n),
               static_cast<okl_long>(static_cast<okl_u64>(off) & 0xffffffffu),
               static_cast<okl_long>(static_cast<okl_u64>(off) >> 32));
#else
    return sys(nr_pread64, fd, reinterpret_cast<okl_long>(buf), static_cast<okl_long>(n), static_cast<okl_long>(off));
#endif
}
inline okl_long pwrite(okl_long fd, const void* buf, okl_uptr n, okl_i64 off) {
#if defined(__i386__)
    return sys(nr_pwrite64_32, fd, reinterpret_cast<okl_long>(buf), static_cast<okl_long>(n),
               static_cast<okl_long>(static_cast<okl_u64>(off) & 0xffffffffu),
               static_cast<okl_long>(static_cast<okl_u64>(off) >> 32));
#else
    return sys(nr_pwrite64, fd, reinterpret_cast<okl_long>(buf), static_cast<okl_long>(n), static_cast<okl_long>(off));
#endif
}
inline okl_long ftruncate(okl_long fd, okl_i64 size) {
#if defined(__i386__)
    return sys(nr_ftruncate64, fd, static_cast<okl_long>(static_cast<okl_u64>(size) & 0xffffffffu),
               static_cast<okl_long>(static_cast<okl_u64>(size) >> 32));
#else
    return sys(nr_ftruncate, fd, static_cast<okl_long>(size));
#endif
}
inline okl_long fstatfs(okl_long fd, kstatfs* sf) {
#if defined(__i386__)
    // `statfs64': this record, with its size stated, since the thirty-two bit
    // `statfs' cannot report a volume's blocks past four gibibytes.
    return sys(nr_fstatfs64, fd, static_cast<okl_long>(sizeof(kstatfs)), reinterpret_cast<okl_long>(sf));
#else
    return sys(nr_fstatfs, fd, reinterpret_cast<okl_long>(sf));
#endif
}
inline okl_long nanosleep(const ktimespec* req, ktimespec* rem) {
#if defined(__i386__)
    return sys(nr_clock_nanosleep_time64, 1 /* CLOCK_MONOTONIC */, 0, reinterpret_cast<okl_long>(req),
               reinterpret_cast<okl_long>(rem));
#else
    return sys(nr_nanosleep, reinterpret_cast<okl_long>(req), reinterpret_cast<okl_long>(rem));
#endif
}

struct kdirent64 {
    okl_u64 ino;
    okl_i64 off;
    unsigned short reclen;
    unsigned char  type;
    char           name[];
};

enum : unsigned char { dt_dir = 4, dt_reg = 8, dt_lnk = 10 };

// --- openkal.terminal ------------------------------------------------------
//
// The kernel's terminal settings, in the kernel's own layout. A C library's
// `struct termios' is not this structure: several of them carry additional
// fields, and one compiled against a different library would read the wrong
// words. The comment at the head of this file states the rule; this is an
// instance of it.
struct ktermios {
    okl_u32 iflag;
    okl_u32 oflag;
    okl_u32 cflag;
    okl_u32 lflag;
    unsigned char line;
    unsigned char cc[19];
};

struct kwinsize {
    unsigned short row;
    unsigned short col;
    unsigned short xpixel;
    unsigned short ypixel;
};

enum : okl_long {
    tcgets = 0x5401, tcsets = 0x5402, tiocgwinsz = 0x5413,
};

// Positions within ktermios::lflag and ktermios::iflag, and the two entries of
// ktermios::cc that decide how long a read waits. Named here for the same
// reason the numbers above are: they belong to the kernel and not to any
// library.
//
// `t_isig', `t_ixon' and `t_iexten' are the three mechanisms by which this
// kernel keeps a keystroke for itself --- the interrupt and its neighbours, the
// pair that stops and starts output, and the one that takes the next keystroke
// literally. KAL_TERM_PASS_CONTROL is the whole of them, because a program that
// asked for every keystroke and was given two mechanisms out of three would
// find one key missing and nothing to ask about it with.
enum : okl_u32 {
    t_isig = 0000001u, t_icanon = 0000002u, t_echo = 0000010u, t_iexten = 0100000u,
};
enum : okl_u32 { t_ixon = 0002000u };
// `t_istrip', `t_inlcr', `t_igncr' and `t_icrnl' are the mechanisms by which
// this kernel alters input on its way: clearing the high bit, rewriting a line
// feed into a carriage return, discarding a carriage return, and rewriting a
// carriage return into a line feed. KAL_TERM_PASS_INPUT is the whole of them,
// for the reason given above for the three that keep a keystroke. The last is
// the one this kernel applies ordinarily, and the one that made the return key
// and Ctrl+J the same byte (mcpplibs/openkal#48).
enum : okl_u32 { t_istrip = 0000040u, t_inlcr = 0000100u, t_igncr = 0000200u, t_icrnl = 0000400u };
enum { v_time = 5, v_min = 6 };

// --- openkal.net and openkal.datagram --------------------------------------
//
// The kernel's socket address structures. `ksockaddr_storage' is large enough
// for either family and is what a call that reports an address is given, so
// that a reply naming a family this implementation did not ask for cannot write
// beyond the object.
enum : okl_long {
    af_inet = 2, af_inet6 = 10,
    sock_stream = 1, sock_dgram = 2, sock_cloexec = 02000000,
    ipproto_tcp = 6, ipproto_udp = 17,
    sol_socket = 1, so_reuseaddr = 2,
};

struct ksockaddr_in {
    unsigned short family;
    unsigned short port;      // network order
    okl_u32        addr;      // network order
    unsigned char  zero[8];
};

struct ksockaddr_in6 {
    unsigned short family;
    unsigned short port;      // network order
    okl_u32        flowinfo;
    unsigned char  addr[16];  // network order
    okl_u32        scope_id;
};

struct ksockaddr_storage {
    unsigned short family;
    unsigned char  pad[126];
};

// --- openkal.timeout -------------------------------------------------------
struct kpollfd {
    int   fd;
    short events;
    short revents;
};

enum : short { poll_in = 0x0001, poll_out = 0x0004 };

// --- operations used by more than one interface ----------------------------

inline okl_long write_all(int fd, const void* p, okl_uptr n) {
    const auto* b = static_cast<const unsigned char*>(p);
    okl_uptr done = 0;
    while (done < n) {
        const okl_long r = sys(nr_write, fd, reinterpret_cast<okl_long>(b + done),
                               static_cast<okl_long>(n - done));
        if (interrupted(r)) continue;
        if (failed(r)) return r;
        if (r == 0) break;
        done += static_cast<okl_uptr>(r);
    }
    return static_cast<okl_long>(done);
}

inline okl_uptr length(const char* s) {
    okl_uptr n = 0; while (s && s[n]) ++n; return n;
}

inline void copy(void* d, const void* s, okl_uptr n) {
    auto* a = static_cast<unsigned char*>(d);
    const auto* b = static_cast<const unsigned char*>(s);
    for (okl_uptr i = 0; i < n; ++i) a[i] = b[i];
}

inline void fill(void* d, unsigned char v, okl_uptr n) {
    auto* a = static_cast<unsigned char*>(d);
    for (okl_uptr i = 0; i < n; ++i) a[i] = v;
}

// A name is a single component or a sequence separated by a forward slash. It
// shall not begin with a separator and shall not contain a component that
// ascends: a program able to ascend from the directory it was given would not
// be confined by having been given it.
//
// The rule belongs here rather than in the file system implementation because
// openkal.process names a program the same way, and a program started by a
// name that ascends is exactly the escape the rule exists to prevent.
inline bool acceptable(const char* name, okl_uptr len) {
    if (name == nullptr || len == 0 || name[0] == '/') return false;
    okl_uptr start = 0;
    for (okl_uptr i = 0; i <= len; ++i) {
        if (i == len || name[i] == '/') {
            const okl_uptr n = i - start;
            if (n == 0) return false;
            if (n == 2 && name[start] == '.' && name[start + 1] == '.') return false;
            start = i + 1;
        }
    }
    return true;
}

// The greatest length of a name this implementation accepts, which is the
// buffer below less the terminator it adds.
//
// A BOUND A CALLER CANNOT LEARN PRODUCES A FAILURE THE CALLER CANNOT ATTRIBUTE.
// A longer name was refused as kal_err_invalid, which is also the answer for a
// name that ascends --- so a program meeting the bound was told that its name
// was malformed. `kal_fs_max_name' reports this.
inline constexpr okl_uptr max_name = 4095;

// A counted name becomes a terminated one for the kernel. The conversion is a
// change of representation, not a namespace being reconstructed.
struct terminated {
    char buf[max_name + 1];
    bool ok;
    terminated(const char* s, okl_uptr n) : ok(n < sizeof buf) {
        if (ok) { copy(buf, s, n); buf[n] = '\0'; }
    }
};

// A name `acceptable' accepts, terminated for the kernel, in one pass over it:
// the operations a program makes most often (an enquiry, an open) read each
// name once rather than twice (0.22).
struct accepted_name {
    char buf[max_name + 1];
    bool ok = false;
    accepted_name(const char* name, okl_uptr len) {
        if (name == nullptr || len == 0 || len >= sizeof buf || name[0] == '/') return;
        okl_uptr start = 0;
        for (okl_uptr i = 0; i < len; ++i) {
            const char c = name[i];
            buf[i] = c;
            if (c != '/') continue;
            const okl_uptr n = i - start;
            if (n == 0 || (n == 2 && name[start] == '.' && name[start + 1] == '.')) return;
            start = i + 1;
        }
        const okl_uptr n = len - start;
        if (n == 0 || (n == 2 && name[start] == '.' && name[start + 1] == '.')) return;
        buf[len] = '\0';
        ok = true;
    }
};

}  // namespace okl
