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
# ── ⚠ THE COST POPULATION WAS BLIND TO TWO OF THE THREE DAMAGE SHAPES ───────
# Until 2026-08-30 COST was measured over `-L bc -L pass` plus three `pass`
# directories, and that is a population of PASSING TESTS. It encodes the
# assumption that damage looks like a passing test that fails. Damage that
# looks like a REWORDED REFUSAL or a STDLIB COMPILE FAILURE was outside its
# universe by construction — and both of that round's false zeros landed there:
#
#   recvselfderef  COST 0, then refused nine `logos.mem` functions on its first
#                  real build; `liblogos-mem.a` did not link.
#   guardmovearm   COST 0 at the statement spelling, then changed five pinned
#                  `fail` diagnostics.
#
# THREE POPULATIONS NOW, EACH NAMED WHERE THE NUMBER IS PRINTED:
#   pass    the old one — ledger rows and legal programs, through the store
#   fail    `scripts/fail_text_oracle.py`: every `-L bc -L fail` fixture's rc,
#           its stderr SHA and whether its `.expected` still matches. The SHA
#           column is the one ctest cannot produce: `run_test.sh` compares
#           `.expected` as a grep -F SUBSTRING, so a probe that appends a note
#           or re-words a hint leaves the ctest verdict green (rule 15).
#   stdlib  `scripts/stdlib-cost.sh`: all four layers compiled from source
#           under the probe. It asserts legality by BEING BUILT.
#
# A number that could not see one of the three is LABELLED at the point of
# printing. A document is not where that belongs.
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
# ⚠ THE FAIL HALF IS NOT REACHABLE BY ADDING A LABEL TO EITHER OF THOSE: ctest
# ANDs its `-L` filters, so `-L bc -L pass` can never name a `fail` fixture. It
# is a THIRD selection, and it is read by TEXT rather than by ctest verdict.
WORK=build/probe
mkdir -p "$WORK"
SKIP_FAIL=${LOGOS_PROBE_SKIP_FAIL:-0}
SKIP_STDLIB=${LOGOS_PROBE_SKIP_STDLIB:-0}

_bid() { grep -oP 'build_id=\K\d+' "$1" | head -1; }

_measure() {   # $1 = log file; runs all three selections under the current env
    { bash scripts/gate-run.sh "${LEDGER[@]}"
      bash scripts/gate-run.sh "${LEGAL_A[@]}"
      bash scripts/gate-run.sh "${LEGAL_B[@]}"; } > "$1" 2>&1
}

