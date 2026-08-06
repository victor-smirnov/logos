#!/usr/bin/env bash
# incr_retraction_gate.sh LOGOSC TEST_LOGOS
#
# WHICH INCREMENTAL QUERIES GAIN A RETRACTION IS A DERIVATION, AND THIS IS WHAT
# MAKES IT CHECKABLE.
#
# `deem!` emits `<q>_apply` / `<q>_retract` UNCONDITIONALLY-WHEN-INVERTIBLE. The
# decision is `incr_invertible`'s and nothing in the surface asks for it, so the
# set of queries whose delta carries a WEIGHT is a derivation — and a derivation
# nobody can read is a list nobody wrote down. This is the same split the
# eligibility pair uses, on the axis one level in:
#
#   `incr_eligible`   → does this query get a HANDLE at all?      (channel:
#                        `[plan] incremental ->`, incr_eligibility_gate.sh)
#   `incr_invertible` → can that handle take a NEGATIVE weight?   (channel:
#                        `[plan] retraction ->`, this gate)
#
# The companion fixture (`pass/wql_incr_retract_matrix.logos`) pins the POSITIVE
# direction on its own: the invertible queries' `_retract` is called there, so an
# emitter that stopped emitting it makes that file fail to compile. It cannot pin
# the NEGATIVE direction, because a query without a `_retract` is a query with
# nothing to name.
#
# ⚠ AND THE NEGATIVE DIRECTION IS THE ONE THAT MATTERS MOST HERE, because one of
# the two grounds for refusing is an OPEN DECISION rather than a missing feature.
# `(S+x)-x != S` over an f64 accumulator is unanswered and is not this emitter's
# to answer; if `incr_invertible` ever started admitting a float, the corpus
# would go on passing and a decision nobody took would have been taken. This gate
# is the thing that says so.
#
# ⚠ THE POPULATION IS PINNED BY COUNT, NOT ONLY BY MEMBERSHIP — a query that
# stops being walked leaves no line and no failure anywhere else.
#
# ⚠⚠ AND A GATE WHOSE VERDICT IS SILENCE MUST PROVE IT CAN SPEAK. The same
# matcher is run, in the same invocation, over a CANARY trace broken four ways,
# and every one must be named back. If the canary passes, this gate measures
# nothing and it fails.
set -uo pipefail

LOGOSC="$1"
TEST_LOGOS="$2"

TMPD=$(mktemp -d)
trap 'rm -rf "$TMPD"' EXIT

TRACE="$TMPD/trace.txt"
LOGOS_TRACE_PLAN=1 "$LOGOSC" "$TEST_LOGOS" -o "$TMPD/out.o" 2>"$TMPD/all.err" >/dev/null
RC=$?
if [ "$RC" != 0 ]; then
    echo "FAIL: the fixture must COMPILE — losing the retraction is not losing the"
    echo "      handle, and if the refusal ever became a hard error this is where it shows."
    echo "      logosc exit $RC:"
    head -40 "$TMPD/all.err"
    exit 1
fi
grep -F '[plan] retraction ->' "$TMPD/all.err" > "$TRACE"

# ── the expected table: query, verdict, a phrase its GROUND must contain ─────
# The phrase is the ANTECEDENT, chosen so that a ground reworded into a generic
# "not supported" stops matching.
EXPECT=(
  "inv_count_sum|EMITTED|invert exactly"
  "inv_usum|EMITTED|invert exactly"
  "no_fsum|declined|accumulates in \`f64\`"
  "no_avg|declined|accumulates in \`f64\`"
  "no_min|declined|cached extremum"
  "no_max|declined|cached extremum"
)

check_rows() {
    local file="$1"; shift
    local row name verdict phrase line rest
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
        # SIGPIPE 141, and pipefail reports the MATCH as a failed pipeline.
        printf '%s' "$line" > "$file.line"
        if ! grep -qF -- "$phrase" "$file.line"; then
            echo "VIOLATION: the ground for '${name}' does not name '${phrase}': ${line}"
        fi
    done
}

VIOL=$(check_rows "$TRACE" "${EXPECT[@]}")

# ── the POPULATION, by count ────────────────────────────────────────────────
# One line per INCREMENTALLY ELIGIBLE query — the invertibility question is only
# asked of a query that got a handle, so this count also pins that all six of
# these are still eligible.
WANT_N=${#EXPECT[@]}
GOT_N=$(grep -c -F '[plan] retraction ->' "$TRACE")
if [ "$GOT_N" != "$WANT_N" ]; then
    VIOL="${VIOL}
VIOLATION: the walk reported ${GOT_N} retraction decisions, expected ${WANT_N} —
           a query that stops being walked, or stops being incrementally
           eligible, leaves no line and no failure anywhere else."
fi

# ── THE CANARY: the same matcher, over a trace broken four ways ─────────────
# 1. a MISSING query (inv_usum has no line at all);
# 2. a FLIPPED verdict (no_fsum reads EMITTED — the D4 regression, exactly);
# 3. a GENERIC ground (no_min's antecedent replaced by "not supported yet");
# 4. a RENAMED query (no_max spelled no_max2).
CAN="$TMPD/canary.txt"
{
  echo "[plan] retraction -> EMITTED on inv_count_sum   (every aggregate is \`count\` or a NON-float \`sum\` — both invert exactly)"
  echo "[plan] retraction -> EMITTED on no_fsum   (every aggregate is \`count\` or a NON-float \`sum\` — both invert exactly)"
  echo "[plan] retraction -> declined on no_avg   (an aggregate accumulates in \`f64\`)"
  echo "[plan] retraction -> declined on no_min   (not supported yet)"
  echo "[plan] retraction -> declined on no_max2   (\`min\`/\`max\` cannot be inverted from a cached extremum)"
} > "$CAN"
CANV=$(check_rows "$CAN" "${EXPECT[@]}")
printf '%s' "$CANV" > "$TMPD/canary.violations"
for want in inv_usum no_fsum no_min no_max; do
    if ! grep -qF -- "'${want}'" "$TMPD/canary.violations"; then
        echo "FAIL: THE GATE'S CANARY DID NOT FIRE for '${want}'."
        echo "      A broken trace passed the same matcher that judges the real one,"
        echo "      so a clean verdict from this gate means nothing. Canary output:"
        printf '%s\n' "$CANV"
        exit 1
    fi
done

if [ -n "$VIOL" ]; then
    echo "FAIL: the retraction population does not match the derivation."
    printf '%s\n' "$VIOL"
    echo "--- traced decisions ---"
    cat "$TRACE"
    exit 1
fi
echo "OK: ${WANT_N} retraction decisions, each with the ground that decided it."
exit 0
