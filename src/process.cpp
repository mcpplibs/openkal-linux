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

// --- reporting a replacement that failed -----------------------------------
//
// ⚠️⚠️ THE REPLACEMENT HAPPENS IN THE DUPLICATE, SO ITS FAILURE WAS REPORTED TO
// NOBODY.
//
// A program is started here by duplicating this image and replacing the
// duplicate. The replacement is the part that can fail --- the name is absent,
// or is a directory, or is not a program, or may not be executed --- and it
// fails inside an image the caller does not have. This implementation ended
// that image with 127 and answered `kal_ok' with a handle, so a caller learned
// something was wrong only by waiting and reading 127, which is exactly what a
// program that RAN and exited 127 reports.
//
// ⭐ WHAT THAT COST, MEASURED BY A CONSUMER RATHER THAN HERE. openkal-musl
// expresses `execve' as starting a program and ending with its status, so a
// name that could not be started ended the CALLING program with 127 instead of
// returning -1. musl's `execvp' issues one `execve' per PATH entry and needs
// each to return, so the search could not survive its first miss: `bwrap',
// installed at /usr/bin/bwrap, was reported as not installed. openkal-linux#13,
// nine of nineteen test failures.
//
// openkal-musl 0.10.0 answers the part it can --- it asks `kal_fs_info' whether
// the name is there before starting. It cannot answer the rest: openkal reports
// no execute permission, so "present and not executable" is invisible above
// this line. It is not invisible HERE. The duplicate knows precisely why, and
// this is the channel that carries it.
//
// The arrangement is the ordinary one: a pipe whose ends close when the image is
// replaced. Nothing arrives ⇒ the replacement happened. A value arrives ⇒ it did
// not, and the value says why.
struct exec_report {
    int  fd[2] = { -1, -1 };
    bool armed = false;

    // ⚠️ THE PIPE MUST NOT SIT WHERE THE DUPLICATE IS ABOUT TO PLACE SOMETHING.
    // The duplicate places streams at 0, 1 and 2 and granted directories at 3
    // and upwards, so a pipe that happened to hold one of those numbers would be
    // closed by the very placement whose failure it exists to report --- and the
    // parent would then read end-of-input and call that success.
    //
    // `F_DUPFD_CLOEXEC' answers the lowest FREE descriptor at or above a bound.
    // That is the primitive for this, and `dup3' is not: `dup3' is told the
    // number and closes whatever the caller had on it.
    bool open(kal_uintptr placements) {
        const okl_long r = okl::sys(okl::nr_pipe2,
                                    reinterpret_cast<okl_long>(fd), okl::o_cloexec);
        if (okl::failed(r)) return false;
        const okl_long floor = 3 + static_cast<okl_long>(placements);
        armed = lift(fd[0], floor) && lift(fd[1], floor);
        if (!armed) close_both();
        return armed;
    }

    void close_both() {
        if (fd[0] >= 0) okl::sys(okl::nr_close, fd[0]);
        if (fd[1] >= 0) okl::sys(okl::nr_close, fd[1]);
        fd[0] = fd[1] = -1;
    }

    // In the duplicate, once the replacement has returned --- which it does only
    // when it did not happen.
    void say(okl_long failure) const {
        if (!armed) return;
        okl_long value = failure;
        okl::sys(okl::nr_write, fd[1],
                 reinterpret_cast<okl_long>(&value), sizeof value);
    }

    // In this image. Zero when the replacement happened, otherwise the kernel's
    // own negative value for why it did not.
    okl_long heard() {
        if (!armed) return 0;
        okl::sys(okl::nr_close, fd[1]);
        fd[1] = -1;
        okl_long value = 0;
        okl_long n;
        // A transfer this short is not divided, but it can be interrupted.
        do {
            n = okl::sys(okl::nr_read, fd[0],
                         reinterpret_cast<okl_long>(&value), sizeof value);
        } while (n == -okl::e_intr);
        okl::sys(okl::nr_close, fd[0]);
        fd[0] = -1;
        return (n == static_cast<okl_long>(sizeof value)) ? value : 0;
    }

private:
    static bool lift(int& f, okl_long floor) {
        const okl_long n = okl::sys(okl::nr_fcntl, f, okl::f_dupfd_cloexec, floor);
        if (okl::failed(n)) return false;
        okl::sys(okl::nr_close, f);
        f = static_cast<int>(n);
        return true;
    }
};

