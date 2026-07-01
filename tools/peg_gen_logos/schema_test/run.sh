#!/usr/bin/env bash
# SCHEMA-EMISSION smoke test for peg_gen_logos (ADR 0012 peg-frontend).
#
# Verifies the generator's schema-emission mode end-to-end: run peg_gen_logos on
# demo.peg (which requests `%schema` mode) → a parser that builds first-class Writ
# schema views (`h.make::<S>()` + typed field writes + ERef edges) → compile +
# link + RUN a harness that parses "2*3+4", walks the EExpr IR, and asserts the
# left-assoc operator tree  EBin(+, EBin(*, ELit, ELit), ELit).
#
# This is a narrow tool for working on the generator's schema-emission path; it is
# NOT a ctest test (it regenerates a parser + compiles + links). Run on demand:
#   tools/peg_gen_logos/schema_test/run.sh [build_dir]
# or:   cmake --build build --target peg_gen_logos_schema_test
#
# Requires logosc + peg_gen_logos already built.
#
# NOTE: the harness + generated parser + demo_ir are merged into ONE compilation
# unit before compiling. This is deliberate: an isolated `--emit-module` of a
# package that only READS a WRef/ERef<S> field (never constructs that S) trips a
# pre-existing, documented compiler mono-enqueue bug (from_wany/from_any not
# enqueued cross-CU — see project_writ_query_language). That bug is unrelated to
# the generator; single-CU compilation is how el.logos etc. build inside
# liblogos-std, and it exercises the generated schema-emission code identically.
set -u

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo="$(cd "$here/../../.." && pwd)"
build="${1:-$repo/build}"

logosc="$build/bin/logosc"
peg_logos="$build/bin/peg_gen_logos"
libd="$build/lib/logos"

for f in "$logosc" "$peg_logos"; do
    [ -x "$f" ] || { echo "missing $f — build logosc + peg_gen_logos first (cmake --build $build)"; exit 1; }
done

cxx="$(sed -n 's/^CMAKE_CXX_COMPILER:[^=]*=//p' "$build/CMakeCache.txt" 2>/dev/null)"
[ -n "$cxx" ] || cxx=clang++

work="$build/peg_gen_logos_schema_test"
rm -rf "$work"; mkdir -p "$work"

echo "== generating schema-emitting parser from demo.peg =="
"$peg_logos" "$here/demo.peg" --out-dir "$work" 2>&1 | grep -v '^peg_gen_logos:' | grep -iE 'error' && { echo "generation failed"; exit 1; }
[ -f "$work/demo_parser.logos" ] || { echo "generator did not write demo_parser.logos"; exit 1; }

# Sanity: the generated parser must actually use schema-emission (make::<S>() +
# typed writes), not the raw-TOM fallback.
grep -q 'doc.make::<' "$work/demo_parser.logos" || { echo "FAIL: generated parser has no make::<S>() — schema mode not active"; exit 1; }
grep -q 'ERef::<' "$work/demo_parser.logos" || { echo "FAIL: generated parser has no ERef edge write"; exit 1; }

echo "== merging into one compilation unit =="
{
  echo "package demo_all;"
  echo "use logos.lang.writ.container;"
  echo "use logos.lang.writ.allocator;"
  echo "use logos.lang.writ.anyval;"
  echo "use logos.lang.writ.wmap;"
  echo "use logos.lang.str;"
  echo "use logos.lang.writ.array;"
  echo "use logos.lang.writ.wstring;"
  echo "use logos.mem.collections.hashmap;"
  echo "use logos.mem.collections.vec;"
  grep -vE '^package |^use ' "$here/demo_ir.logos"
  grep -vE '^package |^use ' "$work/demo_parser.logos"
  grep -vE '^package |^use ' "$here/harness.logos"
} > "$work/demo_all.logos"

echo "== compiling =="
"$logosc" -O0 "$work/demo_all.logos" -o "$work/demo_all.o" 2>&1 | grep -iE 'error' && { echo "compile failed"; exit 1; }

echo "== linking =="
"$cxx" -Wl,--gc-sections "$work/demo_all.o" -Wl,--start-group \
    "$libd/liblogos-lang.a" "$libd/liblogos-mem.a" "$libd/liblogos-std.a" \
    "$libd/liblstdlib_rt.a" "$libd/liblstdlib_fibers.a" \
    -Wl,--end-group -Wl,--allow-multiple-definition -lpthread -lm \
    -o "$work/demo_run" 2>&1 | grep -iE 'undefined|error' && { echo "link failed"; exit 1; }

echo "== running =="
out="$("$work/demo_run")"; rc=$?
echo "$out"
if [ $rc -eq 0 ] && printf '%s' "$out" | grep -q '^OK:'; then
    echo "----------------------------------------"
    echo "PASS: peg_gen_logos schema-emission builds + runs the EExpr IR correctly"
    exit 0
fi
echo "----------------------------------------"
echo "FAIL: schema-emission smoke test (rc=$rc)"
exit 1
