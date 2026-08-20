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
using okl_uptr  = __UINTPTR_TYPE__;

namespace okl {

// --- the calling convention ------------------------------------------------

#if defined(__x86_64__)

inline okl_long sys(okl_long n) {
    okl_ulong r;
    __asm__ __volatile__("syscall" : "=a"(r) : "a"(n) : "rcx", "r11", "memory");
    return static_cast<okl_long>(r);
}
inline okl_long sys(okl_long n, okl_long a) {
    okl_ulong r;
    __asm__ __volatile__("syscall" : "=a"(r) : "a"(n), "D"(a) : "rcx", "r11", "memory");
    return static_cast<okl_long>(r);
}
inline okl_long sys(okl_long n, okl_long a, okl_long b) {
    okl_ulong r;
    __asm__ __volatile__("syscall" : "=a"(r) : "a"(n), "D"(a), "S"(b) : "rcx", "r11", "memory");
    return static_cast<okl_long>(r);
}
inline okl_long sys(okl_long n, okl_long a, okl_long b, okl_long c) {
    okl_ulong r;
    __asm__ __volatile__("syscall" : "=a"(r) : "a"(n), "D"(a), "S"(b), "d"(c)
                         : "rcx", "r11", "memory");
    return static_cast<okl_long>(r);
}
inline okl_long sys(okl_long n, okl_long a, okl_long b, okl_long c, okl_long d) {
    okl_ulong r; register okl_long r10 __asm__("r10") = d;
    __asm__ __volatile__("syscall" : "=a"(r) : "a"(n), "D"(a), "S"(b), "d"(c), "r"(r10)
                         : "rcx", "r11", "memory");
    return static_cast<okl_long>(r);
}
inline okl_long sys(okl_long n, okl_long a, okl_long b, okl_long c, okl_long d, okl_long e) {
    okl_ulong r; register okl_long r10 __asm__("r10") = d; register okl_long r8 __asm__("r8") = e;
    __asm__ __volatile__("syscall" : "=a"(r) : "a"(n), "D"(a), "S"(b), "d"(c), "r"(r10), "r"(r8)
                         : "rcx", "r11", "memory");
    return static_cast<okl_long>(r);
}
inline okl_long sys(okl_long n, okl_long a, okl_long b, okl_long c, okl_long d, okl_long e, okl_long f) {
    okl_ulong r; register okl_long r10 __asm__("r10") = d; register okl_long r8 __asm__("r8") = e;
    register okl_long r9 __asm__("r9") = f;
    __asm__ __volatile__("syscall" : "=a"(r)
                         : "a"(n), "D"(a), "S"(b), "d"(c), "r"(r10), "r"(r8), "r"(r9)
                         : "rcx", "r11", "memory");
    return static_cast<okl_long>(r);
}

enum : okl_long {
    nr_read = 0, nr_write = 1, nr_close = 3, nr_fstat = 5, nr_lseek = 8,
    nr_mmap = 9, nr_mprotect = 10, nr_munmap = 11, nr_ioctl = 16,
    nr_readv = 19, nr_writev = 20, nr_sched_yield = 24, nr_nanosleep = 35,
    nr_getpid = 39, nr_clone = 56, nr_execve = 59, nr_exit = 60, nr_wait4 = 61,
    nr_kill = 62, nr_ftruncate = 77, nr_getcwd = 79, nr_fsync = 74,
    nr_arch_prctl = 158, nr_gettid = 186, nr_futex = 202,
    nr_getdents64 = 217, nr_set_tid_address = 218, nr_clock_gettime = 228,
    nr_clock_getres = 229, nr_exit_group = 231, nr_tgkill = 234,
    nr_openat = 257, nr_mkdirat = 258, nr_newfstatat = 262, nr_unlinkat = 263,
    nr_renameat = 264, nr_readlinkat = 267, nr_dup3 = 292, nr_execveat = 322,
    nr_dup2 = 33, nr_utimensat = 280,
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
    __asm__ __volatile__("svc 0" : "+r"(x0) : "r"(x8), "r"(x1) : "memory", "cc");
    return x0;
}
inline okl_long sys(okl_long n, okl_long a, okl_long b, okl_long c) {
    register okl_long x8 __asm__("x8") = n; register okl_long x0 __asm__("x0") = a;
    register okl_long x1 __asm__("x1") = b; register okl_long x2 __asm__("x2") = c;
    __asm__ __volatile__("svc 0" : "+r"(x0) : "r"(x8), "r"(x1), "r"(x2) : "memory", "cc");
    return x0;
}
inline okl_long sys(okl_long n, okl_long a, okl_long b, okl_long c, okl_long d) {
    register okl_long x8 __asm__("x8") = n; register okl_long x0 __asm__("x0") = a;
    register okl_long x1 __asm__("x1") = b; register okl_long x2 __asm__("x2") = c;
    register okl_long x3 __asm__("x3") = d;
    __asm__ __volatile__("svc 0" : "+r"(x0) : "r"(x8), "r"(x1), "r"(x2), "r"(x3)
                         : "memory", "cc");
    return x0;
}
inline okl_long sys(okl_long n, okl_long a, okl_long b, okl_long c, okl_long d, okl_long e) {
    register okl_long x8 __asm__("x8") = n; register okl_long x0 __asm__("x0") = a;
    register okl_long x1 __asm__("x1") = b; register okl_long x2 __asm__("x2") = c;
    register okl_long x3 __asm__("x3") = d; register okl_long x4 __asm__("x4") = e;
    __asm__ __volatile__("svc 0" : "+r"(x0) : "r"(x8), "r"(x1), "r"(x2), "r"(x3), "r"(x4)
                         : "memory", "cc");
    return x0;
}
inline okl_long sys(okl_long n, okl_long a, okl_long b, okl_long c, okl_long d,
                    okl_long e, okl_long f) {
    register okl_long x8 __asm__("x8") = n; register okl_long x0 __asm__("x0") = a;
    register okl_long x1 __asm__("x1") = b; register okl_long x2 __asm__("x2") = c;
    register okl_long x3 __asm__("x3") = d; register okl_long x4 __asm__("x4") = e;
    register okl_long x5 __asm__("x5") = f;
    __asm__ __volatile__("svc 0" : "+r"(x0)
                         : "r"(x8), "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x5)
                         : "memory", "cc");
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
    nr_getpid = 172, nr_mmap = 222, nr_munmap = 215, nr_mprotect = 226,
    nr_clone = 220, nr_execve = 221, nr_wait4 = 260, nr_renameat = 38,
    nr_dup3 = 24, nr_execveat = 281, nr_dup2 = -1,
    nr_arch_prctl = -1, nr_utimensat = 88,
};

#else
#error "openkal-linux supports x86_64 and aarch64"
#endif

// --- error values, as the kernel returns them ------------------------------
//
// A failing system call returns the error negated, which is why the values
// appear here rather than being taken from a header: the header belongs to a
// C library and this implementation has none.
enum : int {
    e_perm = 1, e_noent = 2, e_intr = 4, e_io = 5, e_badf = 9, e_child = 10,
    e_again = 11, e_nomem = 12, e_acces = 13, e_fault = 14, e_busy = 16,
    e_exist = 17, e_xdev = 18, e_nodev = 19, e_notdir = 20, e_isdir = 21,
    e_inval = 22, e_nfile = 23, e_mfile = 24, e_notty = 25, e_fbig = 27,
    e_nospc = 28, e_spipe = 29, e_rofs = 30, e_pipe = 32, e_range = 34,
    e_nametoolong = 36, e_nosys = 38, e_notempty = 39, e_loop = 40,
    e_timedout = 110, e_connreset = 104, e_dquot = 122,
};

// --- constants the kernel defines ------------------------------------------
enum : okl_long {
    o_rdonly = 0, o_wronly = 1, o_rdwr = 2,
    o_creat = 0100, o_excl = 0200, o_trunc = 01000, o_append = 02000,
    o_directory = 0200000, o_cloexec = 02000000, o_nofollow = 0400000,
    at_fdcwd = -100, at_removedir = 0x200, at_symlink_nofollow = 0x100,
    prot_read = 1, prot_write = 2, prot_none = 0,
    map_private = 2, map_anonymous = 0x20, map_stack = 0x20000,
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

struct kstat {
    okl_u64 dev;
    okl_u64 ino;
#if defined(__x86_64__)
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
    okl_u64 rdev;
    okl_u64 pad1;
    okl_i64 size;
    okl_u32 blksize;
    okl_u32 pad2;
    okl_i64 blocks;
    okl_u32 nlink;
    okl_u32 mode;
    okl_u32 uid;
    okl_u32 gid;
    okl_u32 pad0;
#endif
    okl_i64 atime_sec, atime_nsec;
    okl_i64 mtime_sec, mtime_nsec;
    okl_i64 ctime_sec, ctime_nsec;
    okl_i64 unused[3];
};

enum : okl_u32 {
    s_ifmt = 0170000, s_ifreg = 0100000, s_ifdir = 0040000, s_iflnk = 0120000,
};

struct ktimespec { okl_i64 sec; okl_i64 nsec; };

struct kdirent64 {
    okl_u64 ino;
    okl_i64 off;
    unsigned short reclen;
    unsigned char  type;
    char           name[];
};

enum : unsigned char { dt_dir = 4, dt_reg = 8, dt_lnk = 10 };

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

// A counted name becomes a terminated one for the kernel. The conversion is a
// change of representation, not a namespace being reconstructed.
struct terminated {
    char buf[4096];
    bool ok;
    terminated(const char* s, okl_uptr n) : ok(n < sizeof buf) {
        if (ok) { copy(buf, s, n); buf[n] = '\0'; }
    }
};

}  // namespace okl
