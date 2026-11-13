// A thirty-two bit build of this implementation runs (clause 5.5 of the specification): built
// standalone for i386 by tools/word32-check.sh and run on the machine that built it. A file past
// four gibibytes, a task with its own thread-local storage, a sleep through the sixty-four bit
// clock. The program supplies the C library facilities the compiler calls.
#include <openkal/stream.h>
#include <openkal/fs.h>
#include <openkal/task.h>
#include <openkal/time.h>
#include <openkal/abort.h>
#include <openkal/process.h>

static int failures = 0;
static void say(const char* s) { kal_uintptr n = 0; while (s[n]) ++n; kal_stream_write(kal_stdout(), s, n); }
static void check(bool ok, const char* what) { say(ok ? "  ok   " : "  FAIL "); say(what); say("\n"); if (!ok) ++failures; }

thread_local int tlv = 7;
static volatile int shared = 0;
static void task_body(void*) { tlv = 99; shared = tlv + 1; }

extern "C" int main(int, char**, char**) {
    say("openkal-linux on i386\n");
    check(sizeof(void*) == 4, "a pointer is four bytes");
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
    const char fname[] = "okl-i386-big";
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
    check(kal_fs_remove(work, fname, sizeof fname - 1) == kal_ok, "the file is removed");
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
