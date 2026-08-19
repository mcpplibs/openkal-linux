#include "sys.h"
#include "handle.h"
#include <openkal/fs.h>
#include <openkal/memory.h>

namespace {


// The directories this implementation supplies. A hosted system does not
// confine an ordinary program, so it supplies both the working directory and
// the whole file system; an implementation that confined a program would
// supply fewer, and the program could not tell the difference except by
// finding that a directory it wanted was absent.
//
// The working directory is reported under the name the environment knows it
// by, which is its absolute path rather than ".". A C library above openkal
// must both resolve an absolute path and report one, and a name of "." leaves
// it able to do only the first.
struct preopen { const char* name; kal_uintptr len; okl_uptr handle; };

char g_cwd[4096];

preopen* table(kal_uintptr* count) {
    static preopen t[2];
    static bool opened = false;
    if (!opened) {
        opened = true;

        const okl_long n = okl::sys(okl::nr_getcwd, reinterpret_cast<okl_long>(g_cwd),
                                    static_cast<okl_long>(sizeof g_cwd));
        // getcwd reports the length including the terminator, and reports a
        // failure when the directory has been removed. A program whose working
        // directory no longer has a name still has the directory, so the
        // handle is opened either way and only the name falls back.
        okl_uptr cwd_len = 0;
        if (!okl::failed(n) && n > 1) cwd_len = static_cast<okl_uptr>(n) - 1;
        else { g_cwd[0] = '.'; g_cwd[1] = '\0'; cwd_len = 1; }

        const okl_long fd0 = okl::sys(okl::nr_openat, okl::at_fdcwd,
                                      reinterpret_cast<okl_long>("."),
                                      okl::o_rdonly | okl::o_directory | okl::o_cloexec, 0);
        const okl_long fd1 = okl::sys(okl::nr_openat, okl::at_fdcwd,
                                      reinterpret_cast<okl_long>("/"),
                                      okl::o_rdonly | okl::o_directory | okl::o_cloexec, 0);
        t[0] = { g_cwd, cwd_len, okl::failed(fd0) ? 0u : okl::pack(static_cast<int>(fd0)) };
        t[1] = { "/",   1,       okl::failed(fd1) ? 0u : okl::pack(static_cast<int>(fd1)) };
    }
    if (count) *count = 2;
    return t;
}

int kind_of(okl_u32 mode) {
    switch (mode & okl::s_ifmt) {
        case okl::s_ifreg: return kal_node_file;
        case okl::s_ifdir: return kal_node_directory;
        case okl::s_iflnk: return kal_node_link;
        default:           return kal_node_other;
    }
}

void fill_info(const okl::kstat& st, kal_node_info* out) {
    *out = kal_node_info{
        static_cast<kal_uintptr>(st.size),
        static_cast<__UINT64_TYPE__>(st.mtime_sec) * 1000000000u
            + static_cast<__UINT64_TYPE__>(st.mtime_nsec),
        kind_of(st.mode),
        (st.mode & 0200u) != 0 ? 1 : 0,
    };
}

// Enumeration reads the kernel's own directory records. A C library's
// directory stream is not used, for the reason src/sys.h gives: the C library
// in a program above this one may be the one this implementation would be
// calling.
struct listing {
    int      fd;
    okl_uptr used;
    okl_uptr pos;
    char     buf[8192];
};

}  // namespace

