#!/usr/bin/env bash
# Trama parser BEHAVIOR oracle (ADR 0012 peg-frontend, Phase 3).
#
# Parses a corpus of Trama templates through BOTH the peg_gen_logos-GENERATED
# parser (logos.std.wql.trama_parser::parse_tpl) and the hand-written recursive-
# descent parser (logos.std.wql.trama::parse_trama), and asserts the resulting
# Trama-AST statement SEQUENCES are equal — same kinds, same TEXT runs (T1), and
# DEEP-EQUAL embedded EL SExpr trees (T2). See trama_oracle_main.logos.
#
# Opt-in; NOT a ctest (it links the whole stdlib). Run on demand:
#   tools/peg_gen_logos/wql_oracle/trama_run.sh [build_dir]
# or:   cmake --build build --target trama_peg_oracle
set -u

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo="$(cd "$here/../../.." && pwd)"
build="${1:-$repo/build}"

logosc="$build/bin/logosc"
libd="$build/lib/logos"

[ -x "$logosc" ] || { echo "missing $logosc — build logosc first (cmake --build $build)"; exit 1; }

cxx="$(sed -n 's/^CMAKE_CXX_COMPILER:[^=]*=//p' "$build/CMakeCache.txt" 2>/dev/null)"
[ -n "$cxx" ] || cxx=clang++

work="$build/trama_peg_oracle"
rm -rf "$work"; mkdir -p "$work"

echo "== compiling Trama parser behavior oracle =="
"$logosc" -O0 "$here/trama_oracle_main.logos" -o "$work/trama_oracle.o" 2>&1 | grep -iE 'error' && { echo "compile failed"; exit 1; }

echo "== linking =="
extra_uring=""
[ -f "$libd/liblstdlib_uring.a" ] && extra_uring="$libd/liblstdlib_uring.a"
"$cxx" -Wl,--gc-sections "$work/trama_oracle.o" -Wl,--start-group \
    "$libd/liblogos-lang.a" "$libd/liblogos-mem.a" "$libd/liblogos-lcm.a" "$libd/liblogos-std.a" \
    "$libd/liblstdlib_rt.a" "$libd/liblstdlib_fibers.a" $extra_uring \
    -Wl,--end-group -Wl,--allow-multiple-definition -lpthread -lm \
    -o "$work/trama_oracle_run" 2>&1 | grep -iE 'undefined|error' && { echo "link failed"; exit 1; }

echo "== running =="
out="$("$work/trama_oracle_run")"; rc=$?
echo "$out"
if [ $rc -eq 0 ] && printf '%s' "$out" | grep -q '^PASS:'; then
    echo "----------------------------------------"
    echo "PASS: generated Trama parser deterministic + well-formed (text + embedded EL + elif)"
    exit 0
fi
echo "----------------------------------------"
echo "FAIL: Trama parser behavior oracle (rc=$rc)"
exit 1
