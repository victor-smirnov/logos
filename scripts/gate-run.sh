#!/usr/bin/env bash
# gate-run.sh <ctest args…> — measure only what is NOT already measured.
#
# ⚠ THE DB IS THE SOURCE OF TRUTH; ctest IS ONLY A RUNNER. Victor 2026-08-28.
# Three questions, in order:
#   what tests exist for this filter   → `ctest -N`, names only
#   which of them are already measured → build/gate-state/runs.db, per (build, test)
#   what is missing                    → the difference, and that is what runs
#
# So a second call after a first measures NOTHING, a call after a partial run
# measures only the remainder, and a call after a rebuild measures everything
# again — because the build is part of the identity, not a footnote.
#
# THE BUILD IS WHAT THE COMPILER SAYS ABOUT ITSELF: `logosc --version` already
# carries branch, commit, a dirty flag and a build timestamp. Plus a hash of the
# libraries, because a stdlib rebuild leaves logosc byte-identical and changes
# what every test does — measured, that is the third key this file has had and
# the first two were wrong in the permissive direction.
#
#   gate-run.sh -L bc
#   gate-run.sh -R '^logos_00_bc_admit_'
#   FORCE=1 gate-run.sh -L bc      # re-measure regardless; say why in your report
set -uo pipefail
CTEST_PARALLEL_LEVEL="${CTEST_PARALLEL_LEVEL:-$(nproc)}"; export CTEST_PARALLEL_LEVEL
cd "$(dirname "$0")/.." || exit 2
ROOT=$PWD
BUILD=${LOGOS_BUILD:-$ROOT/build}
DB=${LOGOS_GATE_DB:-$BUILD/gate-state/runs.db}
BIN=$BUILD/bin/logosc
[ -x "$BIN" ] || { echo "gate-run: no $BIN — build first" >&2; exit 2; }

# ⚠ THE IDENTITY IS `scripts/build_hash.py`, NOT THE VERSION STRING. Measured
# 2026-08-29: `logosc --version` carries a timestamp from CMake's CONFIGURE step,
# so it does not move when the compiler is rebuilt — and the first key here
# hashed the LIBRARIES and forgot logosc, so a compiler-only rebuild produced a
# FALSE CACHE HIT: "already measured" for a binary that had never run a test.
# Both halves have now bitten, an hour apart, and both in the permissive
# direction. The version string is kept only as a human-readable annotation.
VER=$("$BIN" --version 2>/dev/null | head -1)
read -r LIBS NFILES < <(python3 scripts/build_hash.py "$BUILD") || {
    echo "gate-run: build_hash failed — refusing to key a run on nothing" >&2; exit 2; }
[ "${NFILES:-0}" -lt 5 ] && { echo "gate-run: build_hash saw only $NFILES files; that is not a build" >&2; exit 2; }
HEAD=$(git rev-parse --short HEAD 2>/dev/null || echo nogit)  # lint:git-ok — recorded for the reader; the identity is the version string plus the libraries
# ⚠ THE ENVIRONMENT IS PART OF THE RUN. `LOGOS_PROBE` arms a probe and changes
# what the compiler decides; `LOGOS_DUMP_*` and the rest may too. MEASURED: a
# sabotage-probe run recorded three FAILED verdicts under the same identity an
# unarmed run would have used, so the store held results from a compiler nobody
# was actually testing. Every LOGOS_* variable goes into the key — that
# over-invalidates for a pure dump flag, which is the safe direction: a spurious
# re-run costs minutes, a poisoned record costs a wrong answer.
# ⚠ LOGOS_PROBE_FIRE IS EXCLUDED: it names an output file, it does not change
# a single decision the compiler makes. Keying on it would put a fresh temp
# path into the identity on every call and defeat the cache entirely.
ENVK=$(env | grep -E '^LOGOS_(PROBE|DUMP|VERIFY|SZ|MRAM)' | grep -v '^LOGOS_PROBE_FIRE=' | sort | sha256sum | cut -c1-12)
BID=$(python3 scripts/gate_db.py build "$DB" "$VER" "$LIBS-$ENVK" "$HEAD")
# Printed on stderr so a caller can capture the identity without parsing the
# report — `ceiling-probe.sh` needs it to ask the store what changed between an
# unarmed and an armed run instead of diffing two temp files of its own.
echo "gate-run: build_id=$BID" >&2

