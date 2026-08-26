#!/usr/bin/env bash
# sweep.sh [<outdir>] — ask the WHOLE compiler, not the files I thought of.
#
# ⚠ NOT A CTEST TEST. It parses every compiler TU with clang (minutes), needs
# libclang-20-dev and souffle, and produces a REPORT to read rather than a
# verdict. Run it by hand. Its rules are bite-proved by selftest.sh.
#
# ⚠ NOT WHILE A SUITE IS RUNNING. This saturates every core; ctest's timing-
# sensitive tests read that as their own slowness. One scheduler at a time.
#
# WHY IT EXISTS. Until now the chain was pointed at two translation units out of
# 108, and at three function names its author typed. Both are the same mistake
# the rules are built to detect, committed by the harness around them: a tool
# that only answers about the places you already suspect cannot tell you the
# thing you do not know. The walker set is now DERIVED (see place_walkers.dl);
# this removes the other half by asking every TU.
set -uo pipefail
cd "$(dirname "$0")/../.." || exit 2
ROOT=$PWD
OUT="${1:-build/dlog/sweep}"
ENUM=::logos::compiler::lir_schema::expr::Code

command -v souffle >/dev/null || { echo "sweep: souffle not installed"; exit 2; }
bash tools/dlog/make.sh >/dev/null 2>&1 || { echo "sweep: lir_facts does not build"; exit 2; }

rm -rf "$OUT"; mkdir -p "$OUT/per"
# Every compiler TU the build actually compiles — from the compilation database,
# not from a glob over src/, so a file excluded from the build is excluded here.
python3 - "$ROOT/build/compile_commands.json" > "$OUT/tus" <<'PY'
import json, sys
for e in json.load(open(sys.argv[1])):
    f = e["file"]
    if "/src/compiler/" in f and f.endswith((".cpp", ".cc")):
        print(f)
PY
N=$(wc -l < "$OUT/tus")
[ "$N" -ge 10 ] || { echo "sweep: only $N TUs found — refusing (is build/ configured?)"; exit 3; }
echo "sweep: $N translation units, -P$(nproc)"

# One process per TU: lir_facts accumulates in globals and writes at exit, so
# concurrency has to be at the process boundary, with a private out dir each.
# ⚠ PASS THE BINARY'S PATH, DO NOT DERIVE IT FROM $OUT. The first cut spelled it
# "$OUT/../../build/dlog/lir_facts", which is only right when OUT is exactly two
# levels deep — and OUT is a parameter. Every worker failed with "not found" and
# the sweep reported "0 codes" rather than an error, which is why the domain
# floor below exists.
xargs -P"$(nproc)" -I{} -a "$OUT/tus" sh -c '
    d="$2/per/$(basename "$3" | tr "/." "__")"; mkdir -p "$d"
    "$1" -p build --enum="$4" --out="$d" "$3" >"$d/log" 2>&1
' _ "$ROOT/build/dlog/lir_facts" "$ROOT/$OUT" {} "$ENUM" 2>/dev/null

OK=$(ls -d "$OUT"/per/*/ 2>/dev/null | wc -l)
FAILED=$(grep -Lq . /dev/null 2>/dev/null; for d in "$OUT"/per/*/; do
    [ -s "$d/tests.facts" ] || echo "$d"; done | wc -l)
echo "sweep: $OK dirs, $FAILED with no facts (a TU with no dispatch is normal)"

cat "$OUT"/per/*/expr_code.facts 2>/dev/null | sort -u > "$OUT/expr_code.facts"
cat "$OUT"/per/*/tests.facts    2>/dev/null | sort -u > "$OUT/tests.facts"
grep -vE '^\s*(#|$)' tools/dlog/not_projection.claim > "$OUT/not_projection.facts"
echo VarRef > "$OUT/place_root_kind.facts"

NC=$(wc -l < "$OUT/expr_code.facts")
[ "$NC" -ge 20 ] || { echo "sweep: domain is $NC codes — refusing to report on it"; exit 3; }

(cd "$OUT" && souffle -F. -D. "$ROOT/tools/dlog/place_walkers.dl" >/dev/null 2>&1) || {
    echo "sweep: souffle failed"; exit 2; }

echo
echo "── domain ──  $NC codes, $(wc -l < "$OUT/projection_kind.csv") projections,"\
     "$(wc -l < "$OUT/tests.facts") tests, $(wc -l < "$OUT/walker.csv") derived walkers"
echo
echo "── incomplete walkers, worst first ──"
join -t'	' -1 1 -2 1 \
    <(sort "$OUT/coverage.csv") \
    <(cut -f1 "$OUT/spelling_keyed.csv" | sort -u) 2>/dev/null |
  sort -t'	' -k2,2n |
  while IFS=$'\t' read -r f have want; do
      printf '  %-44s %s/%s   missing: %s\n' "$f" "$have" "$want" \
             "$(awk -F'\t' -v w="$f" '$1==w{printf "%s ", $2}' "$OUT/spelling_keyed.csv")"
  done
echo
echo "── arms that exist and do nothing ──"
[ -s "$OUT/dead_arm.csv" ] && sed 's/^/  /' "$OUT/dead_arm.csv" || echo "  (none)"
