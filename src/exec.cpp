#include "sys.h"
#include <openkal/exec.h>

// openkal.exec on this kernel.
//
// A mapping obtained writable and made executable afterwards. The kernel places
// no condition upon either step, so this implementation provides the interface
// unconditionally --- unlike one of the other systems, where the same interface
// is available only to a program produced in a particular way, which clause 6.5
// covers and which is not this system's case.
//
// The reservation is not made executable at the outset even though this kernel
// would permit it. Two of the three environments the specification targets
// refuse a mapping that is both, so an implementation that returned one here
// would be offering a program a shape it could not use elsewhere --- and the
// program would discover that only on the other system. The interface states
// the narrower contract and this implementation keeps to it.

namespace {

constexpr okl_uptr kPage = 4096;

okl_uptr round_up(okl_uptr n, okl_uptr to) { return (n + to - 1) & ~(to - 1); }

}  // namespace

extern "C" {

void* kal_exec_alloc(kal_uintptr size) {
    if (size == 0) return nullptr;
    const okl_uptr bytes = round_up(static_cast<okl_uptr>(size), kPage);
    const okl_long r = okl::sys(okl::nr_mmap, 0, static_cast<okl_long>(bytes),
                                okl::prot_read | okl::prot_write,
                                okl::map_private | okl::map_anonymous, -1, 0);
    if (okl::failed(r)) return nullptr;
    return reinterpret_cast<void*>(r);
}

int kal_exec_publish(void* p, kal_uintptr size) {
    if (p == nullptr || size == 0) return kal_err_invalid;
    const okl_uptr bytes = round_up(static_cast<okl_uptr>(size), kPage);
    const okl_long r = okl::sys(okl::nr_mprotect, reinterpret_cast<okl_long>(p),
                                static_cast<okl_long>(bytes),
                                okl::prot_read | okl::prot_exec);
    if (okl::failed(r)) return okl::translate(r);
    return kal_ok;
}

void kal_exec_free(void* p, kal_uintptr size) {
    if (p == nullptr || size == 0) return;
    const okl_uptr bytes = round_up(static_cast<okl_uptr>(size), kPage);
    okl::sys(okl::nr_munmap, reinterpret_cast<okl_long>(p),
             static_cast<okl_long>(bytes));
}

// A published region may be reserved for writing again: this kernel's
// protection call is not one-way. The position is set accordingly, and a
// caller that must change published bytes need not abandon the region.
const kal_uintptr kal_exec_props = KAL_EXEC_PROP_REPUBLISH;

}  // extern "C"
