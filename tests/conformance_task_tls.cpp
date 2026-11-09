// Conformance: the thread-local storage of a started context.
//
// WHAT IS EXAMINED IS THE IMAGE, NOT ONLY THE ISOLATION. A context given a
// storage region of the wrong size, or one whose thread pointer is at the wrong
// end of it, still gives each context a *distinct* instance --- so a test that
// only wrote a variable and read it back would pass while the values a program
// initialised were somewhere else entirely. The observations here are therefore
// of the INITIAL VALUES a thread-local variable was declared with, alongside the
// isolation and the writes a program above relies on.
//
// THE HOSTED ARRANGEMENT IS WHERE THIS WAS WRONG, AND IT IS THE ONE THIS TEST
// RUNS IN. A program this implementation starts describes its own image at the
// entry point it supplies; a program that carries a runtime instead is started
// by that runtime, and nothing described the image. A context's storage was
// then laid out from a segment of size zero, its thread pointer was at the
// START of the region rather than at its end, and every thread-local variable
// of that context was addressed below the allocation --- into whatever the
// allocator kept beside it. Measured 2026-10-03: the specification package's
// own kit test died at the first instruction of a context once this
// implementation's own storage grew from four bytes to twenty-four.
#include <cstdio>

import openkal.types;
import openkal.task;
import openkal.stream;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;

namespace {

constexpr int kSize = 4096;

// NON-ZERO, AND THAT IS THE POINT. A thread-local variable declared with a
// value is the only thing that shows where the image was copied to: an instance
// that is merely misplaced still holds zeroes if the memory it landed in did.
thread_local int g_initialised = 0x5a5a;

// A variable this size is the second point: the region a context is given must
// hold the program's whole thread-local segment, and a region laid out for a
// segment of the wrong size cannot hold this one.
thread_local unsigned char g_block[kSize];

constexpr unsigned char kStarter = 0xa5;
constexpr unsigned char kContext = 0x5a;

volatile int g_ran      = 0;
volatile int g_image    = 0;   // the value the variable was declared with
volatile int g_fresh    = 0;   // the block began as the image says it does
volatile int g_kept     = 0;   // the context's writes were still there after
volatile int g_isolated = 0;   // the block's writes did not disturb the value

void examine(void*) {
    g_image = g_initialised == 0x5a5a ? 1 : 0;

    int fresh = 1;
    for (int i = 0; i < kSize; ++i) if (g_block[i] != 0) { fresh = 0; break; }
    g_fresh = fresh;

    for (int i = 0; i < kSize; ++i) g_block[i] = kContext;
    int held = 1;
    for (int i = 0; i < kSize; ++i) if (g_block[i] != kContext) { held = 0; break; }
    g_kept = held;

    // AND THE DECLARED VALUE SURVIVES THE BLOCK'S WRITES, which is what a region
    // of the right size and the right layout gives: four kilobytes written
    // beside it are not written over it.
    g_isolated = g_initialised == 0x5a5a ? 1 : 0;
    g_ran = 1;
}

}  // namespace

UTest conformance_task_tls_test{"conformance_task_tls", [] {
    for (int i = 0; i < kSize; ++i) g_block[i] = kStarter;

    kal_task t{};
    if (kal_task_start(examine, nullptr, &t) != kal_ok) { fail("a context was not started"); return; }
    if (kal_task_join(t) != kal_ok) { fail("the context was not awaited"); return; }

    expect(eq(g_ran, 1))      << "the context ran";
    expect(eq(g_image, 1))    << "the declared value was in the image";
    expect(eq(g_fresh, 1))    << "the block began as the image says";
    expect(eq(g_kept, 1))     << "the context's writes survived it";
    expect(eq(g_isolated, 1)) << "the context's writes did not disturb its own image";

    // The starter's instance is untouched, which is the other half of
    // "distinct": one instance per context and not one shared.
    int intact = 1;
    for (int i = 0; i < kSize; ++i) if (g_block[i] != kStarter) { intact = 0; break; }
    expect(eq(intact, 1)) << "the starting context's instance was not written";

    // A second context starts from the image again, which is what makes the
    // storage per context rather than per process.
    g_ran = 0;
    g_image = 0;
    g_fresh = 0;
    g_kept = 0;
    g_isolated = 0;
    kal_task u{};
    expect(kal_task_start(examine, nullptr, &u) == kal_ok && kal_task_join(u) == kal_ok
           && g_image == 1 && g_fresh == 1 && g_kept == 1 && g_isolated == 1)
        << "a second context sees its own instance";
}};