# ⚠ THE FIRE COUNT IS RULE 1 AND I DELETED IT BY ACCIDENT. Rewiring this script
# onto the store dropped the fire log, so a batch of six probes reported five
# zeros with the verdict "is the site populous?" — and nothing could answer it.
# A zero without a fire count is not a refutation and not a measurement; it is
# an unreadable result that looks like both.
_fires() {   # $1 = fire log; sums the counts probe::on() appended
    awk -F'\t' -v n="$NAME" '$1==n {s+=$2} END{print s+0}' "$1" 2>/dev/null
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
FIRELOG=$(mktemp); : > "$FIRELOG"
LOGOS_PROBE="$NAME" LOGOS_PROBE_FIRE="$FIRELOG" _measure "$A"; BID_ARMED=$(_bid "$A")

# ── POPULATION 2: THE `fail` HALF, READ BY TEXT ──────────────────────────────
# ⚠ THE BASELINE IS A PROPERTY OF THE BINARY, NOT OF THE PROBE, so it is keyed
# on `build_hash.py` and paid ONCE PER BUILD. Batching N probes into one build
# is the whole point of `probe-batch.sh`; re-measuring the same 1028 unarmed
# compiles N times would throw that away.
FAIL_RC=0; FAIL_TXT=0; FAIL_MATCH=0; FAIL_LINES=""
if [ "$SKIP_FAIL" != "1" ]; then
    read -r BH _ < <(python3 scripts/build_hash.py build) || BH=unknown
    FB="$WORK/failtext-$BH.tsv"
    if [ ! -s "$FB" ]; then
        echo "ceiling-probe: fail-text baseline for build $BH not yet measured"
        python3 scripts/fail_text_oracle.py "$FB" || { rm -f "$FB"; SKIP_FAIL=2; }
    fi
    if [ "$SKIP_FAIL" != "2" ]; then
        FA="$WORK/failtext-$BH-$NAME.tsv"
        LOGOS_PROBE="$NAME" LOGOS_PROBE_FIRE="$FIRELOG" \
            python3 scripts/fail_text_oracle.py "$FA" || SKIP_FAIL=2
    fi
    if [ "$SKIP_FAIL" != "2" ]; then
        # THREE SHAPES, COUNTED SEPARATELY, because they are three different
        # claims: an rc flip is a fixture that stopped being refused, a lost
        # match is what ctest would have said, and a text-only change is the
        # one ctest CANNOT say — the `.expected` still matches as a substring
        # while the compiler says something else.
        FAIL_LINES=$(join -t$'\t' -j1 "$FB" "$FA" | awk -F'\t' '
            # ⚠ FIELD OFFSETS, AND THE NULL POLE CAUGHT THEM. `join` emits
            # KEY, then every remaining field of the LEFT record, then every
            # remaining field of the right: with five columns a side the armed
            # rc is $6, not $5. The first version read $5 — the baseline PATH
            # column — and reported all 1028 fixtures changed under
            # `selftest_inert`, a probe that changes nothing. A reader that
            # invents 1028 differences is what pole 2 exists to catch.
            {rcb=$2; shb=$3; mb=$4; rca=$6; sha=$7; ma=$8;
             if (rcb!=rca)      print "RC   " $1 "  " rcb " -> " rca;
             else if (mb!=ma)   print "MATCH" $1 "  .expected " (ma?"regained":"LOST");
             else if (shb!=sha) print "TEXT " $1 "  stderr changed, .expected still matches"}')
        FAIL_RC=$(printf '%s\n' "$FAIL_LINES" | grep -c '^RC   ' || true)
        FAIL_MATCH=$(printf '%s\n' "$FAIL_LINES" | grep -c '^MATCH' || true)
        FAIL_TXT=$(printf '%s\n' "$FAIL_LINES" | grep -c '^TEXT ' || true)
    fi
fi

# ── POPULATION 3: THE STDLIB, WHICH ASSERTS LEGALITY BY BEING BUILT ──────────
STDLIB_RC=0; STDLIB_OUT=""
if [ "$SKIP_STDLIB" != "1" ]; then
    STDLIB_OUT=$(LOGOS_PROBE_FIRE="$FIRELOG" bash scripts/stdlib-cost.sh "$NAME" 2>&1)
    STDLIB_RC=$?
fi

FIRES=$(_fires "$FIRELOG"); rm -f "$FIRELOG"
[ -z "${BID_BASE:-}" ] || [ -z "${BID_ARMED:-}" ] && {
    echo "ceiling-probe: could not read a build id from gate-run — refusing to report a number" >&2
    exit 2; }
[ "$BID_BASE" = "$BID_ARMED" ] && {
    echo "ceiling-probe: armed and unarmed resolved to the SAME build identity ($BID_BASE)." >&2
    echo "  LOGOS_PROBE is supposed to be part of the key; if it is not, every number" >&2
    echo "  below would be a comparison of a run with itself." >&2
    exit 2; }

CLOSED=$(python3 scripts/gate_db.py delta "$DB" "$BID_BASE" "$BID_ARMED" logos_00_bc_admit_)
N=$(printf '%s' "$CLOSED" | grep -c . || true)
BROKE=$(python3 scripts/gate_db.py delta "$DB" "$BID_BASE" "$BID_ARMED" | grep -v '^logos_00_bc_admit_' || true)
C=$(printf '%s' "$BROKE" | grep -c . || true)

echo "ceiling-probe: '$NAME'  builds $BID_BASE (unarmed) -> $BID_ARMED (armed)"
echo "probe: fired ${FIRES:-0} times"
if [ "${FIRES:-0}" -eq 0 ]; then
    echo "probe: ✗ NEVER FIRED — the site was not reached by any compile in this run." >&2
    echo "  This is NOT ceiling 0. Either the name is mis-typed, the guard sits" >&2
    echo "  upstream of the site, or the path is dead. Prove the site is live" >&2
    echo "  before reading any zero off it." >&2
    exit 3
fi
echo "probe: CEILING = $N rows"
[ "$N" -ne 0 ] && printf '%s\n' "$CLOSED" | sed 's/^/probe:   /'

# ── THE COST LINE NAMES ITS OWN POPULATION ───────────────────────────────────
# ⚠ RULE 4 OF THIS ROUND: a number that cannot see the stdlib must be LABELLED
# AS SUCH AT THE POINT OF PRINTING, not in a document. A reader takes the digit
# and leaves the caveat behind; this makes the caveat part of the digit.
SEEN="pass(ledger+legal)"
[ "$SKIP_FAIL" = 0 ] && SEEN="$SEEN fail(text)" || SEEN="$SEEN ⚠NO-FAIL"
[ "$SKIP_STDLIB" = 0 ] && SEEN="$SEEN stdlib" || SEEN="$SEEN ⚠NO-STDLIB"
echo "probe: COST    = $C legal programs refused   [saw: $SEEN]"
echo "probe:           a LOWER bound — the corpus refuses only what it CONTAINS;"
echo "probe:           write the counter-examples)"
[ "$C" -ne 0 ] && printf '%s\n' "$BROKE" | head -12 | sed 's/^/probe:   /'

if [ "$SKIP_FAIL" = 0 ]; then
    FTOT=$(( FAIL_RC + FAIL_MATCH + FAIL_TXT ))
    echo "probe: COST-fail = $FTOT of $(grep -c . "$FB") \`-L bc -L fail\` fixtures changed" \
         "(rc $FAIL_RC, .expected-match $FAIL_MATCH, text-only $FAIL_TXT)"
    [ "$FTOT" -ne 0 ] && printf '%s\n' "$FAIL_LINES" | head -12 | sed 's/^/probe:   /'
    [ "$FAIL_TXT" -ne 0 ] && {
        echo "probe:   ⚠ the text-only rows are INVISIBLE TO ctest: run_test.sh matches"
        echo "probe:     .expected as a grep -F SUBSTRING, so those fixtures stay GREEN."; }
else
    echo "probe: COST-fail = NOT MEASURED — a reworded refusal would not appear anywhere"
    echo "probe:             in this report. (LOGOS_PROBE_SKIP_FAIL was set, or the"
    echo "probe:             oracle refused to run.)"
fi

if [ "$SKIP_STDLIB" != 1 ]; then
    printf '%s\n' "$STDLIB_OUT" | sed 's/^/probe: /'
    [ "$STDLIB_RC" -ne 0 ] && echo "probe: COST-stdlib = REFUSED — this outranks every number above."
else
    echo "probe: COST-stdlib = NOT MEASURED — recvselfderef priced 0 here and then"
    echo "probe:               refused nine logos.mem functions. (SKIP_STDLIB was set.)"
fi

if [ "$N" -eq 0 ] && [ "$C" -eq 0 ] && [ "$SKIP_FAIL" = 0 ] && [ "$((FAIL_RC+FAIL_MATCH+FAIL_TXT))" -eq 0 ] && [ "$STDLIB_RC" -eq 0 ]; then
    echo "probe: no effect. ⚠ NOT a refutation until the site is proven live — check the"
    echo "probe:   fire count and the region's arrival count before recording a zero."
elif [ "$STDLIB_RC" -ne 0 ]; then
    # ⚠ THE STDLIB OUTRANKS THE ARITHMETIC. `recvselfderef` scored ceiling 2 /
    # cost 0 and would have printed "✓ worth an exemption analysis" while
    # `liblogos-mem.a` did not build. A verdict line that reads the pass
    # population alone is exactly the instrument this round is repairing.
    echo "probe: ⛔ THE STDLIB DID NOT COMPILE — whatever ceiling $N says, nothing"
    echo "probe:    downstream of a stdlib that does not build is meaningful."
elif [ "$SKIP_FAIL" = 0 ] && [ "$((FAIL_RC+FAIL_MATCH+FAIL_TXT))" -gt "$N" ]; then
    echo "probe: ⛔ it changes MORE pinned diagnostics ($((FAIL_RC+FAIL_MATCH+FAIL_TXT)))"
    echo "probe:    than it closes rows ($N). Read every one before funding it — a"
    echo "probe:    branch that only re-words an already-red diagnostic buys nothing."
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
