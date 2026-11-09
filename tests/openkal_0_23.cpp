// What openkal-linux 0.24 changes beside openkal 0.23, as this kernel does it.
//
// A RECORD ACROSS A START: a program granted a recorded handle appends what it
// reached to its caller's record, and its end. A CONFINED PROGRAM IS HELD BY
// THE KERNEL: one that writes without this interface --- a raw system call ---
// is refused by Landlock where the kernel has it. A PREFERENCE FOR ECONOMY
// turns the program's transparent huge pages off.
#include <cstdio>
#include <cerrno>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>
#include <fcntl.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <sys/stat.h>
#include <unistd.h>
import openkal.types;
import openkal.env;
import openkal.fs;
import openkal.process;
import openkal.abort;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;

namespace {
void check(bool held, const char* what) { expect(held) << what; }

kal_dir root() {
    for (kal_uintptr i = 0; i < kal_fs_preopen_count(); ++i) {
        kal_dir d{}; char n[8]; kal_uintptr l = 0;
        if (kal_fs_preopen(i, &d, n, sizeof n, &l) == kal_ok && l == 1 && n[0] == '/') return d;
    }
    return kal_dir{};
}

char g_self[4096];
char g_dir[] = "/tmp/okl-0.24-XXXXXX";

int start(const char* errand, const kal_preopen* grants, kal_uintptr n, kal_uintptr flags,
          const char* operand = nullptr) {
    const kal_dir r = root();
    std::vector<std::string> operands;
    if (operand) operands.emplace_back(operand);
    const std::vector<std::string> command = child_command(errand, operands);
    std::vector<const char*> args;
    std::vector<kal_uintptr> lens;
    for (const auto& a : command) { args.push_back(a.c_str()); lens.push_back(a.size()); }
    const kal_spawn how{ r, r, nullptr, grants, n, flags };
    kal_process p{};
    if (kal_process_spawn(&how, g_self + 1, std::strlen(g_self + 1), args.data(), lens.data(), args.size(), nullptr,
                          nullptr, 0, nullptr, &p) != kal_ok)
        return -1;
    int status = -1, terminated = 0;
    kal_process_wait(p, &status, &terminated);
    kal_process_close(p);
    return terminated ? -2 : status;
}
}  // namespace

// The first preopen is the recorded grant: read a file, ask for an absent one,
// and end through kal_exit so the record is ended.
child read_granted{"read", [](std::vector<std::string>) -> int {
    kal_dir d{}; kal_uintptr l = 0;
    if (kal_fs_preopen(0, &d, nullptr, 0, &l) != kal_ok) kal_exit(51);
    kal_file f{};
    if (kal_fs_open(d, "present.txt", 11, kal::fs::open::read.bits, &f) != kal_ok) kal_exit(52);
    kal_fs_close_file(f);
    kal_node_info i{}; i.self_size = sizeof i;
    kal_fs_info(d, "absent.txt", 10, 0, kal::fs::field::kind, &i);
    kal_exit(0);
}};

// Past openkal: the kernel itself is asked to create a file in the read-only
// grant, and in a directory not granted at all.
child write_raw{"write-raw", [](std::vector<std::string>) {
    const int a = static_cast<int>(::syscall(SYS_openat, AT_FDCWD, "/tmp/okl-0.24-raw.tmp", O_CREAT | O_WRONLY, 0600));
    if (a >= 0) { ::unlink("/tmp/okl-0.24-raw.tmp"); return 61; }
    const int b = static_cast<int>(::syscall(SYS_openat, AT_FDCWD, "/etc/hostname", O_RDONLY));
    if (b >= 0) return 62;
    return 60;
}};

child thp{"thp", [](std::vector<std::string>) {
    return static_cast<int>(::prctl(PR_GET_THP_DISABLE, 0, 0, 0, 0)) == 1 ? 70 : 71;
}};

// Prove the existing file is readable before asking the kernel to truncate it.
// Missing files and unreachable grants are not proof.
int truncate_raw(const std::vector<std::string>& args, bool by_open) {
    if (args.size() != 1) return 67;
    const char* path = args[0].c_str();
    const int readable = static_cast<int>(::syscall(SYS_openat, AT_FDCWD, path, O_RDONLY));
    if (readable < 0) return 66;
    ::close(readable);
    errno = 0;
    const long result = by_open ? ::syscall(SYS_openat, AT_FDCWD, path, O_RDONLY | O_TRUNC)
                                : ::syscall(SYS_truncate, path, 0);
    const int why = errno;
    if (result >= 0) {
        if (by_open) ::close(static_cast<int>(result));
        return 64;
    }
    return why == EACCES ? 63 : 65;
}
child truncate_by_name{"truncate-raw", [](std::vector<std::string> args) { return truncate_raw(args, false); }};
child truncate_by_open{"open-truncate-raw", [](std::vector<std::string> args) { return truncate_raw(args, true); }};

