#!/usr/bin/env bash
# incr_eligibility_gate.sh LOGOSC TEST_LOGOS
#
# THE POPULATION OF INCREMENTAL QUERIES IS DERIVED, AND THIS IS WHAT MAKES THAT
# CHECKABLE.
#
# `deem!` emits the incremental handle UNCONDITIONALLY-WHEN-ELIGIBLE. Nothing in
# the surface asks for it — the `deem` item head has no attribute slot and the
# query DSL has no modifier slot, so every opt-in spelling would be a grammar
# change — so the set of queries that GET a handle is a derivation, and a
# derivation nobody can read is a list nobody wrote down.
#
# The companion fixture (`pass/wql_incr_eligibility_matrix.logos`) pins the
# POSITIVE direction on its own: the eligible query's three derived fns are
# called there, so an emitter that stopped emitting them makes it fail to
# compile. It cannot pin the NEGATIVE direction, because a query whose handle is
# absent is a query with nothing to name. This gate reads the decision itself off
# `LOGOS_TRACE_PLAN` and asserts, per query:
#
#   • the VERDICT (EMITTED / declined), and
#   • that the GROUND names the antecedent that actually failed — not merely that
#     something failed. A refusal whose ground is generic is the one that never
#     gets revisited, and this project has already paid for a justification that
#     drifted from its mechanism.
#
# ⚠ AND THE POPULATION IS PINNED BY COUNT, NOT ONLY BY MEMBERSHIP. Checking that
# each expected query appears is an EXISTENTIAL over the trace and is satisfied by
# a trace with extra lines in it — but worse, it is satisfied when a query stops
# being walked at all only if that query is not in the list. So the total number
# of `[plan] incremental ->` lines is asserted too: a query that vanishes from the
# walk, or an eighth that appears from nowhere, is caught. That is the "predict
# the COUNT" rule this corpus enforces on its own suites.
#
# ⚠⚠ AND A GATE WHOSE VERDICT IS SILENCE MUST PROVE IT CAN SPEAK. Every check
# below is a `grep` over a trace; a renamed query, a reworded ground, a changed
# trace prefix and an empty capture all deliver the same "no violation found" as a
# genuinely clean run. So the same matcher is run, in the same invocation, over a
# CANARY trace broken four ways, and every one of the four must be named back. If
# the canary passes, this gate is not measuring anything and it fails.
set -uo pipefail

LOGOSC="$1"
TEST_LOGOS="$2"

TMPD=$(mktemp -d)
trap 'rm -rf "$TMPD"' EXIT

TRACE="$TMPD/trace.txt"
LOGOS_TRACE_PLAN=1 "$LOGOSC" "$TEST_LOGOS" -o "$TMPD/out.o" 2>"$TMPD/all.err" >/dev/null
RC=$?
if [ "$RC" != 0 ]; then
    echo "FAIL: the fixture must COMPILE — declining the incremental form is not a"
    echo "      refusal of the query, and if it ever became one this is where it shows."
    echo "      logosc exit $RC:"
    head -40 "$TMPD/all.err"
    exit 1
fi
grep -F '[plan] incremental ->' "$TMPD/all.err" > "$TRACE"

# ── the expected table: query, verdict, a phrase its GROUND must contain ─────
# The phrase is the ANTECEDENT, chosen so that a ground reworded into a generic
# "not supported" stops matching. Deliberately not the whole sentence: this gate
# pins what the ground is ABOUT, and `wql_incr_eligibility_matrix.logos` carries
# the prose.
EXPECT=(
  "ok_basic|EMITTED|one bare scan"
  "no_join|declined|JOIN CHAIN"
  "no_where|declined|PRE-GROUP \`where\`"
  "no_order|declined|\`order by\`"
  "no_limit|declined|\`order by\`"
  "no_strkey|declined|GROUP KEY is \`str\`"
  "no_rowsel|declined|\`select\` reaches a name"
)

# Check one expected row against a trace file. Echoes one violation line per
# failure and nothing when the row holds — so the SAME function serves the real
# trace and the canary, which is what makes the canary meaningful.
check_rows() {
    local file="$1"; shift
    local row name verdict phrase line
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
        # ⚠ INTO A FILE, THEN MATCH IT — never `printf … | grep -q`. Under
        # `set -o pipefail` grep exits at the first match, the writer takes
        # SIGPIPE 141, and pipefail reports the MATCH as a failed pipeline. That
        # is a recorded lesson in this corpus and `logos_00_gate_lint` caught
        # this very line writing it again.
        printf '%s' "$line" > "$file.line"
        if ! grep -qF -- "$phrase" "$file.line"; then
            echo "VIOLATION: the ground for '${name}' does not name '${phrase}': ${line}"
        fi
    done
}

VIOL=$(check_rows "$TRACE" "${EXPECT[@]}")

# ── the POPULATION, by count ────────────────────────────────────────────────
WANT_N=${#EXPECT[@]}
GOT_N=$(grep -c -F '[plan] incremental ->' "$TRACE")
if [ "$GOT_N" != "$WANT_N" ]; then
    VIOL="${VIOL}
VIOLATION: the walk reported ${GOT_N} incremental decisions, expected ${WANT_N} —
           a query that stops being walked leaves no trace line and no failure
           anywhere else in the corpus."
fi

# ── THE CANARY: the same matcher, over a trace broken four ways ─────────────
# 1. a MISSING query (no_join has no line at all);
# 2. a FLIPPED verdict (no_where reads EMITTED);
# 3. a GENERIC ground (no_strkey's antecedent replaced by "not supported yet");
# 4. a RENAMED query (no_rowsel spelled no_rowsel2).
CAN="$TMPD/canary.txt"
{
  echo "[plan] incremental -> EMITTED on ok_basic   (one bare scan, group by, insert-only aggregates over self-contained types)"
  echo "[plan] incremental -> EMITTED on no_where   (one bare scan, group by, insert-only aggregates over self-contained types)"
  echo "[plan] incremental -> declined on no_order   (\`order by\` / \`limit\` / \`distinct\` act on the SNAPSHOT)"
  echo "[plan] incremental -> declined on no_limit   (\`order by\` / \`limit\` / \`distinct\` act on the SNAPSHOT)"
  echo "[plan] incremental -> declined on no_strkey   (not supported yet)"
  echo "[plan] incremental -> declined on no_rowsel2   (\`select\` reaches a name that is neither \`key\` nor an aggregate output)"
} > "$CAN"
CANV=$(check_rows "$CAN" "${EXPECT[@]}")
printf '%s' "$CANV" > "$TMPD/canary.violations"
for want in no_join no_where no_strkey no_rowsel; do
    if ! grep -qF -- "'${want}'" "$TMPD/canary.violations"; then
        echo "FAIL: THE GATE'S CANARY DID NOT FIRE for '${want}'."
        echo "      A broken trace passed the same matcher that judges the real one,"
        echo "      so a clean verdict from this gate means nothing. Canary output:"
        printf '%s\n' "$CANV"
        exit 1
    fi
done

if [ -n "$VIOL" ]; then
    echo "FAIL: the incremental-eligibility population does not match the derivation."
    printf '%s\n' "$VIOL"
    echo "--- traced decisions ---"
    cat "$TRACE"
    exit 1
fi
echo "OK: ${WANT_N} incremental decisions, each with the ground that decided it."
exit 0