ALL=$(ctest --test-dir "$BUILD" -N "$@" 2>/dev/null | grep -oP '^\s+Test\s+#\d+: \K\S+' | sort)
N=$(printf '%s\n' "$ALL" | grep -c . || true)
[ "$N" -eq 0 ] && { echo "gate-run: that filter selects NO tests — a filter is not a count" >&2; exit 2; }

printf '%s\n' "$ALL" | python3 scripts/gate_db.py inventory "$DB" "$BID" >/dev/null
MISS=$(python3 scripts/gate_db.py missing "$DB" "$BID" | grep -Fx -f <(printf '%s\n' "$ALL") || true)
M=$(printf '%s\n' "$MISS" | grep -c . || true)

if [ "${FORCE:-0}" != "1" ] && [ "$M" -eq 0 ]; then
    echo "gate-run: all $N tests in this filter are ALREADY MEASURED under this build."
    echo "  build $BID: $VER (libs $LIBS)"
    python3 scripts/gate_db.py verdicts "$DB" "$BID" "$(printf '%s' "$@" | grep -oP "(?<=-R )\S+" || true)"
    echo "  Nothing has changed that a test run could see. (FORCE=1 re-measures;"
    echo "  say in your report why the record was not enough.)"
    exit 0
fi

if [ "${FORCE:-0}" = "1" ]; then RUN=$ALL; M=$N; echo "gate-run: FORCE — re-measuring all $N";
else echo "gate-run: $N in filter, $M not yet measured under build $BID — running those"; RUN=$MISS; fi

# ⚠ HOW THE MISSING SET REACHES ctest, and why it is not always a name list.
# The obvious form — one anchored alternation of every missing name — DIED AT
# SCALE: 1769 names is a 99,911-character regex and ctest answered "No tests
# were found!!!", recorded ZERO, and exited 0. A silent partial, in the tool
# built to stop silent partials. So:
#   · nothing measured yet  → pass the caller's own filter through, which is
#     what it was written for and has no length at all;
#   · a partial top-up      → name list, CHUNKED, because the limit is real and
#     a chunk that is small enough is the only form that provably works.
# And in both cases the number of tests ctest actually ran is checked against
# the number we asked for: a run that measured fewer than it was told to is a
# failure, never a pass.
JU=$(mktemp --suffix=.xml)
RC=0
if [ "$M" -eq "$N" ]; then
    ctest --test-dir "$BUILD" --output-junit "$JU" "$@"; RC=$?
    python3 scripts/gate_db.py ingest "$DB" "$BID" "$JU" "$*"
else
    CH=$(mktemp -d); printf '%s\n' "$RUN" | split -l 150 - "$CH/part."
    for part in "$CH"/part.*; do
        RX=$(sed 's/[][\.^$*+?(){}|]/\\&/g' "$part" | paste -sd'|' -)
        J2=$(mktemp --suffix=.xml)
        ctest --test-dir "$BUILD" --output-junit "$J2" \
            -j"$(nproc)" --output-on-failure -R "^($RX)$"; r=$?
        [ "$r" -ne 0 ] && RC=$r
        python3 scripts/gate_db.py ingest "$DB" "$BID" "$J2" "$*"
        rm -f "$J2"
    done
    rm -rf "$CH"
fi
rm -f "$JU"

# ⚠ A RUN THAT MEASURED NOTHING IS NOT A PASS. The scale failure above exited 0
# with zero rows recorded; nothing in the pipeline objected. Count what actually
# landed and refuse to call a shortfall success.
GOT=$(python3 - "$DB" "$BID" <<'PYEOF'
import sqlite3, sys
c = sqlite3.connect(sys.argv[1])
print(c.execute("SELECT count(*) FROM verdicts WHERE build_id=?", (sys.argv[2],)).fetchone()[0])
PYEOF
)
WANT=$(( M + $(python3 - "$DB" "$BID" "$M" <<'PYEOF'
import sqlite3, sys
c = sqlite3.connect(sys.argv[1])
print(c.execute("SELECT count(*) FROM verdicts WHERE build_id=?", (sys.argv[2],)).fetchone()[0] - int(sys.argv[3]))
PYEOF
) ))
if [ "$GOT" -lt "$M" ]; then
    echo "gate-run: ⚠ asked for $M tests and the store holds $GOT for this build." >&2
    echo "  A run that measured fewer than it was told to is a FAILURE, not a pass." >&2
    exit 3
fi
exit $RC  # lint:exit-ok — the runner's own status, which is the point of running it
