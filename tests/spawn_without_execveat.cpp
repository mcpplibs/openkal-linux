// A start where `execveat' answers ENOSYS (0.18, K11).
//
// termux's PRoot implements `execveat' only for AT_FDCWD. A seccomp filter
// that answers ENOSYS to `execveat' reproduces it without PRoot: every start
// failed as `kal_err_not_supported' before 0.18, and now the implementation
// starts the program with `execve' and an absolute name.
#include <cstddef>
#include <cstdio>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <unistd.h>
import openkal.types;
import openkal.fs;
import openkal.process;

namespace {
kal_dir root() {
    for (kal_uintptr i = 0; i < kal_fs_preopen_count(); ++i) {
        kal_dir d{}; char n[8]; kal_uintptr l = 0;
        if (kal_fs_preopen(i, &d, n, sizeof n, &l) == kal_ok && l == 1 && n[0] == '/') return d;
    }
    return kal_dir{};
}

bool refuse_execveat() {
    sock_filter code[] = {
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(seccomp_data, nr)),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SYS_execveat, 0, 1),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | 38 /* ENOSYS */),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
    };
    sock_fprog prog{ static_cast<unsigned short>(sizeof code / sizeof code[0]), code };
    if (::prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0) return false;
    return ::syscall(SYS_seccomp, SECCOMP_SET_MODE_FILTER, 0, &prog) == 0;
}
}

int main() {
    if (!refuse_execveat()) {
        std::printf("openkal-linux: seccomp is not available here; nothing to observe\n");
        return 0;
    }
    const kal_dir r = root();
    const char* argv[] = { "sh", "-c", "exit 7" };
    const kal_uintptr lens[] = { 2, 2, 6 };
    const kal_spawn how{ r, r, nullptr, nullptr, 0, 0 };
    kal_process p{};
    const int e = kal_process_spawn(&how, "bin/sh", 6, argv, lens, 3, nullptr, nullptr, 0, nullptr, &p);
    int status = -1, terminated = 0;
    if (e == kal_ok) kal_process_wait(p, &status, &terminated);
    if (e != kal_ok || terminated != 0 || status != 7) {
        std::printf("FAIL: a program is started where execveat answers ENOSYS (rc %d, status %d)\n", e, status);
        return 1;
    }
    std::printf("openkal-linux: a program is started where execveat answers ENOSYS\n");
    return 0;
}
