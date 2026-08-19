#include <pthread.h>
#include <sched.h>
#include <errno.h>
#include <stdlib.h>
#include <time.h>
#include <linux/futex.h>
#include <sys/syscall.h>
#include <unistd.h>
import openkal.task;
import openkal.types;

namespace {

int translate(int e) {
    switch (e) {
        case EINVAL: case ESRCH: case EFAULT: return kal_err_invalid;
        case EAGAIN:                          return kal_err_again;
        case ENOMEM:                          return kal_err_no_memory;
        case EPERM:                           return kal_err_permission;
        case ENOSYS:                          return kal_err_not_supported;
        default:                              return kal_err_io;
    }
}

struct trampoline { void (*entry)(void*); void* arg; };

void* run(void* p) {
    trampoline* t = static_cast<trampoline*>(p);
    void (*entry)(void*) = t->entry;
    void* arg = t->arg;
    ::free(t);
    entry(arg);
    return nullptr;
}

}  // namespace

extern "C" {

int kal_task_start(void (*entry)(void*), void* arg, kal_task* out) {
    if (entry == nullptr || out == nullptr) return kal_err_invalid;
    trampoline* t = static_cast<trampoline*>(::malloc(sizeof(trampoline)));
    if (t == nullptr) return kal_err_no_memory;
    t->entry = entry; t->arg = arg;
    pthread_t id{};
    const int rc = ::pthread_create(&id, nullptr, run, t);
    if (rc != 0) { ::free(t); return translate(rc); }
    *out = kal_task{ static_cast<kal_uintptr>(id) };
    return kal_ok;
}

int kal_task_join(kal_task h) {
    const int rc = ::pthread_join(static_cast<pthread_t>(h.h), nullptr);
    return rc == 0 ? kal_ok : translate(rc);
}

void kal_task_yield(void) { ::sched_yield(); }

kal_uintptr kal_task_current(void) {
    return static_cast<kal_uintptr>(::pthread_self());
}

// The primitive. It is the operation a caller cannot construct: the comparison
// and the suspension occur without an intervening opportunity for the value to
// change unobserved, and only the environment can arrange that.
int kal_task_wait(const __UINT32_TYPE__* word, __UINT32_TYPE__ expected,
                  __UINT64_TYPE__ timeout_ns) {
    timespec ts{};
    timespec* tp = nullptr;
    if (timeout_ns != 0) {
        ts.tv_sec  = static_cast<time_t>(timeout_ns / 1000000000u);
        ts.tv_nsec = static_cast<long>(timeout_ns % 1000000000u);
        tp = &ts;
    }
    for (;;) {
        const long r = ::syscall(SYS_futex, const_cast<__UINT32_TYPE__*>(word),
                                 FUTEX_WAIT_PRIVATE, expected, tp, nullptr, 0);
        if (r == 0) return kal_ok;
        // The value had already changed, which is a successful outcome: the
        // caller's condition no longer holds and it should re-examine it.
        if (errno == EAGAIN) return kal_ok;
        if (errno == EINTR)  continue;
        if (errno == ETIMEDOUT) return kal_err_again;
        return translate(errno);
    }
}

int kal_task_wake(const __UINT32_TYPE__* word, kal_uintptr count, kal_uintptr* woken) {
    const long r = ::syscall(SYS_futex, const_cast<__UINT32_TYPE__*>(word),
                             FUTEX_WAKE_PRIVATE, static_cast<int>(count),
                             nullptr, nullptr, 0);
    if (r < 0) return translate(errno);
    if (woken) *woken = static_cast<kal_uintptr>(r);
    return kal_ok;
}

const kal_uintptr kal_task_props =
    kal::task::prop_preemptive | kal::task::prop_parallel
  | kal::task::prop_wait_timeout;

}
