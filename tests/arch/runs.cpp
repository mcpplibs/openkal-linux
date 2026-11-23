// A build of this implementation for an architecture the building machine does not run natively
// runs: built standalone by tools/arch-check.sh, run on the machine that built it (i386: clause 5.5
// of the specification, a thirty-two bit word with sixty-four bit positions and times) or under
// qemu's user-mode translator (riscv64). What differs between architectures is each a check: the
// gate and its numbers (a file past four gibibytes, a directory opened and listed, a rename), the
// kernel's records (a node's kind and size), the thread pointer (a task with its own thread-local
// storage), the clock, a handler's record and the frame chain (a sample of the program's own stack),
// the record of a signal's action as the kernel and the translator each write it, a started
// program and its peak, and published instructions run. The program supplies the C library facilities the compiler calls.
#include <openkal/stream.h>
#include <openkal/fs.h>
#include <openkal/task.h>
#include <openkal/time.h>
#include <openkal/abort.h>
#include <openkal/process.h>
#include <openkal/exec.h>
#include <openkal/prof.h>
#include "../../src/sys.h"

#if defined(__i386__)
#define OKL_ARCH "i386"
constexpr kal_uintptr word = 4;
// mov $42, %eax; ret
constexpr unsigned char answer_code[] = { 0xb8, 0x2a, 0x00, 0x00, 0x00, 0xc3 };
#elif defined(__riscv) && __riscv_xlen == 64
#define OKL_ARCH "riscv64"
constexpr kal_uintptr word = 8;
// li a0, 42; ret
constexpr unsigned char answer_code[] = { 0x13, 0x05, 0xa0, 0x02, 0x67, 0x80, 0x00, 0x00 };
#else
#error "tests/arch/runs.cpp is built for i386 and riscv64 (tools/arch-check.sh)"
#endif

static int failures = 0;
static void say(const char* s) { kal_uintptr n = 0; while (s[n]) ++n; kal_stream_write(kal_stdout(), s, n); }
static void check(bool ok, const char* what) { say(ok ? "  ok   " : "  FAIL "); say(what); say("\n"); if (!ok) ++failures; }

thread_local int tlv = 7;
static volatile int shared = 0;
static void task_body(void*) { tlv = 99; shared = tlv + 1; }

// Work the sampler finds on the stack, two frames deep.
[[gnu::noinline]] static kal_u64 sampled_inner(kal_duration until) {
    volatile kal_u64 sink = 0;
    while (kal_time_monotonic() < until) sink = sink + 1;
    return sink;
}
[[gnu::noinline]] static kal_u64 sampled(kal_duration ns) { return sampled_inner(kal_time_monotonic() + ns) + 1; }

