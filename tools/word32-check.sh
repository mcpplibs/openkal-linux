#!/usr/bin/env bash
# Builds this implementation standalone for i386 and runs tests/word32/runs.cpp on the machine that
# built it (an x86_64 kernel runs a static i386 program). Clause 5.5 of the specification: a
# thirty-two bit word, sixty-four bit positions and times. The full conformance suite runs for
# i686 where the C library and the C++ runtime of this family are built for it (openkal-platform).
#
#   tools/word32-check.sh [LLVM_BIN]     # default: the llvm payload mcpp installed
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
spec="${OPENKAL_INCLUDE:-}"
llvm="${1:-$(ls -d "${MCPP_HOME:-$HOME/.mcpp}"/registry/data/xpkgs/xim-x-llvm/*/bin 2>/dev/null | sort -V | tail -1)}"
[ -x "$llvm/clang++" ] || { echo "no clang++ in '$llvm'" >&2; exit 2; }
if [ -z "$spec" ]; then
    spec="$(ls -d "${MCPP_HOME:-$HOME/.mcpp}"/xpkgs/mcpplibs-x-openkal/*/openkal-*/include 2>/dev/null | sort -V | tail -1)"
fi
[ -d "$spec/openkal" ] || { echo "no openkal include directory (set OPENKAL_INCLUDE)" >&2; exit 2; }
out="$(mktemp -d)"
trap 'rm -rf "$out"' EXIT
flags=(--target=i686-linux-musl -march=i686 -std=c++23 -O1 -ffreestanding -fno-exceptions -fno-rtti
       -nostdlibinc -fPIC -DOKL_STANDALONE -D__OPENKAL__ -I"$spec" -Werror)
for f in "$here"/src/*.cpp "$here"/tests/word32/runs.cpp; do
    "$llvm/clang++" "${flags[@]}" -c "$f" -o "$out/$(basename "$f" .cpp).o"
done
"$llvm/ld.lld" -m elf_i386 -static -e _start -o "$out/runs" "$out"/*.o
( cd "$out" && ./runs )
