#!/usr/bin/env bash
# join_order_multi_gate.sh LOGOSC TEST_LOGOS
#
# JOIN ORDER BEYOND THE FIRST PAIR (ADR 0024 S4k). The fixture asserts the ROWS from
# every carried order — that is what says the reorder is sound — but it cannot say
# anything about the orders that were REFUSED, and a refusal is where this slice's
# risk lives: an illegal permutation does not crash, it returns different rows. So
# what is gated here is the derivation's own account and the artifact it produced:
#
#   • THE CENSUS on the one decision channel (`LOGOS_TRACE_PLAN`): every
#     permutation enumerated, admitted or refused, and for a refusal WHICH
#     antecedent failed. A planner that only spoke when it acted would leave "no
#     line" meaning both "did not consider it" and "refused it".
#   • the SEARCH SUMMARY: how large the space was, how much of it is legal, how
#     much the artifact carries. The three numbers differ, and the case where they
#     differ (more legal orders than nests) is the one the fallback exists for.
#   • the COST FUNCTION IS NOT PER QUERY: generated code CALLS `join_cost`, it does
#     not spell the model. One copy is the whole claim of that module.
#   • ONE NEST PER CARRIED ORDER, and the discriminant read OUTSIDE EVERY LOOP —
#     checked by brace nesting, not by indentation, because a four-way branch chain
#     legitimately puts its later tests at deeper indents while still standing above
#     every loop.
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
# ⚠ `grep -c` over many files exits non-zero on a zero total, which under
# `set -euo pipefail` would kill this script with no message at all. Guarded.
count() { grep -Ec "$1" "${DUMPS[@]}" 2>/dev/null | awk -F: '{s+=$2} END{print s+0}' || true; }

# ── THE CENSUS: every permutation, with its verdict ─────────────────────────
# q3 is a three-source linear chain: 6 permutations, and every one of them is
# reported by the row-var sequence it names.
for seq in 'a>b>c' 'a>c>b' 'b>a>c' 'b>c>a' 'c>a>b' 'c>b>a'; do
    if ! grep -Eq "^\[plan\] ${seq} -> order (admitted|refused)" "$TMPD/err"; then
        echo "FAIL: permutation ${seq} was never reported — the search is not accounted for"
        fail=1
    fi
done
n_adm=$(grep -c '^\[plan\] [a-z0-9>_]* -> order admitted on ' "$TMPD/err" || true)
if [ "$n_adm" -lt 12 ]; then
    echo "FAIL: $n_adm admitted-order lines (want >= 12: 4 for q3 + 4 for q4 + 2 for q3self + 1 for q5 + …)"
    fail=1
fi

# ── ONE FIXTURE PER DERIVED CONSTRAINT, and the antecedent it names ─────────
# C4 — a pinned step's dependency is not bound at its depth (q4's anti join).
if ! grep -q 'C4: a pinned step (an anti join or a traversal) would stand before a row var' "$TMPD/err"; then
    echo "FAIL: no C4 refusal — the pinned-dependency constraint is not exercised or not named"
    fail=1
fi
# C5 — two predicates onto one position, and a step carries one `on` (q3).
if ! grep -q 'C5: two join predicates land on the same position' "$TMPD/err"; then
    echo "FAIL: no C5 refusal — the predicate-collision constraint is not exercised or not named"
    fail=1
fi
# C7 — an order that cannot be cheaper for any data (q3self's repeated source).
if ! grep -q "C7: this order's (size, role) sequence is the one an earlier candidate already has" "$TMPD/err"; then
    echo "FAIL: no C7 refusal — a cost-identical order is being emitted as a nest no data can select"
    fail=1
fi
# THE ARTIFACT BOUND — legal orders the artifact declines to carry, declined WHOLE.
if ! grep -q 'order admitted but not carried' "$TMPD/err"; then
    echo "FAIL: no over-bound order reported — the artifact bound is not exercised"
    fail=1
fi
if ! grep -q 'more admissible orders than an artifact carries nests (four): the reorder is declined whole' "$TMPD/err"; then
    echo "FAIL: the over-bound case does not state its fallback — a truncated search would look the same"
    fail=1
fi
# ⚠ AND THE PLAN OBJECT MUST SAY IT TOO (ADR 0024 S4n). The trace is a compile-time
# channel; the plan is what survives the compile, and it carried the OPPOSITE claim —
# "[no candidate table: nothing was considered, so `considered()` is 0]" appended to a
# ground that had just said eight orders were proved legal. This is the one case
# `JC_MAX_CAND` exists to make visible, so the bound's disclosure is asserted on the
# artifact and not only on the channel.
if ! grep -q '`enumerated()` 24, `proved()` 8, `considered()` 0' "$TMPD/err"; then
    echo "FAIL: the over-bound plan's own ground does not report what the search proved"
    fail=1
