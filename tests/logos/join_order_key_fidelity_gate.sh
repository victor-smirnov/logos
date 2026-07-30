#!/usr/bin/env bash
# join_order_key_fidelity_gate.sh LOGOSC TEST_LOGOS
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
#     A refusal must name the FAILED CONDITION — the emitted comparison does not
#     realize the order the query named — the KEY TYPE that failed it, and the
#     remedy. "No reorder" and "a reorder I could not license" are different facts
#     about a plan.
#   • THE ARTIFACT: a refused query carries ONE nest and a plan whose `dyn_order` is
#     false, with the ground travelling into the plan as `why` so `explain()` can
#     answer a caller at run time.
#
# ⚠ THE AXIS IS FIDELITY, NOT TOTALITY, AND NOT SIGNEDNESS — the two controls are
# the point of this gate. `str` compares byte-lexicographically and `u32` widens
# into i64 without moving a value, so both keep all four orders. A `u64` key would
# have passed the totality test this replaced (its emitted comparison IS total) and
# sorted by the signed reinterpretation; it is refused where that reinterpretation
# is introduced instead — wql_udf_wide_int_ret_fail, not here.
#
# ⚠ AND THE DEFAULT-DENY BRANCH IS ASSERTED FROM THIS CALLER, because it used to be
# unreachable from it: every fallback in the class-based classifier returned INT, so
# `el_total_order` could only ever refuse f64 and the "every unnamed class" branch
# was dead code with a comment claiming it protected you. The licence now reads the
# key's DECLARED TYPE NAME, so a name the lattice does not admit arrives and is
# refused — query `qn`, whose key resolves to the struct `P`.
#
# ⚠ ATTRIBUTION IS BY ROW VAR. `plan_trace` names a decision by its base var, so the
# fixture gives each query its own (`a` = f64, `p` = i64, `x` = str, `u` = u32,
# `d` = unnamed). A gate that grepped for a verdict without an owner would pass on
# five queries deciding the same way.
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

# ── THE TWO REFUSALS: fixed order, each naming ITS OWN failed condition ─────
for owner in 'a' 'd'; do
    if ! grep -q "^\[plan\] ${owner} -> drive fixed on " "$TMPD/err"; then
        echo "FAIL: the query based on \`${owner}\` does not report a fixed order — the licence is not being refused"
        grep "^\[plan\] ${owner} ->" "$TMPD/err" || true
        fail=1
    fi
    # The refusal precedes the derivation, so no search was run and none is claimed.
    if grep -q "^\[plan\] ${owner} -> order search on " "$TMPD/err"; then
        echo "FAIL: the query based on \`${owner}\` reports an order search it did not run"
        fail=1
    fi
done

# The SHARED frame, clause by clause: the condition that failed (the EMITTED
# comparison realizing the named sequence), that totality is one of its conditions
# and not the whole of it, and the consequence that would follow.
for clause in "C1's licence is that the EMITTED comparison REALIZES that sequence" \
              "being total is one of that licence's conditions rather than the whole of it" \
              'the answer would stay a fact about WHICH NEST collected the tuples'; do
    if ! grep -Fq "$clause" "$TMPD/err"; then
        echo "FAIL: a refusal does not state /$clause/ — the ground is not the failed condition"
        fail=1
    fi
done

# The f64 refusal's OWN ground: the key TYPE that failed, the mechanism, the remedy.
# A verdict without these is a decision nobody can argue with.
for clause in 'This key (`f64`) fails it' \
              'an f64/f32 key is only PARTIALLY ordered' \
              'a NaN compares false against every value including itself' \
              'the tiebreak is never reached' \
              'The remedy is a key whose comparison is total'; do
    if ! grep -Fq "$clause" "$TMPD/err"; then
        echo "FAIL: the f64 refusal does not state /$clause/"
        fail=1
    fi
done

# The DEFAULT-DENY refusal's own ground — a different failed condition, so a
# different mechanism and a different remedy. This is the branch that could not fire
# from this caller before the licence read the key's declared type NAME.
for clause in 'This key (`P`) fails it' \
              'outside the EL scalar lattice' \
              'The default is deny' \
              'cannot inherit a licence resting on a comparison nobody has checked' \
              'The remedy is a key of an admitted scalar type'; do
    if ! grep -Fq "$clause" "$TMPD/err"; then
        echo "FAIL: the unnamed-key refusal does not state /$clause/ — default-deny is not being reached with its own ground"
        fail=1
    fi
