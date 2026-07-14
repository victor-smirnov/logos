#!/usr/bin/env bash
# WQL cross-BACKEND oracle.
#
# Parses one corpus of deem!-body queries twice — once with the parser peg_gen_cpp
# generates from wql.peg, once with the parser peg_gen_logos generates from the
# SAME wql.peg — and requires the structural stringification of the two RQProgram
# trees to be identical.
#
# The sibling el oracle (run.sh) does this for EXPRESSIONS only. That is exactly
# how the INTEGER-type-suffix divergence survived four grammars: no expression in
# the EL corpus ever wrote `1000000i64`, while nearly every real deem! body does.
# Since 42270e73 the compiler parses rule bodies with the C++ parser and the
# runtime `Query::compile` still uses the Logos one, so the two must agree here or
# a query means different things at compile time and at run time.
#
#   tools/peg_gen_cpp/oracle/run_wql.sh [<build-dir>]
set -u

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo="$(cd "$here/../../.." && pwd)"
build="${1:-$repo/build}"
logosc="$build/bin/logosc"
peg_cpp="$build/tools/peg_gen_cpp/peg_gen_cpp"
libd="$build/lib/logos"
grammars="$repo/stdlib/mem/wql/grammars"
corpus="$here/wql_corpus.txt"

for f in "$logosc" "$peg_cpp" "$corpus"; do
    [ -e "$f" ] || { echo "missing $f — build first (cmake --build $build)"; exit 1; }
done

cxx="$(sed -n 's/^CMAKE_CXX_COMPILER:[^=]*=//p' "$build/CMakeCache.txt" 2>/dev/null)"
[ -n "$cxx" ] || cxx=clang++
flt() { grep -vE 'version-mismatch|no ABI guarantee|may be unstable'; }

work="$build/peg_gen_cpp_wql_oracle"
rm -rf "$work"; mkdir -p "$work"

# wql.peg %imports el.peg, and the embed emits a stack-local ElParser — so both
# generated translation units are needed.
echo "== generating the C++ WQL + EL parsers =="
for g in wql el; do
    "$peg_cpp" "$grammars/$g.peg" --out-dir "$work" >/dev/null 2>&1 \
        || { echo "peg_gen_cpp failed on $g.peg"; exit 1; }
done

echo "== building C++ harness =="
core="$(find "$build" -name 'liblogos_core*.a' | head -1)"
"$cxx" -std=c++23 -fno-rtti -Wno-unused-variable -I "$repo/include" -I "$work" \
    "$here/wql_stringify.cpp" "$work/wql_surface_parser.cpp" "$work/el_parser.cpp" \
    -Wl,--start-group \
      "$build/src/writ/liblogos_writ.a" \
      "$build/src/verification/liblogos_verification.a" \
      $core \
    -Wl,--end-group -lpthread \
    -o "$work/cpp_wql" || { echo "C++ harness build failed"; exit 1; }

# The Logos side needs no regeneration: stdlib/mem/wql/wql_surface_parser.logos is
# the committed peg_gen_logos output and is already inside liblogos-std.
echo "== building Logos harness =="
"$logosc" -O0 "$here/wql_sdump.logos" -o "$work/wql_sdump.o" 2>&1 | flt | grep -iE 'error' \
    && { echo "logosc failed"; exit 1; }
extra_uring=""
[ -f "$libd/liblstdlib_uring.a" ] && extra_uring="$libd/liblstdlib_uring.a"
"$cxx" -Wl,--gc-sections "$work/wql_sdump.o" -Wl,--start-group \
    "$libd/liblogos-lang.a" "$libd/liblogos-mem.a" "$libd/liblogos-lcm.a" "$libd/liblogos-std.a" \
    "$libd/liblstdlib_rt.a" "$libd/liblstdlib_fibers.a" $extra_uring \
    -Wl,--end-group -Wl,--allow-multiple-definition -lpthread -lm \
    -o "$work/logos_wql" 2>&1 | grep -iE 'undefined|error' && { echo "link failed"; exit 1; }

n="$(grep -cve '^$' -e '^#' "$corpus")"
echo "== comparing $n queries =="
"$work/cpp_wql"   "$corpus" > "$work/cpp.out"   || { echo "C++ harness crashed";   exit 1; }
"$work/logos_wql" "$corpus" > "$work/logos.out" || { echo "Logos harness crashed"; exit 1; }

# A corpus where BOTH backends reject everything is not agreement worth having —
# the oracle would pass on a grammar that parses nothing.
if ! grep -qve '<parse-error>' "$work/cpp.out"; then
    echo "every query failed to parse — the harness is broken, not agreeing"; exit 1
fi

if diff -u "$work/logos.out" "$work/cpp.out" > "$work/diff.txt"; then
    echo "----------------------------------------"
    echo "✓ all $n queries: peg_gen_logos AST == peg_gen_cpp AST"
    exit 0
fi

echo "----------------------------------------"
echo "MISMATCH (left = Logos backend, right = C++ backend):"
paste -d'|' <(nl -ba "$work/logos.out") <(nl -ba "$work/cpp.out") \
    | awk -F'|' '{ split($1,a,"\t"); split($2,b,"\t"); if (a[2] != b[2]) print "  line" a[1] ":\n    logos: " a[2] "\n    cpp:   " b[2] }' \
    | head -40
exit 1
