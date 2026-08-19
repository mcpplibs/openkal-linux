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

kal_uintptr kal_env_arg_count(void) { return static_cast<kal_uintptr>(okl::g_argc); }

const char* kal_env_arg(kal_uintptr index, kal_uintptr* len) {
    if (index >= static_cast<kal_uintptr>(okl::g_argc)) { if (len) *len = 0; return nullptr; }
    const char* s = okl::g_argv[index];
    if (len) *len = okl::length(s);
    return s;
}

const char* kal_env_var(const char* name, kal_uintptr name_len, kal_uintptr* value_len) {
    for (char** e = okl::g_envp; e && *e; ++e) {
        const char* entry = *e;
        kal_uintptr i = 0;
        while (i < name_len && entry[i] != '\0' && entry[i] == name[i]) ++i;
        if (i == name_len && entry[i] == '=') {
            const char* v = entry + name_len + 1;
            if (value_len) *value_len = okl::length(v);
            return v;
        }
    }
    if (value_len) *value_len = 0;
    return nullptr;
}

kal_uintptr kal_env_var_count(void) {
    kal_uintptr n = 0; for (char** e = okl::g_envp; e && *e; ++e) ++n; return n;
}

const char* kal_env_var_at(kal_uintptr index, kal_uintptr* name_len,
                           const char** value, kal_uintptr* value_len) {
    kal_uintptr n = 0;
    for (char** e = okl::g_envp; e && *e; ++e, ++n) {
        if (n != index) continue;
        const char* entry = *e;
        kal_uintptr i = 0; while (entry[i] != '\0' && entry[i] != '=') ++i;
        if (name_len) *name_len = i;
        const char* v = entry[i] == '=' ? entry + i + 1 : entry + i;
        if (value)     *value = v;
        if (value_len) *value_len = okl::length(v);
        return entry;
    }
    return nullptr;
}

}
