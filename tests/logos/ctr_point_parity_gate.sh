#!/usr/bin/env bash
# ctr_point_parity_gate.sh LOGOSC FIXTURE LIB_DIR EXTRACTOR
#
# ADR 0020 §10 step 3 — THE PARITY GATE (#362), "the load-bearing benchmark":
# a bound-key seek through the generated projection against a direct call.
#
# The fixture probes one container P times through `c.find(k)` and P times
# through `deem at_key(m, k) { from m e where e.key == k select e.val }`, and
# checks the answers agree. Under callgrind this gate checks the COST, in two
# deterministic measures (wall time on a shared box is not one):
#
#   STRUCTURE — the query path DESCENDS ONCE per probe, as `find` does: P
#     landings (`__ctr_bat_` → `seek_key`), and NO re-descent from the walk's
#     first `advance()` (it starts from the landing's cursor). Before #362 every
#     probe descended twice and binary-searched the leaf a second time.
#   PRICE — the query's inclusive instruction count over the direct call's is
#     at most CEIL. It is not 1.0 and the gap is named: the landing computes the
#     row's ORDINAL (`seek_key` sums the left siblings' counts per level; `find`
#     does not), and a query returns a `Vec`. Measured at landing: 1.60. Before
#     #362: 2.6 (wall time, interleaved).
#
# CONTROL (measured when the gate landed): taking the landing cursor out of
# `advance()` (always `cp.seek(self.at)`) reds STRUCTURE and PRICE both.
set -euo pipefail

LOGOSC="${1:?logosc path}"
FIXTURE="${2:?fixture .logos}"
LIB_DIR="${3:?stdlib archive dir}"
EXTRACTOR="${4:?callgrind_calls.py path}"
CEIL_X100=175

for f in "$LOGOSC" "$FIXTURE" "$EXTRACTOR"; do
    if [ ! -e "$f" ]; then echo "FAIL(2): missing input: $f"; exit 2; fi
done
for t in valgrind callgrind_annotate; do
    if ! command -v "$t" > /dev/null 2>&1; then
        echo "FAIL(2): $t is not installed — this gate MEASURES and has no verdict without it."
        exit 2
    fi
done
P=$(sed -n 's/^const P: u64 = \([0-9]*\)u64;.*/\1/p' "$FIXTURE" | head -1)
[ -n "$P" ] || { echo "FAIL(2): could not read const P from $FIXTURE"; exit 2; }

TMPD=$(mktemp -d)
trap 'rm -rf "$TMPD"' EXIT
if ! "$LOGOSC" "$FIXTURE" -o "$TMPD/t.o" > "$TMPD/cc.log" 2>&1; then
    echo "FAIL(2): logosc failed on $FIXTURE:"; cat "$TMPD/cc.log"; exit 2
fi
shopt -s nullglob
ARCHIVES=("$LIB_DIR"/liblstdlib*.a "$LIB_DIR"/liblogos-*.a)
for a in "$LIB_DIR"/*.a; do
    case "$(basename "$a")" in liblstdlib*|liblogos-*) ;; *) ARCHIVES+=("$a") ;; esac
done
if ! cc "$TMPD/t.o" -Wl,--start-group "${ARCHIVES[@]}" -Wl,--end-group \
        -lpthread -lm -lstdc++ -Wl,--gc-sections -Wl,--allow-multiple-definition \
        -o "$TMPD/t" > "$TMPD/link.log" 2>&1; then
    echo "FAIL(2): link failed:"; cat "$TMPD/link.log"; exit 2
fi
set +e
"$TMPD/t" > "$TMPD/prog.out" 2>&1
RC=$?
set -e
if [ "$RC" != 0 ]; then
    echo "FAIL(2): $FIXTURE exited $RC — the two paths disagree or the program failed,"
    echo "         so a cost comparison would describe a different execution."
    cat "$TMPD/prog.out"; exit 2
fi
if ! valgrind --tool=callgrind --callgrind-out-file="$TMPD/cg.out" "$TMPD/t" > /dev/null 2> "$TMPD/cg.err"; then
    echo "FAIL(2): callgrind run failed:"; tail -20 "$TMPD/cg.err"; exit 2
fi
python3 "$EXTRACTOR" "$TMPD/cg.out" > "$TMPD/edges"
edge() { awk -F'\t' -v ca="$1" -v ce="$2" '$2 ~ ca && $3 ~ ce { n += $1 } END { print n + 0 }' "$TMPD/edges"; }

LANDINGS=$(edge '__ctr_bat_' '__seek_key__f__')
ADV_CALLS=$(edge 'LeafWalk__advance' '.')
REDESCENTS=$(edge 'LeafWalk__advance' '__seek__f__')
DIRECT=$(edge '__find__f__' 'bt_descend_find')

callgrind_annotate --inclusive=yes "$TMPD/cg.out" > "$TMPD/ann" 2>/dev/null
ir() { grep -E "$1" "$TMPD/ann" | head -1 | awk '{ gsub(",", "", $1); print $1 }'; }
IR_Q=$(ir '[$]at_key__f__')
IR_D=$(ir '__find__f__')
if [ -z "$IR_Q" ] || [ -z "$IR_D" ] || [ "$ADV_CALLS" = 0 ]; then
    echo "FAIL(2): the profile does not show the query (\`at_key\`), the direct call"
    echo "         (\`find\`) or the walk's \`advance\` — inlined or renamed; no verdict."
    exit 2
fi
RATIO_X100=$(( IR_Q * 100 / IR_D ))

echo "measured: probes=$P landings=$LANDINGS direct_descents=$DIRECT re-descents=$REDESCENTS" \
     "Ir(query)=$IR_Q Ir(find)=$IR_D ratio=$((RATIO_X100 / 100)).$(printf %02d $((RATIO_X100 % 100))) ceiling=$((CEIL_X100 / 100)).$(printf %02d $((CEIL_X100 % 100)))"
fail() { echo "FAIL(1): $1"; exit 1; }
[ "$DIRECT" = "$P" ] || fail "the direct path descended $DIRECT times for $P probes."
[ "$LANDINGS" = "$P" ] || fail "the query path landed $LANDINGS times for $P probes — one landing per probe is the claim."
[ "$REDESCENTS" = 0 ] || fail "the walk re-descended $REDESCENTS times after landing — the
         landing's cursor must carry the first leaf (two descents per probe was the pre-#362 tax)."
[ "$RATIO_X100" -le "$CEIL_X100" ] || fail "the query costs $RATIO_X100% of the direct call; the ceiling is $CEIL_X100%."
echo "PASS"
