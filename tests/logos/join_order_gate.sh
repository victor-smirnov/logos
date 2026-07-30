#!/usr/bin/env bash
# join_order_gate.sh LOGOSC TEST_LOGOS
#
# EITHER SIDE MAY DRIVE (ADR 0024 S4). The fixture asserts the ROWS — that is what
# says the reorder is sound — but it cannot say that a second order was emitted at
# all: a compiler that quietly kept one nest returns exactly the same rows and
# passes. So the artifact and the plan's own account are gated here:
#
#   • the DECISION, on the one decision channel (`LOGOS_TRACE_PLAN`), for the
#     admitted shape AND for both refusals — a plan that only spoke when it acted
#     would leave "no line" meaning both "did not consider it" and "refused";
#   • BOTH NESTS in the emitted source, one walking each side, under ONE run-time
#     discriminant per query — read out of the dump rather than argued from the
#     emitter;
#   • the sort's TUPLE TIEBREAK, because that is what makes the two nests
#     interchangeable. Without it equal keys keep COLLECTION order, which is a
#     fact about which nest ran, and the reorder would silently reorder rows.
set -euo pipefail

LOGOSC="$1"
TEST_LOGOS="$2"

TMPD=$(mktemp -d)
trap 'rm -rf "$TMPD"' EXIT

if ! LOGOS_TRACE_PLAN=1 "$LOGOSC" "$TEST_LOGOS" --gen-dir "$TMPD/gen" \
        -o "$TMPD/test.o" 2>"$TMPD/err"; then
    echo "FAIL: logosc failed:"; cat "$TMPD/err"; exit 1
fi

fail=0

# ── the decision, and both refusals ────────────────────────────────────────
if ! grep -Eq '^\[plan\] l -> drive either side on r ' "$TMPD/err"; then
    echo "FAIL: no join-order decision for the admitted shape"
    fail=1
fi
if ! grep -q 'the plan indexes the SMALLER side and walks the larger' "$TMPD/err"; then
    echo "FAIL: the admitted join order carries no ground"
    fail=1
fi
if ! grep -Eq '^\[plan\] l -> drive fixed on r ' "$TMPD/err"; then
    echo "FAIL: a refused join order is not reported at all"
    fail=1
fi
if ! grep -q 'an anti join is not symmetric' "$TMPD/err"; then
    echo "FAIL: the anti-join refusal carries no ground"
    fail=1
fi
if ! grep -q 'without .order by. there is no sequence to restore' "$TMPD/err"; then
    echo "FAIL: the no-order-by refusal carries no ground"
    fail=1
fi
# The REVERSE direction's strategy is a decision of its own, made by the same
# rule and recorded — not re-derived in the emitter.
if ! grep -Eq '^\[plan\] l -> hash join when driven from r ' "$TMPD/err"; then
    echo "FAIL: the transposed order's own strategy is not recorded"
    fail=1
fi

# ── the artifact: two nests, one discriminant, a total sort ────────────────
shopt -s nullglob
DUMPS=("$TMPD"/gen/test.*.gen.logos)
if [ "${#DUMPS[@]}" -lt 1 ]; then
    echo "FAIL: no test.*.gen.logos dump — the emitted side was not asserted"
    exit 1
fi

# One discriminant per DYNAMIC query (4 of them in the fixture), and it is a
# comparison of the two sources' sizes.
n_disc=$(grep -Fc 'if (((rs).len() > (ls).len()))' "${DUMPS[@]}" | awk -F: '{s+=$2} END{print s+0}')
if [ "$n_disc" -ne 4 ]; then
    echo "FAIL: $n_disc size discriminants in the emitted fns (want 4, one per dynamic query)"
    grep -n 'len() > ' "${DUMPS[@]}" || true
    fail=1
fi

# BOTH orders are present: one nest's outermost loop walks `ls`, the other's `rs`.
n_ls=$(grep -Fc 'while (__i0 < (ls).len())' "${DUMPS[@]}" | awk -F: '{s+=$2} END{print s+0}')
n_rs=$(grep -Fc 'while (__i0 < (rs).len())' "${DUMPS[@]}" | awk -F: '{s+=$2} END{print s+0}')
if [ "$n_ls" -lt 4 ] || [ "$n_rs" -lt 4 ]; then
    echo "FAIL: base loops over ls=$n_ls rs=$n_rs (want >= 4 each: both nests, per dynamic query)"
    fail=1
fi

# The sort is TOTAL: equal keys fall back to the row-index tuple, lexicographic
# in query source order. This is the licence for two nests to exist.
if ! grep -Fq '__ix0.get((__b - 1i64)) > __iv0' "${DUMPS[@]}"; then
    echo "FAIL: the sort has no row-index tiebreak — equal keys would keep collection order"
    grep -n '__b - 1i64' "${DUMPS[@]}" | head -5 || true
    fail=1
fi
if ! grep -Fq '__ix0.get((__b - 1i64)) == __iv0' "${DUMPS[@]}"; then
    echo "FAIL: the tiebreak is not lexicographic (no equal-component descent)"
    fail=1
fi

if [ "$fail" -ne 0 ]; then
    echo "---- trace ----"
    grep '^\[plan\]' "$TMPD/err" || true
    exit 1
fi
exit 0
