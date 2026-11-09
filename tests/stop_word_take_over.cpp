// The stop word, taken over and given back (openkal 0.17, process.h; K1, K2).
//
// Four facts, each of which this implementation got wrong once:
//   1. a request delivered after the word is armed returns from the handler ---
//      under GCC -O0 the return trampoline was in `.bss' and the program died
//      with SIGSEGV on its way back (K1);
//   2. a request the starter arranged not to deliver stays undelivered: SIGINT
//      ignored before arming is still ignored after it (K2);
//   3. ending as requested ends the program as the request would have, which a
//      started copy reports to its starter as a signal and not as a status (G4);
//   4. (0.18.2) a context waiting upon the word with `kal_task_wait' and no
//      bound is woken by the request. The handler woke the word as a shared
//      futex and `kal_task_wait' waits on it as a private one, which the kernel
//      keys apart: the waiter slept on, and a program that ends at a request by
//      waiting for it never ended.
#include <csignal>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <unistd.h>
#include <sys/wait.h>
import openkal.types;
import openkal.process;
import openkal.task;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;

namespace {
void check(bool held, const char* what) { expect(held) << what; }

// Starts this program again as the child `name' and answers its wait status.
int run_copy(const char* name) {
    const std::vector<std::string> command = child_command(name);
    std::vector<char*> argv;
    for (const auto& a : command) argv.push_back(const_cast<char*>(a.c_str()));
    argv.push_back(nullptr);
    const pid_t copy = ::fork();
    if (copy == 0) {
        ::execv("/proc/self/exe", argv.data());
        ::_exit(93);
    }
    int st = 0;
    ::waitpid(copy, &st, 0);
    return st;
}
}

struct waiter {
    const kal_u32* word;
    volatile int woken;
};
void wait_for_request(void* p) {
    auto* w = static_cast<waiter*>(p);
    while (*w->word == 0) kal_task_wait(w->word, 0, 0);   // no bound
    w->woken = 1;
}

child waiting{"waiter", [](std::vector<std::string>) {
    const kal_u32* word = kal_process_stop_requested();
    if (word == nullptr) return 90;
    waiter w{ word, 0 };
    kal_task t{};
    if (kal_task_start(wait_for_request, &w, &t) != kal_ok) return 94;
    ::usleep(100000);   // the context is waiting by now
    ::kill(::getpid(), SIGTERM);
    for (int i = 0; i < 2000 && !w.woken; ++i) ::usleep(1000);
    if (!w.woken) return 95;
    kal_task_join(t);
    return 0;
}};

child hang_up{"hang-up", [](std::vector<std::string>) {
    // 0.18.1: the closing of the terminal is a request to end, taken over
    // with the others, and the program ends as it would have.
    const kal_u32* word = kal_process_stop_requested();
    if (word == nullptr) return 90;
    ::kill(::getpid(), SIGHUP);
    for (int i = 0; i < 1000 && *word == 0; ++i) ::usleep(1000);
    if (*word == 0) return 91;
    kal_process_end_as_requested();
    return 92;
}};

child end_as_requested{"end-as-requested", [](std::vector<std::string>) {
    const kal_u32* word = kal_process_stop_requested();
    if (word == nullptr) return 90;
    ::kill(::getpid(), SIGTERM);
    for (int i = 0; i < 1000 && *word == 0; ++i) ::usleep(1000);
    if (*word == 0) return 91;
    kal_process_end_as_requested();
    return 92;
}};

UTest stop_word_take_over_test{"stop_word_take_over", [] {
    std::signal(SIGINT, SIG_IGN);
    const kal_u32* word = kal_process_stop_requested();
    check(word != nullptr, "the stop word is armed");
    if (word == nullptr) return;

    ::kill(::getpid(), SIGINT);
    ::usleep(20000);
    check(*word == 0, "a request ignored before arming is still not delivered");

    ::kill(::getpid(), SIGTERM);
    for (int i = 0; i < 1000 && *word == 0; ++i) ::usleep(1000);
    check(*word != 0, "a request after arming sets the word, and the handler returns");

    int st = run_copy("end-as-requested");
    check(WIFSIGNALED(st) && WTERMSIG(st) == SIGTERM,
          "a program that ends as requested is reported to its starter as ended by the request");

    st = run_copy("hang-up");
    check(WIFSIGNALED(st) && WTERMSIG(st) == SIGHUP,
          "a hang-up is taken over as a request, and the program ends as it would have");

    st = run_copy("waiter");
    check(WIFEXITED(st) && WEXITSTATUS(st) == 0,
          "a context waiting upon the word with no bound is woken by the request");
}};
