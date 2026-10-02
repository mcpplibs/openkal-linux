// Thread-local storage for a program that has no C library to establish it.
//
// openkal reports, through kal_task_props, whether a context started by
// kal_task_start observes the thread-local storage of the toolchain that
// compiled the program. This implementation reports that it does, and this
// file is what makes the report true when the program is linked without a C
// library: the register that names the current context's storage is set by
// whoever creates the context, and in that configuration that is this
// implementation.
//
// The layout is the processor's, not the specification's. It is described by
// the psABI of each architecture and is reproduced here rather than obtained
// from a C library, for the reason src/sys.h gives.
#pragma once
#include "sys.h"

namespace okl {

// The vectors the kernel placed at inception. `src/env.cpp` records them ---
// from the stack when this implementation receives control, and from the
// arguments a C library gives an initialiser when one receives control instead
// --- and they are the only source of the program's own headers here.
okl_ulong auxval(okl_ulong key);

// The program's own thread-local segment, as the loader described it.
struct tls_image {
    const unsigned char* data;   // the initialised part
    okl_uptr filesz;
    okl_uptr memsz;
    okl_uptr align;
    bool     known;
};

inline tls_image& image() { static tls_image i{}; return i; }

inline okl_uptr round_up(okl_uptr n, okl_uptr to) { return (n + to - 1) & ~(to - 1); }

// Reads the program's own headers, which the kernel reports at inception. The
// loader has already applied any bias, so the addresses are the ones the
// program will use.
inline void describe_tls(okl_ulong phdr, okl_ulong phent, okl_ulong phnum) {
    struct elf_phdr {
        okl_u32 type, flags;
        okl_u64 offset, vaddr, paddr, filesz, memsz, align;
    };
    constexpr okl_u32 pt_tls = 7;
    auto& im = image();
    im.known = true;
    if (phdr == 0 || phnum == 0) return;
    for (okl_ulong i = 0; i < phnum; ++i) {
        const auto* p = reinterpret_cast<const elf_phdr*>(phdr + i * phent);
        if (p->type != pt_tls) continue;
        im.data   = reinterpret_cast<const unsigned char*>(p->vaddr);
        im.filesz = static_cast<okl_uptr>(p->filesz);
        im.memsz  = static_cast<okl_uptr>(p->memsz);
        // THE ALIGNMENT IS KEPT AS THE LOADER STATED IT, AND THE CLAMP THAT
        // USED TO BE HERE WAS HALF OF A DEFECT.
        //
        // Every thread-local address is `tp + st_value - tls_size', where
        // `tls_size' is the block the LINKER laid out --- `round_up(p_memsz,
        // p_align)' --- and a region of any other size puts the image it holds
        // at the wrong offset within it. Clamping the alignment up to sixteen
        // HERE lost the number the size has to be computed from, and the image
        // then sat below the variables that name it: measured 2026-10-03 on a
        // segment stating `p_align = 8, p_memsz = 56', whose variables the
        // linker put at `tp - 56' while the region was built 64 bytes deep.
        //
        // Two four-byte thread-local variables, each initialised with a
        // different value, read each other's bytes; and openkal-llvm-runtime's
        // own probe reported it as a `thread_local' whose constructor never
        // ran, because its guard byte had moved onto a non-zero neighbour.
        //
        // The clamp belongs to `make_tls', which applies it to the ALLOCATION
        // and not to the size.
        im.align  = static_cast<okl_uptr>(p->align);
        return;
    }
}

// How large a region one context's storage occupies, and where within it the
// thread pointer goes. Two conventions exist and both are represented, because
// the two architectures this implementation supports use one each.
struct tls_block { void* base; okl_uptr bytes; okl_uptr align; void* tp; };

// DESCRIBES THE PROGRAM'S OWN IMAGE IF NOTHING HAS, AND THE LAZINESS IS THE FIX
// FOR A DEFECT THIS FILE HAD.
//
// `describe_tls' is called by `src/start.cpp', which is the entry point of a
// program that carries no runtime. A program that DOES carry one is started by
// that runtime instead, nothing described the image, `memsz' stayed zero, and a
// context's storage was therefore allocated with the thread pointer at its
// START instead of at its end. Every thread-local variable of a started context
// was then addressed BELOW that allocation, and it did not fault: what lies
// below an allocation is usually mapped, so a program's own thread-local
// variables were written into whatever the allocator kept beside it. It took a
// large enough variable to make the defect visible --- measured 2026-10-03, a
// 24-byte one ended the kit's test program at the first instruction of a
// context, where four bytes of it had been silently misplaced before.
//
// AND NOTHING WAS MISSING IN THAT ARRANGEMENT. The vectors are recorded in both
// of them --- from the stack at entry, or from the initialiser a C library calls
// --- and every arrangement reaches `make_tls' long after either. What was
// missing was that anyone asked.
inline void describe_self() {
    if (image().known) return;
    describe_tls(auxval(3 /* AT_PHDR */), auxval(4 /* AT_PHENT */),
                 auxval(5 /* AT_PHNUM */));
}

inline tls_block make_tls(void* (*alloc)(okl_uptr, okl_uptr)) {
    describe_self();
    const auto& im = image();
    // TWO ALIGNMENTS, BECAUSE TWO DIFFERENT QUESTIONS ARE ASKED OF ONE NUMBER,
    // AND ASKING BOTH OF ONE NUMBER WAS A DEFECT.
    //
    // The SIZE is the linker's: `round_up(p_memsz, p_align)', the block every
    // thread-local offset was measured backwards from, so that the image copied
    // to the start of the region is where the variables' addresses say it is.
    // The ALLOCATION is asked for at least sixteen bytes of alignment, because
    // the region is handed to code the compiler emitted and a smaller alignment
    // is a promise the allocator was never asked for.
    //
    // They were one number, clamped up to sixteen. On a segment stating
    // `p_align = 8, p_memsz = 56' --- measured 2026-10-03 with two initialised
    // thread-local variables --- the linker laid the variables out at `tp - 56'
    // and the clamp built a region 64 bytes deep, so the image sat eight bytes
    // below them and each variable read the other's bytes. openkal-linux 0.16.0
    // shipped that: the specification package's own kit test passed, because a
    // four-byte variable of this implementation's own was all the storage there
    // was, and it took this round's twenty-four to move anything into the way.
    const okl_uptr laid_out = im.align ? im.align : 1;
    const okl_uptr align = laid_out < 16 ? 16 : laid_out;
    tls_block b{};
    b.align = align;

    // A CONTEXT WHOSE STORAGE CANNOT BE LAID OUT IS REFUSED RATHER THAN GIVEN A
    // WRONG ONE. The image is not described only where the program's own
    // headers could not be reached at all, and a context established with a
    // block that does not fit the segment would corrupt memory in a way the
    // program cannot see. The caller of `kal_task_start' is told instead.
    if (im.memsz == 0) return b;

#if defined(__x86_64__)
    // Variant II: the storage lies below the thread pointer, and the word the
    // thread pointer addresses holds the thread pointer itself, which is how a
    // program obtains it without an instruction that reads the register.
    const okl_uptr size = round_up(im.memsz, laid_out);
    b.bytes = size + 64;
    b.base  = alloc(b.bytes, align);
    if (!b.base) return b;
    auto* base = static_cast<unsigned char*>(b.base);
    fill(base, 0, b.bytes);
    // The linker measured every offset backwards from the thread pointer, so
    // the initialised image sits at the start of the region and the thread
    // pointer at its end.
    if (im.data && im.filesz) copy(base, im.data, im.filesz);
    b.tp = base + size;
    // The word the thread pointer addresses holds the thread pointer itself.
    // A program obtains it with one load and no instruction that reads the
    // segment register, which is why the convention exists.
    *reinterpret_cast<void**>(b.tp) = b.tp;
#elif defined(__aarch64__)
    // Variant I: the storage lies above the thread pointer, after a gap of two
    // words reserved by the procedure call standard.
    const okl_uptr gap = round_up(16, align);
    b.bytes = gap + round_up(im.memsz, laid_out) + 64;
    b.base  = alloc(b.bytes, align);
    if (!b.base) return b;
    auto* base = static_cast<unsigned char*>(b.base);
    fill(base, 0, b.bytes);
    if (im.data && im.filesz) copy(base + gap, im.data, im.filesz);
    b.tp = base;
#endif
    return b;
}

inline void set_thread_pointer(void* tp) {
#if defined(__x86_64__)
    sys(nr_arch_prctl, 0x1002 /* ARCH_SET_FS */, reinterpret_cast<okl_long>(tp));
#elif defined(__aarch64__)
    __asm__ __volatile__("msr tpidr_el0, %0" :: "r"(tp));
#endif
}

}  // namespace okl
