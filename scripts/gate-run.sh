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
cd "$(dirname "$0")/.." || exit 2
ROOT=$PWD
BUILD=${LOGOS_BUILD:-$ROOT/build}
DB=${LOGOS_GATE_DB:-$BUILD/gate-state/runs.db}
BIN=$BUILD/bin/logosc
[ -x "$BIN" ] || { echo "gate-run: no $BIN — build first" >&2; exit 2; }

VER=$("$BIN" --version 2>/dev/null | head -1)
LIBS=$( { find "$BUILD/lib/logos" -type f 2>/dev/null | sort | xargs -r sha256sum
          find "$BUILD/tests/logos" -maxdepth 1 -name '*.a' 2>/dev/null | sort | xargs -r sha256sum
        } | sha256sum | cut -c1-16)
HEAD=$(git rev-parse --short HEAD 2>/dev/null || echo nogit)  # lint:git-ok — recorded for the reader; the identity is the version string plus the libraries
BID=$(python3 scripts/gate_db.py build "$DB" "$VER" "$LIBS" "$HEAD")

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

# ⚠ ctest gets an EXACT NAME LIST, not the caller's filter. The DB decided what
# to run; ctest is the runner. `-R` takes one regex, so the names are anchored
# and joined — 8000 names is a long regex but ctest handles it, and the
# alternative (one ctest per test) is the serial trap this arc has been about.
RX=$(printf '%s\n' "$RUN" | sed 's/[][\.^$*+?(){}|]/\\&/g' | paste -sd'|' -)
JU=$(mktemp --suffix=.xml)
ctest --test-dir "$BUILD" --output-junit "$JU" -R "^($RX)$"; RC=$?
python3 scripts/gate_db.py ingest "$DB" "$BID" "$JU" "$*"
rm -f "$JU"
exit $RC  # lint:exit-ok — the runner's own status, which is the point of running it
