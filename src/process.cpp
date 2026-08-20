#include "sys.h"
#include "handle.h"
#include <openkal/process.h>
#include <openkal/memory.h>

namespace {


constexpr okl_uptr kMaxEntries = 512;

// The counted arrays the interface takes become the terminated arrays the
// kernel takes. Every allocation happens before the program is duplicated, so
// that the duplicate performs nothing but two system calls: a duplicate of a
// program that has more than one execution context may hold a lock no context
// in it will release.
struct vector {
    char** slots = nullptr;
    char*  bytes = nullptr;
    okl_uptr slots_bytes = 0;
    okl_uptr bytes_bytes = 0;
    bool   ok = true;

    bool build(const char** items, const kal_uintptr* lens, kal_uintptr n) {
        if (n > kMaxEntries) { ok = false; return false; }
        okl_uptr total = 0;
        for (kal_uintptr i = 0; i < n; ++i) total += lens[i] + 1;
        slots_bytes = (n + 1) * sizeof(char*);
        bytes_bytes = total == 0 ? 1 : total;
        slots = static_cast<char**>(kal_alloc(slots_bytes, alignof(char*)));
        bytes = static_cast<char*>(kal_alloc(bytes_bytes, 1));
        if (!slots || !bytes) { ok = false; return false; }
        okl_uptr at = 0;
        for (kal_uintptr i = 0; i < n; ++i) {
            okl::copy(bytes + at, items[i], lens[i]);
            bytes[at + lens[i]] = '\0';
            slots[i] = bytes + at;
            at += lens[i] + 1;
        }
        slots[n] = nullptr;
        return true;
    }

    ~vector() {
        if (slots) kal_free(slots, slots_bytes, alignof(char*));
        if (bytes) kal_free(bytes, bytes_bytes, 1);
    }
};

}  // namespace

extern "C" {

int kal_process_spawn(kal_dir base,
                      const char* path, kal_uintptr path_len,
                      const char** argv, const kal_uintptr* argv_lens, kal_uintptr argc,
                      const char** envp, const kal_uintptr* envp_lens, kal_uintptr envc,
                      const kal_spawn_streams* streams,
                      kal_process* out) {
    const int b = okl::unpack(base.h);
    if (b < 0 || out == nullptr) return kal_err_invalid;
    if (!okl::acceptable(path, path_len)) return kal_err_invalid;
    okl::terminated p(path, path_len);
    if (!p.ok) return kal_err_invalid;

    // The vector is passed unaltered. Clause 7.6: argv[0] is the name the
    // started program observes as its own, and it is the caller's to choose ---
    // the started program reads it through kal_env_arg(0), so a caller that did
    // not supply it could not predict what the program would read.
    vector args, envs;
    if (!args.build(argv, argv_lens, argc)) return kal_err_no_memory;
    if (!envs.build(envp, envp_lens, envc)) return kal_err_no_memory;

    const okl_long in  = streams ? static_cast<okl_long>(streams->in)  : 0;
    const okl_long ou  = streams ? static_cast<okl_long>(streams->out) : 0;
    const okl_long er  = streams ? static_cast<okl_long>(streams->err) : 0;

    // The image is duplicated and then replaced. openkal has no operation that
    // duplicates the calling image, and this is why: the duplicate is not a
    // resource the caller receives, it exists for the length of two system
    // calls, and no environment without it could be asked to reproduce it.
    const okl_long child = okl::sys(okl::nr_clone, 17 /* SIGCHLD */, 0, 0, 0, 0);
    if (okl::failed(child)) return okl::translate(child);

    if (child == 0) {
        if (in != 0) okl::sys(okl::nr_dup3, in, 0, 0);
        if (ou != 0) okl::sys(okl::nr_dup3, ou, 1, 0);
        if (er != 0) okl::sys(okl::nr_dup3, er, 2, 0);
        // The started program's working directory is the directory supplied
        // here, expressed by naming the program relative to it. There is no
        // operation that changes a working directory afterwards, because a
        // working directory that can be changed is shared mutable state
        // between execution contexts.
        okl::sys(okl::nr_execveat, b, reinterpret_cast<okl_long>(p.buf),
                 reinterpret_cast<okl_long>(args.slots),
                 reinterpret_cast<okl_long>(envs.slots), 0);
        okl::sys(okl::nr_exit_group, 127);
        for (;;) { }
    }

    *out = kal_process{ static_cast<kal_uintptr>(child) };
    return kal_ok;
}

int kal_process_wait(kal_process h, int* status, int* terminated_by_environment) {
    if (h.h == 0) return kal_err_invalid;
    int st = 0;
    for (;;) {
        const okl_long r = okl::sys(okl::nr_wait4, static_cast<okl_long>(h.h),
                                    reinterpret_cast<okl_long>(&st), 0, 0);
        if (okl::interrupted(r)) continue;
        if (okl::failed(r)) return okl::translate(r);
        break;
    }
    // The encoding is the kernel's: the low seven bits name the signal that
    // ended the program and are zero when it ended by returning, in which case
    // the next eight bits are what it returned.
    const int signalled = st & 0x7f;
    if (signalled == 0) {
        if (status) *status = (st >> 8) & 0xff;
        if (terminated_by_environment) *terminated_by_environment = 0;
    } else {
        if (status) *status = signalled;
        if (terminated_by_environment) *terminated_by_environment = 1;
    }
    return kal_ok;
}

int kal_process_terminate(kal_process h) {
    if (h.h == 0) return kal_err_invalid;
    const okl_long r = okl::sys(okl::nr_kill, static_cast<okl_long>(h.h), 15 /* SIGTERM */);
    return okl::failed(r) ? okl::translate(r) : kal_ok;
}

// Releasing the handle does not affect the program. A program that has not been
// waited for continues, and this environment collects it when the caller exits.
void kal_process_close(kal_process) { }

const kal_uintptr kal_process_props =
    KAL_PROCESS_PROP_TERMINATE | KAL_PROCESS_PROP_STREAM_PASSING
  | KAL_PROCESS_PROP_EXIT_STATUS;

}
