#include "sys.h"
#include "handle.h"
#include "endpoint.h"
#include <openkal/timeout.h>

// openkal.timeout upon ppoll(2) and wait4(2).
//
// THE BOUND IS APPLIED BEFORE THE OPERATION AND NOT DURING IT. ppoll reports
// whether a descriptor would transfer without blocking, so a bounded read is a
// bounded wait for readiness followed by the ordinary read. This is what the
// environment already does at the point of the call, which is why clause 6.3
// records readiness notification as the alternative that was NOT adopted: an
// interface reporting readiness would oblige an implementation to maintain a set
// and a context of its own, and this one obliges it to maintain nothing.
//
// The bound is therefore upon the WAIT and not upon the transfer. A read that
// becomes ready within the bound and then transfers slowly is not interrupted,
// which is the behaviour every environment's own bounded read has.

namespace {

// A duration of zero denotes no bound, which is the convention kal_task_wait
// establishes. ppoll expresses that by being given no timespec at all.
const okl::ktimespec* bound_of(kal_u64 ns, okl::ktimespec& storage) {
    if (ns == 0) return nullptr;
    storage.sec  = static_cast<okl_i64>(ns / 1000000000ull);
    storage.nsec = static_cast<okl_i64>(ns % 1000000000ull);
    return &storage;
}

// Waits for one descriptor. Reports kal_ok when it is ready, kal_err_again when
// the bound expired, and a translated error otherwise.
int await(int fd, short events, kal_u64 ns) {
    okl::kpollfd p{ fd, events, 0 };
    okl::ktimespec ts{};
    const okl::ktimespec* to = bound_of(ns, ts);

    for (;;) {
        const okl_long r = okl::sys(okl::nr_ppoll, reinterpret_cast<okl_long>(&p),
                                    1, reinterpret_cast<okl_long>(to), 0, 0);
        // AN INTERRUPTED WAIT IS NOT RETRIED WITH THE WHOLE BOUND AGAIN.
        //
        // Retrying with the original duration would make the bound restart at
        // every signal, so a program on a system that delivers them regularly
        // would wait without end while appearing to be bounded. ppoll leaves the
        // caller's timespec untouched, so there is nothing to resume from, and
        // the honest report is that the operation did not complete.
        if (okl::interrupted(r)) return kal_err_again;
        if (okl::failed(r)) return okl::translate(r);
        if (r == 0) return kal_err_again;   // the bound expired
        return kal_ok;
    }
}

}  // namespace

extern "C" {

kal_io_result kal_timeout_read(kal_stream s, void* buf, kal_uintptr len, kal_u64 ns) {
    // A transfer of zero bytes does not wait and is not bounded. Waiting first
    // would turn a call that always succeeds into one that can expire.
    if (len == 0) return { 0, kal_ok };

    const int fd = okl::unpack(s.h);
    // THE STANDARD STREAMS ARE NOT PACKED HANDLES. openkal.stream reports them
    // as the descriptors themselves, so a word that does not unpack is taken to
    // be one of those rather than being refused.
    const int use = (fd >= 0) ? fd : static_cast<int>(s.h);

    if (const int rc = await(use, okl::poll_in, ns); rc != kal_ok) return { 0, rc };
    return kal_stream_read(s, buf, len);
}

kal_io_result kal_timeout_write(kal_stream s, const void* buf, kal_uintptr len, kal_u64 ns) {
    if (len == 0) return { 0, kal_ok };

    const int fd  = okl::unpack(s.h);
    const int use = (fd >= 0) ? fd : static_cast<int>(s.h);

    if (const int rc = await(use, okl::poll_out, ns); rc != kal_ok) return { 0, rc };
    return kal_stream_write(s, buf, len);
}

int kal_timeout_accept(kal_net_listener l, kal_u64 ns, kal_net_conn* out) {
    if (out == nullptr) return kal_err_invalid;
    const int fd = okl::unpack(l.h);
    if (fd < 0) return kal_err_invalid;

    if (const int rc = await(fd, okl::poll_in, ns); rc != kal_ok) return rc;
    return kal_net_accept(l, out);
}

kal_io_result kal_timeout_recv_from(kal_datagram d, void* buf, kal_uintptr len,
                                    kal_endpoint* from, kal_u64 ns) {
    const int fd = okl::unpack(d.h);
    if (fd < 0) return { 0, kal_err_invalid };

    if (const int rc = await(fd, okl::poll_in, ns); rc != kal_ok) return { 0, rc };
    return kal_datagram_recv_from(d, buf, len, from);
}

int kal_timeout_wait_process(kal_process p, kal_u64 ns, int* status, int* terminated) {
    if (p.h == 0) return kal_err_invalid;

    // WNOHANG AND A POLLING LOOP, BECAUSE THE KERNEL HAS NO BOUNDED WAIT FOR A
    // CHILD. wait4 either blocks or does not wait at all; there is no timespec.
    //
    // The alternative is a signal handler for SIGCHLD, which this implementation
    // does not have and would not want: a handler is process-wide state, and an
    // implementation that installed one would take a facility away from the
    // program above it. The loop is what the environment permits, and the
    // interval is bounded below by the granularity this interface reports so
    // that the polling cost is stated rather than hidden.
    constexpr okl_u64 interval_ns = 1000000ull;   // one millisecond
    okl_u64 waited = 0;

    for (;;) {
        int st = 0;
        const okl_long r = okl::sys(okl::nr_wait4, static_cast<okl_long>(p.h),
                                    reinterpret_cast<okl_long>(&st),
                                    1 /* WNOHANG */, 0);
        if (okl::interrupted(r)) continue;
        if (okl::failed(r)) return okl::translate(r);

        if (r != 0) {
            const int signalled = st & 0x7f;
            if (signalled == 0) {
                if (status) *status = (st >> 8) & 0xff;
                if (terminated) *terminated = 0;
            } else {
                if (status) *status = signalled;
                if (terminated) *terminated = 1;
            }
            return kal_ok;
        }

        if (ns != 0 && waited >= ns) return kal_err_again;

        okl::ktimespec ts{ 0, static_cast<okl_i64>(interval_ns) };
        okl::sys(okl::nr_nanosleep, reinterpret_cast<okl_long>(&ts), 0);
        waited += interval_ns;
    }
}

// The kernel states a bound in nanoseconds and the clock advances at the
// scheduler's resolution, so a bound finer than a tick is rounded up by the
// scheduler rather than refused. One millisecond is reported because it is the
// interval the child-waiting loop above polls at, and a caller asking for less
// than the coarsest of the operations here would otherwise be told a number one
// of them cannot meet.
const kal_uintptr kal_timeout_granularity_ns = 1000000u;

}  // extern "C"