extern "C" {

kal_uintptr kal_fs_preopen_count(void) {
    kal_uintptr n = 0; table(&n); return n;
}

int kal_fs_preopen(kal_uintptr index, kal_dir* out, const char** name, kal_uintptr* len) {
    kal_uintptr n = 0;
    preopen* t = table(&n);
    if (index >= n || out == nullptr) return kal_err_invalid;
    if (t[index].handle == 0) return kal_err_permission;
    *out = kal_dir{ t[index].handle };
    if (name) *name = t[index].name;
    if (len)  *len  = t[index].len;
    return kal_ok;
}

int kal_fs_open_dir(kal_dir base, const char* name, kal_uintptr len, kal_dir* out) {
    const int b = okl::unpack(base.h);
    if (b < 0 || out == nullptr || !okl::acceptable(name, len)) return kal_err_invalid;
    okl::terminated t(name, len); if (!t.ok) return kal_err_invalid;
    const okl_long fd = okl::sys(okl::nr_openat, b, reinterpret_cast<okl_long>(t.buf),
                                 okl::o_rdonly | okl::o_directory | okl::o_cloexec, 0);
    if (okl::failed(fd)) return okl::translate(fd);
    *out = kal_dir{ okl::pack(static_cast<int>(fd)) };
    return kal_ok;
}

int kal_fs_open(kal_dir base, const char* name, kal_uintptr len,
                kal_uintptr flags, kal_file* out) {
    const int b = okl::unpack(base.h);
    if (b < 0 || out == nullptr || !okl::acceptable(name, len)) return kal_err_invalid;
    okl::terminated t(name, len); if (!t.ok) return kal_err_invalid;

    const bool r = (flags & KAL_OPEN_READ)  != 0;
    const bool w = (flags & KAL_OPEN_WRITE) != 0;
    okl_long f = w ? (r ? okl::o_rdwr : okl::o_wronly) : okl::o_rdonly;
    f |= okl::o_cloexec;
    if (flags & KAL_OPEN_CREATE)    f |= okl::o_creat;
    if (flags & KAL_OPEN_EXCLUSIVE) f |= okl::o_excl;
    if (flags & KAL_OPEN_TRUNCATE)  f |= okl::o_trunc;
    if (flags & KAL_OPEN_APPEND)    f |= okl::o_append;

    const okl_long fd = okl::sys(okl::nr_openat, b, reinterpret_cast<okl_long>(t.buf), f, 0666);
    if (okl::failed(fd)) return okl::translate(fd);
    *out = kal_file{ okl::pack(static_cast<int>(fd)) };
    return kal_ok;
}

// The form the earlier version specified, defined in terms of the one above,
// which is what the specification records that an implementation ordinarily
// does.
int kal_fs_open_file(kal_dir base, const char* name, kal_uintptr len,
                     int write, int create, kal_file* out) {
    kal_uintptr flags = KAL_OPEN_READ;
    if (write)  flags |= KAL_OPEN_WRITE;
    if (create) flags |= KAL_OPEN_WRITE | KAL_OPEN_CREATE | KAL_OPEN_TRUNCATE;
    return kal_fs_open(base, name, len, flags, out);
}

void kal_fs_close_dir(kal_dir d) {
    const int fd = okl::unpack(d.h);
    if (fd >= 0) { okl::retire(d.h); okl::sys(okl::nr_close, fd); }
}

void kal_fs_close_file(kal_file f) {
    const int fd = okl::unpack(f.h);
    if (fd >= 0) { okl::retire(f.h); okl::sys(okl::nr_close, fd); }
}

// A file's stream is the file. The descriptor is what openkal.stream's handle
// holds on this implementation, so no conversion is required and none is
// performed: the two interfaces agree because both are descriptor-shaped here,
// which is a property of this implementation and not of the specification.
kal_uintptr kal_fs_stream(kal_file f) {
    const int fd = okl::unpack(f.h);
    return fd < 0 ? 0u : static_cast<kal_uintptr>(fd);
}

int kal_fs_seek(kal_file f, __INT64_TYPE__ offset, int whence, __UINT64_TYPE__* result) {
    const int fd = okl::unpack(f.h);
    if (fd < 0) return kal_err_invalid;
    int w = 0;
    if (whence == KAL_SEEK_CURRENT) w = 1;
    else if (whence == KAL_SEEK_END) w = 2;
    const okl_long r = okl::sys(okl::nr_lseek, fd, static_cast<okl_long>(offset), w);
    if (okl::failed(r)) return okl::translate(r);
    if (result) *result = static_cast<__UINT64_TYPE__>(r);
    return kal_ok;
}

int kal_fs_truncate(kal_file f, __UINT64_TYPE__ size) {
    const int fd = okl::unpack(f.h);
    if (fd < 0) return kal_err_invalid;
    const okl_long r = okl::sys(okl::nr_ftruncate, fd, static_cast<okl_long>(size));
    return okl::failed(r) ? okl::translate(r) : kal_ok;
}

int kal_fs_info(kal_dir base, const char* name, kal_uintptr len, kal_node_info* out) {
    const int b = okl::unpack(base.h);
    if (b < 0 || out == nullptr || !okl::acceptable(name, len)) return kal_err_invalid;
    okl::terminated t(name, len); if (!t.ok) return kal_err_invalid;
    okl::kstat st{};
    const okl_long r = okl::sys(okl::nr_newfstatat, b, reinterpret_cast<okl_long>(t.buf),
                                reinterpret_cast<okl_long>(&st), okl::at_symlink_nofollow);
    if (okl::failed(r)) {
        // Clause 7.7: enquiry about a name that does not exist is answered,
        // not refused. A component of the name that is not a directory is the
        // same answer, because the name still refers to nothing.
        if (r == -okl::e_noent || r == -okl::e_notdir) {
            *out = kal_node_info{ 0, 0, kal_node_absent, 0 };
            return kal_ok;
        }
        return okl::translate(r);
    }
    fill_info(st, out);
    return kal_ok;
}

int kal_fs_file_info(kal_file f, kal_node_info* out) {
    const int fd = okl::unpack(f.h);
    if (fd < 0 || out == nullptr) return kal_err_invalid;
    okl::kstat st{};
    const okl_long r = okl::sys(okl::nr_fstat, fd, reinterpret_cast<okl_long>(&st));
    if (okl::failed(r)) return okl::translate(r);
    fill_info(st, out);
    return kal_ok;
}

int kal_fs_mkdir(kal_dir base, const char* name, kal_uintptr len) {
    const int b = okl::unpack(base.h);
    if (b < 0 || !okl::acceptable(name, len)) return kal_err_invalid;
    okl::terminated t(name, len); if (!t.ok) return kal_err_invalid;
    const okl_long r = okl::sys(okl::nr_mkdirat, b, reinterpret_cast<okl_long>(t.buf), 0777);
    return okl::failed(r) ? okl::translate(r) : kal_ok;
}

int kal_fs_remove(kal_dir base, const char* name, kal_uintptr len) {
    const int b = okl::unpack(base.h);
    if (b < 0 || !okl::acceptable(name, len)) return kal_err_invalid;
    okl::terminated t(name, len); if (!t.ok) return kal_err_invalid;
    okl_long r = okl::sys(okl::nr_unlinkat, b, reinterpret_cast<okl_long>(t.buf), 0);
    if (!okl::failed(r)) return kal_ok;
    // One operation removes a name, and the kernel distinguishes two kinds of
    // name where this interface does not.
    if (r == -okl::e_isdir || r == -okl::e_perm) {
        const okl_long d = okl::sys(okl::nr_unlinkat, b, reinterpret_cast<okl_long>(t.buf),
                                    okl::at_removedir);
        if (!okl::failed(d)) return kal_ok;
        r = d;
    }
    return okl::translate(r);
}

int kal_fs_rename(kal_dir from, const char* a, kal_uintptr alen,
                  kal_dir to,   const char* b, kal_uintptr blen) {
    const int f = okl::unpack(from.h), t2 = okl::unpack(to.h);
    if (f < 0 || t2 < 0 || !okl::acceptable(a, alen) || !okl::acceptable(b, blen)) return kal_err_invalid;
    okl::terminated ta(a, alen), tb(b, blen);
    if (!ta.ok || !tb.ok) return kal_err_invalid;
    const okl_long r = okl::sys(okl::nr_renameat, f, reinterpret_cast<okl_long>(ta.buf),
                                t2, reinterpret_cast<okl_long>(tb.buf));
    return okl::failed(r) ? okl::translate(r) : kal_ok;
}

// Enumeration holds a descriptor of its own, obtained by opening the directory
// through itself. A duplicate would share the file offset with the handle the
// caller holds, so two enumerations of one directory would consume each
// other's entries.
int kal_fs_list_begin(kal_dir d, kal_uintptr* iter) {
    const int fd = okl::unpack(d.h);
    if (fd < 0 || iter == nullptr) return kal_err_invalid;
    const okl_long own = okl::sys(okl::nr_openat, fd, reinterpret_cast<okl_long>("."),
                                  okl::o_rdonly | okl::o_directory | okl::o_cloexec, 0);
    if (okl::failed(own)) return okl::translate(own);
    auto* s = static_cast<listing*>(kal_alloc(sizeof(listing), alignof(listing)));
    if (s == nullptr) { okl::sys(okl::nr_close, own); return kal_err_no_memory; }
    s->fd = static_cast<int>(own); s->used = 0; s->pos = 0;
    *iter = reinterpret_cast<kal_uintptr>(s);
    return kal_ok;
}

int kal_fs_list_next(kal_dir, kal_uintptr* iter, const char** name,
                     kal_uintptr* len, int* kind) {
    if (iter == nullptr || *iter == 0) return kal_err_invalid;
    auto* s = reinterpret_cast<listing*>(*iter);
    for (;;) {
        if (s->pos >= s->used) {
            const okl_long r = okl::sys(okl::nr_getdents64, s->fd,
                                        reinterpret_cast<okl_long>(s->buf),
                                        static_cast<okl_long>(sizeof s->buf));
            if (okl::interrupted(r)) continue;
            if (okl::failed(r) || r == 0) {
                okl::sys(okl::nr_close, s->fd);
                kal_free(s, sizeof(listing), alignof(listing));
                *iter = 0;
                if (name) *name = nullptr;
                if (len)  *len  = 0;
                return okl::failed(r) ? okl::translate(r) : kal_ok;
            }
            s->used = static_cast<okl_uptr>(r);
            s->pos  = 0;
        }
        auto* e = reinterpret_cast<okl::kdirent64*>(s->buf + s->pos);
        s->pos += e->reclen;
        // The two entries that name the directory and its parent are omitted.
        // They exist to support ascent, which this interface does not offer.
        if (e->name[0] == '.' && (e->name[1] == '\0'
            || (e->name[1] == '.' && e->name[2] == '\0'))) continue;
        if (name) *name = e->name;
        if (len)  *len  = okl::length(e->name);
        if (kind) *kind = e->type == okl::dt_dir ? kal_node_directory
                        : e->type == okl::dt_reg ? kal_node_file
                        : e->type == okl::dt_lnk ? kal_node_link : kal_node_other;
        return kal_ok;
    }
}

const kal_uintptr kal_fs_props =
    KAL_FS_PROP_CASE_SENSITIVE | KAL_FS_PROP_MODIFIED_TIME
  | KAL_FS_PROP_ATOMIC_RENAME;

}
