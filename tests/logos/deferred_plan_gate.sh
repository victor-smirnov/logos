#!/usr/bin/env bash
# deferred_plan_gate.sh LOGOSC TEST_LOGOS
#
# THE SECOND DECISION POINT (ADR 0024 S4j). The fixture asserts behaviour — the
# rows, their order, the pull count, and that ONE plan drives different sides on
# different data — but four properties decide whether the deferral is worth having
# at all, and none of them is visible from the outside:
#
#   • THE PER-ROW COST DID NOT MOVE. The two `len()` reads and the discriminant
#     must sit at BODY LEVEL (indent 4) and above every loop, exactly where the
#     prepared plan's field read sits. A compiler that put the comparison inside
#     the nest returns identical rows and passes every behavioural assertion while
#     paying for the decision per row.
#
#   • THE NUMBER IS THE MATERIALIZED LENGTH. `__defer_n0` must read the REL's
#     binding, not the input parameter the planner could have measured in
#     `prepare`. Both are `i64` and both are in scope; only one is the right one,
#     and on the mirror data set they agree, so half the fixture would pass with
#     the wrong read.
#
#   • THE DEFERRAL COSTS `prepare` NOTHING. A deferred plan's `prepare` must
#     contain no loop and no measurement at all — that is what keeps `agrees`
#     affordable for every plan a caller holds, deferred ones included.
#
#   • THE TWO POINTS ARE TELLABLE APART, in the artifact and in the trace. The
#     prepared query in the same fixture reads `__pl.order_ix`; the deferred one
#     reads `__defer_ix`, bound above the nest from the same `order_pick` the plan
#     re-derives `agrees` through (ADR 0024 S4k — one cost function, in stdlib); the
#     trace names the point AND the relation that supplies the number. A reader must
#     not have to open the emitter to know which half of a plan decided.
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

shopt -s nullglob
DUMPS=("$TMPD"/gen/test.*.gen.logos)
if [ "${#DUMPS[@]}" -lt 1 ]; then
    echo "FAIL: no test.*.gen.logos dump — the emitted side was not asserted"
    exit 1
fi

# ⚠ `grep -c` over MANY files exits non-zero on a zero total, and under
# `set -euo pipefail` that would kill this script with no message — a gate that
# returns 1 while saying nothing. Every count is guarded and reported.
count() { grep -Ec "$1" "${DUMPS[@]}" 2>/dev/null | awk -F: '{s+=$2} END{print s+0}' || true; }

# ── the trace names the DECISION POINT, for both points ────────────────────
if ! grep -Eq '^\[plan\] x -> drive either side in .run. on r ' "$TMPD/err"; then
    echo 'FAIL: the deferred decision does not name `run` as the point it was taken at'
    fail=1
fi
if ! grep -Eq '^\[plan\] l -> drive either side on r ' "$TMPD/err"; then
    echo "FAIL: the PREPARED decision is missing — the contrast that makes the deferred one legible"
    fail=1
fi
if ! grep -q 'LENGTHS OF RELATIONS THIS QUERY MATERIALIZES ANYWAY' "$TMPD/err"; then
    echo "FAIL: the deferred decision carries no ground for being affordable"
    fail=1
fi
# WHICH RELATION SUPPLIED THE NUMBER, and where the read sits relative to the join.
if ! grep -Eq '^\[plan\] x -> sizes deferred to run on r .*\(__rel_hot_sl\)\.len\(\) vs \(rs\)\.len\(\)' "$TMPD/err"; then
    echo "FAIL: the deferred trace does not name the two relations it will compare"
    grep '^\[plan\].*deferred' "$TMPD/err" || true
    fail=1
fi
if ! grep -q 'after the prelude, before the join.s index' "$TMPD/err"; then
    echo 'FAIL: the deferred trace does not say WHERE in `run` the number becomes free'
    fail=1
fi
# `agrees`/`margin` must be stated NOT to cover the deferred half.
if ! grep -q 'agrees. and .margin. do not speak about this half' "$TMPD/err"; then
    echo "FAIL: the deferred ground does not disclaim the re-checkable surface"
    fail=1
fi

# ── the plan type carries the deferred half and the accessor that discloses it ──
for member in 'pub defer_order: bool' \
              'pub fn settled\(&self\) -> bool' \
              'pub fn order_pick\(&self, n0: i64, n1: i64, n2: i64, n3: i64\) -> i64'; do
    if ! grep -Eq "$member" "${DUMPS[@]}"; then
        echo "FAIL: the plan type has no /$member/ — the deferred half would be indistinguishable from a refusal"
        fail=1
    fi
