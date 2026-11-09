// Conformance: the least stack size a started context is given (openkal 0.16).
//
// WHAT IS OBSERVED IS ROOM. The report this answers (mcpplibs/openkal-musl#48)
// asked for a mebibyte, was given 256 KiB, and faulted at a depth the host's own
// C library completed; a size that was reported and not provided would pass a
// test that only read the number back.
import openkal.types;
import openkal.task;
import openkal.stream;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;

namespace {
void check(bool ok, const char* what) { expect(ok) << what; }

long recurse(long n);
long (*volatile g_recurse)(long) = recurse;
long recurse(long n) {
    volatile char pad[1000];
    pad[0] = static_cast<char>(n);
    if (n == 0) return 0;
    return g_recurse(n - 1) + pad[0];
}

struct probe { kal_uintptr ask; kal_uintptr told; volatile int done; };

void room(void* p) {
    auto* q = static_cast<probe*>(p);
    void* base = nullptr; kal_uintptr size = 0;
    if (kal_task_stack(&base, &size) == kal_ok) q->told = size;
    g_recurse(static_cast<long>(q->ask / 1024 * 3 / 4));
    __atomic_store_n(&q->done, 1, __ATOMIC_RELEASE);
}
}

UTest conformance_stack_size_test{"conformance_stack_size", [] {
    const kal_uintptr asks[] = { 1u << 20, 16u << 20, 64u << 20 };
    for (kal_uintptr ask : asks) {
        probe q{ ask, 0, 0 };
        kal_task t{};
        check(kal_task_start_sized(room, &q, ask, &t) == kal_ok, "a context with a least stack size starts");
        kal_task_join(t);
        check(q.told >= ask, "the context is told at least the size asked for");
        check(q.done == 1, "the context has the room it asked for");
    }
    // An unmeetable request is exhaustion, and nothing is started.
    kal_task never{};
    check(kal_task_start_sized(room, nullptr, ~kal_uintptr{0} - 4095, &never) == kal_err_no_memory,
          "a stack larger than the address space is reported as exhaustion");
}};