fi
if ! grep -q 'NO CANDIDATE TABLE IS CARRIED, and NOT because nothing was proved' "$TMPD/err"; then
    echo "FAIL: the over-bound plan does not distinguish 'proved none' from 'carried none'"
    fail=1
fi
if grep -q 'nothing was considered' "$TMPD/err"; then
    echo "FAIL: a plan whose axis was entered claims nothing was considered"
    fail=1
fi
# ⚠ AND THE SUCCESSOR SENTENCE MUST NOT APPEAR HERE EITHER. 'the order axis was never
# entered' replaced 'nothing was considered' and was asserted on 59 of 259 plans whose
# axis HAD been entered and had refused them. No emitter composes a census sentence
# now; `why::why_render` selects one from `why_axis`, which is a partition.
if grep -q 'the order axis was never entered' "$TMPD/err"; then
    echo "FAIL: a plan whose axis was entered claims the axis was never entered"
    fail=1
fi
# ⚠ AND THE STATE IS ON THE OBJECT, NOT ONLY IN THE PROSE. `axis()` == 2
# (`why::AX_SEARCHED`) and `ground()` == 20 (`why::WG_MAX_CAND`) — which is what lets
# a PROGRAM tell "the derivation ran and the artifact bound declined it" from "there
# was no join". `wql_plan_census_e2e` pins both numbers against the named functions.
n_ax2=$(grep -A1 -h 'pub fn axis(&self)' "${DUMPS[@]}" 2>/dev/null | grep -c 'return 2i32;' || true)
if [ "$n_ax2" -lt 1 ]; then
    echo "FAIL: the over-bound plan does not report axis() == AX_SEARCHED"
    grep -En -A1 'fn axis' "${DUMPS[@]}" || true
    fail=1
fi
n_g20=$(grep -A1 -h 'pub fn ground(&self)' "${DUMPS[@]}" 2>/dev/null | grep -c 'return 20i32;' || true)
if [ "$n_g20" -ne 1 ]; then
    echo "FAIL: $n_g20 plans record ground() == WG_MAX_CAND (want 1 — q5)"
    grep -En -A1 'fn ground' "${DUMPS[@]}" || true
    fail=1
fi
# `proved()` is the same fact where a PROGRAM reads it — a compile-time constant on
# the plan's impl, so `considered() == 0` stops meaning two different things.
n_pv8=$(grep -A1 -h 'pub fn proved(&self)' "${DUMPS[@]}" 2>/dev/null | grep -c 'return 8i64;' || true)
if [ "$n_pv8" -lt 1 ]; then
    echo "FAIL: the over-bound plan does not carry its census as proved() == 8"
    grep -En 'fn proved' "${DUMPS[@]}" || true
    fail=1
fi
# The census is the same TRIPLE the trace line prints — enumerated / proved /
# carried. Two numbers could not tell "the axis was never entered" from "entered and
# admitted nothing"; three can, and `q5` is the artifact where all three differ.
n_en24=$(grep -A1 -h 'pub fn enumerated(&self)' "${DUMPS[@]}" 2>/dev/null | grep -c 'return 24i64;' || true)
if [ "$n_en24" -lt 1 ]; then
    echo "FAIL: the over-bound plan does not carry enumerated() == 24"
    grep -En 'fn enumerated' "${DUMPS[@]}" || true
    fail=1
fi

# ── THE SEARCH SUMMARY: the three counts, and the model it priced with ──────
if ! grep -q '6 permutations of 3 floatable sources enumerated, 4 admissible, 4 carried as nests' "$TMPD/err"; then
    echo "FAIL: the three-source search does not report enumerated/admissible/carried"
    grep 'order search' "$TMPD/err" || true
    fail=1
fi
if ! grep -q '24 permutations of 4 floatable sources enumerated, 8 admissible, 1 carried as nests' "$TMPD/err"; then
    echo "FAIL: the four-source search does not report that it proved more than it carries"
    grep 'order search' "$TMPD/err" || true
    fail=1
fi
# The cost model states its weights AND its one assumption on the same line — a
# model whose selectivity guess is unnamed is a number nobody can argue with.
if ! grep -q 'a base SCAN weighs 2 per row, an index BUILD 4 per row, a PROBE 1 per row' "$TMPD/err"; then
    echo "FAIL: the trace does not state the cost model's weights"
    fail=1
fi
if ! grep -q 'no fact reports selectivity' "$TMPD/err"; then
    echo "FAIL: the cost model does not name its selectivity assumption"
    fail=1
fi
if ! grep -q 'pinned steps are unpriced' "$TMPD/err"; then
    echo "FAIL: the cost model does not disclose what it charges nothing for"
    fail=1
fi

