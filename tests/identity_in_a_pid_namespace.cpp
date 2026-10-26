// A program's identity where the sandbox numbers its programs apart from the
// /proc it shows (0.18).
//
// A pid namespace whose /proc is the system's: `getpid' answers 1 inside, and
// /proc/1 is another program. Measured beneath a language server's sandbox,
// every instance named itself pid 1 with pid 1's start, so a killed instance and
// its restart were one program. Here a program in such a namespace names itself
// by the number /proc gives it, and a program outside finds it by that number
// with the same start; and the program inside finds the one outside, which
// `pidfd_open' cannot reach from there, through its /proc entry.
#include <cstdio>
#include <sched.h>
#include <sys/wait.h>
#include <unistd.h>
import openkal.types;
import openkal.process;

namespace {
struct identity { kal_u64 id = 0, started = 0; int ended = 0; };

bool describe(kal_process p, identity& out) {
    kal_program_info info{};
    info.self_size = sizeof info;
    if (kal_process_info(p, 0xffffffffu, &info) != kal_ok) return false;
    out = { info.id, info.started_ns, static_cast<int>(info.ended) };
    return true;
}
bool self(identity& out) {
    kal_process me{};
    if (kal_process_self(&me) != kal_ok) return false;
    const bool ok = describe(me, out);
    kal_process_close(me);
    return ok;
}
bool find(kal_u64 id, identity& out) {
    kal_process p{};
    if (kal_process_open_id(id, &p) != kal_ok) return false;
    const bool ok = describe(p, out);
    kal_process_close(p);
    return ok;
}
}

int main() {
    identity outside{};
    if (!self(outside)) { std::printf("FAIL: this program names itself\n"); return 1; }
    int report[2], release[2];
    if (::pipe(report) != 0 || ::pipe(release) != 0) return 1;
    const pid_t helper = ::fork();
    if (helper == 0) {
        if (::unshare(CLONE_NEWUSER | CLONE_NEWPID) != 0) {
            identity none{};
            (void)!::write(report[1], &none, sizeof none);
            ::_exit(0);
        }
        const pid_t inner = ::fork();
        if (inner == 0) {
            identity me{}, parent{};
            const bool named = self(me);
            const bool found = find(outside.id, parent);
            const int failures = (!named || me.id == 1) + (!found || parent.ended || parent.started != outside.started);
            if (!named || me.id == 1) std::printf("FAIL: inside the namespace this program is named %llu\n", (unsigned long long)me.id);
            if (!found || parent.ended || parent.started != outside.started)
                std::printf("FAIL: inside the namespace the program outside is %s\n", found ? "another" : "not found");
            (void)!::write(report[1], &me, sizeof me);
            char go;
            (void)!::read(release[0], &go, 1);
            ::_exit(failures);
        }
        int st = 0;
        ::waitpid(inner, &st, 0);
        ::_exit(WIFEXITED(st) ? WEXITSTATUS(st) : 1);
    }
    identity inner{};
    (void)!::read(report[0], &inner, sizeof inner);
    int failures = 0;
    if (inner.id == 0) {
        std::printf("openkal-linux: no pid namespace can be made here; nothing to observe\n");
    } else {
        identity seen{};
        if (!find(inner.id, seen) || seen.ended || seen.started != inner.started) {
            std::printf("FAIL: outside, the program inside is not found by the number it named itself by\n");
            ++failures;
        }
    }
    (void)!::write(release[1], "x", 1);
    int st = 0;
    ::waitpid(helper, &st, 0);
    if (!WIFEXITED(st) || WEXITSTATUS(st) != 0) ++failures;
    if (failures == 0 && inner.id != 0)
        std::printf("openkal-linux: a program in a pid namespace is named as /proc names it, both ways\n");
    return failures;
}
