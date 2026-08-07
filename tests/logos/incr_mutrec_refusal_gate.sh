#!/usr/bin/env bash
# incr_mutrec_refusal_gate.sh LOGOSC MUTREC_FIXTURE
#
# THE THREE `fail/wql_incr_mutrec_*` DOORS ARE SATISFIED BY A STRICTLY STRONGER
# REFUSAL, AND THAT WAS MEASURED RATHER THAN SUSPECTED.
#
# ⚠⚠ THE MEASUREMENT, 2026-08-07. `run_test.sh`'s fail mode asserts that logosc
# FAILS and that its stderr CONTAINS the expected string. The three doors expect
# `call to undefined function 'oddq_retract'` / `'evenq_retract'` /
# `'oddq_incremental'`. Restore P3b's `nmem != 1` clause in
# `stamp_rel_incr_shape` — i.e. refuse the mutually recursive SCC ENTIRELY, so
# the query gets no handle, no `_epoch`, no `_retract` and no `_incremental` —
# drop the archives, rebuild the whole stdlib, and ALL THREE DOORS GO ON PASSING.
# `pass/wql_incr_rel_mutrec_epochs.logos` is what reds (`unknown type 'OddqIncr'`).
#
# So a green `no_retract_odd` is NOT evidence that the RETRACTION clause refused.
# It is evidence that the name is absent, and the name is absent under every
# refusal that reaches the query — including one that withdraws the whole
# capability. The doors prove the fn is not emitted; nothing in the same
# measurement proves the surface was otherwise ELIGIBLE. That is the shape of
# "an oracle's SILENCE is not an answer": no branch for the case reads as
# agreement.
#
# THIS GATE IS THAT MISSING CONTROL, AND IT IS ONE COMPILE SO THE TWO FACTS
# CANNOT DRIFT APART. Over the SAME trace of the SAME compile it asserts, for
# BOTH members' queries:
#
#   `[plan] incremental -> EMITTED`  — the handle EXISTS. This is the eligible
#                                      control the `fail/` files cannot carry:
#                                      a file that fails to compile has no room
#                                      for a positive assertion.
#   `[plan] retraction   -> declined` — and the retraction, specifically, is the
#                                      thing withheld, on the RECURSION ground.
#
# Under the P3b restore the first pair vanishes and this gate reds. Under
# "disable `incr_retract_eligible`'s `rel_backed` clause" the second pair turns
# EMITTED and this gate reds — the same edit that makes the two `no_retract`
# doors compile (MEASURED: both go red as `fail/` tests, `outside_rel` stays
# green, which is what shows the two clauses are separable).
#
# ⚠ TWO QUERIES, NOT ONE, BECAUSE AN SCC OF N MEMBERS HAS N DOORS. `oddq`
# selects from `odd` and `evenq` from `even`; each reaches `incr_retract_eligible`
# through its own `rel_find_src`. Pinning one would leave the other's verdict
# unread.
#
# ⚠ AND A GATE WHOSE VERDICT IS SILENCE MUST PROVE IT CAN SPEAK. `grep` finding
# nothing, a renamed channel and a query that stopped being walked all look
# exactly like a clean run. The same matcher therefore runs, in this same
# invocation, over a CANARY trace broken four ways — a missing EMITTED, a
# retraction flipped to EMITTED, a reworded ground, and a query that vanished —
# and every one must be named back. A clean canary fails this gate.
set -uo pipefail

LOGOSC="$1"
MUTREC="$2"

TMPD=$(mktemp -d)
trap 'rm -rf "$TMPD"' EXIT

LOGOS_TRACE_PLAN=1 "$LOGOSC" "$MUTREC" -o "$TMPD/out.o" 2>"$TMPD/all.err" >/dev/null
RC=$?
if [ "$RC" != 0 ]; then
    echo "FAIL: the fixture must COMPILE — a mutually recursive SCC that stops"
    echo "      getting a handle is the P3b refusal returning, and the three"
    echo "      fail/wql_incr_mutrec_* doors would go on passing through it."
    echo "      logosc exit $RC:"
    head -40 "$TMPD/all.err"
    exit 1