extern "C" int main(int, char**, char**) {
    say("openkal-linux on " OKL_ARCH "\n");
    check(sizeof(void*) == word, "a pointer is the architecture's word");
    kal_dir work{};
    char name[256]; kal_uintptr name_len = 0;
    const kal_uintptr n = kal_fs_preopen_count();
    bool found = false;
    for (kal_uintptr i = 0; i < n && !found; ++i)
        if (kal_fs_preopen(i, &work, name, sizeof name, &name_len) == kal_ok) {
            found = true;  // the first is the working directory
        }
    check(found, "a directory was supplied");
    kal_file f{};
    const char fname[] = "okl-arch-big";
    int rc = kal_fs_open(work, fname, sizeof fname - 1, KAL_OPEN_READ | KAL_OPEN_WRITE | KAL_OPEN_CREATE | KAL_OPEN_TRUNCATE, &f);
    check(rc == kal_ok, "a file is created");
    const kal_u64 far = (kal_u64{5} << 30);
    check(kal_fs_write_at(f, "0123456789", 10, far) == 10, "ten bytes are written at 5 GiB");
    char back[10] = {};
    check(kal_fs_read_at(f, back, 10, far) == 10 && back[0] == '0' && back[9] == '9', "they are read back at 5 GiB");
    kal_node_info info{}; info.self_size = sizeof info;
    rc = kal_fs_file_info(f, KAL_INFO_SIZE, &info);
    check(rc == kal_ok && info.size == far + 10, "the file's size is past 4 GiB");
    kal_u64 at = 0;
    rc = kal_fs_seek(f, 0, KAL_SEEK_END, &at);
    check(rc == kal_ok && at == far + 10, "a seek to the end answers a position past 4 GiB");
    kal_fs_close_file(f);
    info = kal_node_info{}; info.self_size = sizeof info;
    rc = kal_fs_info(work, fname, sizeof fname - 1, 0, KAL_INFO_KIND | KAL_INFO_SIZE, &info);
    check(rc == kal_ok && info.kind == kal_node_file && info.size == far + 10,
          "the kernel's record of the node: a file, of its size");
    const char dname[] = "okl-arch-dir", moved[] = "okl-arch-big2";
    check(kal_fs_mkdir(work, dname, sizeof dname - 1) == kal_ok, "a directory is made");
    kal_dir sub{};
    check(kal_fs_open_dir(work, dname, sizeof dname - 1, &sub) == kal_ok, "it is opened as a directory");
    check(kal_fs_rename(work, fname, sizeof fname - 1, sub, moved, sizeof moved - 1) == kal_ok,
          "the file is renamed into it");
    kal_uintptr iter = 0; char entry[64]; kal_uintptr entry_len = 0; int kind = 0; int listed = 0;
    if (kal_fs_list_begin(sub, &iter) == kal_ok)
        while (kal_fs_list_next(sub, &iter, entry, sizeof entry, &entry_len, &kind) == kal_ok)
            if (entry_len == sizeof moved - 1 && kind == kal_node_file) ++listed;
    check(listed == 1, "the directory lists it");
    check(kal_fs_remove(sub, moved, sizeof moved - 1) == kal_ok, "the file is removed");
    kal_fs_close_dir(sub);
    check(kal_fs_remove(work, dname, sizeof dname - 1) == kal_ok, "the directory is removed");
    kal_task t{};
    check(kal_task_start(task_body, nullptr, &t) == kal_ok, "a task starts");
    check(kal_task_join(t) == kal_ok, "it is joined");
    check(shared == 100, "the task saw its own thread-local value");
    check(tlv == 7, "this context's thread-local value is its own");
    const kal_duration before = kal_time_monotonic();
    kal_time_sleep(20u * 1000u * 1000u);
    check(kal_time_monotonic() - before >= 20u * 1000u * 1000u, "a sleep of 20 ms lasts at least that");
    // The processor time is read through the sixty-four bit clock as well: worked for about
    // 50 ms, it is more than 10 ms and less than the time since the program started.
    const kal_duration spin = kal_time_monotonic();
    volatile kal_u64 sink = 0;
    while (kal_time_monotonic() - spin < 50u * 1000u * 1000u) sink = sink + 1;
    kal_process me{};
    kal_program_info pi{}; pi.self_size = sizeof pi;
    check(kal_process_self(&me) == kal_ok && kal_process_info(me, KAL_PINFO_CPU, &pi) == kal_ok
              && (pi.present & KAL_PINFO_CPU) && pi.cpu_ns > 10u * 1000u * 1000u
              && pi.cpu_ns < kal_time_monotonic() - before + 1000u * 1000u * 1000u,
          "the processor time counts the work done");
    kal_process_close(me);
    // A handler's record and the frame chain: a sample of this program's stack names the work and
    // the frames above it.
    if (kal_prof_props() & KAL_PROF_PROP_SAMPLES) {
        check(kal_prof_sample_begin(1000) == kal_ok, "sampling begins");
        sampled(200u * 1000u * 1000u);
        static kal_prof_sample samples[64];
        const kal_intptr got = kal_prof_sample_take(samples, 64);
        kal_prof_sample_end();
        bool deep = false;
        for (kal_intptr i = 0; i < got; ++i) deep = deep || (samples[i].depth >= 3 && samples[i].pc[0] != 0);
        check(got > 0 && deep, "a sample holds the work and the frames above it");
    }
    // The record of a signal's action is as long as what is written into it: the kernel writes its
    // own record, and qemu's user-mode translator writes the riscv64 record with a restorer slot.
    struct { okl::k_sigaction act; volatile okl_u64 guard; } probe{};
    probe.guard = 0x5a5a5a5a5a5a5a5aull;
    const bool queried = !okl::failed(okl::sys(okl::nr_rt_sigaction, 1 /* SIGHUP */, 0,
                                               reinterpret_cast<okl_long>(&probe.act), sizeof probe.act.mask));
    check(queried && probe.guard == 0x5a5a5a5a5a5a5a5aull, "an action's record holds what is written into it");
    // A started program: the started image queries and resets every signal's action through the
    // kernel's record before it is replaced, and ends with the status it was given. The program
    // is the host's shell, which the translator hands to the host kernel.
    kal_dir root{};
    for (kal_uintptr i = 0; i < n; ++i)
        if (kal_fs_preopen(i, &root, name, sizeof name, &name_len) == kal_ok && name_len == 1 && name[0] == '/')
            break;
    const char* argv[] = { "sh", "-c", "exit 3" };
    const kal_uintptr argv_lens[] = { 2, 2, 6 };
    kal_spawn how{}; how.base = root; how.work = work;
    kal_process child{};
    int status = -1, terminated = 1;
    check(kal_process_spawn(&how, "bin/sh", 6, argv, argv_lens, 3, nullptr, nullptr, 0, nullptr, &child) == kal_ok
              && kal_process_wait(child, &status, &terminated) == kal_ok && status == 3 && terminated == 0,
          "a started program ends with its own status");
    kal_program_info ended{}; ended.self_size = sizeof ended;
    check(kal_process_info(child, KAL_PINFO_MEMORY, &ended) == kal_ok
              && ((ended.present & KAL_PINFO_MEMORY) == 0 || ended.memory_peak_bytes > 0),
          "an ended program's peak is stated only from a record the kernel wrote");
    kal_process_close(child);
    // Published instructions run.
    void* code = kal_exec_alloc(sizeof answer_code);
    bool ran = false;
    if (code != nullptr) {
        auto* to = static_cast<unsigned char*>(code);
        for (kal_uintptr i = 0; i < sizeof answer_code; ++i) to[i] = answer_code[i];
        if (kal_exec_publish(code, sizeof answer_code) == kal_ok)
            ran = reinterpret_cast<int (*)()>(code)() == 42;
        kal_exec_free(code, sizeof answer_code);
    }
    check(ran, "published instructions run");
    say(failures ? "FAILED\n" : "all held\n");
    return failures;
}

// What the compiler calls and a C library would supply.
extern "C" {
void* memcpy(void* d, const void* s, __SIZE_TYPE__ n) { auto* o = static_cast<char*>(d); auto* i = static_cast<const char*>(s); while (n--) *o++ = *i++; return d; }
void* memset(void* d, int c, __SIZE_TYPE__ n) { auto* o = static_cast<char*>(d); while (n--) *o++ = static_cast<char>(c); return d; }
unsigned long long __udivmoddi4(unsigned long long a, unsigned long long b, unsigned long long* rem) {
    unsigned long long q = 0, r = 0;
    for (int i = 63; i >= 0; --i) {
        r = (r << 1) | ((a >> i) & 1u);
        if (r >= b) { r -= b; q |= 1ull << i; }
    }
    if (rem) *rem = r;
    return q;
}
unsigned long long __udivdi3(unsigned long long a, unsigned long long b) { return __udivmoddi4(a, b, nullptr); }
unsigned long long __umoddi3(unsigned long long a, unsigned long long b) { unsigned long long r; __udivmoddi4(a, b, &r); return r; }
}
