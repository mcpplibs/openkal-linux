// The stop word, taken over and given back (openkal 0.17, process.h; K1, K2).
//
// Three facts, each of which this implementation got wrong before 0.18:
//   1. a request delivered after the word is armed returns from the handler ---
//      under GCC -O0 the return trampoline was in `.bss' and the program died
//      with SIGSEGV on its way back (K1);
//   2. a request the starter arranged not to deliver stays undelivered: SIGINT
//      ignored before arming is still ignored after it (K2);
//   3. ending as requested ends the program as the request would have, which a
//      started copy reports to its starter as a signal and not as a status (G4).
#include <csignal>
#include <cstdio>
#include <cstring>
#include <unistd.h>
#include <sys/wait.h>
import openkal.types;
import openkal.process;
import openkal.task;

namespace {
int failures = 0;
void check(bool held, const char* what) {
    if (!held) { std::printf("FAIL: %s\n", what); ++failures; }
}
}

int main(int argc, char** argv) {
    if (argc > 1 && std::strcmp(argv[1], "--end-as-requested") == 0) {
        const kal_u32* word = kal_process_stop_requested();
        if (word == nullptr) return 90;
        ::kill(::getpid(), SIGTERM);
        for (int i = 0; i < 1000 && *word == 0; ++i) ::usleep(1000);
        if (*word == 0) return 91;
        kal_process_end_as_requested();
        return 92;
    }

    std::signal(SIGINT, SIG_IGN);
    const kal_u32* word = kal_process_stop_requested();
    check(word != nullptr, "the stop word is armed");
    if (word == nullptr) return 1;

    ::kill(::getpid(), SIGINT);
    ::usleep(20000);
    check(*word == 0, "a request ignored before arming is still not delivered");

    ::kill(::getpid(), SIGTERM);
    for (int i = 0; i < 1000 && *word == 0; ++i) ::usleep(1000);
    check(*word != 0, "a request after arming sets the word, and the handler returns");

    const pid_t child = ::fork();
    if (child == 0) {
        ::execl("/proc/self/exe", argv[0], "--end-as-requested", nullptr);
        ::_exit(93);
    }
    int st = 0;
    ::waitpid(child, &st, 0);
    check(WIFSIGNALED(st) && WTERMSIG(st) == SIGTERM,
          "a program that ends as requested is reported to its starter as ended by the request");

    if (failures == 0) std::printf("openkal-linux: the stop word is taken over and given back\n");
    return failures == 0 ? 0 : 1;
}
