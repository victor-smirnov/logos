#!/usr/bin/env bash
# WQL EL-parser BEHAVIOR oracle (ADR 0012 peg-frontend, Phase 4).
#
# Parses a corpus of EL expression snippets through BOTH the peg_gen_logos-
# GENERATED parser (logos.std.wql.el_parser) and the hand-written recursive-
# descent parser (logos.std.wql.el), and asserts the resulting SExpr IR trees are
# STRUCTURALLY identical (same schema node codes + operator ids + tree shape,
# recursively over the WRef<SExpr> edges). See wql_oracle_main.logos for why the
# invariant is structural (the documented placeholder GAPs in el.peg).
#
# Opt-in; NOT a ctest (it links the whole stdlib archive). Run on demand:
#   tools/peg_gen_logos/wql_oracle/run.sh [build_dir]
# or:   cmake --build build --target wql_peg_oracle
#
# Requires logosc + a built liblogos-std that INCLUDES the committed generated
# parser (stdlib/mem/wql/el_parser.logos) — i.e. a normal `cmake --build`.
set -u

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo="$(cd "$here/../../.." && pwd)"
build="${1:-$repo/build}"

logosc="$build/bin/logosc"
libd="$build/lib/logos"

[ -x "$logosc" ] || { echo "missing $logosc — build logosc first (cmake --build $build)"; exit 1; }

cxx="$(sed -n 's/^CMAKE_CXX_COMPILER:[^=]*=//p' "$build/CMakeCache.txt" 2>/dev/null)"
[ -n "$cxx" ] || cxx=clang++

work="$build/wql_peg_oracle"
rm -rf "$work"; mkdir -p "$work"

echo "== compiling WQL EL-parser behavior oracle =="
"$logosc" -O0 "$here/wql_oracle_main.logos" -o "$work/wql_oracle.o" 2>&1 | grep -iE 'error' && { echo "compile failed"; exit 1; }

echo "== linking =="
extra_uring=""
[ -f "$libd/liblstdlib_uring.a" ] && extra_uring="$libd/liblstdlib_uring.a"
"$cxx" -Wl,--gc-sections "$work/wql_oracle.o" -Wl,--start-group \
    "$libd/liblogos-lang.a" "$libd/liblogos-mem.a" "$libd/liblogos-lcm.a" "$libd/liblogos-std.a" \
    "$libd/liblstdlib_rt.a" "$libd/liblstdlib_fibers.a" $extra_uring \
    -Wl,--end-group -Wl,--allow-multiple-definition -lpthread -lm \
    -o "$work/wql_oracle_run" 2>&1 | grep -iE 'undefined|error' && { echo "link failed"; exit 1; }

echo "== running =="
out="$("$work/wql_oracle_run")"; rc=$?
echo "$out"
if [ $rc -eq 0 ] && printf '%s' "$out" | grep -q '^PASS:'; then
    echo "----------------------------------------"
    echo "PASS: generated EL parser deterministic + well-formed (deep IR)"
    exit 0
fi
echo "----------------------------------------"
echo "FAIL: WQL EL-parser behavior oracle (rc=$rc)"
exit 1
