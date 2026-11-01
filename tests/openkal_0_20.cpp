// What openkal 0.20 adds, as this kernel does it (openkal-linux 0.21).
//
// TRANSFERS AT AN OFFSET FROM SEVERAL CONTEXTS AT ONCE, beside a sequential
// reader of the same file: four contexts read at offsets while the first reads
// the file from start to end through its stream. Composed from a seek, a read
// and a seek back, a transfer at an offset moves the position the sequential
// reader depends on, and the sequential reader then skips or repeats bytes.
//
// THE CLOCK WITHOUT A SYSTEM CALL keeps the clock's properties: the monotonic
// clock never goes back across many reads from two contexts, and the wall clock
// agrees with the kernel's to within a second.
import openkal.fs;
import openkal.stream;
import openkal.task;
import openkal.time;
import openkal.types;

namespace {
int failures = 0;
void check(bool ok, const char* what) {
    if (ok) return;
    ++failures;
    const char pre[] = "FAIL: ";
    kal::write(kal::err(), pre, sizeof(pre) - 1);
    kal_uintptr n = 0; while (what[n]) ++n;
    kal::write(kal::err(), what, n);
    kal::write(kal::err(), "\n", 1);
}

constexpr kal_uintptr kSize = 1u << 16;
constexpr char kName[] = "openkal-0-20-at-offsets.tmp";

unsigned char byte_at(kal_u64 i) { return static_cast<unsigned char>(i * 131u + (i >> 8)); }

struct reader { kal_file f; int rounds; int bad; };

void read_at_offsets(void* arg) {
    auto* r = static_cast<reader*>(arg);
    unsigned seed = 12345u + static_cast<unsigned>(reinterpret_cast<kal_uintptr>(arg) >> 4);
    unsigned char buf[97];
    for (int i = 0; i < r->rounds; ++i) {
        seed = seed * 1103515245u + 12345u;
        const kal_u64 at = (seed >> 8) % (kSize - sizeof buf);
        const kal_intptr n = kal_fs_read_at(r->f, buf, sizeof buf, at);
        if (n != static_cast<kal_intptr>(sizeof buf)) { ++r->bad; continue; }
        for (kal_uintptr k = 0; k < sizeof buf; ++k) if (buf[k] != byte_at(at + k)) { ++r->bad; break; }
    }
}

struct clock_reader { int rounds; int backwards; };

void read_clock(void* arg) {
    auto* c = static_cast<clock_reader*>(arg);
    kal_duration last = kal_time_monotonic();
    for (int i = 0; i < c->rounds; ++i) {
        const kal_duration now = kal_time_monotonic();
        if (now < last) ++c->backwards;
        last = now;
    }
}
}  // namespace

int main() {
    const kal_dir here = kal::fs::working();
    {
        kal_file f{};
        const auto flags = kal::fs::open::read | kal::fs::open::write
                         | kal::fs::open::create | kal::fs::open::truncate;
        check(kal::fs::open_file(here, kName, sizeof kName - 1, flags, &f) == kal_ok, "the file is made");
        static unsigned char block[kSize];
        for (kal_uintptr i = 0; i < kSize; ++i) block[i] = byte_at(i);
        check(kal_fs_write_at(f, block, kSize, 0) == static_cast<kal_intptr>(kSize),
              "the file is written at offset zero, whole");
        kal_u64 at = 1;
        check(kal_fs_seek(f, 0, kal::fs::seek_current, &at) == kal_ok && at == 0,
              "and its position is still zero");

        reader r[4]{ { f, 4000, 0 }, { f, 4000, 0 }, { f, 4000, 0 }, { f, 4000, 0 } };
        kal_task t[4]{};
        for (int i = 0; i < 4; ++i) check(kal_task_start(read_at_offsets, &r[i], &t[i]) == kal_ok, "a reader starts");
        // The sequential reader, meanwhile.
        kal_u64 seen = 0;
        int out_of_order = 0;
        unsigned char chunk[251];
        for (;;) {
            const kal_intptr n = kal_stream_read(kal_fs_stream(f), chunk, sizeof chunk);
            if (n <= 0) break;
            for (kal_intptr k = 0; k < n; ++k) if (chunk[k] != byte_at(seen + static_cast<kal_u64>(k))) ++out_of_order;
            seen += static_cast<kal_u64>(n);
        }
        for (int i = 0; i < 4; ++i) kal_task_join(t[i]);
        check(seen == kSize && out_of_order == 0,
              "a sequential reader reads every byte in order while others read at offsets");
        int bad = 0;
        for (const auto& x : r) bad += x.bad;
        check(bad == 0, "every read at an offset reads the bytes at that offset");
        kal_fs_close_file(f);
        kal_fs_remove(here, kName, sizeof kName - 1);
    }
    {
        clock_reader c[2]{ { 200000, 0 }, { 200000, 0 } };
        kal_task t{};
        check(kal_task_start(read_clock, &c[1], &t) == kal_ok, "a clock reader starts");
        read_clock(&c[0]);
        kal_task_join(t);
        check(c[0].backwards == 0 && c[1].backwards == 0, "the monotonic clock never goes back");
        const kal_duration a = kal_time_wall();
        kal_time_sleep(20u * 1000u * 1000u);
        const kal_duration b = kal_time_wall();
        check(b > a && b - a >= 19u * 1000u * 1000u && b - a < 1000u * 1000u * 1000u,
              "the wall clock advances with a sleep, and by about as much");
    }
    return failures == 0 ? 0 : 1;
}