fi
grep -F '[plan] incremental ->' "$TMPD/all.err" > "$TMPD/itrace.txt"
grep -F '[plan] retraction ->'  "$TMPD/all.err" > "$TMPD/rtrace.txt"

# query | verdict | a phrase the GROUND must contain (the ANTECEDENT, so a
# ground reworded into a generic "unsupported" stops matching).
IEXPECT=(
  "oddq|EMITTED|RECURSIVE \`rel\`"
  "evenq|EMITTED|RECURSIVE \`rel\`"
)
REXPECT=(
  "oddq|declined|support ITSELF through a cycle"
  "evenq|declined|support ITSELF through a cycle"
)

check_rows() {
    local file="$1"; shift
    local row name rest verdict phrase line
    for row in "$@"; do
        name="${row%%|*}"
        rest="${row#*|}"
        verdict="${rest%%|*}"
        phrase="${rest#*|}"
        line=$(grep -F -- " -> ${verdict} on ${name}   (" "$file")
        if [ -z "$line" ]; then
            echo "VIOLATION: no '${verdict}' verdict traced for query '${name}'"
            continue
        fi
        # ⚠ INTO A FILE, THEN MATCH — never `printf … | grep -q`. Under
        # `set -o pipefail` grep exits at the first match, the writer takes
        # SIGPIPE 141, and pipefail reports the MATCH as a failed pipeline.
        printf '%s' "$line" > "$file.line"
        if ! grep -qF -- "$phrase" "$file.line"; then
            echo "VIOLATION: the ground for '${name}' does not name '${phrase}': ${line}"
        fi
    done
}

# ⚠ THE NEWLINE IS LOAD-BEARING. `$(…)` strips the trailing newline, so a bare
# concatenation glues the last violation of the first axis onto the first of the
# second — and the canary counts LINES. Measured: four breakages reported as
# three, which would have hidden one real violation per run too.
VIOL=$(check_rows "$TMPD/itrace.txt" "${IEXPECT[@]}")
VIOL="${VIOL}
$(check_rows "$TMPD/rtrace.txt" "${REXPECT[@]}")"
VIOL=$(printf '%s' "$VIOL" | grep 'VIOLATION' || true)

# ── THE CANARY: the same matcher over a trace broken four ways ──────────────
#  1. `oddq`'s incremental EMITTED line is MISSING   (the P3b restore's shape);
#  2. `evenq`'s retraction says EMITTED, not declined (the disabled clause);
#  3. `oddq`'s retraction ground is REWORDED generically;
#  4. `evenq` is absent from the incremental trace entirely (never walked).
CI="$TMPD/can_i.txt"
CR="$TMPD/can_r.txt"
echo "[plan] incremental -> declined on oddq   (something else entirely)" > "$CI"
{
  echo "[plan] retraction -> declined on oddq   (not supported at this time)"
  echo "[plan] retraction -> EMITTED on evenq   (invert exactly)"
} > "$CR"
CANV=$(check_rows "$CI" "${IEXPECT[@]}")
CANV="${CANV}
$(check_rows "$CR" "${REXPECT[@]}")"
# Four independent breakages ⇒ at least four violation lines. Fewer means the
# matcher is not looking at what it claims to look at.
CANN=$(printf '%s' "$CANV" | grep -c 'VIOLATION')
if [ "$CANN" -lt 4 ]; then
    echo "FAIL: THE CANARY DID NOT SPEAK. A trace with oddq's handle missing,"
    echo "      evenq's retraction EMITTED, oddq's ground reworded and evenq"
    echo "      absent produced only ${CANN} violations (expected >= 4), so the"
    echo "      matcher above is not comparing anything and every green run of"
    echo "      this gate so far means nothing."
    printf '%s\n' "$CANV"
    exit 1
fi

if [ -n "$VIOL" ]; then
    echo "MUTREC REFUSAL GATE — the handle is EMITTED and the RETRACTION, specifically, is declined"
    printf '%s\n' "$VIOL"
    echo ""
    echo "See stdlib/mem/wql/rexpr_walk.logos, incr_retract_eligible, and"
    echo "stdlib/mem/wql/params.logos, stamp_rel_incr_shape."
    exit 1
fi
exit 0
