#!/usr/bin/env bash
# ceiling-probe.sh <probe-name> | --selftest
#
# What a hypothesis COULD close, and what it costs, WITHOUT making it correct.
#
# CEILING = ledger rows the crude edit closes. An UPPER bound.
# COST    = legal programs it refuses. A LOWER bound — a corpus refuses only
#           what it CONTAINS, and four days running the counter-example had to
#           be hand-written. COST 0 IS NOT A SAFETY CLAIM.
#
# ⚠ EVERY RUN GOES THROUGH `gate-run.sh` AND LANDS IN THE STORE. The previous
# version drove `ctest` directly, four times per probe, so:
#   · no probe run was ever recorded — the identity work (build hash, env in the
#     key, per-test history, build-to-build compare) applied to everything EXCEPT
#     the runs where the environment actually changes the verdicts;
#   · re-pricing the same probe on an unchanged tree re-ran 368 ledger rows and
#     837 legal programs from scratch, every time.
# Now the unarmed baseline is whatever the store already holds for this build —
# usually free — and the armed run is its own build identity (LOGOS_PROBE is in
# the key), so the second pricing of a probe costs nothing either.
#
# The verdicts are then a QUERY, not a diff of two temp files: rows that PASSED
# unarmed and FAILED armed. In the ledger half that is the ceiling; in the legal
# half it is the cost.
set -uo pipefail
cd "$(dirname "$0")/.." || exit 2
DB=${LOGOS_GATE_DB:-$PWD/build/gate-state/runs.db}
LEDGER=(-R '^logos_00_bc_admit_')
# ⚠ TWO SELECTIONS FOR THE LEGAL HALF, because ctest ANDs its filters and the
# `bc` label does not reach the spec dirs — and those are what caught an
# over-refusal on 2026-08-27 that L1 could not see.
LEGAL_A=(-L bc -L pass)
LEGAL_B=(-R '^logos_(25_spec|03_ownership|04_advanced)_pass')

_bid() { grep -oP 'build_id=\K\d+' "$1" | head -1; }

_measure() {   # $1 = log file; runs all three selections under the current env
    { bash scripts/gate-run.sh "${LEDGER[@]}"
      bash scripts/gate-run.sh "${LEGAL_A[@]}"
      bash scripts/gate-run.sh "${LEGAL_B[@]}"; } > "$1" 2>&1
}

NAME="${1:?usage: ceiling-probe.sh <probe-name> | --selftest}"

if [ "$NAME" = "--selftest" ]; then
    # ⚠ THE READER'S OWN KNOWN ANSWER. A reader that has never SEEN a row close
    # cannot tell a dead hypothesis from a broken reader, and on the first run
    # this one WAS broken. `selftest_refuse` refuses every recorded borrow, so
    # it must close ALL the ledger rows.
    out=$("$0" selftest_refuse 2>&1) || { echo "$out"; exit 1; }
    got=$(printf '%s' "$out" | grep -oP 'CEILING = \K\d+')
    tot=$(ctest --test-dir build -N "${LEDGER[@]}" 2>/dev/null | grep -oP 'Total Tests: \K\d+')
    if [ "${got:-0}" = "$tot" ] && [ "${tot:-0}" -gt 0 ]; then
        echo "ok  ceiling-probe: selftest_refuse closes $got/$tot rows — the reader sees closures"
        exit 0
    fi
    echo "FAIL ceiling-probe: selftest_refuse closed ${got:-none} of $tot — the READER is broken, not the tree"
    printf '%s\n' "$out" | head -8
    exit 1
fi

B=$(mktemp); A=$(mktemp)
echo "ceiling-probe: unarmed baseline (from the store where it is already there)"
_measure "$B"; BID_BASE=$(_bid "$B")
echo "ceiling-probe: armed run — LOGOS_PROBE=$NAME"
LOGOS_PROBE="$NAME" _measure "$A"; BID_ARMED=$(_bid "$A")
[ -z "${BID_BASE:-}" ] || [ -z "${BID_ARMED:-}" ] && {
    echo "ceiling-probe: could not read a build id from gate-run — refusing to report a number" >&2
    exit 2; }
[ "$BID_BASE" = "$BID_ARMED" ] && {
    echo "ceiling-probe: armed and unarmed resolved to the SAME build identity ($BID_BASE)." >&2
    echo "  LOGOS_PROBE is supposed to be part of the key; if it is not, every number" >&2
    echo "  below would be a comparison of a run with itself." >&2
    exit 2; }

FIRES=$(grep -c . /dev/null; true)
CLOSED=$(python3 scripts/gate_db.py delta "$DB" "$BID_BASE" "$BID_ARMED" logos_00_bc_admit_)
N=$(printf '%s' "$CLOSED" | grep -c . || true)
BROKE=$(python3 scripts/gate_db.py delta "$DB" "$BID_BASE" "$BID_ARMED" | grep -v '^logos_00_bc_admit_' || true)
C=$(printf '%s' "$BROKE" | grep -c . || true)

echo "ceiling-probe: '$NAME'  builds $BID_BASE (unarmed) -> $BID_ARMED (armed)"
echo "probe: CEILING = $N rows"
[ "$N" -ne 0 ] && printf '%s\n' "$CLOSED" | sed 's/^/probe:   /'
echo "probe: COST    = $C legal programs refused (a LOWER bound — the corpus refuses"
echo "probe:           only what it CONTAINS; write the counter-examples)"
[ "$C" -ne 0 ] && printf '%s\n' "$BROKE" | head -12 | sed 's/^/probe:   /'
if [ "$N" -eq 0 ] && [ "$C" -eq 0 ]; then
    echo "probe: no effect. ⚠ NOT a refutation until the site is proven live — check the"
    echo "probe:   fire count and the region's arrival count before recording a zero."
elif [ "$C" -ge "$N" ]; then
    echo "probe: ⛔ COST >= CEILING — it refuses at least as many legal programs as it"
    echo "probe:    closes rows. Exemptions might rescue it; the burden is showing WHICH."
else
    echo "probe: ✓ ceiling $N vs cost $C — worth an exemption analysis"
fi
echo "probe: ⚠ a CEILING bounds the COUNT, not the SET: diff these names against what"
echo "probe:   you predicted BY NAME. A matching count can be two errors cancelling."
rm -f "$A" "$B"
exit 0
