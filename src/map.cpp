#include "sys.h"
#include "handle.h"
#include <openkal/map.h>

// openkal.map on this kernel (openkal 0.18).
//
// One system call each way: mmap, msync, munmap. The region is the address the
// kernel returned and the length the caller passed, so there is nothing to
// record; a handle would have needed a table from handles to mappings, which
// clause 7.1 excludes.
//
// THE FILE'S LENGTH IS ASKED FIRST. The kernel maps beyond the end of a file
// and lets an access there fault; the specification refuses such a region at
// the call (map.h), so the length is read with fstat and compared. The same
// fstat says whether the file is a regular one: the kernel maps some devices,
// and a device's contents are not a file's.
//
// NO PROT_EXEC. Instructions run only from memory openkal.exec published.

extern "C" {

int kal_map_file(kal_file f, kal_u64 offset, kal_uintptr length, kal_uintptr how, void** out) {
    if (out == nullptr) return kal_err_invalid;
    *out = nullptr;
    const int fd = okl::unpack(f.h);
    if (fd < 0 || length == 0 || offset % KAL_MAP_ALIGNMENT != 0
        || (how & ~(KAL_MAP_WRITE | KAL_MAP_SHARED)) != 0)
        return kal_err_invalid;
    okl::kstat st{};
    const okl_long s = okl::sys(okl::nr_fstat, fd, reinterpret_cast<okl_long>(&st));
    if (okl::failed(s)) return okl::translate(s);
    if ((st.mode & okl::s_ifmt) != okl::s_ifreg) return kal_err_not_supported;
    const kal_u64 size = st.size > 0 ? static_cast<kal_u64>(st.size) : 0;
    if (offset > size || static_cast<kal_u64>(length) > size - offset) return kal_err_invalid;
    const okl_long prot  = okl::prot_read | ((how & KAL_MAP_WRITE) ? okl::prot_write : 0);
    const okl_long flags = (how & KAL_MAP_SHARED) ? okl::map_shared : okl::map_private;
    const okl_long r = okl::sys(okl::nr_mmap, 0, static_cast<okl_long>(length), prot, flags, fd,
                                static_cast<okl_long>(offset));
    if (okl::failed(r)) {
        // ENODEV: the file system does not map. The resource, not the
        // implementation, refuses (map.h).
        if (-r == okl::e_nodev) return kal_err_not_supported;
        return okl::translate(r);
    }
    *out = reinterpret_cast<void*>(r);
    return kal_ok;
}

int kal_map_sync(void* region, kal_uintptr length) {
    if (region == nullptr || length == 0) return kal_err_invalid;
    // MS_ASYNC: a shared mapping is the page cache, which a read of the file
    // already reads; what the specification asks of this call is that
    // visibility, not durability, and asking for durability here would make
    // every caller wait for the medium.
    const okl_long r = okl::sys(okl::nr_msync, reinterpret_cast<okl_long>(region),
                                static_cast<okl_long>(length), okl::ms_async);
    return okl::failed(r) ? okl::translate(r) : kal_ok;
}

void kal_map_release(void* region, kal_uintptr length) {
    if (region == nullptr || length == 0) return;
    (void)okl::sys(okl::nr_munmap, reinterpret_cast<okl_long>(region), static_cast<okl_long>(length));
}

}