done

# ⚠ AND THE TWO REFUSALS MUST NOT SHARE A GROUND. Both are C1, and a composed
# message that collapsed them would be a justification that misstates one of them.
if grep -Fq 'This key (`f64`) fails it: this type is outside' "$TMPD/err"; then
    echo "FAIL: the f64 refusal is carrying the default-deny ground"
    fail=1
fi

# ── THE FAITHFUL KEYS: unchanged, all four orders carried ───────────────────
# `p` = i64, `x` = str (byte-lexicographic), `u` = u32 through a UDF (a
# value-preserving widening — the control that says the axis is not signedness).
for owner in 'p' 'x' 'u'; do
    if ! grep -q "^\[plan\] ${owner} -> drive 1 of 4 on " "$TMPD/err"; then
        echo "FAIL: the query based on \`${owner}\` does not carry four orders — a faithfully ordered key must keep the licence"
        grep "^\[plan\] ${owner} ->" "$TMPD/err" || true
        fail=1
    fi
    if ! grep -q "^\[plan\] ${owner} -> order search on .*6 permutations of 3 floatable sources enumerated, 4 admissible, 4 carried as nests" "$TMPD/err"; then
        echo "FAIL: the query based on \`${owner}\` does not report the same census as before the licence was narrowed"
        fail=1
    fi
done

# ── THE ARTIFACT: one nest for a refused key, four for the others ───────────
for f in "${DUMPS[@]}"; do
    for r in 'qf_run' 'qn_run'; do
        grep -Eq "^pub fn ${r}\(" "$f" || continue
        n_disc=$(grep -Ec '\(__pl\.order_ix == [0-9]+i64\)' "$f" || true)
        if [ "$n_disc" -ne 0 ]; then
            echo "FAIL: $n_disc order tests in ${r} — a refused reorder must emit no discriminant"
            fail=1
        fi
        n_base=$(grep -Ec '^ +let mut __i0: i64 = 0i64;$' "$f" || true)
        if [ "$n_base" -ne 1 ]; then
            echo "FAIL: $n_base base loops in ${r} (want 1: the query's own order)"
            fail=1
        fi
    done
    for r in 'qi_run' 'qs_run' 'qu_run'; do
        grep -Eq "^pub fn ${r}\(" "$f" || continue
        n_disc=$(grep -Ec '\(__pl\.order_ix == [0-9]+i64\)' "$f" || true)
        if [ "$n_disc" -ne 3 ]; then
            echo "FAIL: $n_disc order tests in ${r} (want 3: candidates 1..3 with 0 as the final else)"
            fail=1
        fi
    done
done

# ⚠ THE u32 KEY IS STILL NORMALIZED, AND THE CAST MUST BE THE LOSSLESS ONE. The
# emitted key vector is the class representative (`Vec<i64>`), so what makes this
# query faithful is that `u32 as i64` moves no value. Assert the cast is emitted —
# if that widening ever became a truncating one, the licence above would be granted
# over a comparison that no longer realizes the key's order.
if ! grep -Fq '(((w32(u.g)) as i64))' "${DUMPS[@]}"; then
    echo "FAIL: the u32 key is not widened with an explicit \`as i64\` — the licence rests on that cast being value-preserving"
    grep -F 'w32(' "${DUMPS[@]}" | head -3 || true
    fail=1
fi

# ── THE GROUND TRAVELS INTO THE PLAN, not only onto the trace ──────────────
# `explain()` returns this literal, so the refusal is answerable at run time. It is
# also why the composed ground cannot live in a buffer local to the decision: it is
# written into the artifact after that fn has returned.
for pl in 'QfPlan' 'QnPlan'; do
    if ! grep -Fq "return ${pl} { dyn_order: false," "${DUMPS[@]}"; then
        echo "FAIL: the ${pl} prepare does not return a non-dynamic plan"
        grep -n "return ${pl}" "${DUMPS[@]}" || true
        fail=1
    fi
    if ! grep -F "return ${pl} { dyn_order: false," "${DUMPS[@]}" | grep -Fq 'why: "`order by` names a sequence, but C1'; then
        echo "FAIL: ${pl}'s refusal ground is not carried in the plan's own why — explain() cannot state it"
        fail=1
    fi
done

if [ "$fail" -ne 0 ]; then
    echo "---- trace ----"
    grep '^\[plan\]' "$TMPD/err" || true
    exit 1
fi
exit 0
