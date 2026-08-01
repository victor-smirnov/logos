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
#
# ⚠⚠ AND A RULE WHOSE VERDICT IS SILENCE MUST PROVE IT CAN STILL SPEAK. The two
# rules below that judge an EXTRACTED body (`impl_block_rules`) and a BRACE WALK
# (`loop_nesting_violations`) both deliver "clean" as an empty string, which is
# also what a renamed fn, a changed indent or a broken awk deliver. A canary dump
# — a real dump broken four ways — is pushed through those same two functions in
# the same run and every violation must be named back.
#
# ⚠⚠⚠ AND A RULE'S QUANTIFIER IS PART OF THE RULE (2026-08-01).
#
# `grep -Eq PATTERN "${DUMPS[@]}"` is an EXISTENTIAL. Two rules here were
# UNIVERSAL sentences written that way, and one of them is the rule that keeps
# the two decision points from drifting:
#
#   if ! grep -Eq 'self\.order_ix == self\.order_pick\(…\)' "${DUMPS[@]}"
#       "FAIL: agrees does not go through order_pick"
#
# MEASURED: rewriting TWO of the THREE emitted `agrees` bodies to compare against
# a hand-written `if (fresh.base_n < fresh.step_n) …` — exactly the drift the rule
# forbids — left this gate at EXIT 0 printing "OK: deferred plan — guarded blocks
# ran on 3/1/1/2/3 dumps". One surviving dump satisfied the ∃ and the ∀ was never
# asked. The canary could not cover it: the canary dump is ONE file and the rule
# was already satisfied by another.
#
# ⚠ AND THE GUARD ITSELF ADMITTED 3 OF 4. `^impl [A-Za-z]+Plan \{` does not match
# `impl ViaRel3Plan {` — the `3`. The three-source query, the one the whole
# loop-nesting argument (S4m) was rewritten for, was never judged by the
# agrees/margin/member rules at all; and `check_guard … "$n_impl" 3` was written
# at the OBSERVED 3, so the floor certified the blindness instead of catching it.
#
# SO THE RULES THAT JUDGE THE EMITTED ARTIFACT MOVED TO plan_dump_rules.py: a
# loop over the dumps that CARRY the artifact, bodies extracted by BRACE
# MATCHING (a `sed` range ends at the first `^    }$`, which is a guess about
# formatting and returns NOTHING when a fn is renamed), a violation as a NAMED
# ROW, and the whole census as ONE JSON VERDICT read by verdict.py — where a
# renamed field is exit 3 and not an empty shell variable. The canary dump goes
# through the SAME program in the same run and every injected violation must
# come back named.
#
# BLINDING MUTATION, RE-RUN: a wrapper that renames the emitted `margin` in the
# `--gen-dir` dumps. Was GREEN. Now RED three ways in one run:
#   "no `margin` body could be extracted … the rule below judged NOTHING" (x3),
#   "FAIL (CANARY NOT CAUGHT): a dump deliberately broken so that /margin
#    consults defer_order/ must be reported came back without it",
#   "FAIL: the 'margin body extracted' block ran on 0 dump(s), want >= 3".
set -euo pipefail

HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
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