// A duplicate that could not be replaced is ended, and this image waits for it
// so that nothing is left for a caller to meet later. It is the one wait this
// implementation performs that a caller did not ask for, and it is bounded: the
// duplicate has already reached `exit_group'.
inline void reap(okl_long child) {
    int status = 0;
    okl_long r;
    do {
        r = okl::sys(okl::nr_wait4, child,
                     reinterpret_cast<okl_long>(&status), 0, 0);
    } while (r == -okl::e_intr);
}

}  // namespace

extern "C" {

// Starting a program. ⭐ ONE FUNCTION SINCE 0.11, AND THE SAVING IS NOT ONLY IN
// THE HEADER: this file used to hold THREE bodies of sixty lines that differed
// by four. Every fix to the shared part --- and there have been several, the
// exec-report pipe among them --- had to be made three times or be made once and
// be wrong twice.
//
// The modifiers are now positions in `how': a working directory, a set of
// grants, and two flags. They compose, which the three declarations could not
// do: there was no way to grant directories AND bind a lifetime, and no way at
// all to say the two things a shell runner needs together.
int kal_process_spawn(const kal_spawn* how,
                      const char* path, kal_uintptr path_len,
                      const char** argv, const kal_uintptr* argv_lens, kal_uintptr argc,
                      const char** envp, const kal_uintptr* envp_lens, kal_uintptr envc,
                      const kal_spawn_streams* streams,
                      kal_process* out) {
    if (how == nullptr || out == nullptr) return kal_err_invalid;

    const int b = okl::unpack(how->base.h);
    const int w = okl::unpack(how->work.h);
    if (b < 0 || w < 0) return kal_err_invalid;
    if (!okl::acceptable(path, path_len)) return kal_err_invalid;
    if (how->grant_count > 0 && how->grants == nullptr) return kal_err_invalid;

    // ⚠️ REFUSED BEFORE ANYTHING IS STARTED, not after. A caller that asked for a
    // bound lifetime and received a program without one has been given a program
    // that outlives it --- which is the failure the flag exists to remove --- so an
    // unclaimed position is an error and not a thing to proceed without.
    if (how->flags & ~KAL_SPAWN_BOUND_LIFETIME) return kal_err_not_supported;

    okl::terminated p(path, path_len);
    if (!p.ok) return kal_err_invalid;

    vector args, envs;
    if (!args.build(argv, argv_lens, argc)) return kal_err_no_memory;
    if (!envs.build(envp, envp_lens, envc)) return kal_err_no_memory;

    // Resolved before the duplication, because a failure after it would leave a
    // child to be reaped and a caller with an error it cannot act upon.
    constexpr kal_uintptr max_grants = 16;
    if (how->grant_count > max_grants) return kal_err_invalid;
    int granted[max_grants];
    for (kal_uintptr i = 0; i < how->grant_count; ++i) {
        granted[i] = okl::unpack(how->grants[i].dir.h);
        if (granted[i] < 0) return kal_err_invalid;
    }

    const okl_long in = streams ? static_cast<okl_long>(streams->in.h)  : 0;
    const okl_long ou = streams ? static_cast<okl_long>(streams->out.h) : 0;
    const okl_long er = streams ? static_cast<okl_long>(streams->err.h) : 0;

    const bool bind = (how->flags & KAL_SPAWN_BOUND_LIFETIME) != 0;

    // ⭐ THE UNIT, WHOSE IDENTITY HERE IS A PROCESS GROUP'S --- which is to say,
    // the identifier of whichever program formed it first. `join' is zero for the
    // first member, and the child then makes the group its own; a later member is
    // given the number to join.
    const okl_long join = how->job ? static_cast<okl_long>(how->job->h) : 0;
    const bool     unit = how->job != nullptr;

    const okl_long mine = bind ? okl::sys(okl::nr_getpid) : 0;

    // The bound is 3 + grant_count, because the placements below reach that far.
    exec_report report;
    report.open(how->grant_count);

    const okl_long child = okl::sys(okl::nr_clone, 17 /* SIGCHLD */, 0, 0, 0, 0);
    if (okl::failed(child)) { report.close_both(); return okl::translate(child); }

    if (child == 0) {
        if (in != 0) okl::sys(okl::nr_dup3, in, 0, 0);
        if (ou != 0) okl::sys(okl::nr_dup3, ou, 1, 0);
        if (er != 0) okl::sys(okl::nr_dup3, er, 2, 0);

        // ⚠️ dup3 REFUSES A DUPLICATION ONTO ITSELF, which the ordinary case
        // reaches whenever a granted directory already occupies the number it
        // is destined for. Refusing there is correct of dup3 --- the flags could
        // not be applied --- and here it means the descriptor is already in
        // place, so it is left alone rather than treated as a failure.
        for (kal_uintptr i = 0; i < how->grant_count; ++i) {
            const okl_long want = static_cast<okl_long>(3 + i);
            if (granted[i] != want)
                okl::sys(okl::nr_dup3, granted[i], want, 0);
        }

        // ⭐ THE DIRECTORY THE PROGRAM RUNS IN, AND THIS LINE IS THE WHOLE OF IT.
        //
        // `execveat' below takes `b' as a dirfd, but that only RESOLVES the
        // name --- resolving a name is not entering a directory, which is what
        // the comment here used to get wrong. Until 0.11 there was no second
        // directory to enter, and a started program ran wherever this
        // implementation happened to be.
        //
        // ⚠️ A FAILURE HERE MUST NOT REACH `execveat'. Running the right program
        // in the wrong directory is precisely the silent wrongness this exists to
        // remove, so it is reported through the same pipe an exec failure uses.
        if (const okl_long e = okl::sys(okl::nr_fchdir, w); okl::failed(e)) {
            report.say(e);
            okl::sys(okl::nr_exit_group, 127);
            for (;;) { }
        }

        // ⭐ THE UNIT, ENTERED HERE AND NOT FROM THE PARENT: the parent's own
        // `setpgid' on this child races the replacement below and loses once the
        // program has been replaced. Zero means "your own", which is how a group
        // comes into existence at all --- there is nothing to create beforehand,
        // which is why the interface reports the identity rather than taking it.
        if (unit) okl::sys(okl::nr_setpgid, 0, join);

        if (bind) {
            // 9 is SIGKILL: the binding must not be something the started program
            // can decline, because the caller asked for a program that does not
            // outlive it and not for one that is invited not to.
            okl::sys(okl::nr_prctl, okl::pr_set_pdeathsig, 9, 0, 0, 0);
            // The window: if the caller ended between the clone and the line
            // above, the signal is already spent and this image would survive it.
            if (okl::sys(okl::nr_getppid) != mine)
                okl::sys(okl::nr_exit_group, 127);
        }

        const okl_long why =
            okl::sys(okl::nr_execveat, b, reinterpret_cast<okl_long>(p.buf),
                     reinterpret_cast<okl_long>(args.slots),
                     reinterpret_cast<okl_long>(envs.slots), 0);
        // Reached only when the replacement did not happen, because when it does
        // there is nothing here to reach.
        report.say(why);
        okl::sys(okl::nr_exit_group, 127);
        for (;;) { }
    }

    if (const okl_long why = report.heard()) {
        reap(child);
        return okl::translate(why);
    }

    // ⚠️ WRITTEN ONLY AFTER THE START HAS SUCCEEDED, and only when the unit was
    // new. The first member's identifier IS the group's, so this is where the
    // caller learns it; a later member joins one the caller already holds and
    // there is nothing to report.
    if (unit && join == 0) how->job->h = static_cast<kal_uintptr>(child);

    *out = kal_process{ static_cast<kal_uintptr>(child) };
    return kal_ok;
}

// A channel: a pair of streams of which one end is meant to cross a spawn.
//
// WHY THIS IS A KERNEL FACILITY AND kal::kit's CHANNEL IS NOT. A started program
// is another address space, so a pointer into this one is not something it can
// be handed. The pair must therefore be made of whatever the environment carries
// across a spawn, which here is a descriptor.
//
// BOTH ENDS ARE OWNED AND BOTH ARE RELEASED THROUGH kal_process_channel_close.
// A parent that does not release the far end after the spawn never observes the
// end of input on its own --- the classic deadlock of this arrangement, and the
// reason the release is declared beside the operation rather than left to
// openkal.stream, which has no release at all.
int kal_process_channel(kal_stream* mine, kal_stream* theirs) {
    if (mine == nullptr || theirs == nullptr) return kal_err_invalid;

    int fds[2] = { -1, -1 };
    // O_CLOEXEC on both. The far end is placed deliberately, by the spawn that
    // receives it; an end that leaked into every other started program would
    // keep the channel open after the intended reader had closed it, and the
    // writer would then never see the end of input.
    const okl_long r = okl::sys(okl::nr_pipe2, reinterpret_cast<okl_long>(fds),
                                okl::o_cloexec);
    if (okl::failed(r)) return okl::translate(r);

    // THE STREAMS ARE BARE DESCRIPTORS AND NOT PACKED HANDLES, because
    // openkal.stream's transfer operations take what the environment takes.
    // kal_fs_stream reports a file's stream the same way and for the same
    // reason.
    *mine   = kal_stream{ static_cast<kal_uintptr>(fds[0]) };   // the reading end
    *theirs = kal_stream{ static_cast<kal_uintptr>(fds[1]) };   // the writing end
    return kal_ok;
}

void kal_process_channel_close(kal_stream s) {
    // A bare descriptor, so there is no generation to retire. The standard
    // streams are borrowed and are numbered 0, 1 and 2; closing one of those
    // through this operation would take a stream away from the whole program,
    // so they are refused rather than closed.
    const okl_long fd = static_cast<okl_long>(s.h);
    if (fd < 3) return;
    okl::sys(okl::nr_close, fd);
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

// ⭐ ONE PROGRAM, WHATEVER UNIT IT IS IN.
//
// An earlier draft made this reach the whole group when the started program had
// formed one, recovering that fact with `getpgid(pid) == pid'. It worked, and it
// was the wrong shape: the meaning of this operation then turned on a property of
// the handle that no caller could see. The unit has its own operation below, and
// the caller says which of the two it means.
int kal_process_terminate(kal_process h) {
    if (h.h == 0) return kal_err_invalid;
    const okl_long r = okl::sys(okl::nr_kill, static_cast<okl_long>(h.h), 15 /* SIGTERM */);
    return okl::failed(r) ? okl::translate(r) : kal_ok;
}

// Every program in the unit, including ones this implementation never held a
// handle to --- which is the whole reason a unit exists.
//
// ⚠️ A GROUP IS NAMED BY A PROCESS IDENTIFIER, AND THOSE ARE REUSED. Once the
// program that formed the group has ended and the numbers have wrapped, this can
// reach a different group. That is what this system does --- every program that
// calls `killpg' lives with it --- and the interface records it rather than
// reading as though it were not so.
int kal_process_job_terminate(kal_job j) {
    if (j.h == 0) return kal_err_invalid;
    const okl_long r = okl::sys(okl::nr_kill, -static_cast<okl_long>(j.h), 15 /* SIGTERM */);
    return okl::failed(r) ? okl::translate(r) : kal_ok;
}

// ⚠️ RELEASES NOTHING AND ENDS NOTHING. A group here is a number, not a resource,
// so there is no handle to close --- and the operation exists so that a caller
// need not know that. Where the unit IS a resource, releasing it must still not
// end its members; the interface says so at the declaration.
void kal_process_job_close(kal_job) { }

// Releasing the handle does not affect the program. A program that has not been
// waited for continues, and this environment collects it when the caller exits.
void kal_process_close(kal_process) { }

kal_uintptr kal_process_props(void) {
    return KAL_PROCESS_PROP_TERMINATE | KAL_PROCESS_PROP_STREAM_PASSING
         | KAL_PROCESS_PROP_EXIT_STATUS
         | KAL_PROCESS_PROP_CHANNEL | KAL_PROCESS_PROP_GRANT_DIR
         | KAL_PROCESS_PROP_BOUND_LIFETIME
         | KAL_PROCESS_PROP_JOB;
}

}