done
# ONE rule, in one place: `agrees` re-derives the prepared decision THROUGH
# `order_pick`, which is the same method the deferred binding calls — and which is
# itself one call into `logos.std.wql.join_cost` (ADR 0024 S4k). A second
# hand-written comparison in `agrees` is how the two points come to disagree.
if ! grep -Eq 'self\.order_ix == self\.order_pick\(fresh\.base_n, fresh\.step_n, fresh\.n2, fresh\.n3\)' "${DUMPS[@]}"; then
    echo "FAIL: agrees does not go through order_pick — the two decision points can drift apart"
    grep -n 'agrees' "${DUMPS[@]}" || true
    fail=1
fi
# `agrees` answers about the PREPARED half only: it gates on `dyn_order`, and
# `defer_order` appears in exactly one method — `settled`, whose whole job is to
# disclose it. Asserted by EXTRACTING each method's body rather than grepping the
# file: the plan's recorded ground is a string literal in this very dump and its
# prose contains both "agrees" and "defer_order", so a file-wide match reads as a
# violation of a rule it is actually stating. (The prepared gate hit the same trap
# with the word "for".)
for f in "${DUMPS[@]}"; do
    grep -Eq '^impl [A-Za-z]+Plan \{' "$f" || continue
    agr=$(sed -n '/^    pub fn agrees(/,/^    }$/p' "$f")
    if echo "$agr" | grep -q 'defer_order'; then
        echo "FAIL: agrees consults defer_order — it would claim to cover a half that is re-decided every call"
        echo "$agr"
        fail=1
    fi
    if ! echo "$agr" | grep -Fq 'if (!self.dyn_order) {'; then
        echo "FAIL: agrees does not gate on dyn_order — it no longer answers about the prepared half alone"
        echo "$agr"
        fail=1
    fi
    mrg=$(sed -n '/^    pub fn margin(/,/^    }$/p' "$f")
    if echo "$mrg" | grep -q 'defer_order'; then
        echo "FAIL: margin consults defer_order — a per-call decision has no distance to a flip"
        fail=1
    fi
done

# ── PER-ROW COST: the reads and the branch sit at body level, above every loop ──
# Anchored at exactly four spaces. One more level of indentation means inside a
# block, and inside a block here means inside the nest.
for pat in '^    let __defer_n0: i64 = ' \
           '^    let __defer_n1: i64 = ' \
           '^    let __defer_ix: i64 = __pl\.order_pick\(__defer_n0, __defer_n1, __defer_n2, __defer_n3\);$' \
           '^    if \(\(__defer_ix == 1i64\)\) \{$'; do
    n=$(count "$pat")
    if [ "$n" -lt 1 ]; then
        echo "FAIL: no body-level (indent 4) match for /$pat/ — the deferred read left the prelude region"
        grep -n '__defer' "${DUMPS[@]}" || true
        fail=1
    fi
done
# …and NOWHERE else. Any `__defer_` at a deeper indent is a per-row read.
n_deep=$(count '^     +(let __defer|if \(\(__defer_ix)')
if [ "$n_deep" -ne 0 ]; then
    echo "FAIL: $n_deep deferred read(s) below body level — the decision moved inside the nest"
    grep -n '__defer' "${DUMPS[@]}" || true
    fail=1
fi
# ONE discriminant per deferred query (via_rel, iter_step), read once each.
n_disc=$(count '^    let __defer_ix: i64')
if [ "$n_disc" -ne 2 ]; then
    echo "FAIL: $n_disc deferred discriminants (want 2, one per deferred query)"
    grep -n '__defer_ix' "${DUMPS[@]}" || true
    fail=1
fi

# ── the number is the MATERIALIZED length, not the input parameter's ───────
if ! grep -Fq 'let __defer_n0: i64 = (__rel_hot_sl).len();' "${DUMPS[@]}"; then
    echo "FAIL: the deferred base size is not the rel's own length"
    grep -n '__defer_n0' "${DUMPS[@]}" || true
    fail=1
fi
# …and in `via_rel`'s own fn, NEITHER read may be the rel's INPUT. `ls` is 9 rows
# long in both data sets while the rel is 2 and 7, so the wrong read is invisible on
# the mirror case and only the small one separates them — which is a property of the
# data, not of the compiler. Pinned here on the text instead.
for f in "${DUMPS[@]}"; do
    grep -Eq '^pub fn via_rel_run\(' "$f" || continue
    if grep -Eq '^    let __defer_n[01]: i64 = \(ls\)\.len\(\);' "$f"; then
        echo "FAIL: via_rel's deferred size reads the input parameter instead of the materialized rel"
        grep -n '__defer_n' "$f" || true
        fail=1
    fi
