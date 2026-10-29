// A directory the program may search but not read is still a base (0.18.3).
//
// A `kal_dir' names a directory; resolving beneath it, starting a program in
// it and describing it read nothing, so none of them needs the right to read
// it. Only listing does. Until 0.18.2 every handle was opened for reading, and
// on Android 14 under `run-as' (SELinux `runas_app') the root preopen itself
// failed with EACCES: every program started by absolute name was reported as
// not found (mcpp#857).
//
// Here the same situation is made with a mode instead of a policy: a directory
// of mode 0111. A privileged user reads it regardless, so the one assertion
// that it cannot be listed is made only for an unprivileged one.
#include <cstdio>
#include <cstring>
#include <sys/stat.h>
#include <unistd.h>
import openkal.fs;
import openkal.stream;
import openkal.types;
import openkal.process;

namespace {
int failures = 0;
void check(bool held, const char* what) {
    if (!held) { std::printf("FAIL: %s\n", what); ++failures; }
}

kal_dir root() {
    for (kal_uintptr i = 0; i < kal::fs::preopen_count(); ++i) {
        kal_dir d{}; char name[512]; kal_uintptr len = 0;
        if (kal_fs_preopen(i, &d, name, sizeof name, &len) == kal_ok && len == 1 && name[0] == '/')
            return d;
    }
    return kal_dir{};
}
}

int main() {
    const auto work = kal::fs::working();
    check(kal_fs_mkdir(work, "okl_search_only", 15) == kal_ok, "a directory is created");
    kal_dir d{};
    check(kal_fs_open_dir(work, "okl_search_only", 15, &d) == kal_ok, "it opens");
    kal_file f{};
    check(kal::fs::open_file(d, "inside", 6, kal::fs::open::write | kal::fs::open::create, &f) == kal_ok,
          "a file is created in it");
    kal_stream_write(kal_fs_stream(f), "x", 1);
    kal_fs_close_file(f);
    kal_fs_close_dir(d);

    check(::chmod("okl_search_only", 0111) == 0, "the directory loses its read right");

    kal_dir s{};
    check(kal_fs_open_dir(work, "okl_search_only", 15, &s) == kal_ok,
          "a searchable directory without the read right opens as a base");
    kal_file g{};
    check(kal::fs::open_file(s, "inside", 6, kal::fs::open::read, &g) == kal_ok,
          "a name beneath it resolves");
    char c = 0;
    check(kal_stream_read(kal_fs_stream(g), &c, 1) == 1 && c == 'x', "the file beneath it reads");
    kal_fs_close_file(g);

    // A program is started IN it: the base it is resolved under is the root
    // preopen, and its working directory is the search-only directory.
    const char* argv[] = { "true" };
    const kal_uintptr lens[] = { 4 };
    const kal_spawn how{ root(), s, nullptr, nullptr, 0, 0 };
    kal_process p{};
    const int rc = kal_process_spawn(&how, "bin/true", 8, argv, lens, 1, nullptr, nullptr, 0, nullptr, &p);
    check(rc == kal_ok, "a program starts in a search-only directory");
    if (rc == kal_ok) {
        int status = -1, terminated = -1;
        kal_process_wait(p, &status, &terminated);
        kal_process_close(p);
        check(terminated == 0 && status == 0, "the program ran to completion");
    }

    if (::geteuid() != 0) {
        kal_uintptr iter = 0;
        check(kal_fs_list_begin(s, &iter) == kal_err_permission,
              "listing it is what the missing read right refuses");
    }
    kal_fs_close_dir(s);
    ::chmod("okl_search_only", 0755);
    ::unlink("okl_search_only/inside");
    ::rmdir("okl_search_only");

    if (failures == 0) std::printf("search_only_base: ok\n");
    return failures == 0 ? 0 : 1;
}
