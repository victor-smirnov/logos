#!/usr/bin/env bash
# join_order_key_total_gate.sh LOGOSC TEST_LOGOS
#
# THE JOIN-ORDER LICENCE NAMES THE KEY'S ORDER (ADR 0024 S4k, constraint C1).
#
# The fixture asserts the ROWS: one answer for two data shapes that used to pick
# different nests. What it cannot assert is the shape of the DECISION — a plan that
# reached the right answer by accident (say, by measuring sizes that happened to
# select nest 0) would look identical from the outside. So what is gated here is the
# decision itself, in the two places it is recorded:
#
#   • THE VERDICT AND ITS GROUND on the one decision channel (`LOGOS_TRACE_PLAN`).
#     A refusal must name the FAILED ANTECEDENT — a sort key whose comparison is not
#     total — and its remedy, not merely report "fixed". "No reorder" and "a reorder
#     I could not license" are different facts about a plan.
#   • THE ARTIFACT: the f64-keyed query carries ONE nest and a plan whose
#     `dyn_order` is false, with the ground travelling into the plan as `why` so
#     `explain()` can answer a caller at run time.
#
# And the licence is removed only where it is unsound: the i64- and str-keyed
# queries over the SAME sources keep all four nests. `str` compares
# byte-lexicographically and IS total — the refusal is about the ORDER, not about
# "keys that are not integers".
#
# ⚠ ATTRIBUTION IS BY ROW VAR. `plan_trace` names a decision by its base var, so the
# fixture gives each query its own (`a` = f64, `p` = i64, `x` = str). A gate that
# grepped for a verdict without an owner would pass on three queries deciding the
# same way.
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

# ── THE f64 KEY: refused, with C1's failed antecedent named ────────────────
if ! grep -q '^\[plan\] a -> drive fixed on b' "$TMPD/err"; then
    echo "FAIL: the f64-keyed query does not report a fixed order — the licence is not being refused"
    grep '^\[plan\] a ->' "$TMPD/err" || true
    fail=1
fi
# The GROUND, clause by clause: the antecedent that failed (a TOTAL order), the
# mechanism (a NaN reaches neither disjunct, so the tiebreak never engages), the
# consequence that would follow (the answer would depend on the nest), and the
# remedy. A verdict without these is a decision nobody can argue with.
for clause in "C1's licence is that the sequence is TOTAL, not that a key exists" \
              'an f64/f32 key is only PARTIALLY ordered' \
              'a NaN compares false against every value including itself' \
              'the tiebreak is never reached' \
              'the answer would stay a fact about WHICH NEST collected the tuples' \
              'The remedy is a totally ordered key'; do
    if ! grep -Fq "$clause" "$TMPD/err"; then
        echo "FAIL: the refusal does not state /$clause/ — the ground is not the failed antecedent"
        fail=1
    fi
done
# The refusal precedes the derivation, so no search was run and none is claimed.
if grep -q '^\[plan\] a -> order search on b' "$TMPD/err"; then
    echo "FAIL: the f64-keyed query reports an order search it did not run"
    fail=1
fi

# ── THE TOTALLY ORDERED KEYS: unchanged, all four orders carried ───────────
for owner in 'p' 'x'; do
    if ! grep -q "^\[plan\] ${owner} -> drive 1 of 4 on " "$TMPD/err"; then
        echo "FAIL: the query based on \`${owner}\` does not carry four orders — a total key must keep the licence"
        grep "^\[plan\] ${owner} ->" "$TMPD/err" || true
        fail=1
    fi
    if ! grep -q "^\[plan\] ${owner} -> order search on .*6 permutations of 3 floatable sources enumerated, 4 admissible, 4 carried as nests" "$TMPD/err"; then
        echo "FAIL: the query based on \`${owner}\` does not report the same census as before the licence was narrowed"
        fail=1
    fi
done

# ── THE ARTIFACT: one nest for the f64 key, four for the others ────────────
for f in "${DUMPS[@]}"; do
    if grep -Eq '^pub fn qf_run\(' "$f"; then
        n_disc=$(grep -Ec '\(__pl\.order_ix == [0-9]+i64\)' "$f" || true)
        if [ "$n_disc" -ne 0 ]; then
            echo "FAIL: $n_disc order tests in qf_run — a refused reorder must emit no discriminant"
            fail=1
        fi
        n_base=$(grep -Ec '^ +let mut __i0: i64 = 0i64;$' "$f" || true)
        if [ "$n_base" -ne 1 ]; then
            echo "FAIL: $n_base base loops in qf_run (want 1: the query's own order)"
            fail=1
        fi
    fi
    for r in 'qi_run' 'qs_run'; do
        grep -Eq "^pub fn ${r}\(" "$f" || continue
        n_disc=$(grep -Ec '\(__pl\.order_ix == [0-9]+i64\)' "$f" || true)
        if [ "$n_disc" -ne 3 ]; then
            echo "FAIL: $n_disc order tests in ${r} (want 3: candidates 1..3 with 0 as the final else)"
            fail=1
        fi
    done
done

# ── THE GROUND TRAVELS INTO THE PLAN, not only onto the trace ──────────────
# `explain()` returns this literal, so the refusal is answerable at run time. It is
# also why the composed ground cannot live in a buffer local to the decision: it is
# written into the artifact after that fn has returned.
if ! grep -Fq 'return QfPlan { dyn_order: false,' "${DUMPS[@]}"; then
    echo "FAIL: qf_prepare does not return a non-dynamic plan"
    grep -n 'return QfPlan' "${DUMPS[@]}" || true
    fail=1
fi
if ! grep -F 'return QfPlan { dyn_order: false,' "${DUMPS[@]}" | grep -Fq 'why: "`order by` names a sequence, but C1'; then
    echo "FAIL: the refusal's ground is not carried in the plan's own why — explain() cannot state it"
    fail=1
fi

if [ "$fail" -ne 0 ]; then
    echo "---- trace ----"
    grep '^\[plan\]' "$TMPD/err" || true
    exit 1
fi
exit 0
