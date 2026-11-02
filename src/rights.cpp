#include "rights.h"
#include "handle.h"
#include <openkal/memory.h>

namespace okl { int resource_preference(); }

namespace okl::rights {

volatile bool g_any = false;

namespace {

// --- the lock ----------------------------------------------------------------
//
// One lock for the table, the records and the caches. Each holder does a little
// work and leaves, except a listing built for a tree that does not change,
// which happens once for each directory listed.
struct spin {
    volatile int held = 0;
    void lock() { while (__atomic_exchange_n(&held, 1, __ATOMIC_ACQUIRE)) okl::sys(okl::nr_sched_yield); }
    void unlock() { __atomic_store_n(&held, 0, __ATOMIC_RELEASE); }
};
spin g_lock;
struct held { held() { g_lock.lock(); } ~held() { g_lock.unlock(); } };

okl_u64 hash(const char* s, okl_uptr n, okl_u64 h = 1469598103934665603ull) {
    for (okl_uptr i = 0; i < n; ++i) { h ^= static_cast<unsigned char>(s[i]); h *= 1099511628211ull; }
    return h;
}

void* grab(okl_uptr n) { return kal_alloc(n, 16); }
void  give_back(void* p, okl_uptr n) { if (p) kal_free(p, n, 16); }

// --- a set of 64-bit keys, with a byte beside each -----------------------------
struct keyset {
    okl_u64* keys = nullptr;          // zero is never a key: hash() of anything is made non-zero
    unsigned char* vals = nullptr;
    okl_uptr cap = 0, used = 0;