done
# `iter_step`'s base IS a slice param, so `(ls).len()` is the right read there —
# the two queries together say the rule is "the length of what this side actually
# is", not "always a rel" and not "always a parameter".
if ! grep -Fq 'let __defer_n0: i64 = (ls).len();' "${DUMPS[@]}"; then
    echo "FAIL: iter_step's deferred base size is not the slice param's own length"
    fail=1
fi
# The deferred read comes AFTER the rel's materialization and BEFORE the index.
for f in "${DUMPS[@]}"; do
    grep -Fq 'let __defer_n0: i64 = (__rel_hot_sl).len();' "$f" || continue
    l_mat=$(grep -n '__rel_hot_sl: &\[' "$f" | head -1 | cut -d: -f1)
    l_def=$(grep -n 'let __defer_n0:' "$f" | head -1 | cut -d: -f1)
    l_idx=$(grep -n 'hashmap_new::' "$f" | head -1 | cut -d: -f1)
    if [ -z "$l_mat" ] || [ -z "$l_def" ] || [ -z "$l_idx" ]; then
        echo "FAIL: cannot locate materialize/defer/index in $f (mat=$l_mat def=$l_def idx=$l_idx)"
        fail=1
    elif [ "$l_mat" -ge "$l_def" ] || [ "$l_def" -ge "$l_idx" ]; then
        echo "FAIL: the deferred read is not between the materialization and the index build (mat=$l_mat def=$l_def idx=$l_idx)"
        fail=1
    fi
done

# ── the deferral costs `prepare` nothing ──────────────────────────────────
# A deferred prepare measures nothing: no loop, no reporter binding, no `__n0`.
for f in "${DUMPS[@]}"; do
    grep -Eq '^pub fn (via_rel|iter_step)_prepare\(' "$f" || continue
    if grep -Eq '^[[:space:]]*(while|loop|for)[[:space:](]' "$f"; then
        echo "FAIL: a deferred prepare contains a loop — it would run the query it declines to plan:"
        sed -n '/^pub fn /,$p' "$f"
        fail=1
    fi
    if grep -Eq '^    let __n[01]: i64 = ' "$f"; then
        echo "FAIL: a deferred prepare measures a size it will not compare: $f"
        fail=1
    fi
    if ! grep -Eq 'defer_order: true' "$f"; then
        echo "FAIL: a deferred prepare does not record that the order is left to run: $f"
        fail=1
    fi
done

# ── the two points coexist and are distinguishable in the artifact ────────
n_pl=$(count '^    if \(\(__pl\.order_ix == 1i64\)\) \{$')
if [ "$n_pl" -ne 1 ]; then
    echo "FAIL: $n_pl prepared discriminants (want 1 — settled_by_prepare)"
    grep -n '__pl.order_ix' "${DUMPS[@]}" || true
    fail=1
fi
# The prepared query still DECIDES in `prepare`, unchanged by this slice — and it
# decides through the one cost function, over a table it carries (ADR 0024 S4k).
if ! grep -Fq 'let __ix: i64 = jc_order_pick((&__tb), __n0, __n1, __n2, __n3);' "${DUMPS[@]}"; then
    echo "FAIL: the prepared decision is no longer made in prepare"
    fail=1
fi
# A deferred plan reports NO facts: -1 in every slot, so nothing invites a stale
# comparison. It DOES carry the candidate table — the rule without the answer,
# which is what `run` prices the orders with (ADR 0024 S4k).
if ! grep -Eq 'defer_order: true, swap: false, order_ix: 0i64, base_n: \(-1i64\), step_n: \(-1i64\), n2: \(-1i64\), n3: \(-1i64\), tbl: JCTable \{' "${DUMPS[@]}"; then
    echo "FAIL: no deferred plan literal in the dump (or it does not carry the table run needs)"
    grep -n 'defer_order: true' "${DUMPS[@]}" || true
    fail=1
fi
if grep -Eq 'defer_order: true.*base_n: __n' "${DUMPS[@]}"; then
    echo "FAIL: a deferred plan carries measured facts — the deferred half leaked into the reusable surface"
    fail=1
fi

if [ "$fail" -ne 0 ]; then
    echo "---- trace ----"
    grep '^\[plan\]' "$TMPD/err" || true
    exit 1
fi
exit 0
