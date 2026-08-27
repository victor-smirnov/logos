#!/usr/bin/env bash
# ceiling-probe.sh <probe-name> — how many ledger rows COULD this mechanism close?
#
# Reads an UPPER BOUND off the whole acceptance population in ~32 s, so a
# hypothesis is killed or funded before anyone writes a correct fix for it.
# See include/logos/compiler/probe.hpp for what a ceiling probe is and why it is
# allowed to be wrong.
#
# ⚠ THE READER IS THE CORPUS ITSELF, not a new instrument. Every ledger row is
# a registered ctest test asserting THE DEFECT IS STILL THERE. So a row that the
# probe closes shows up as a FAILING test, and `ctest` names it. Nothing had to
# be built to read this; it had been sitting there since the per-row
# registration landed.
#
# ⚠ A ZERO IS NOT AN ANSWER UNTIL THE SITE IS PROVEN LIVE. A probe that never
# executes reports ceiling 0 and reads exactly like a refuted hypothesis. This
# script refuses such a run: no fires, no verdict.
set -uo pipefail
cd "$(dirname "$0")/.." || exit 2
ROOT=$PWD
NAME="${1:?usage: ceiling-probe.sh <probe-name> | --selftest}"

# ⚠ THE HARNESS'S OWN KNOWN ANSWER. A reader that has never SEEN a row close
# cannot tell a dead hypothesis from a broken reader — and on the very first
# run this reader WAS broken (a relative fire-log path, opened from ctest's
# working directory instead of this one, reported NEVER FIRED for a probe that
# had fired 193 times). `selftest_refuse` refuses every borrow the pass records,
# so it must close ALL of them.
if [ "$NAME" = "--selftest" ]; then
    # CEILING_ONLY: the selftest asks whether the READER sees closures; the
    # cost side would just re-measure that a total sabotage breaks everything.
    out=$(CEILING_ONLY=1 "$0" selftest_refuse 2>&1) || { echo "$out"; exit 1; }
    got=$(printf '%s' "$out" | grep -oP 'CEILING = \K\d+')
    tot=$(ctest --test-dir build -N -R "^logos_00_bc_admit_" 2>/dev/null | grep -oP "Total Tests: \K[0-9]+")
    if [ "${got:-0}" = "$tot" ] && [ "$tot" -gt 0 ]; then
        echo "ok  ceiling-probe: selftest_refuse closes $got/$tot rows — the reader sees closures"
        exit 0
    fi
    echo "FAIL ceiling-probe: selftest_refuse closed ${got:-none} of $tot — the READER is broken, not the tree"
    printf '%s\n' "$out" | head -5
    exit 1
fi
JOBS=$(nproc)
LEDGER_RE='^logos_00_bc_admit_'
# ⚠ THE PRICE HAS TWO SIDES. A ceiling alone is half a number: a probe can buy
# rows with a refusal that is simply WRONG. Measured 2026-08-27 — `callroot`
# priced at 4 and one of its rows closes only because the probe refuses
# `match *x { Cycle::Node(ref mut y) => ... }` over a Box, which is legal Rust;
# `refwhole` priced at 1 against FORTY legal-program refusals. Both were caught
# by hand, by agents who thought to look. Nobody should have to think to look.
# This corpus is programs that MUST COMPILE, so a failure here is the probe
# refusing something legal. 1168 tests, 73 s.
# ⚠ AND THE POPULATION MUST BE CHOSEN BY THE PROPERTY, NOT BY A SPELLING I HAD
# TO HAND. The first version of this line named four DIRECTORIES and missed
# `02_semantic_core`, which is where every `bc_*` fixture lives — i.e. the hole
# was exactly where borrow legality is pinned. Measured: it scored `refwhole`
# at COST 1 where an agent had counted 40 by hand. Selecting by the `pass`
# LABEL instead is worse in a different way (3016 tests, 438 s, and it breaks
# FIXTURES_REQUIRED so tests fail for not having run their producers). What is
# below is the borrow-legality corpus plus the three directories whose spec
# programs caught the last over-refusal that L1 could not see: 478 tests, 39 s.
LEGAL_RE='_pass_(bc_|zone_|place_|branch_merge|borrow|reborrow|move_|drop_|nll)|^logos_(25_spec|03_ownership|04_advanced)_pass'
BIN=build/bin/logosc
WORK=build/probe
mkdir -p "$WORK"

[ -x "$BIN" ] || { echo "probe: no $BIN — build first"; exit 2; }

# ⚠ FRESHNESS IS A PROPERTY OF THE BUILD, not of the checkout. Measuring a
# stale binary is the failure this whole directory keeps re-learning: the
# control and the non-control are indistinguishable when neither was compiled.
NEWER=$(find src include -newer "$BIN" -name '*.cpp' -o -newer "$BIN" -name '*.hpp' 2>/dev/null | head -1)
if [ -n "$NEWER" ]; then
    echo "probe: $BIN is OLDER than $NEWER — rebuild, or you are measuring the previous edit"
    exit 2
fi

# The baseline is a property of the BINARY, so key it on the binary and pay for
# it once per build rather than once per probe — the point of batching N probes
# into one build is lost if each re-measures the same baseline.
KEY=$(sha256sum "$BIN" | cut -c1-16)
BASE="$WORK/baseline-$KEY.txt"
if [ ! -f "$BASE" ]; then
    echo "probe: baseline for this binary not yet measured (${JOBS}-way, ~32 s)"
    ctest --test-dir build -j"$JOBS" -R "$LEDGER_RE" 2>/dev/null \
        | grep -oP '^\s*\d+ - \K\S+(?= \(Failed\))' | sort > "$BASE"
