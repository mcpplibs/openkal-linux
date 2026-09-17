// The operations version 0.5 added, and the declarations the modules export.
//
// Each assertion is written so that it can fail. The three conditions
// kal_fs_open exists to express are exactly the three that an implementation
// could appear to satisfy while satisfying nothing: a truncation that did not
// happen leaves a longer file, an exclusion that did not happen succeeds, and
// an append that did not happen overwrites. Each is therefore observed by its
// effect rather than by its return value alone.
#include <cstdio>
#include <cstring>
import openkal.fs;
import openkal.stream;
import openkal.types;
import openkal.task;
import openkal.time;
import openkal.env;
import openkal.abort;
import openkal.memory;
import openkal.process;

namespace {

int failures = 0;

void check(bool held, const char* what) {
    if (!held) { std::printf("FAIL: %s\n", what); ++failures; }
}

kal_dir here() { return kal::fs::working(); }

// Writes the given bytes to a name, replacing whatever was there.
bool put(const char* name, const char* text) {
    kal_file f{};
    const auto flags = kal::fs::open::write | kal::fs::open::create
                     | kal::fs::open::truncate;
    if (kal::fs::open_file(here(), name, std::strlen(name), flags, &f) != kal_ok) return false;
    const kal_intptr r = kal_stream_write(kal_fs_stream(f), text, std::strlen(text));
    kal_fs_close_file(f);
    return r == static_cast<kal_intptr>(std::strlen(text));
}

// Reads a whole file into the buffer and reports its length, or -1.
long get(const char* name, char* buf, kal_uintptr cap) {
    kal_file f{};
    if (kal::fs::open_file(here(), name, std::strlen(name), kal::fs::open::read, &f) != kal_ok)
        return -1;
    const kal_intptr r = kal_stream_read(kal_fs_stream(f), buf, cap);
    kal_fs_close_file(f);
    return r >= 0 ? static_cast<long>(r) : -1;
}

}  // namespace

