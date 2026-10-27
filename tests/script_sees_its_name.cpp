// A `#!' script is given its name (0.18.1).
//
// Started through the descriptor, a script saw `$0' as `/dev/fd/<n>', and one
// that locates its neighbours with `dirname "$0"' found none of them. A
// program that needs an interpreter is now started by its absolute name where
// that name reaches the file the descriptor holds.
//
// The script writes beside itself, under the name its `$0' gives; the test
// reads the file back from the directory it placed the script in.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
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
}

int main() {
    char dir[] = "/tmp/okl-script-XXXXXX";
    if (::mkdtemp(dir) == nullptr) { std::printf("FAIL: a scratch directory\n"); return 1; }
    char script[256], beside[256];
    std::snprintf(script, sizeof script, "%s/stub", dir);
    std::snprintf(beside, sizeof beside, "%s/beside", dir);
    {
        const int fd = ::open(script, O_WRONLY | O_CREAT | O_TRUNC, 0755);
        const char text[] = "#!/bin/sh\nprintf '%s' \"$0\" > \"${0%/*}/beside\"\n";
        const bool written = fd >= 0 && ::write(fd, text, sizeof text - 1) == static_cast<ssize_t>(sizeof text - 1);
        if (fd >= 0) ::close(fd);
        if (!written) { std::printf("FAIL: the script is written\n"); return 1; }
    }

    const kal_dir r = root();
    const char* argv[] = { script };
    const kal_uintptr lens[] = { std::strlen(script) };
    const kal_spawn how{ r, r, nullptr, nullptr, 0, 0 };
    kal_process p{};
    const int e = kal_process_spawn(&how, script + 1, std::strlen(script + 1), argv, lens, 1,
                                    nullptr, nullptr, 0, nullptr, &p);
    int status = -1, terminated = 0;
    if (e == kal_ok) kal_process_wait(p, &status, &terminated);

    char seen[256] = {};
    if (FILE* f = std::fopen(beside, "r")) { std::fgets(seen, sizeof seen, f); std::fclose(f); }
    ::unlink(beside);
    ::unlink(script);
    ::rmdir(dir);

    int failures = 0;
    if (e != kal_ok || terminated != 0 || status != 0) {
        std::printf("FAIL: the script is started (rc %d, status %d)\n", e, status);
        ++failures;
    }
    if (std::strcmp(seen, script) != 0) {
        std::printf("FAIL: the script sees its own name as $0 (saw '%s')\n", seen);
        ++failures;
    }
    if (failures == 0) std::printf("openkal-linux: a script is given its name\n");
    return failures == 0 ? 0 : 1;
}
