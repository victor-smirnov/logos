#!/usr/bin/env bash
# EL cross-BACKEND oracle.
#
# Parses one corpus of EL expressions twice — once with the parser peg_gen_cpp
# generates from el.peg, once with the parser peg_gen_logos generates from the
# SAME el.peg — and requires the structural stringification of the two SExpr
# trees to be identical.
#
# This is the check that makes "one dialect, two backends" a fact rather than a
# hope. The sibling oracle (tools/peg_gen_logos/oracle) does the same for
# logos.peg's numeric dialect; tools/peg_gen_logos/wql_oracle does NOT compare
# backends at all (it parses twice with the same Logos parser).
#
#   tools/peg_gen_cpp/oracle/run.sh [<build-dir>]
set -u

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo="$(cd "$here/../../.." && pwd)"
build="${1:-$repo/build}"
logosc="$build/bin/logosc"
peg_cpp="$build/tools/peg_gen_cpp/peg_gen_cpp"
libd="$build/lib/logos"
grammar="$repo/stdlib/std/wql/grammars/el.peg"
corpus="$here/el_corpus.txt"

for f in "$logosc" "$peg_cpp" "$corpus"; do
    [ -e "$f" ] || { echo "missing $f — build first (cmake --build $build)"; exit 1; }
done

cxx="$(sed -n 's/^CMAKE_CXX_COMPILER:[^=]*=//p' "$build/CMakeCache.txt" 2>/dev/null)"
[ -n "$cxx" ] || cxx=clang++
flt() { grep -vE 'version-mismatch|no ABI guarantee|may be unstable'; }

work="$build/peg_gen_cpp_el_oracle"
rm -rf "$work"; mkdir -p "$work"

echo "== generating the C++ EL parser from el.peg =="
"$peg_cpp" "$grammar" --out-dir "$work" >/dev/null 2>&1 \
    || { echo "peg_gen_cpp failed on el.peg"; exit 1; }

echo "== building C++ harness =="
core="$(find "$build" -name 'liblogos_core*.a' | head -1)"
"$cxx" -std=c++23 -fno-rtti -I "$repo/include" -I "$work" \
    "$here/el_stringify.cpp" "$work/el_parser.cpp" \
    -Wl,--start-group \
      "$build/src/writ/liblogos_writ.a" \
      "$build/src/verification/liblogos_verification.a" \
      $core \
    -Wl,--end-group -lpthread \
    -o "$work/cpp_el" || { echo "C++ harness build failed"; exit 1; }

# The Logos side needs no regeneration: stdlib/std/wql/el_parser.logos is the
# committed peg_gen_logos output and is already inside liblogos-std.
echo "== building Logos harness =="
"$logosc" -O0 "$here/el_sdump.logos" -o "$work/el_sdump.o" 2>&1 | flt | grep -iE 'error' \
    && { echo "logosc failed"; exit 1; }
extra_uring=""
[ -f "$libd/liblstdlib_uring.a" ] && extra_uring="$libd/liblstdlib_uring.a"
"$cxx" -Wl,--gc-sections "$work/el_sdump.o" -Wl,--start-group \
    "$libd/liblogos-lang.a" "$libd/liblogos-mem.a" "$libd/liblogos-std.a" \
    "$libd/liblstdlib_rt.a" "$libd/liblstdlib_fibers.a" $extra_uring \
    -Wl,--end-group -Wl,--allow-multiple-definition -lpthread -lm \
    -o "$work/logos_el" 2>&1 | grep -iE 'undefined|error' && { echo "link failed"; exit 1; }

echo "== comparing $(grep -cve '^$' "$corpus") expressions =="
"$work/cpp_el"   "$corpus" > "$work/cpp.out"   || { echo "C++ harness crashed";   exit 1; }
"$work/logos_el" "$corpus" > "$work/logos.out" || { echo "Logos harness crashed"; exit 1; }

# A corpus line that BOTH backends reject is not agreement worth having — the
# oracle would pass on a grammar that parses nothing.
if ! grep -qve '<parse-error>' "$work/cpp.out"; then
    echo "every expression failed to parse — the harness is broken, not agreeing"; exit 1
fi

if diff -u "$work/logos.out" "$work/cpp.out" > "$work/diff.txt"; then
    n="$(grep -cve '^$' "$corpus")"
    echo "----------------------------------------"
    echo "✓ all $n expressions: peg_gen_logos AST == peg_gen_cpp AST"
    exit 0
fi

echo "----------------------------------------"
echo "MISMATCH (left = Logos backend, right = C++ backend):"
paste -d'|' <(nl -ba "$work/logos.out") <(nl -ba "$work/cpp.out") \
    | awk -F'|' '{ split($1,a,"\t"); split($2,b,"\t"); if (a[2] != b[2]) print "  line" a[1] ":\n    logos: " a[2] "\n    cpp:   " b[2] }' \
    | head -40
exit 1