# ⚠ THE TWO RULES THAT STOOD HERE — "the plan type carries defer_order /
# settled / order_pick" and "agrees re-derives through order_pick" — WERE
# EXISTENTIAL `grep`s OVER THE WHOLE DUMP SET AND ARE GONE. They are rules of
# plan_dump_rules.py now, applied to EVERY dump that carries an `impl …Plan`
# block and counted. Leaving the weaker copy here would be a second statement of
# the rule that no longer enforces it — which is how a justification comes to
# describe a mechanism it has drifted from.
# `agrees` answers about the PREPARED half only: it gates on `dyn_order`, and
# `defer_order` appears in exactly one method — `settled`, whose whole job is to
# disclose it. Asserted by EXTRACTING each method's body rather than grepping the
# file: the plan's recorded ground is a string literal in this very dump and its
# prose contains both "agrees" and "defer_order", so a file-wide match reads as a
# violation of a rule it is actually stating. (The prepared gate hit the same trap
# with the word "for".)
# ── THE RULES THAT JUDGE THE EMITTED ARTIFACT, IN ONE PLACE, UNIVERSALLY ─────
# plan_dump_rules.py loops over the dumps, extracts each `pub fn` body by BRACE
# MATCHING, and reports a violation as a NAMED ROW. Its whole census is one JSON
# verdict; verdict.py reads it, and a renamed field there is a FATAL exit 3, not
# an empty variable that a floor happens not to catch.
#
# FLOORS, MEASURED 2026-08-01 on `wql_deferred_plan_e2e`: 18 dumps, 4 carrying an
# `impl …Plan` block (the old shell guard saw 3 — it could not match
# `ViaRel3Plan`), 4 `agrees` bodies, 4 `margin` bodies, 3 deferred discriminants,
# 3 `__defer_n0` and 3 `__defer_n1` prelude reads, 3 deferred prepares, 1
# `via_rel3_run`, 1 `via_rel_run`, 2 dumps carrying the materialized-rel read.
VERDICT="$HERE/verdict.py"
if ! python3 "$VERDICT" --selftest; then
    echo "FAIL: the verdict parser did not pass its own selftest — every number"
    echo "      this gate reads would be read by it."
    exit 1
fi
set +e
python3 "$HERE/plan_dump_rules.py" --label real "${DUMPS[@]}" 2>"$TMPD/rules.err"
RULES_RC=$?
set -e
if [ "$RULES_RC" -ne 0 ]; then
    echo "FAIL: plan_dump_rules.py could not judge the dumps (exit $RULES_RC):"
    cat "$TMPD/rules.err"; exit 1
fi
grep '^plan-rules-violation: ' "$TMPD/rules.err" | sed 's/^plan-rules-violation: /FAIL: /' || true
set +e
python3 "$VERDICT" --file "$TMPD/rules.err" --prefix 'plan-rules-json:' \
    --label "the emitted plan dumps" \
    --eq violations 0 \
    --floor dumps 18 --floor impl 4 --floor agrees 4 --floor margin 4 \
    --floor defer_ix 3 --floor prelude_n0 3 --floor prelude_n1 3 \
    --floor deferred_prepare 3 --floor via_rel3 1 --floor via_rel 1 \
    --floor rel_read 2 --floor defer_ix_pick 3 --floor defer_ix_test4 2 \
    --eq r3_chain 3 --floor r3_nested 1
VRC=$?
set -e
if [ "$VRC" -ne 0 ]; then
    [ "$VRC" -eq 3 ] && echo "       ⚠ EXIT 3: the gate could not read its own verdict. Nothing above is evidence."
    fail=1
fi
# The discriminant count is an EQUALITY, not a floor: one per deferred query.
if ! python3 "$VERDICT" --file "$TMPD/rules.err" --prefix 'plan-rules-json:' \
        --label "the deferred discriminants" --eq defer_ix 3 --quiet; then
    echo "       (want exactly 3 — one per deferred query: via_rel, via_rel3, iter_step)"
    grep -n '__defer_ix' "${DUMPS[@]}" || true
    fail=1
fi

# ── THE CANARY: THE SAME PROGRAM, OVER A DUMP BROKEN FIVE WAYS ───────────────
# Every rule above delivers "clean" as the ABSENCE of a row, and a renamed fn, a
# changed indent or a broken extraction produce the same absence. So a real dump
# is copied and deliberately broken, and the SAME plan_dump_rules.py judges it.
# Each injected violation must be named back; if one is not, the gate reports
# ITSELF broken. WHAT IT RIDES: the same brace-matching extraction, the same
# rules, the same verdict document. WHAT IT DOES NOT RIDE: the compile that
# produced the dump — that is the `${#DUMPS[@]}` check and the floors above.
IMPL_DUMP=""
for f in "${DUMPS[@]}"; do
    grep -Eq '^impl [A-Za-z_][A-Za-z0-9_]*Plan \{' "$f" || continue
    [ -n "$IMPL_DUMP" ] || IMPL_DUMP="$f"
