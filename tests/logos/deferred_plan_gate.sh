#!/usr/bin/env bash
# deferred_plan_gate.sh LOGOSC TEST_LOGOS
#
# THE SECOND DECISION POINT (ADR 0024 S4j). The fixture asserts behaviour — the
# rows, their order, the pull count, and that ONE plan drives different sides on
# different data — but four properties decide whether the deferral is worth having
# at all, and none of them is visible from the outside:
#
#   • THE PER-ROW COST DID NOT MOVE. The `len()` reads and the discriminant binding
#     sit at BODY LEVEL (indent 4), and NO `__defer` read may stand at loop depth > 0
#     — measured by braces, not by column, because a four-nest chain's later tests sit
#     at indents 8 and 12 while still standing above every loop (ADR 0024 S4m; the
#     column rule would have rejected `via_rel3`, and the corpus had no such query for
#     it to bite on). A compiler that put the comparison inside the nest returns
#     identical rows and passes every behavioural assertion while paying per row.
#
#   • THE GROUND NAMES THE ANTECEDENT THAT ACTUALLY FAILED. A deferral is forced by
#     the side whose size point (1) cannot have; a side that is a PARAMETER is not
#     that side, and saying so of both is a false justification, which in this project
#     ranks with a wrong row. The gate asserts the composed ground names each group.
#
#   • THE EXCLUDED SURFACE ANSWERS "I CANNOT". `order_ix`, `cost_of` and `margin` are
#     not answerable on a plan that measured nothing, and their answers must be
#     distinguishable from real ones (-1 `no such candidate`, -2 `no facts here`).
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
# ⚠ THE GROUND MUST NAME THE SIDE THAT LOST ITS NUMBER, AND THE SIDE THAT DID NOT.
# This rule used to be `grep 'LENGTHS OF RELATIONS THIS QUERY MATERIALIZES ANYWAY'`,
# which is the sentence the emitter used to record — and it was FALSE for
# `via_rel(ls, rs)`: `rs` is a slice parameter, `size_expr_of` measures it at point (1)
# without complaint, and `run` reads `(rs).len()`. The gate asserted the presence of a
# lie. It now asserts the two halves of the accurate ground, which is strictly more
# than one substring: WHICH source has no number before the query runs, and WHICH one
# `prepare` could have had — plus the antecedent that moved the second one.
if ! grep -q '`__rel_hot_sl` is a `rel` BLOCK OF THIS QUERY' "$TMPD/err"; then
    echo "FAIL: the deferred ground does not name the source that has no size at point (1)"
    fail=1
fi
# ⚠ AND THE REMEDY MUST BE ONE THE NAMED SOURCE CAN TAKE (ADR 0024 S4n). The tail
# read "Declaring a `size` operation on the source(s) above would move the whole
# decision back to `prepare`" whenever ANY side was measurable — with no test of
# whether the side that FAILED has a declaration surface at all. `hot` is a parse-side
# `rel` block and `rel_size_fn` is written only by `plan_walker::reg_native` from a
# natspec, so that declaration cannot be written for it anywhere. Two rules, because
# both halves can regress independently: the unactionable advice must be gone, and the
# ACTIONABLE one must still be given where it applies (`iter_step`'s `s` declares its
# rows through an `impl` and can declare its size the same way).
if grep -q 'Declaring a `size` operation on the source(s) above' "$TMPD/err"; then
    echo "FAIL: the undifferentiated remedy is back — it advises a declaration a `rel` block cannot carry"
    fail=1
fi
if ! grep -q '`__rel_hot_sl` — there is NOTHING TO DECLARE A SIZE ON' "$TMPD/err"; then
    echo "FAIL: the `rel`-block deferral records no remedy, or one not tied to the source that forced it"
    fail=1
fi
if ! grep -q '`__rel_s_sl` — declaring a size is ACTIONABLE for it' "$TMPD/err"; then
    echo "FAIL: a source that CAN declare a size is not told so — the remedy was derived away instead of derived"
    fail=1
fi
if ! grep -q '`prepare` COULD have measured `rs`' "$TMPD/err"; then
    echo "FAIL: the deferred ground claims no side was measurable — `rs` is a slice parameter and one of two was"
    fail=1
fi
if ! grep -q 'may not mix a fact from point (1) with one from point (2)' "$TMPD/err"; then
    echo "FAIL: the deferred ground does not state WHY the measurable side is read at point (2) anyway"
    fail=1
fi
if grep -q 'both sizes are LENGTHS OF RELATIONS' "$TMPD/err"; then
    echo "FAIL: the false ground is back — a side that is a PARAMETER is not a relation this query materializes"
    fail=1
fi
# WHICH RELATION SUPPLIED THE NUMBER, where the read sits relative to the join, and
# WHAT EACH SIDE IS — the annotation comes from the same classifier the decision used.
if ! grep -Eq '^\[plan\] x -> sizes deferred to run on r .*\(__rel_hot_sl\)\.len\(\) \[a relation the prelude materialized[^]]*\] vs \(rs\)\.len\(\) \[an input the caller passed in[^]]*\]' "$TMPD/err"; then
    echo "FAIL: the deferred trace does not name the two relations it will compare, each with what it is"
    grep '^\[plan\].*deferred' "$TMPD/err" || true
    fail=1
