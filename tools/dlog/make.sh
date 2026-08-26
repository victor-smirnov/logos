#!/usr/bin/env bash
# make.sh — build tools/dlog/lir_facts against the system LLVM 20.
#
# ⚠ NAMED make.sh, NOT build.sh: .gitignore line 13 is a bare `build*`, which
# matches at every depth, so tools/dlog/build.sh was invisible to `git add` and
# would have been committed as a tool that cannot be built. `git status` did not
# list it; `git check-ignore -v` did.
#
# ⚠ DELIBERATELY OUTSIDE THE PROJECT'S CMake. lir_facts links clang, which the
# compiler itself does not; putting it in the main build would make every
# contributor's configure depend on libclang-20-dev for a tool that is not a
# gate and runs by hand. Built on demand into build/dlog/ instead.
#
# Links libclang-cpp.so rather than the ~40 static archives: one shared object,
# and the link is seconds instead of a minute.
set -uo pipefail
cd "$(dirname "$0")/../.." || exit 2
LLVM="${LLVM_DIR:-/usr/lib/llvm-20}"
OUT=build/dlog
CFG="$LLVM/bin/llvm-config"

[ -x "$CFG" ] || { echo "make.sh: no llvm-config at $CFG — set LLVM_DIR"; exit 2; }
[ -f "$LLVM/include/clang/Tooling/Tooling.h" ] || {
    echo "make.sh: clang tooling headers missing — install libclang-20-dev"; exit 2; }

# ⚠ -lclang-cpp DOES NOT RESOLVE HERE: the packaging ships libclang-cpp.so.20.1
# with no unversioned .so symlink, so `-l` finds nothing. Link the file itself,
# and pick it by GLOB rather than by a version I typed — the same discipline the
# rules enforce on the compiler.
SO=$(ls -1 "$LLVM/lib"/libclang-cpp.so* 2>/dev/null | head -1)
[ -n "$SO" ] || { echo "make.sh: no libclang-cpp.so* under $LLVM/lib"; exit 2; }

mkdir -p "$OUT"
set -x
"$LLVM/bin/clang++" -std=c++20 -fno-rtti -O1 \
    $("$CFG" --cxxflags | sed 's/-fno-exceptions//') \
    tools/dlog/lir_facts.cpp \
    -o "$OUT/lir_facts" \
    "$SO" $("$CFG" --ldflags --libs support core --system-libs) \
    -Wl,-rpath,"$LLVM/lib"
