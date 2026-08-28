#include "sys.h"
#include <openkal/env.h>

namespace okl {

// The vectors the kernel places on the stack at inception. They are recorded
// by whichever of the two entrances the program has: this implementation's own
// program entry, when the program is linked without a C library, and otherwise
// an initialiser, which every environment that starts a hosted program calls
// with the same three arguments.
//
// Reading the pseudo-file the kernel provides was the alternative. It requires
// a file system before the program has one, and reports the arguments as they
// were at inception rather than as the program received them.
int    g_argc = 0;
char** g_argv = nullptr;
char** g_envp = nullptr;
okl_ulong* g_auxv = nullptr;

void record(int argc, char** argv, char** envp) {
    g_argc = argc; g_argv = argv; g_envp = envp;
    if (envp) {
        char** e = envp;
        while (*e) ++e;
        g_auxv = reinterpret_cast<okl_ulong*>(e + 1);
    }
}

okl_ulong auxval(okl_ulong key) {
    for (okl_ulong* a = g_auxv; a && a[0]; a += 2) if (a[0] == key) return a[1];
    return 0;
}

}  // namespace okl

namespace {
[[gnu::constructor(101)]] void capture(int argc, char** argv, char** envp) {
    if (okl::g_argv == nullptr) okl::record(argc, argv, envp);
}
}  // namespace

extern "C" {

// EVERY VALUE IS COPIED INTO THE CALLER'S BUFFER. These answered with a pointer
// into this implementation's own storage, which is meaningful only while the
// implementation shares the caller's address space --- so the interface said
// something different depending on how it was reached, which is the one thing a
// contract must not do (clause 4.4).
//
// Each returns the length the value HAS, so a caller with a large enough buffer
// is done in one call, a caller that wants to size first passes a capacity of
// zero, and a caller whose buffer was too small learns it by comparing.
namespace {
kal_intptr give(const char* v, kal_uintptr n, char* out, kal_uintptr cap) {
    if (out != nullptr && cap != 0) okl::copy(out, v, n < cap ? n : cap);
    return static_cast<kal_intptr>(n);
}
}  // namespace

kal_uintptr kal_env_arg_count(void) { return static_cast<kal_uintptr>(okl::g_argc); }

kal_intptr kal_env_arg(kal_uintptr index, char* out, kal_uintptr cap) {
    if (index >= static_cast<kal_uintptr>(okl::g_argc)) return -kal_err_not_found;
    const char* s = okl::g_argv[index];
    return give(s, okl::length(s), out, cap);
}

kal_intptr kal_env_var(const char* name, kal_uintptr name_len,
                       char* out, kal_uintptr cap) {
    if (name == nullptr) return -kal_err_invalid;
    for (char** e = okl::g_envp; e && *e; ++e) {
        const char* entry = *e;
        kal_uintptr i = 0;
        while (i < name_len && entry[i] != '\0' && entry[i] == name[i]) ++i;
        if (i == name_len && entry[i] == '=') {
            const char* v = entry + name_len + 1;
            return give(v, okl::length(v), out, cap);
        }
    }
    // A name that is not there is distinct from one whose value is empty, and
    // reporting a length of zero for both would lose that.
    return -kal_err_not_found;
}

kal_uintptr kal_env_var_count(void) {
    kal_uintptr n = 0; for (char** e = okl::g_envp; e && *e; ++e) ++n; return n;
}

// The NAME at a position. The value is then obtained by kal_env_var: an
// operation answering both needs two buffers, two capacities and two lengths,
// and its second half is kal_env_var written again. The set does not change
// while the program runs, so an index may be held across the two calls.
kal_intptr kal_env_var_at(kal_uintptr index, char* out, kal_uintptr cap) {
    kal_uintptr n = 0;
    for (char** e = okl::g_envp; e && *e; ++e, ++n) {
        if (n != index) continue;
        const char* entry = *e;
        kal_uintptr i = 0; while (entry[i] != '\0' && entry[i] != '=') ++i;
        return give(entry, i, out, cap);
    }
    return -kal_err_not_found;
}

}