done
if [ -z "$IMPL_DUMP" ]; then
    echo "FAIL (CANARY unavailable): no dump carries an \`impl …Plan {\` block, so"
    echo "      the rules that judge \`agrees\` and \`margin\` were never run and"
    echo "      cannot be proven live."
    fail=1
else
    CAN="$TMPD/canary.gen.logos"
    awk '
        /^    pub fn agrees\(/ { print; print "        let _canary_a: bool = self.defer_order;"; ina=1; next }
        /^    pub fn margin\(/ { print; print "        let _canary_m: bool = self.defer_order;"; inm=1; next }
        ina && /^    \}$/ { ina=0 }
        inm && /^    \}$/ { inm=0 }
        ina && /if \(!self\.dyn_order\) \{/ { print "        // CANARY: the dyn_order gate was removed here"; next }
        # THE FIFTH BREAK: the prepared decision re-derived by hand instead of
        # through `order_pick`. This is the mutation that was GREEN, because the
        # rule was an existential grep over every dump at once.
        ina && /self\.order_ix == self\.order_pick\(/ {
            print "        return (self.order_ix == (if (fresh.base_n < fresh.step_n) { 0i64 } else { 1i64 }));"
            next }
        { print }
        END {
            print ""
            print "pub fn __canary_per_row(n: i64) -> i64 {"
            print "    let mut i: i64 = 0i64;"
            print "    while (i < n) {"
            print "        let __defer_n7: i64 = (n).len();"
            print "        i = i + 1i64;"
            print "    }"
            print "    return i;"
            print "}"
        }
    ' "$IMPL_DUMP" > "$CAN"
    set +e
    python3 "$HERE/plan_dump_rules.py" --label canary "$CAN" 2>"$TMPD/canary.rules.err"
    CRC=$?
    set -e
    cfail=0
    if [ "$CRC" -ne 0 ]; then
        echo "FAIL (CANARY): plan_dump_rules.py could not judge the canary dump:"
        cat "$TMPD/canary.rules.err"; fail=1; cfail=1
    fi
    while IFS='|' read -r expect what; do
        [ -n "$expect" ] || continue
        if ! grep -Fq "$expect" "$TMPD/canary.rules.err"; then
            echo "FAIL (CANARY NOT CAUGHT): a dump deliberately broken so that"
            echo "      /$expect/ must be reported came back without it ($what)."
            echo "      The rule cannot see its own violation, so its silence on the"
            echo "      real dumps is not evidence. THE GATE IS BROKEN, not the tree."
            grep '^plan-rules-violation: ' "$TMPD/canary.rules.err" | sed 's/^/        /' || echo "        <no violations at all>"
            fail=1; cfail=1
        fi
    done <<'EXPECTS'
agrees consults defer_order|the prepared answer claiming a re-decided half
agrees does not gate on dyn_order|the gate that scopes it to the prepared half
agrees does not re-derive the decision through `order_pick`|THE ∃/∀ HOLE
margin consults defer_order|a per-call decision has no distance to a flip
__defer_n7|a deferred read placed INSIDE a `while` body
EXPECTS
    # …and the canary must be judged AS A DUMP WITH THE ARTIFACT, or its five
    # violations would be five rules skipping a file they did not recognise.
    if ! python3 "$VERDICT" --file "$TMPD/canary.rules.err" \
            --prefix 'plan-rules-json:' --label "the canary dump" \
            --eq impl 1 --eq agrees 1 --eq margin 1 --floor violations 5 --quiet; then
        echo "FAIL (CANARY): the canary dump was not judged as a plan dump, or"
        echo "      fewer than the five injected violations were reported."
        fail=1; cfail=1
    fi
    if [ "$cfail" -eq 0 ]; then
        echo "OK: canary — a dump broken FIVE ways (defer_order in \`agrees\`, the"
        echo "    dyn_order gate deleted, \`agrees\` re-deriving the order by hand"
        echo "    instead of through \`order_pick\`, defer_order in \`margin\`, a"
        echo "    \`__defer\` read inside a loop) was named back by the same rules"
        echo "    that are silent on the real dumps."
    fi
fi

# ── the number is the MATERIALIZED length, not the input parameter's ───────
if ! grep -Fq 'let __defer_n0: i64 = (__rel_hot_sl).len();' "${DUMPS[@]}"; then
    echo "FAIL: the deferred base size is not the rel's own length"
    grep -n '__defer_n0' "${DUMPS[@]}" || true
    fail=1
fi
# (`via_rel`'s reads, the deferred prepares and the loop-nesting walk are now
#  rules of plan_dump_rules.py — UNIVERSAL over the dumps that carry the
#  artifact, counted, and canaried by the same program.)
# The deferred read comes AFTER the rel's materialization and BEFORE the index.
n_seq=0
for f in "${DUMPS[@]}"; do
    grep -Fq 'let __defer_n0: i64 = (__rel_hot_sl).len();' "$f" || continue
    n_seq=$((n_seq + 1))
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

# ── WHAT THE GUARDED BLOCKS ACTUALLY RAN ON ───────────────────────────────
# THE MINIMUM THIS GATE MUST OBSERVE. Each `|| continue` above is right for a
# dump that does not hold the artifact and catastrophic if NO dump does: the
# block's whole body is skipped and the gate passes on a compiler that emitted
# none of it. These are FLOORS at the values measured when they were written
# (`wql_deferred_plan_e2e`: 3 dumps with an impl block, 1 with `via_rel3_run`,
# 1 with `via_rel_run`, 2 carrying the materialized-rel read, 3 with a deferred
# prepare) — the dump layout may grow, it may not stop carrying these.
check_guard() {  # name got want
    case "$2" in
        ''|*[!0-9-]*)
            echo "FAIL: the '$1' block ran on '$2', which is NOT A NUMBER — a floor"
            echo "      that cannot be applied must never read as a floor that held."
            fail=1; return 0 ;;
    esac
    if [ "$2" -lt "$3" ]; then
        echo "FAIL: the '$1' block ran on $2 dump(s), want >= $3 — a guard that admits"
        echo "      nothing skips every assertion inside it, and this gate cannot tell"
        echo "      'the artifact is correct' from 'the artifact was never emitted'."
        fail=1
    fi
}
# The two per-dump blocks that remain in shell (they compare LINE NUMBERS inside
# one file, which is a per-file question and already universal). MEASURED
# 2026-08-01: 2 dumps carry the materialized-rel read.
check_guard 'materialize/defer/index order' "$n_seq" 2
# The trace channel this gate greps 20 rules out of must have SPOKEN. Each of
# those rules fails on absence, so a dead channel is caught — but it is caught 20
# times with 20 misleading messages; one floor says the real thing once.
# MEASURED: 50 `[plan]` lines. `grep -c` is a COUNT OF A PROSE CHANNEL and is
# left as one deliberately: the [plan] trace IS prose — it is the justification a
# human reads — and the rules above assert its SENTENCES. What a structured
# verdict would add here is a second spelling of the same absence.
n_plan=$(grep -c '^\[plan\]' "$TMPD/err" || true)
check_guard 'the [plan] trace channel' "$n_plan" 50

if [ "$fail" -ne 0 ]; then
    echo "---- trace ----"
    grep '^\[plan\]' "$TMPD/err" || true
    exit 1
fi
echo "OK: deferred plan — the rules that judge the emitted artifact ran"
echo "    UNIVERSALLY over every dump that carries it (plan_dump_rules.py), their"
echo "    whole census was read as a JSON verdict by verdict.py (which passed its"
echo "    own selftest first), $n_seq dumps carry the materialized-rel read,"
echo "    $n_plan trace lines, and the rules were proven live by a dump broken"
echo "    FIVE ways — including the hand-written re-derivation that the previous"
echo "    existential grep let through in two of three plans."
exit 0
