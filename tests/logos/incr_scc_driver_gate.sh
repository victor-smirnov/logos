#!/usr/bin/env bash
# incr_scc_driver_gate.sh LOGOSC MUTREC_FIXTURE SINGLE_FIXTURE
#
# ONE CROSS-EPOCH DRIVER PER RECURSIVE SCC — NOT ONE PER ADMITTED MEMBER.
#
# WHY THIS GATE EXISTS, AND IT IS A MEASUREMENT RATHER THAN A PRECAUTION.
# `emit_rel_fns` used to call `emit_scc_fn(..., incr=true, ...)` from INSIDE the
# loop over the SCC's members. That was indistinguishable from correct for as
# long as `stamp_rel_incr_shape` refused every SCC with more than one member: the
# loop body ran exactly once. P3c admits mutually recursive SCCs, so the same
# code would have emitted the item `__wql_<q>_scc<c>_i` ONCE PER MEMBER.
#
# ⚠⚠ AND THE CONTROL SAYS NO ANSWER CAN SEE IT. MEASURED 2026-08-07: the
# per-member loop was restored, the archives dropped, the whole stdlib rebuilt —
# and every fixture in the tree stayed GREEN, including both recursion fixtures
# and all four `fail/` refusal fixtures. The compiler accepts the second
# identical definition without a diagnostic. So the defect is real, it is
# invisible to every answer, and a fix for it that is pinned by nothing is a fix
# that comes back. What is asserted here is the ARTIFACT — the emitter's own
# report of what it emitted — because that is the only place the difference
# between one emission and two is visible at all.
#
# ⚠ AND A GATE WHOSE VERDICT IS SILENCE MUST PROVE IT CAN SPEAK. The check below
# is a count over a trace, and a renamed channel, a query that stopped being
# walked, and an empty capture all produce the same "no violation found" as a
# clean run. So the same counter is run, in the same invocation, over a CANARY
# trace broken three ways, and every one must be named back. If the canary passes
# clean, this gate is measuring nothing and it fails.
set -uo pipefail

LOGOSC="$1"
MUTREC="$2"
SINGLE="$3"

TMPD=$(mktemp -d)
trap 'rm -rf "$TMPD"' EXIT

CHAN='[plan] rel incr driver -> EMITTED on '

# Count the driver-emission lines per query in a trace file. Prints
# "<query> <count>" per distinct query, sorted — the whole verdict, so the same
# function serves the real trace and the canary.
per_query() {
    sed -n "s/^\[plan\] rel incr driver -> EMITTED on \([A-Za-z0-9_]*\) .*/\1/p" "$1" \
        | sort | uniq -c | awk '{print $2" "$1}' | sort
}

# ── the two real subjects ──────────────────────────────────────────────────
# MUTREC declares two queries (`oddq`, `evenq`), each over the SAME two-member
# SCC. SINGLE declares one query (`tc`) over a one-member SCC. Expected, in both
# cases, is exactly ONE driver per query — which is what makes this gate about
# the SCC rather than about the member count.
declare -A WANT
WANT["$MUTREC"]=$'evenq 1\noddq 1'
WANT["$SINGLE"]=$'tc 1'

VIOL=""
for F in "$MUTREC" "$SINGLE"; do
    ERR="$TMPD/$(basename "$F").err"
    LOGOS_TRACE_PLAN=1 "$LOGOSC" "$F" -o "$TMPD/out.o" 2>"$ERR" >/dev/null
    RC=$?
    if [ "$RC" != 0 ]; then
        echo "FAIL: the fixture must COMPILE — $F"
        head -40 "$ERR"
        exit 1
    fi
    grep -F -- "$CHAN" "$ERR" > "$TMPD/t.txt"
    GOT=$(per_query "$TMPD/t.txt")
    if [ "$GOT" != "${WANT[$F]}" ]; then
        VIOL="${VIOL}
VIOLATION: $(basename "$F") reported driver emissions
             got:  $(printf '%s' "$GOT" | tr '\n' ';')
             want: $(printf '%s' "${WANT[$F]}" | tr '\n' ';')
           One line per admitted recursive SCC. A count above the expectation is
           the per-member emission defect; a count below it is a query that
           stopped getting a cross-epoch driver at all."
    fi
done

# ── THE CANARY: the same counter, over a trace broken three ways ────────────
#  1. DUPLICATED — `oddq` emitted twice, which is the defect itself;
#  2. MISSING    — `evenq` has no line, which is the fix over-applied;
#  3. RENAMED    — a third query nobody expects appears.
CAN="$TMPD/canary.txt"
{
  echo "[plan] rel incr driver -> EMITTED on oddq   (one cross-epoch semi-naive driver per admitted recursive SCC)"
  echo "[plan] rel incr driver -> EMITTED on oddq   (one cross-epoch semi-naive driver per admitted recursive SCC)"
  echo "[plan] rel incr driver -> EMITTED on ghostq   (one cross-epoch semi-naive driver per admitted recursive SCC)"
} > "$CAN"
CANGOT=$(per_query "$CAN")
CANWANT=$'evenq 1\noddq 1'
if [ "$CANGOT" = "$CANWANT" ]; then
    echo "FAIL: THE CANARY PASSED. A trace with oddq duplicated, evenq missing and a"
    echo "      query nobody declared was accepted as equal to the expectation, so"
    echo "      the counter above is not comparing anything and every green run of"
    echo "      this gate so far means nothing."
    exit 1
fi

if [ -n "$VIOL" ]; then
    echo "SCC DRIVER GATE — one cross-epoch driver per recursive SCC${VIOL}"
    echo ""
    echo "See stdlib/mem/wql/rexpr_walk.logos, emit_rel_fns."
    exit 1
fi
exit 0