fi
if ! grep -q 'after the prelude, before the join.s index' "$TMPD/err"; then
    echo 'FAIL: the deferred trace does not say WHERE in `run` the number becomes free'
    fail=1
fi
# ⚠ THE DISCLOSURE MUST COVER THE WHOLE EXCLUDED SURFACE, not the two members S4j
# happened to name. S4k added `order_ix` and `cost_of` and the sentence stayed at
# `agrees`/`margin`, so a caller was told exactly which members to distrust and the
# list was short by the two just added. It is `prepared::defer_not_held()` now — one
# copy, naming the members AND the values they answer with.
for disc in '`order_ix` is -1' \
            '`cost_of` and `margin` return `join_cost::JC_NOFACT()`' \
            '`agrees` is vacuously true' \
            '`settled()` is false'; do
    if ! grep -Fq "$disc" "$TMPD/err"; then
        echo "FAIL: the deferred ground does not disclaim the re-checkable surface: missing /$disc/"
        fail=1
    fi
done

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

# ── PER-ROW COST: the reads and the branch sit above every loop ────────────────
# The SIZE READS and the discriminant BINDING are still anchored at exactly four
# spaces: they are the prelude region's own statements and there is no legal shape in
# which one of them is nested.
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
# ⚠ THE BRANCH IS MEASURED BY LOOP NESTING, NOT BY COLUMN. This rule read
# `^     +(let __defer|if \(\(__defer_ix)` — anything past indent 4 is a per-row read
# — which is right for TWO candidates and wrong for three: `via_rel3` carries four
# nests, so its chain tests `__defer_ix == 3` at indent 4 and then 2 and 1 inside the
# `else` arms at indents 8 and 12, every one of them still above every loop. S4k had
# already rewritten the MULTI gate for exactly this and this rule was left on
# indentation, where it asserted something false about a shape the corpus did not yet
# contain. What it MEANS is "the decision is not paid per row", so it counts braces
# and flags a `__defer` read at loop depth > 0 — which is strictly stronger than the
# column test on the shape that matters: a read at indent 4 INSIDE a loop body (a
# `while` whose brace opened earlier) was invisible to the old rule and is caught here.
for f in "${DUMPS[@]}"; do
    bad=$(awk '
        /^[[:space:]]*(while|loop|for)[[:space:](]/ { pending = 1 }
        {
            line = $0
            if (loopdepth > 0 && line ~ /__defer/) { printf "%d: %s\n", NR, line }
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
        echo "FAIL: a deferred read or the discriminant is INSIDE a loop in $f:"
        echo "$bad"
        fail=1
    fi
done
# ONE discriminant per deferred query (via_rel, via_rel3, iter_step), read once each.
n_disc=$(count '^    let __defer_ix: i64')
if [ "$n_disc" -ne 3 ]; then
    echo "FAIL: $n_disc deferred discriminants (want 3, one per deferred query)"
    grep -n '__defer_ix' "${DUMPS[@]}" || true
    fail=1
fi
# …and the THREE-SOURCE one carries the nested chain the rule above had to be fixed
# for: four nests, so three tests, two of them past indent 4.
for f in "${DUMPS[@]}"; do
    grep -Eq '^pub fn via_rel3_run\(' "$f" || continue
    n_ch=$(grep -Ec '^ +if \(\(__defer_ix == [0-9]+i64\)\) \{$' "$f" || true)
    if [ "$n_ch" -ne 3 ]; then
        echo "FAIL: $n_ch order tests in via_rel3_run (want 3: candidates 1..3, with 0 as the final else)"
        grep -n '__defer_ix' "$f" || true
        fail=1
    fi
    n_nested=$(grep -Ec '^ {5,}if \(\(__defer_ix == [0-9]+i64\)\) \{$' "$f" || true)
    if [ "$n_nested" -lt 1 ]; then
        echo "FAIL: via_rel3_run has no branch past indent 4 — the artifact the loop-nesting rule exists for is gone"
        fail=1
    fi
done

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
    grep -Eq '^pub fn (via_rel|via_rel3|iter_step)_prepare\(' "$f" || continue
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
# ⚠ `order_ix: -1i64`, NOT 0. A fixed plan HAS an order and it is the query's own, so
# 0 is a fact there. A deferred plan names none — `run` names one per call — and 0 was
# the plan claiming a decision it never took, which is what a caller re-deriving the
# choice from `order_ix`/`cost_of` was handed (measured: order_ix 0, both costs 0,
# executed nest candidate 1). -1 is the same "not held" the fact fields on this very
# line already carry.
if ! grep -Eq 'defer_order: true, swap: false, order_ix: \(-1i64\), base_n: \(-1i64\), step_n: \(-1i64\), n2: \(-1i64\), n3: \(-1i64\), tbl: JCTable \{' "${DUMPS[@]}"; then
    echo "FAIL: no deferred plan literal in the dump (or it names an order / does not carry the table run needs)"
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