    bool grow() {
        const okl_uptr ncap = cap ? cap * 2 : 256;
        auto* nk = static_cast<okl_u64*>(grab(ncap * sizeof(okl_u64)));
        auto* nv = static_cast<unsigned char*>(grab(ncap));
        if (!nk || !nv) { give_back(nk, ncap * sizeof(okl_u64)); give_back(nv, ncap); return false; }
        okl::fill(nk, 0, ncap * sizeof(okl_u64));
        for (okl_uptr i = 0; i < cap; ++i) {
            if (keys[i] == 0) continue;
            okl_uptr j = keys[i] & (ncap - 1);
            while (nk[j] != 0) j = (j + 1) & (ncap - 1);
            nk[j] = keys[i]; nv[j] = vals[i];
        }
        give_back(keys, cap * sizeof(okl_u64)); give_back(vals, cap);
        keys = nk; vals = nv; cap = ncap;
        return true;
    }
    // Inserts; false when it was there already (or could not be added).
    bool add(okl_u64 k, unsigned char v) {
        if (k == 0) k = 1;
        if ((used + 1) * 2 > cap && !grow()) return false;
        okl_uptr j = k & (cap - 1);
        while (keys[j] != 0) { if (keys[j] == k) return false; j = (j + 1) & (cap - 1); }
        keys[j] = k; vals[j] = v; ++used;
        return true;
    }
    // The byte beside `k', or -1.
    int find(okl_u64 k) const {
        if (k == 0) k = 1;
        if (cap == 0) return -1;
        okl_uptr j = k & (cap - 1);
        while (keys[j] != 0) { if (keys[j] == k) return vals[j]; j = (j + 1) & (cap - 1); }
        return -1;
    }
    void clear() {
        give_back(keys, cap * sizeof(okl_u64)); give_back(vals, cap);
        keys = nullptr; vals = nullptr; cap = used = 0;
    }
};

// --- the trees ---------------------------------------------------------------
//
// A ROOT is one attenuation: the directory it was made of, its rights, its
// record and what it has listed. Every handle derived from it names the root
// and the path beneath it.
constexpr int kRoots = 64;
constexpr okl_uptr kBatch = 64 * 1024;

struct root {
    bool       live = false;
    int        refs = 0;
    okl_u32    flags = 0;
    int        base = -1;        // a descriptor of the root, to list beneath
    // The record.
    int        record = -1;      // opened to append
    char*      rprefix = nullptr; // where this root lies beneath the directory the record is of
    okl_uptr   rplen = 0;
    keyset     seen;             // kind and name, once each
    char*      batch = nullptr;
    okl_uptr   batched = 0;
    bool       wrote = false;
    // What was listed, for a tree that does not change: each directory listed,
    // keyed by its path, and the names in it.
    keyset     listed;           // directory path -> 1
    keyset     names;            // (directory path, name) -> kind
    okl_uptr   budget = 0;
};
root g_roots[kRoots];

// A HANDLE: its root and the path of its directory beneath the root.
struct entry {
    int      fd = -1;
    int      root = -1;
    okl_u32  flags = 0;
    char*    prefix = nullptr;
    okl_uptr plen = 0;
};
constexpr int kEntries = 4096;
entry g_entries[kEntries];
unsigned short g_slot[okl::kMaxDescriptor];   // entry index + 1; zero for none

entry* entry_of(int fd) {
    if (fd < 0 || fd >= okl::kMaxDescriptor || g_slot[fd] == 0) return nullptr;
    return &g_entries[g_slot[fd] - 1];
}

entry* new_entry(int fd) {
    if (fd < 0 || fd >= okl::kMaxDescriptor) return nullptr;
    for (int i = 0; i < kEntries; ++i)
        if (g_entries[i].fd < 0) { g_entries[i].fd = fd; g_slot[fd] = static_cast<unsigned short>(i + 1); return &g_entries[i]; }
    return nullptr;
}

void drop_entry(entry* e) {
    if (!e) return;
    if (e->fd >= 0 && e->fd < okl::kMaxDescriptor) g_slot[e->fd] = 0;
    give_back(e->prefix, e->plen + 1);
    *e = entry{};
}

int new_root() {
    for (int i = 0; i < kRoots; ++i) if (!g_roots[i].live) { g_roots[i] = root{}; g_roots[i].live = true; return i; }
    return -1;
}

okl_uptr budget_for_preference() {
    switch (okl::resource_preference()) {
        case 1:  return okl_uptr{1} << 20;   // performance
        case 2:  return 0;                   // economy: nothing kept
        default: return okl_uptr{1} << 16;   // balanced
    }
}

// The path of `name' beneath the root of `e' --- after `lead', where given ---
// into `out'; its length, or 0 when it does not fit. "." components are
// dropped.
okl_uptr full_path(const entry& e, const char* name, okl_uptr len, char* out, okl_uptr cap,
                   const char* lead = nullptr, okl_uptr lead_len = 0) {
    okl_uptr o = 0;
    auto put = [&](const char* s, okl_uptr n) {
        if (n == 0 || (n == 1 && s[0] == '.')) return true;
        if (o + n + 1 >= cap) return false;
        if (o) out[o++] = '/';
        okl::copy(out + o, s, n); o += n;
        return true;
    };
    okl_uptr start = 0;
    for (okl_uptr i = 0; lead && i <= lead_len; ++i)
        if (i == lead_len || lead[i] == '/') { if (!put(lead + start, i - start)) return 0; start = i + 1; }
    start = 0;
    for (okl_uptr i = 0; i <= e.plen; ++i)
        if (i == e.plen || e.prefix[i] == '/') { if (!put(e.prefix + start, i - start)) return 0; start = i + 1; }
    start = 0;
    for (okl_uptr i = 0; i <= len; ++i)
        if (i == len || name[i] == '/') { if (!put(name + start, i - start)) return 0; start = i + 1; }
    out[o] = '\0';
    return o == 0 ? 1 : o;   // the root itself: "" written as one byte below
}

// --- the record ----------------------------------------------------------------
void flush(root& r) {
    if (r.record < 0 || r.batched == 0) return;
    okl_uptr at = 0;
    while (at < r.batched) {
        const okl_long w = okl::sys(okl::nr_write, r.record, reinterpret_cast<okl_long>(r.batch + at),
                                    static_cast<okl_long>(r.batched - at));
        if (w == -okl::e_intr) continue;
        if (okl::failed(w) || w == 0) break;
        at += static_cast<okl_uptr>(w);
    }
    r.batched = 0;
}

void append(root& r, char kind, const char* path, okl_uptr n) {
    if (r.record < 0) return;
    if (!r.batch) { r.batch = static_cast<char*>(grab(kBatch)); if (!r.batch) return; }
    if (n + 2 > kBatch) return;
    if (r.batched + n + 2 > kBatch) flush(r);
    r.batch[r.batched++] = kind;
    okl::copy(r.batch + r.batched, path, n); r.batched += n;
    r.batch[r.batched++] = '\0';
    r.wrote = true;
}

// --- the listings of a tree that does not change ---------------------------------
okl_u64 name_key(okl_u64 dir, const char* s, okl_uptr n) { return hash(s, n, dir ^ 0x9e3779b97f4a7c15ull); }

// Lists `dir' (a path beneath the root, "" for the root) into the root's names;
// false when it cannot.
bool list(root& r, const char* dir, okl_uptr dlen, okl_u64 key) {
    if (r.listed.find(key) >= 0) return true;
    if (r.budget == 0) return false;
    char path[4096];
    if (dlen + 1 >= sizeof path) return false;
    if (dlen == 0) { path[0] = '.'; path[1] = '\0'; }
    else { okl::copy(path, dir, dlen); path[dlen] = '\0'; }
    const okl_long fd = okl::sys(okl::nr_openat, r.base, reinterpret_cast<okl_long>(path),
                                 okl::o_rdonly | okl::o_directory | okl::o_cloexec, 0);
    if (okl::failed(fd)) return false;
    alignas(8) char buf[16384];
    okl_uptr count = 0;
    bool ok = true;
    for (;;) {
        const okl_long got = okl::sys(okl::nr_getdents64, fd, reinterpret_cast<okl_long>(buf), sizeof buf);
        if (got == -okl::e_intr) continue;
        if (okl::failed(got)) { ok = false; break; }
        if (got == 0) break;
        for (okl_uptr at = 0; at < static_cast<okl_uptr>(got);) {
            auto* d = reinterpret_cast<okl::kdirent64*>(buf + at);
            at += d->reclen;
            const okl_uptr n = okl::length(d->name);
            if ((n == 1 && d->name[0] == '.') || (n == 2 && d->name[0] == '.' && d->name[1] == '.')) continue;
            if (count >= r.budget) { ok = false; break; }
            r.names.add(name_key(key, d->name, n), d->type);
            ++count;
        }
        if (!ok) break;
    }
    okl::sys(okl::nr_close, fd);
    if (!ok) return false;
    r.budget -= count;
    r.listed.add(key, 1);
    return true;
}

}  // namespace

okl_u32 of(int fd) {
    if (!g_any) return 0;
    held h;
    const entry* e = entry_of(fd);
    return e ? e->flags : 0;
}

int attenuate(int fd, okl_u32 add, int record, int* out) {
    const okl_long dup = okl::sys(okl::nr_fcntl, fd, okl::f_dupfd_cloexec, 3);
    if (okl::failed(dup)) return okl::translate(dup);
    const int nfd = static_cast<int>(dup);
    // The record is appended to through a description of its own, so that the
    // holder's position in the file is not moved and every write appends.
    int rec = -1;
    if (add & recorded) {
        char proc[32] = "/proc/self/fd/";
        okl_uptr o = 14;
        char digits[12]; int n = 0; unsigned v = static_cast<unsigned>(record);
        do { digits[n++] = static_cast<char>('0' + v % 10); v /= 10; } while (v);
        while (n) proc[o++] = digits[--n];
        proc[o] = '\0';
        okl_long r = okl::sys(okl::nr_openat, okl::at_fdcwd, reinterpret_cast<okl_long>(proc),
                              okl::o_wronly | okl::o_append | okl::o_cloexec, 0);
        if (okl::failed(r)) r = okl::sys(okl::nr_fcntl, record, okl::f_dupfd_cloexec, 3);
        if (okl::failed(r)) { okl::sys(okl::nr_close, nfd); return okl::translate(r); }
        rec = static_cast<int>(r);
    }
    held h;
    const entry* from = entry_of(fd);
    entry* e = new_entry(nfd);
    if (!e) { okl::sys(okl::nr_close, nfd); if (rec >= 0) okl::sys(okl::nr_close, rec); return 4; /* kal_err_no_memory */ }
    const okl_u32 inherited = from ? from->flags : 0;
    // A NEW ROOT WHERE THERE IS SOMETHING NEW TO RECORD OR TO STATE; otherwise
    // the handle is the same tree with fewer rights.
    const bool new_tree = from == nullptr || (add & (recorded | unchanging)) != 0;
    if (new_tree) {
        const int ri = new_root();
        if (ri < 0) { drop_entry(e); okl::sys(okl::nr_close, nfd); if (rec >= 0) okl::sys(okl::nr_close, rec); return 4; }
        root& r = g_roots[ri];
        r.flags = inherited | add;
        r.refs = 1;
        r.base = static_cast<int>(okl::sys(okl::nr_fcntl, nfd, okl::f_dupfd_cloexec, 3));
        r.record = rec;
        r.budget = (r.flags & unchanging) ? budget_for_preference() : 0;
        // A RECORD ALREADY KEPT FOR THE TREE GOES ON BEING KEPT, and its names
        // stay relative to the directory it is of: this root lies at the
        // handle's path beneath that directory.
        if (from && from->root >= 0 && !(add & recorded) && g_roots[from->root].record >= 0) {
            const root& f = g_roots[from->root];
            r.record = static_cast<int>(okl::sys(okl::nr_fcntl, f.record, okl::f_dupfd_cloexec, 3));
            r.flags |= recorded;
            char lead[4096];
            entry at = *from;
            okl_uptr n = full_path(at, "", 0, lead, sizeof lead, f.rprefix, f.rplen);
            if (n == 1 && lead[0] == '\0') n = 0;
            r.rprefix = static_cast<char*>(grab(n + 1));
            if (r.rprefix) { okl::copy(r.rprefix, lead, n); r.rprefix[n] = '\0'; r.rplen = n; }
        }
        e->root = ri;
        e->prefix = static_cast<char*>(grab(1)); if (e->prefix) e->prefix[0] = '\0';
        e->plen = 0;
    } else {
        e->root = from->root;
        if (e->root >= 0) g_roots[e->root].refs++;
        e->prefix = static_cast<char*>(grab(from->plen + 1));
        if (e->prefix) { okl::copy(e->prefix, from->prefix, from->plen); e->prefix[from->plen] = '\0'; }
        e->plen = from->plen;
    }
    e->flags = inherited | add;
    g_any = true;
    *out = nfd;
    return 0;
}

bool derive(int parent, int child, const char* name, okl_uptr len) {
    if (!g_any) return true;
    held h;
    const entry* p = entry_of(parent);
    if (!p) return true;
    entry* e = new_entry(child);
    if (!e) return false;
    e->flags = p->flags;
    e->root = p->root;
    if (e->root >= 0) g_roots[e->root].refs++;
    char buf[4096];
    entry tmp = *p;
    const okl_uptr n = full_path(tmp, name, len, buf, sizeof buf);
    const bool root_itself = n == 1 && buf[0] == '\0';
    const okl_uptr plen = root_itself ? 0 : n;
    e->prefix = static_cast<char*>(grab(plen + 1));
    if (e->prefix) { okl::copy(e->prefix, buf, plen); e->prefix[plen] = '\0'; }
    e->plen = e->prefix ? plen : 0;
    return true;
}

void forget(int fd) {
    if (!g_any) return;
    held h;
    entry* e = entry_of(fd);
    if (!e) return;
    const int ri = e->root;
    drop_entry(e);
    if (ri >= 0 && --g_roots[ri].refs <= 0) {
        // The last handle of the tree: what it recorded is written now. The
        // root itself stays until the program ends, which writes its `e'.
        flush(g_roots[ri]);
        g_roots[ri].names.clear();
        g_roots[ri].listed.clear();
        if (g_roots[ri].base >= 0) { okl::sys(okl::nr_close, g_roots[ri].base); g_roots[ri].base = -1; }
    }
}

void note(int fd, char kind, const char* name, okl_uptr len) {
    if (!g_any) return;
    held h;
    const entry* e = entry_of(fd);
    if (!e || e->root < 0 || !(e->flags & recorded)) return;
    root& r = g_roots[e->root];
    if (r.record < 0) return;
    char buf[4096];
    okl_uptr n = full_path(*e, name, len, buf, sizeof buf, r.rprefix, r.rplen);
    if (n == 0) return;
    if (n == 1 && buf[0] == '\0') { buf[0] = '.'; buf[1] = '\0'; }
    if (!r.seen.add(hash(buf, n, static_cast<okl_u64>(static_cast<unsigned char>(kind)) * 0x100000001b3ull), 1)) return;
    append(r, kind, buf, n);
}

bool known_absent(int fd, const char* name, okl_uptr len) {
    if (!g_any) return false;
    held h;
    const entry* e = entry_of(fd);
    if (!e || e->root < 0 || !(e->flags & unchanging)) return false;
    root& r = g_roots[e->root];
    if (r.base < 0) return false;
    char path[4096];
    const okl_uptr n = full_path(*e, name, len, path, sizeof path);
    if (n == 0 || (n == 1 && path[0] == '\0')) return false;
    // Each component in the listing of the directory before it; a component
    // that is not a directory, or a directory that cannot be listed, settles
    // nothing.
    okl_uptr start = 0;
    okl_u64 dir = hash("", 0);
    for (okl_uptr i = 0; i <= n; ++i) {
        if (i < n && path[i] != '/') continue;
        if (!list(r, path, start == 0 ? 0 : start - 1, dir)) return false;
        const int kind = r.names.find(name_key(dir, path + start, i - start));
        if (kind < 0) return true;
        if (i == n) return false;
        if (kind != okl::dt_dir) return false;
        dir = hash(path, i);
        start = i + 1;
    }
    return false;
}

void changed(int fd) {
    if (!g_any) return;
    held h;
    const entry* e = entry_of(fd);
    if (!e || e->root < 0) return;
    root& r = g_roots[e->root];
    r.names.clear();
    r.listed.clear();
    if (r.flags & unchanging) r.budget = budget_for_preference();
}

bool conveyed(int fd, okl_u32* flags, int* record, const char** prefix, okl_uptr* prefix_len) {
    if (!g_any) return false;
    held h;
    const entry* e = entry_of(fd);
    if (!e) return false;
    *flags = e->flags;
    *record = (e->root >= 0 && (e->flags & recorded)) ? g_roots[e->root].record : -1;
    // Where the granted directory lies beneath the directory the record is
    // of, written to a buffer of this table's that lives until the next call.
    static char lead[4096];
    okl_uptr n = 0;
    if (e->root >= 0) {
        const root& r = g_roots[e->root];
        n = full_path(*e, "", 0, lead, sizeof lead, r.rprefix, r.rplen);
        if (n == 1 && lead[0] == '\0') n = 0;
    }
    *prefix = lead;
    *prefix_len = n;
    return true;
}

void adopt(int fd, okl_u32 flags, int record, const char* prefix, okl_uptr prefix_len) {
    held h;
    entry* e = new_entry(fd);
    if (!e) return;
    const int ri = new_root();
    if (ri < 0) { drop_entry(e); return; }
    root& r = g_roots[ri];
    r.flags = flags;
    r.refs = 1;
    r.base = static_cast<int>(okl::sys(okl::nr_fcntl, fd, okl::f_dupfd_cloexec, 3));
    if (record >= 0) {
        // Marked to close on replacement as the grant itself is: the record
        // reaches this program and not the programs it starts, unless it grants
        // the handle on.
        okl::sys(okl::nr_fcntl, record, okl::f_setfd, okl::fd_cloexec);
        r.record = record;
    }
    r.budget = (flags & unchanging) ? budget_for_preference() : 0;
    r.rprefix = static_cast<char*>(grab(prefix_len + 1));
    if (r.rprefix) { okl::copy(r.rprefix, prefix, prefix_len); r.rprefix[prefix_len] = '\0'; r.rplen = prefix_len; }
    e->root = ri;
    e->flags = flags;
    e->prefix = static_cast<char*>(grab(1));
    if (e->prefix) e->prefix[0] = '\0';
    e->plen = 0;
    g_any = true;
}

void end_of_program() {
    if (!g_any) return;
    static volatile int ended = 0;
    if (__atomic_exchange_n(&ended, 1, __ATOMIC_ACQ_REL)) return;
    held h;
    for (auto& r : g_roots) {
        if (!r.live || r.record < 0) continue;
        if (!r.batch) r.batch = static_cast<char*>(grab(kBatch));
        if (r.batch) {
            if (r.batched + 2 > kBatch) flush(r);
            r.batch[r.batched++] = 'e';
            r.batch[r.batched++] = '\0';
        }
        flush(r);
    }
}

}  // namespace okl::rights

namespace {
// A program whose C library ends it without kal_exit --- one of the
// environment's own --- writes its records as its destructors run.
[[gnu::destructor]] void records_at_exit() { okl::rights::end_of_program(); }
}  // namespace
