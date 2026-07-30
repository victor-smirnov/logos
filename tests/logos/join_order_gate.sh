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
#   • BOTH NESTS in the emitted source, one walking each side, under ONE
#     discriminant per query — read out of the dump rather than argued from the
#     emitter. Since ADR 0024 S4i that discriminant is a FIELD OF THE PREPARED
#     PLAN and the size comparison that fills it lives in the query's `prepare`,
#     so both halves are asserted where they now are;
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

# ⚠ `grep -Fc` over MANY files exits 1 when the total is zero, and under
# `set -euo pipefail` that killed this script with no message at all — a gate
# that "returned 1" while saying nothing about which assertion failed. Every
# count below is therefore `|| true`-guarded and reported.
count() { grep -Fc "$1" "${DUMPS[@]}" 2>/dev/null | awk -F: '{s+=$2} END{print s+0}' || true; }

# One discriminant per DYNAMIC query (4 of them in the fixture) — and since
# ADR 0024 S4i it is a field of the PREPARED PLAN, not a comparison in the
# query's body: the sizes are compared once, in `prepare`.
n_disc=$(count 'if ((__pl.order_ix == 1i64))')
if [ "$n_disc" -ne 4 ]; then
    echo "FAIL: $n_disc plan discriminants in the emitted fns (want 4, one per dynamic query)"
    grep -n 'if (__pl' "${DUMPS[@]}" || true
    fail=1
fi
# …and the DECISION itself is in `prepare`, through the one cost function over the
# candidate table (ADR 0024 S4k — a two-source chain is the degenerate case of it,
# and its answer must be the pair rule's: index the smaller side).
n_cmp=$(count 'let __ix: i64 = jc_order_pick((&__tb), __n0, __n1, __n2, __n3);')
if [ "$n_cmp" -ne 4 ]; then
    echo "FAIL: $n_cmp prepared order decisions (want 4, one prepare fn per dynamic query)"
    grep -n 'jc_order_pick' "${DUMPS[@]}" || true
    fail=1
fi
# TWO candidates, both of them a permutation of the two size facts, and the roles
# say which side is scanned and which is indexed.
n_tbl=$(count 'JCTable { ncand: 2i64, nfl: 2i64, slot: [0i64, 1i64, 0i64, 0i64, 1i64, 0i64,')
if [ "$n_tbl" -ne 4 ]; then
    echo "FAIL: $n_tbl two-candidate tables (want 4) — the pair case is not the degenerate cost comparison"
    grep -n 'JCTable {' "${DUMPS[@]}" || true
    fail=1
fi
if ! grep -Fq 'let __n1: i64 = (rs).len();' "${DUMPS[@]}"; then
    echo "FAIL: the prepare fn does not measure the step side"
    fail=1
fi

# BOTH orders are present: one nest's outermost loop walks `ls`, the other's `rs`.
n_ls=$(count 'while (__i0 < (ls).len())')
n_rs=$(count 'while (__i0 < (rs).len())')
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