int main() {
    const char* name = "okl-additions.tmp";
    const kal_uintptr n = std::strlen(name);
    char buf[256];

    // --- truncation on opening ----------------------------------------------
    check(put(name, "0123456789"), "a file is written");
    check(put(name, "abc"), "the same file is rewritten shorter");
    long len = get(name, buf, sizeof buf);
    // Without truncation the file would still hold "abc3456789", which is the
    // silent wrongness clause 7.8 describes: every call reported success.
    check(len == 3, "opening with truncate discarded what lay beyond");

    // --- exclusion ----------------------------------------------------------
    kal_file f{};
    const auto excl = kal::fs::open::write | kal::fs::open::create
                    | kal::fs::open::exclusive;
    check(kal::fs::open_file(here(), name, n, excl, &f) == kal_err_exists,
          "creating a name that exists is refused, and says which condition held");
    kal_fs_remove(here(), name, n);
    check(kal::fs::open_file(here(), name, n, excl, &f) == kal_ok,
          "creating a name that does not exist succeeds");
    kal_fs_close_file(f);

    // --- appending ----------------------------------------------------------
    check(put(name, "one"), "a file is written before appending");
    {
        kal_file a{};
        const auto flags = kal::fs::open::write | kal::fs::open::append;
        check(kal::fs::open_file(here(), name, n, flags, &a) == kal_ok, "a file opens for appending");
        // Positioning at the start and then writing must still append. An
        // implementation that ignored the flag would produce "two" and report
        // success for every call.
        kal_u64 at = 0;
        kal_fs_seek(a, 0, kal::fs::seek_set, &at);
        kal_stream_write(kal_fs_stream(a), "two", 3);
        kal_fs_close_file(a);
    }
    len = get(name, buf, sizeof buf);
    check(len == 6 && std::memcmp(buf, "onetwo", 6) == 0,
          "a write to a file opened for appending went to the end");

    // --- the length of an open file ----------------------------------------
    {
        kal_file t{};
        check(kal::fs::open_file(here(), name, n, kal::fs::open::write, &t) == kal_ok,
              "a file opens for setting its length");
        check(kal_fs_truncate(t, 2) == kal_ok, "the length is set");
        kal_node_info info{}; info.self_size = sizeof info;
        check(kal_fs_file_info(t, kal::fs::field::all, &info) == kal_ok, "an open file is enquired about");
        check(info.size == 2, "the enquiry reports the length that was set");
        check(info.kind == kal_node_file, "the enquiry reports what the handle refers to");
        check(kal_fs_truncate(t, 9) == kal_ok, "the length is extended");
        check(kal_fs_file_info(t, kal::fs::field::all, &info) == kal_ok && info.size == 9,
              "extending reports the larger length");
        kal_fs_close_file(t);
    }

    // --- absence is an answer, and is distinguishable ------------------------
    kal_fs_remove(here(), name, n);
    kal_node_info gone{}; gone.self_size = sizeof gone;
    check(kal_fs_info(here(), name, n, 0, kal::fs::field::all, &gone) == kal_ok && gone.kind == kal_node_absent,
          "enquiry about a name that does not exist is answered");
    check(kal::fs::open_file(here(), name, n, kal::fs::open::read, &f) == kal_err_not_found,
          "opening a name that does not exist reports that it does not exist");

    // --- a stream reports what a C library must know before writing ---------
    // The value is not asserted: whether the test runs on a terminal is not a
    // property of the implementation. What is asserted is that the enquiry
    // answers for every standard stream and that the answer is confined to the
    // positions the specification assigns.
    const kal_stream standard[3] = { kal_stdin(), kal_stdout(), kal_stderr() };
    for (const kal_stream s : standard) {
        const auto p = kal::properties(s);
        check((p.bits & ~kal::stream_prop::interactive.bits) == 0,
              "a stream reports no position the specification has not assigned");
    }

    // --- the property a C library cannot be ported without -------------------
    check(kal::task::has(kal::task::thread_local_storage),
          "a started context observes thread-local storage");

    // --- the capability words are of distinct types ---------------------------
    // The following would compile in version 0.4 and answer a question nobody
    // asked, because every capability word was a kal_uintptr:
    //
    //     kal::time::has(kal::fs::links)
    //
    // It is now a diagnostic. A test cannot assert that something does not
    // compile, so what is asserted here is the property that makes it so.
    static_assert(!__is_same(kal::time::props, kal::fs::props));
    static_assert(!__is_same(kal::task::props, kal::process::props));
    static_assert(!__is_same(kal::fs::props, kal::fs::open_flags));

    // --- version 0.13: a node that may be started, and one that is not a program
    //
    // The mode is read back from the kernel's own record rather than through
    // the enquiry, so that the enquiry and the operation cannot agree with each
    // other while both are wrong.
    {
        const char* prog = "okl-not-a-program.tmp";
        const kal_uintptr pn = std::strlen(prog);
        check(put(prog, "this is text\n"), "a text file is written");
        check((kal_fs_props(here()) & kal::fs::executable.bits) != 0,
              "the volume the tests run on records whether a node may be started");
        check(kal_fs_set_executable_at(here(), prog, pn, 1) == kal_ok,
              "the node is recorded as startable");
        kal_node_info info = kal::fs::info_for_caller();
        check(kal_fs_info(here(), prog, pn, 0, kal::fs::field::executable, &info) == kal_ok
                  && (info.present & kal::fs::field::executable) != 0 && info.executable == 1,
              "the enquiry reports it");

        kal_process p{};
        const char* argv[1] = { prog };
        const kal_uintptr lens[1] = { pn };
        const kal_spawn how{ here(), here(), nullptr, nullptr, 0, 0 };
        const int e = kal_process_spawn(&how, prog, pn, argv, lens, 1,
                                        nullptr, nullptr, 0, nullptr, &p);
        check(e == kal_err_not_program,
              "the kernel's ENOEXEC reaches the caller as kal_err_not_program");

        check(kal_fs_set_executable_at(here(), prog, pn, 0) == kal_ok,
              "the record is cleared");
        const kal_spawn again{ here(), here(), nullptr, nullptr, 0, 0 };
        check(kal_process_spawn(&again, prog, pn, argv, lens, 1,
                                nullptr, nullptr, 0, nullptr, &p) == kal_err_permission,
              "and a node not recorded as startable is refused as a permission");
        kal_fs_remove(here(), prog, pn);
    }

    std::printf("openkal-linux: the operations version 0.5 added\n");
    return failures == 0 ? 0 : 1;
}
