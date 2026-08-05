#!/usr/bin/env bash
# wide_key_index_tier_gate.sh LOGOSC TEST_LOGOS
#
# A TYPE THAT GAINS `Hash` MUST GAIN THE INDEX, AND THAT IS A FACT ABOUT THE
# DECISION, NOT ABOUT THE ROWS (ADR 0024 S6 — the value domain).
#
# `wql_join_wide_key_e2e.logos` asserts the ANSWERS of seven equi-joins. What it
# cannot assert is which tier produced them: a correct hash join and a nested loop
# return the same pairs by construction. Before 2026-08-05 those six joins took the
# LOOP tier — `el_index_key_ok` refused the 128-bit and packed widths — and every
# assertion in that fixture already passed. So the fixture is blind to the whole
# change, and this is where the change is visible.
#
# The subject is the one decision channel (`LOGOS_TRACE_PLAN`), the same one
# `join_order_key_fidelity_gate.sh` reads. `plan_walker` emits one line per join
# step naming the step's new row var, the tier, and THE TYPE THE INDEX IS KEYED IN:
#
#   [plan] b -> hash join on u128   (the key type is hashable — build once, probe per row)
#
# ⚠ THE KEY TYPE ON THE LINE IS ASSERTED, NOT JUST THE TIER. `hash join` alone
# would pass over an index keyed in `el_class_repr` — i64 — which is the ONE way
# this widening can be wrong: i128/u128 normalize into i64 by dropping the high
# bits, so a normalized index answers a u128 join with rows that are not equal.
# (The fixture's key values are picked to make that visible in the ROWS as well;
# the two checks are independent and both are wanted, because a truncating index
# on a data set without a collision is silent.)
#
# ⚠ THE `String` STEP IS THE CONTROL AND IT IS WHY THIS GATE CAN FAIL. Every check
# below is "a line matched", and a trace that said `hash join` for everything would
# satisfy all six. `String` is refused for a ground that did NOT move — an index
# would have to OWN each key and the borrow does not outlive the row var's loop
# body (E0597) — so it must still report `loop join`, from the same run of the same
# compiler over the same file. Six admissions and one refusal, or this exits 1.
#
# Exit: 0 all seven decided as stated · 1 a decision moved · 2 could not look.
set -euo pipefail

if [ "$#" -ne 2 ]; then
    echo "usage: $0 LOGOSC TEST_LOGOS" >&2
    exit 2
fi
LOGOSC="$1"
TEST_LOGOS="$2"

if [ ! -x "$LOGOSC" ];   then echo "FAIL: no logosc at $LOGOSC" >&2;      exit 2; fi
if [ ! -f "$TEST_LOGOS" ]; then echo "FAIL: no fixture at $TEST_LOGOS" >&2; exit 2; fi

TMPD=$(mktemp -d)
trap 'rm -rf "$TMPD"' EXIT

if ! LOGOS_TRACE_PLAN=1 "$LOGOSC" "$TEST_LOGOS" -o "$TMPD/test.o" 2>"$TMPD/err"; then
    echo "FAIL: the fixture did not compile — no decision was measured."
    sed -n '1,20p' "$TMPD/err"
    exit 2
fi

# ⚠ A FLOOR ON THE CHANNEL ITSELF. A trace that printed nothing — the variable
# renamed, the call moved, the steps never reached — makes every grep below fail
# to find a WRONG line just as reliably as it makes them fail to find a right one,
# and "no join decisions at all" would then read as six clean refusals.
N_JOIN=$(grep -c -- '-> \(hash\|loop\|tree\|sort-merge\) join on ' "$TMPD/err" || true)
if [ "$N_JOIN" -lt 7 ]; then
    echo "FAIL: the plan channel reported $N_JOIN join decisions, want >= 7 — the"
    echo "      trace is not carrying this file's steps, so nothing below was measured."
    sed -n '1,20p' "$TMPD/err"
    exit 2
fi

fail=0

# ── THE SIX WIDENED KEYS: hash tier, keyed in the type's OWN type ────────────
# var → declared key type. The vars are the JOINED-side ones (`plan_trace` names a
# step by its new row var), and the fixture gives every query its own pair so a
# line can be attributed.
check_step() {   # check_step <var> <type> <tier>
    local var="$1" ty="$2" tier="$3"
    if ! grep -q -- "^\[plan\] ${var} -> ${tier} on ${ty}   (" "$TMPD/err"; then
        echo "FAIL: the step on \`${var}\` (key \`${ty}\`) does not report \`${tier} on ${ty}\`."
        grep -- "^\[plan\] ${var} ->" "$TMPD/err" || echo "      (no decision reported for \`${var}\` at all)"
        fail=1
    fi
}

check_step b u128   "hash join"
check_step d i128   "hash join"
check_step f u56    "hash join"
check_step h i56    "hash join"
check_step j u24    "hash join"
check_step n i24    "hash join"

# ── THE CONTROL ─────────────────────────────────────────────────────────────
check_step q String "loop join"

if [ "$fail" -ne 0 ]; then
    echo ""
    echo "  The tier is \`join_sel::step_cascade\`, whose \`hash\` conjunct is"
    echo "  \`self_ident == EL_ID_OK && el::el_index_key_ok(name)\`. A widened type"
    echo "  that reports \`loop join\` has lost its \`Hash\`/\`Eq\` instance or been"
    echo "  refused by \`el_index_key_ok\` again; one that reports \`hash join on i64\`"
    echo "  is being indexed in the CLASS REPRESENTATIVE, which drops the high bits"
    echo "  of a 128-bit key and matches rows that are not equal."
    exit 1
fi

echo "PASS: 6 widened key types (u128 i128 u56 i56 u24 i24) reach the HASH tier"
echo "      keyed in their own type, and the \`String\` control still reports the"
echo "      LOOP tier on the same run."
exit 0