# ── THE COST FUNCTION IS ONE COPY, IN STDLIB ───────────────────────────────
# Generated code CALLS it. A `fn jc_order_cost` in a dump would mean the model was
# spelled per query, which is one chance per query to drift from the account above.
n_def=$(count '^[[:space:]]*(pub )?fn jc_order_(cost|pick)\(')
if [ "$n_def" -ne 0 ]; then
    echo "FAIL: $n_def definition(s) of the cost function in generated code — the model must have exactly one copy"
    grep -n 'fn jc_order' "${DUMPS[@]}" || true
    fail=1
fi
for wanted in 'use logos.std.wql.join_cost;' \
              'pub tbl: JCTable' \
              'return jc_order_pick((&self.tbl)' \
              'return jc_order_cost((&self.tbl)' \
              'return jc_margin((&self.tbl)'; do
    if ! grep -Fq "$wanted" "${DUMPS[@]}"; then
        echo "FAIL: the plan type does not delegate to join_cost: missing /$wanted/"
        fail=1
    fi
done
# `agrees` re-derives the decision THROUGH the same entry point the emitted
# `prepare` used. A second hand-written comparison is how a plan comes to disagree
# with the branch it selected.
if ! grep -Fq 'return (self.order_ix == self.order_pick(fresh.base_n, fresh.step_n, fresh.n2, fresh.n3));' "${DUMPS[@]}"; then
    echo "FAIL: agrees does not re-derive through order_pick"
    grep -n 'agrees' "${DUMPS[@]}" || true
    fail=1
fi
# The emitted `prepare` decides through the same function, over a table it names.
if ! grep -Fq 'let __ix: i64 = jc_order_pick((&__tb), __n0, __n1, __n2, __n3);' "${DUMPS[@]}"; then
    echo "FAIL: a prepare fn does not pick the order through jc_order_pick"
    fail=1
fi
if ! grep -Eq 'let __tb: JCTable = JCTable \{ ncand: 4i64, nfl: 3i64, slot: \[' "${DUMPS[@]}"; then
    echo "FAIL: no four-candidate / three-source table literal — the plan does not carry the set it decided over"
    grep -n 'JCTable {' "${DUMPS[@]}" || true
    fail=1
fi

# ── FOUR NESTS, one per carried order, each driving a different source ──────
for f in "${DUMPS[@]}"; do
    grep -Eq '^pub fn q3_run\(' "$f" || continue
    n_disc=$(grep -Ec '\(__pl\.order_ix == [0-9]+i64\)' "$f" || true)
    if [ "$n_disc" -ne 3 ]; then
        echo "FAIL: $n_disc order tests in q3_run (want 3: candidates 1..3, with 0 as the final else)"
        grep -n 'order_ix' "$f" || true
        fail=1
    fi
    # Four outermost loops, and between them all three sources drive at least once.
    n_base=$(grep -Ec '^ +let mut __i0: i64 = 0i64;$' "$f" || true)
    if [ "$n_base" -ne 4 ]; then
        echo "FAIL: $n_base base loops in q3_run (want 4, one per carried order)"
        fail=1
    fi
    for src in 'as_' 'bs' 'cs'; do
        if ! grep -Eq "while \(__i0 < \(${src}\)\.len\(\)\)" "$f"; then
            echo "FAIL: no carried order drives from ${src} — the set is not the one the trace claims"
            fail=1
        fi
    done
done

# ── PER-ROW COST: the discriminant is never read inside a loop ──────────────
# Checked by BRACE NESTING, not by column. A four-way branch chain puts its later
# tests at indent 8 and 12 while still standing above every loop, so an indent
# assertion would have to be loosened to pass and would then stop meaning anything.
for f in "${DUMPS[@]}"; do
    bad=$(awk '
        /^[[:space:]]*(while|loop|for)[[:space:](]/ { pending = 1 }
        {
            line = $0
            if (loopdepth > 0 && (line ~ /__pl\.order_ix/ || line ~ /__defer_ix/)) {
                printf "%d: %s\n", NR, line
            }
            n = gsub(/\{/, "{", line)
            m = gsub(/\}/, "}", line)
            for (i = 0; i < n; i++) {
                stack[++top] = (pending && i == 0) ? 1 : 0
                if (stack[top]) loopdepth++
                pending = 0
            }
            for (i = 0; i < m; i++) {
                if (top > 0) { if (stack[top]) loopdepth--; top-- }
            }
            pending = 0
        }
    ' "$f")
    if [ -n "$bad" ]; then
        echo "FAIL: the join-order discriminant is read INSIDE a loop in $f:"
        echo "$bad"
        fail=1
    fi
done

if [ "$fail" -ne 0 ]; then
    echo "---- trace ----"
    grep '^\[plan\]' "$TMPD/err" || true
    exit 1
fi
exit 0