fi
NBASE=$(wc -l < "$BASE")
if [ "$NBASE" -ne 0 ]; then
    echo "probe: ⚠ BASELINE IS NOT CLEAN — $NBASE ledger rows already fail without any probe."
    echo "probe:   every ceiling below is a delta against THAT, not against a green tree:"
    sed 's/^/probe:     /' "$BASE"
fi

# ⚠ ABSOLUTE. Each ledger row is its own logosc process run by ctest from a
# working directory that is NOT this script's, so a relative fire-log path is
# opened somewhere else — or nowhere — and the harness then reports NEVER FIRED
# for a probe that fired 193 times. Measured: that is exactly what happened on
# the first run, and the known-answer probe is what exposed it.
FIRE=$ROOT/$WORK/fire-$NAME.$$
: > "$FIRE"
RUN=$WORK/run-$NAME.txt
LOGOS_PROBE="$NAME" LOGOS_PROBE_FIRE="$FIRE" \
    ctest --test-dir build -j"$JOBS" -R "$LEDGER_RE" 2>/dev/null \
    | grep -oP '^\s*\d+ - \K\S+(?= \(Failed\))' | sort > "$RUN"

FIRES=$(awk -F'\t' -v n="$NAME" '$1==n {s+=$2} END{print s+0}' "$FIRE")
rm -f "$FIRE"

if [ "$FIRES" -eq 0 ]; then
    echo "probe: ✗ '$NAME' NEVER FIRED — the site was not reached in any of the 447 compiles."
    echo "probe:   This is NOT ceiling 0. Either the name is mis-typed, the guard is"
    echo "probe:   upstream of the site, or the code path is dead. Prove the site is live"
    echo "probe:   before reading any zero off it."
    exit 3
fi

CLOSED=$(comm -13 "$BASE" "$RUN")
N=$(printf '%s' "$CLOSED" | grep -c . || true)
REOPEN=$(comm -23 "$BASE" "$RUN" | grep -c . || true)

NROWS=$(ctest --test-dir build -N -R "$LEDGER_RE" 2>/dev/null | grep -oP "Total Tests: \K[0-9]+")
echo "probe: '$NAME' fired $FIRES times across ${NROWS:-?} compiles"
echo "probe: CEILING = $N rows"
[ "$REOPEN" -ne 0 ] && echo "probe: ⚠ $REOPEN previously-failing rows now pass — the probe UN-refused something"
[ "$N" -ne 0 ] && printf '%s\n' "$CLOSED" | sed 's/^/probe:   /'

if [ "${CEILING_ONLY:-0}" = "1" ]; then
    echo "probe: (cost side SKIPPED by CEILING_ONLY=1 — this is half a price)"
    exit 0
fi

# ── THE OTHER HALF: what does the refusal COST in legal programs? ────────────
LBASE="$WORK/legalbase-$KEY.txt"
if [ ! -f "$LBASE" ]; then
    ctest --test-dir build -j"$JOBS" -R "$LEGAL_RE" 2>/dev/null \
        | grep -oP '^\s*\d+ - \K\S+(?= \(Failed\))' | sort > "$LBASE"
fi
LRUN=$WORK/legalrun-$NAME.txt
LOGOS_PROBE="$NAME" ctest --test-dir build -j"$JOBS" -R "$LEGAL_RE" 2>/dev/null \
    | grep -oP '^\s*\d+ - \K\S+(?= \(Failed\))' | sort > "$LRUN"
BROKE=$(comm -13 "$LBASE" "$LRUN")
C=$(printf '%s' "$BROKE" | grep -c . || true)

echo "probe: COST    = $C legal programs refused (a LOWER bound — this corpus is"
echo "probe:           borrow-legality plus three spec dirs, not the whole pass tree)"
[ "$C" -ne 0 ] && printf '%s\n' "$BROKE" | head -12 | sed 's/^/probe:   /'

# The verdict is the RATIO, and it is stated rather than left to be inferred —
# a probe whose cost exceeds its ceiling is not a cheap fix waiting for
# exemptions, it is the wrong mechanism.
if [ "$C" -ge "$N" ]; then
    echo "probe: ⛔ COST >= CEILING — this mechanism refuses at least as many legal"
    echo "probe:    programs as it closes rows. Exemptions might rescue it, but the"
    echo "probe:    burden is now on showing WHICH, not on finding one that fits."
else
    echo "probe: ✓ ceiling $N vs cost $C — worth an exemption analysis"
    [ "$C" -eq 0 ] && cat <<'LIMIT'
probe: ⚠ COST 0 IS NOT A SAFETY CLAIM. A corpus refuses only programs it
probe:    CONTAINS. Measured 2026-08-27: `callroot` scores cost 0 here, and an
probe:    agent refuted it by WRITING A NEW PROGRAM — `match *x { Node(ref mut
probe:    y) => … }` over a Box, legal Rust, refused under the probe. The cost
probe:    side removes bad candidates cheaply; it never certifies a good one.
probe:    The exemption analysis still has to construct its own counter-examples.
LIMIT
fi
echo "probe: ⚠ still an UPPER BOUND. It says what to fund, never what you will get."
exit 0