UTest openkal_0_23_test{"openkal_0_23", [] {
    // The pinned compiler's Linux headers can predate Landlock. This syscall
    // has number 444 on both architectures this implementation builds for.
    const long landlock = ::syscall(444, nullptr, 0, 1 /* _VERSION */);
    check(kal::process::has(kal::process::confine) == (landlock >= 3),
          "confinement is claimed only where Landlock can restrict file truncation");

    const kal_intptr n = kal_env_image(g_self, sizeof g_self - 1);
    if (n <= 0 || g_self[0] != '/') { fail("the program's own image is named"); return; }
    g_self[n] = 0;
    if (!mkdtemp(g_dir)) { fail("a directory for the test"); return; }
    char path[256];
    std::snprintf(path, sizeof path, "%s/present.txt", g_dir);
    { const int fd = ::open(path, O_CREAT | O_WRONLY, 0600); (void)!::write(fd, "x", 1); ::close(fd); }

    // A record across a start.
    kal_dir dir{};
    check(kal_fs_open_dir(root(), g_dir + 1, std::strlen(g_dir + 1), &dir) == kal_ok, "the test's directory is opened");
    kal_file rec{};
    check(kal_fs_open(dir, "record", 6, (kal::fs::open::write | kal::fs::open::create | kal::fs::open::truncate).bits, &rec) == kal_ok,
          "a record file is opened");
    kal_dir_rights rights{};
    rights.self_size = sizeof rights;
    rights.flags = (kal::fs::dir_recorded | kal::fs::dir_read_only).bits;
    rights.record = rec;
    kal_dir rd{};
    check(kal_fs_dir_attenuate(dir, &rights, &rd) == kal_ok, "the directory is attenuated, recorded");
    const kal_preopen grant{ rd, "/granted", 8 };
    check(start("read", &grant, 1, 0) == 0, "a program granted the recorded handle reads through it");
    kal_fs_close_dir(rd);
    kal_fs_close_file(rec);
    char buf[4096] = {};
    std::snprintf(path, sizeof path, "%s/record", g_dir);
    const int fd = ::open(path, O_RDONLY);
    const long got = fd >= 0 ? ::read(fd, buf, sizeof buf) : -1;
    if (fd >= 0) ::close(fd);
    auto holds = [&](const char* entry) {
        const std::size_t len = std::strlen(entry) + 1;
        for (long i = 0; i + static_cast<long>(len) <= got; ++i)
            if (std::memcmp(buf + i, entry, len) == 0 && (i == 0 || buf[i - 1] == 0)) return true;
        return false;
    };
    check(holds("rpresent.txt"), "the started program's read is in its caller's record");
    check(holds("aabsent.txt"), "and so is the name it found absent");
    check(holds("e"), "and the record of the program that ended is ended");

    // Confinement, where this kernel has Landlock.
    if (kal::process::has(kal::process::confine)) {
        kal_dir_rights ro{};
        ro.self_size = sizeof ro;
        ro.flags = kal::fs::dir_read_only.bits;
        // What a program of this system reads to start --- its loader and
        // libraries, wherever the toolchain put them --- and /tmp, read-only;
        // /etc is not granted.
        static const char* const trees[] = { "tmp", "usr", "lib", "lib64", "home" };
        kal_preopen grants[8];
        static char names[8][16];
        kal_uintptr count = 0;
        for (const char* t : trees) {
            kal_dir d{}, a{};
            if (kal_fs_open_dir(root(), t, std::strlen(t), &d) != kal_ok) continue;
            if (kal_fs_dir_attenuate(d, &ro, &a) != kal_ok) continue;
            std::snprintf(names[count], sizeof names[count], "/%s", t);
            grants[count] = kal_preopen{ a, names[count], std::strlen(names[count]) };
            ++count;
        }
        const int s = start("write-raw", grants, count, kal::process::confine_flag.bits);
        expect(s == 60) << "a confined program is refused by the kernel what its grants do not allow, without openkal"
                        << " (the confined program reported " << s << ")";
        std::snprintf(path, sizeof path, "%s/present.txt", g_dir);
        const char* truncations[] = { "truncate-raw", "open-truncate-raw" };
        for (const char* errand : truncations) {
            check(start(errand, grants, count, kal::process::confine_flag.bits, path) == 63,
                  "a readable file in a read-only grant cannot be truncated without openkal");
            struct stat info{};
            check(::stat(path, &info) == 0 && info.st_size == 1,
                  "the read-only file retains its contents after the raw truncation attempt");
        }
    } else {
        std::printf("  this kernel has no Landlock ABI 3; confinement is refused, which the conformance suite observes\n");
    }

    // Economy.
    check(start("thp", nullptr, 0, kal::process::economy_flag.bits) == 70,
          "a program started for economy has no transparent huge pages");
    check(start("thp", nullptr, 0, kal::process::performance_flag.bits) == 71,
          "and one started for performance has them as the system decides");

    std::snprintf(path, sizeof path, "%s/present.txt", g_dir); ::unlink(path);
    std::snprintf(path, sizeof path, "%s/record", g_dir); ::unlink(path);
    ::rmdir(g_dir);
}};
