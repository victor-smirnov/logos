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

command -v souffle >/dev/null || { echo "sweep: souffle not installed"; exit 2; }
bash tools/dlog/make.sh >/dev/null 2>&1 || { echo "sweep: cxx_facts does not build"; exit 2; }

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
    "$1" -p build --out="$d" "$3" >"$d/log" 2>&1
' _ "$ROOT/build/dlog/cxx_facts" "$ROOT/$OUT" {} 2>/dev/null

OK=$(ls -d "$OUT"/per/*/ 2>/dev/null | wc -l)
# ⚠ CHECK A RELATION THE EXTRACTOR ALWAYS WRITES. This tested `tests.facts`,
# which the question-shaped extractor produced per TU and the general one does
# not — so the line read "39 with no facts" while the sweep went on to report
# 871 tests. A progress line that contradicts the result is worse than none.
FAILED=$(for d in "$OUT"/per/*/; do [ -s "$d/node.facts" ] || echo "$d"; done | wc -l)
echo "sweep: $OK dirs, $FAILED that produced no AST at all"
[ "$FAILED" -lt "$((OK / 2))" ] || { echo "sweep: more than half the TUs failed to parse — refusing"; exit 3; }

# ⚠ MERGING IS ONLY SOUND BECAUSE OF HOW IDENTITY IS KEYED: declarations by
# their canonical source location (stable across TUs) and nodes by a TU-tagged
# counter (TU-local by construction). Concatenating the relational facts of 40
# TUs would be meaningless otherwise.
for r in node loc decl decl_name decl_node ref call enum_member type type_of \
         type_pointee type_decl cast_kind cfg_block cfg_entry cfg_exit \
         cfg_edge cfg_stmt; do
    cat "$OUT"/per/*/"$r".facts 2>/dev/null | sort -u > "$OUT/$r.facts"
done
cp "$ROOT"/tools/dlog/*.dl "$OUT/"
grep -vE '^\s*(#|$)' tools/dlog/not_projection.claim > "$OUT/not_projection.facts"
echo VarRef > "$OUT/place_root_kind.facts"

(cd "$OUT" && souffle -F. -D. -I. "$ROOT/tools/dlog/place_walkers.dl" >/dev/null 2>&1) || {
    echo "sweep: souffle failed"; exit 2; }
NC=$(wc -l < "$OUT/expr_code.csv")
[ "$NC" -ge 20 ] || { echo "sweep: domain is $NC codes — refusing to report on it"; exit 3; }

echo
echo "── domain ──  $NC codes, $(wc -l < "$OUT/projection_kind.csv") projections,"\
     "$(wc -l < "$OUT/tests.csv") tests, $(wc -l < "$OUT/walker.csv") derived walkers"
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
