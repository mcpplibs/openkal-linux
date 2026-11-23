#!/usr/bin/env bash
# Builds this implementation standalone for an architecture the building machine does not run
# natively, and runs tests/arch/runs.cpp: i386 on the machine that built it (an x86_64 kernel runs a
# static i386 program; clause 5.5 of the specification, a thirty-two bit word with sixty-four bit
# positions and times), riscv64 under qemu's user-mode translator. The full conformance suite runs
# for these rows where the C library and the C++ runtime of this family are built for them
# (openkal-platform).
#
#   tools/arch-check.sh i386|riscv64 [LLVM_BIN]     # default: the llvm payload mcpp installed
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
arch="${1:?usage: tools/arch-check.sh i386|riscv64 [LLVM_BIN]}"
spec="${OPENKAL_INCLUDE:-}"
llvm="${2:-$(ls -d "${MCPP_HOME:-$HOME/.mcpp}"/registry/data/xpkgs/xim-x-llvm/*/bin 2>/dev/null | sort -V | tail -1)}"
[ -x "$llvm/clang++" ] || { echo "no clang++ in '$llvm'" >&2; exit 2; }
if [ -z "$spec" ]; then
    spec="$(ls -d "${MCPP_HOME:-$HOME/.mcpp}"/xpkgs/mcpplibs-x-openkal/*/openkal-*/include 2>/dev/null | sort -V | tail -1)"
fi
[ -d "$spec/openkal" ] || { echo "no openkal include directory (set OPENKAL_INCLUDE)" >&2; exit 2; }
case "$arch" in
    i386)    target=(--target=i686-linux-musl -march=i686); emulation=elf_i386; run=() ;;
    # lld relaxes no general-dynamic TLS access on riscv64, and a static program has no
    # `__tls_get_addr': the accesses are compiled as the static link resolves them.
    riscv64) target=(--target=riscv64-linux-musl -march=rv64gc -mabi=lp64d -ftls-model=initial-exec)
             emulation=elf64lriscv
             run=("${QEMU_RISCV64:-qemu-riscv64}")
             command -v "${run[0]}" > /dev/null || { echo "no ${run[0]} to run a riscv64 program" >&2; exit 2; } ;;
    *)       echo "unknown architecture '$arch'" >&2; exit 2 ;;
esac
out="$(mktemp -d)"
trap 'rm -rf "$out"' EXIT
flags=("${target[@]}" -std=c++23 -O1 -ffreestanding -fno-exceptions -fno-rtti -nostdlibinc -fPIC
       -fno-omit-frame-pointer -DOKL_STANDALONE -D__OPENKAL__ -I"$spec" -Werror)
for f in "$here"/src/*.cpp "$here"/tests/arch/runs.cpp; do
    "$llvm/clang++" "${flags[@]}" -c "$f" -o "$out/$(basename "$f" .cpp).o"
done
"$llvm/ld.lld" -m "$emulation" -static -e _start -o "$out/runs" "$out"/*.o
( cd "$out" && "${run[@]}" ./runs )
