// The five interfaces version 0.8 added, examined through the module form.
//
// THIS IS THE C++ HALF OF CLAUSE 4.3. The specification distributes its
// declarations in two forms and requires the two to declare the same entities.
// tools/check-declarations.sh in the specification repository examines the C
// form against SURFACE.txt; the C++ form is examined here, where a build of the
// modules already exists. Neither form is therefore the other's source.
//
// The observations are of behaviour and not only of existence. A test that named
// each entity and did nothing with it would compile against an implementation
// whose every operation returned an error, and would report that as conformance.
#include <cstdio>
#include <cstring>
import openkal.types;
import openkal.stream;
import openkal.terminal;
import openkal.net;
import openkal.datagram;
import openkal.space;
import openkal.timeout;
import openkal.process;
import openkal.abort;

namespace {

int failures = 0;

void check(bool held, const char* what) {
    if (!held) { std::printf("FAIL: %s\n", what); ++failures; }
}

kal_endpoint loopback(kal_u32 port) {
    kal_endpoint ep{};
    ep.addr[0] = 127; ep.addr[3] = 1;
    ep.addr_len = 4;
    ep.port = port;
    return ep;
}

// openkal.terminal
//
// A run under a pipe has no terminal, which is the ordinary case here. What can
// be observed without one is the refusal, and the refusal is the half of the
// contract that says the interface reports rather than acts upon a stream it
// does not apply to.
void terminal_section() {
    const auto out = kal_stdout();
    // The property is named through the module rather than through the macro:
    // a macro does not cross a module boundary, and kal::stream is where the
    // module form states it.
    const bool interactive =
        kal::stream_props{kal_stream_props(out)}.has(kal::stream_prop::interactive);

    const auto m = kal::terminal::get_mode(out);
    if (interactive) {
        check(m.e == kal_ok, "an interactive stream reports its mode");
        check(kal::terminal::set_mode(out, m.m) == kal_ok,
              "the mode that was read can be set back");
    } else {
        check(m.e == kal_err_not_supported,
              "a stream that is not interactive refuses get_mode");
    }

    // The size outputs survive a refusal. They are pre-set to values the
    // operation would not produce, so a backend that wrote them before failing
    // would be visible here rather than in a caller's arithmetic.
    kal_uintptr cols = 0xDEAD, rows = 0xBEEF;
    const int rc = kal_terminal_size(out, &cols, &rows);
    if (rc != kal_ok)
        check(cols == 0xDEAD && rows == 0xBEEF,
              "a refused size leaves both outputs untouched");
}

// openkal.net
void net_section() {
    const auto want = loopback(0);
    const auto l = kal::net::listen(want);
    check(l.e == kal_ok, "a listener opens on the loopback address");
    if (l.e != kal_ok) return;

    const auto bound = kal::net::local(l.l);
    check(bound.e == kal_ok && bound.ep.port != 0,
          "a listener opened on port zero reports the port it was given");
    if (bound.e != kal_ok || bound.ep.port == 0) { kal::net::close(l.l); return; }

    const auto c = kal::net::connect(loopback(bound.ep.port));
    check(c.e == kal_ok, "a connection to the listener is established");
    if (c.e != kal_ok) { kal::net::close(l.l); return; }

    const auto a = kal::net::accept(l.l);
    check(a.e == kal_ok, "the listener accepts the connection");
    if (a.e != kal_ok) { kal::net::close(c.c); kal::net::close(l.l); return; }

    // The streams the two connections own. Borrowed, and released with the
    // connection rather than separately.
    const auto cs = kal::net::stream(c.c);
    const auto ss = kal::net::stream(a.c);

    // A connection is a stream, and its bytes move through the stream
    // operations. That this interface adds no transfer operation of its own is
    // the property being observed.
    const char msg[] = "openkal";
    const kal_intptr w = kal_stream_write(cs, msg, sizeof msg - 1);
    check(w == static_cast<kal_intptr>(sizeof msg - 1),
          "a connection carries bytes through the stream operations");

    char buf[16] = {};
    const kal_intptr r = kal_stream_read(ss, buf, sizeof buf);
    check(r == static_cast<kal_intptr>(sizeof msg - 1) &&
              std::memcmp(buf, msg, sizeof msg - 1) == 0,
          "the bytes read are the bytes written");

    if (kal::net::has(kal::net::halfclose)) {
        check(kal::net::shutdown(c.c, kal::net::shut::write) == kal_ok,
              "a claimed half-closure is performed");
        char eof[4] = {};
        const kal_intptr e = kal_stream_read(ss, eof, sizeof eof);
        check(e == 0, "the peer observes end of input after a half-closure");
    }

    // An endpoint whose length this implementation does not know is refused
    // rather than read as one it does.
    kal_endpoint odd{};
    odd.addr_len = 7;
    const auto bad = kal::net::connect(odd);
    check(bad.e == kal_err_invalid,
          "an endpoint of unknown length is refused, not misread");

    kal::net::close(a.c);
    kal::net::close(c.c);
    kal::net::close(l.l);
}

// openkal.datagram
void datagram_section() {
    const auto rx = kal::datagram::open(loopback(0));
    check(rx.e == kal_ok, "a datagram endpoint opens on the loopback address");
    if (rx.e != kal_ok) return;

    const auto bound = kal::datagram::local(rx.d);
    check(bound.e == kal_ok && bound.ep.port != 0,
          "an endpoint opened on port zero reports the port it was given");
    if (bound.e != kal_ok || bound.ep.port == 0) { kal::datagram::close(rx.d); return; }

    const auto tx = kal::datagram::open();
    check(tx.e == kal_ok, "an endpoint that only sends opens without an address");
    if (tx.e != kal_ok) { kal::datagram::close(rx.d); return; }

    const char msg[] = "openkal";
    const auto w = kal::datagram::send_to(tx.d, msg, sizeof msg - 1,
                                          loopback(bound.ep.port));
    check(w == static_cast<kal_intptr>(sizeof msg - 1),
          "a message is sent whole and the count is the length given");

    char buf[16] = {};
    const auto got = kal::datagram::recv_from(rx.d, buf, sizeof buf);
    check(got.n == static_cast<kal_intptr>(sizeof msg - 1) &&
              std::memcmp(buf, msg, sizeof msg - 1) == 0,
          "the message received is the message sent");
    check(got.from.addr_len == 4, "the sender of a received message is reported");

    kal::datagram::close(tx.d);
    kal::datagram::close(rx.d);
}

// openkal.space
//
// The exit status is the only channel a separate address space has, so the entry
// reports through it and the caller's own memory is checked to be unchanged.
int marker = 0;

void child_entry(void* arg) {
    marker = 1;                       // in the copy, not in the caller
    kal_exit(arg == nullptr ? 3 : 7);
}

void space_section() {
    int local = 0;
    const auto p = kal::space::start(&child_entry, static_cast<void*>(&local),
                                     nullptr);
    check(p.e == kal_ok, "a context starts in a copy of the space");
    if (p.e != kal_ok) return;

    int status = 0, terminated = 0;
    check(kal_process_wait(p.p, &status, &terminated) == kal_ok,
          "the started context is waited for as a process");
    check(terminated == 0, "the started context ended of its own accord");
    check(status == 7, "the entry received the argument it was given");
    check(marker == 0, "a store in the copied space is not observed in the original");
    kal_process_close(p.p);
}

// A BOUNDED TRANSFER WAITS UPON THE STREAM IT THEN TRANSFERS UPON.
//
// This is the observation that was missing while the two operations it examines
// waited upon the descriptor below the one they transferred upon. Nothing else
// in this file reached them: the two observations in timeout_section below are
// of an accept, whose handle is owned and was decoded correctly, and of a
// zero-length write, which returns before it waits.
//
// WHY IT MAKES SIXTEEN CHANNELS AND NOT ONE. Under the defect the wait was
// performed upon descriptor N-1, and whether that expires depends on what
// occupies N-1. For a channel made after another, N-1 is the previous channel's
// writing end, upon which input is never reported, so the read expires while
// bytes sit in the stream that was asked for. One channel would be answering
// about whichever descriptor happened to precede it; sixteen make the answer a
// property of the implementation rather than of the process that ran it.
//
// AND WHY IT RUNS FIRST. The mistaken decode succeeded only while the
// generation recorded for N-1 was still zero, so it corrected itself for every
// index at which an owned handle had already been released. Placed after the
// sections that create and release listeners and datagrams, this would have
// examined reused descriptors and held upon the defect.
//
// The count is reported rather than the first failure. Sixteen distinguishes a
// correct implementation from one that answers about a neighbouring descriptor;
// the first failure alone would not say which.
void timeout_stream_section() {
    constexpr int n = 16;
    kal_stream mine[n]{}, theirs[n]{};
    int made = 0;

    for (; made < n; ++made) {
        if (kal_process_channel(&mine[made], &theirs[made]) != kal_ok) break;
        const char byte = 'x';
        if (kal_stream_write(theirs[made], &byte, 1) != 1) break;
    }
    check(made == n, "sixteen channels are created and each is written to");

    int transferred = 0;
    for (int i = 0; i < made; ++i) {
        char buf[1] = {};
        // One millisecond. Every one of these streams has a byte waiting, so a
        // correct implementation does not reach the bound at all; the bound is
        // present so that a wait upon the wrong descriptor ends the run rather
        // than hanging it.
        if (kal::timeout::read(mine[i], buf, 1, 1000000) == 1 && buf[0] == 'x')
            ++transferred;
    }
    if (transferred != made)
        std::printf("  %d of %d bounded reads transferred\n", transferred, made);
    check(transferred == made,
          "a bounded read of a stream that has bytes waiting transfers them");

    // The mirror. A stream with nothing in it must expire, and must expire
    // rather than transfer a byte belonging to another stream.
    kal_stream empty_mine{}, empty_theirs{};
    if (kal_process_channel(&empty_mine, &empty_theirs) == kal_ok) {
        char buf[1] = {};
        check(kal::timeout::read(empty_mine, buf, 1, 1000000) == -kal_err_again,
              "a bounded read of a stream with nothing waiting expires");
        kal_process_channel_close(empty_theirs);
        kal_process_channel_close(empty_mine);
    }

    for (int i = 0; i < made; ++i) {
        kal_process_channel_close(theirs[i]);
        kal_process_channel_close(mine[i]);
    }
}

// openkal.timeout
void timeout_section() {
    check(kal_timeout_granularity() > 0,
          "the granularity is a positive number of nanoseconds");

    // A bounded read of a listener that nobody connects to must expire rather
    // than wait. A listener is used rather than the standard input because the
    // latter may be a file, which is always ready.
    const auto l = kal::net::listen(loopback(0));
    if (l.e == kal_ok) {
        const auto a = kal::timeout::accept(l.l, 1000000 /* one millisecond */);
        check(a.e == kal_err_again,
              "an accept that nobody answers expires as kal_err_again");
        kal::net::close(l.l);
    }

    // A transfer of zero bytes does not wait and is not bounded.
    const kal_intptr w = kal::timeout::write(kal_stdout(), "", 0, 1);
    check(w >= 0, "a bounded transfer of zero bytes succeeds");
}

// The three operations openkal 0.8 adds to openkal.process.
//
// ADDING TO AN EXISTING INTERFACE OBLIGES EVERY IMPLEMENTATION OF IT, which is
// not true of adding a new interface: clause 6.1 makes a new one optional and
// clause 6.1 makes an incomplete one a deviation. The surface checker caught the
// omission here before anything else did, and these observations are what says
// the names do something rather than merely existing.
void process_additions_section() {
    // A channel carries bytes from one end to the other. Both ends are owned and
    // both are released through kal_process_channel_close.
    kal_stream mine{}, theirs{};
    const int rc = kal_process_channel(&mine, &theirs);
    check(rc == kal_ok, "a channel is created");
    if (rc != kal_ok) return;

    const char msg[] = "through the channel";
    const kal_intptr w = kal_stream_write(theirs, msg, sizeof msg - 1);
    check(w == static_cast<kal_intptr>(sizeof msg - 1),
          "the far end of a channel accepts bytes");

    char buf[64] = {};
    const kal_intptr r = kal_stream_read(mine, buf, sizeof buf);
    check(r == static_cast<kal_intptr>(sizeof msg - 1) &&
              std::memcmp(buf, msg, sizeof msg - 1) == 0,
          "the near end reads what the far end wrote");

    // THE END OF INPUT IS WHAT THE RELEASE IS FOR. A parent that does not close
    // the far end after a spawn never observes it, which is the deadlock this
    // pair invites and the reason the release is declared beside the operation.
    kal_process_channel_close(theirs);
    const kal_intptr eof = kal_stream_read(mine, buf, sizeof buf);
    check(eof == 0,
          "closing the far end is observed as end of input on the near one");
    kal_process_channel_close(mine);

    // The property word claims both additions, so both must be answered. Named
    // through the module, because a macro does not cross a module boundary.
    check(kal::process::has(kal::process::channel),
          "the property word claims the channel it just provided");
    check(kal::process::has(kal::process::grant_dir),
          "the property word claims the directory grant");
}

}  // namespace

int main() {
    // FIRST, AND THE ORDER IS LOAD-BEARING. The comment above this section
    // records why: it examines a defect that corrects itself for any descriptor
    // index at which an owned handle has already been released, so running it
    // after the sections that make and release listeners would hold upon the
    // defect it exists to detect.
    timeout_stream_section();
    process_additions_section();
    terminal_section();
    net_section();
    datagram_section();
    space_section();
    timeout_section();

    if (failures == 0) std::printf("openkal 0.8 interfaces: every observation held\n");
    return failures == 0 ? 0 : 1;
}
