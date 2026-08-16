#!/usr/bin/env bash
# Run ONE `conuco/memoria` test binary, built by the conuco setup fixture.
#
# TWO WAYS THIS CAN LIE, AND BOTH ARE CHECKED:
#
#  1. NO BINARY — the test failed to compile or link. Reported as the compile
#     failure it is, with the command to reproduce it alone: lforge compiles the
#     whole suite in one pass, so this test's diagnostics scrolled past inside
#     the fixture's output.
#
#  2. A STALE BINARY — and this one was MEASURED, not imagined. When a test
#     stops compiling, lforge leaves the PREVIOUS binary in place. A runner that
#     only checks for existence then runs yesterday's build and reports PASS
#     over a test that no longer compiles: exactly the silence this whole suite
#     exists to end. Caught while probing this script by breaking a test on
#     purpose — it went green.
#
#     So the binary must be NEWER than everything that feeds it: its own source,
#     the compiler, and the package archives it links. That is the same
#     freshness relation lforge itself uses to decide what to rebuild; asserting
#     it here means the check does not depend on lforge having got it right.
#
# Usage: conuco_run.sh <test-stem> <logosc> [profile]

set -uo pipefail

STEM="${1:?test stem}"
LOGOSC="${2:?logosc path}"
PROFILE="${3:-debug}"

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO=$(cd "$SCRIPT_DIR/../.." && pwd)
PKG="$REPO/conuco/memoria"
BIN="$PKG/.lforge/$PROFILE/test/$STEM"
SRC="$PKG/tests/$STEM.logos"

if [ ! -x "$BIN" ]; then
    echo "conuco: '$STEM' produced NO BINARY — it failed to compile or link."
    echo "conuco: the diagnostics are in the logos_26_conuco_build fixture's output"
    echo "conuco: (it compiles the whole suite in one pass); to see this test alone:"
    echo "conuco:   cd conuco/memoria && logosc tests/$STEM.logos -o /tmp/$STEM.o \\"
    echo "conuco:     -l .lforge/$PROFILE/out/libmemoria-ctr.a \\"
    echo "conuco:     -l .lforge/$PROFILE/out/libmemoria-store.a \\"
    echo "conuco:     -l .lforge/$PROFILE/out/libmemoria-testkit.a"
    exit 1
fi

# ── freshness: the binary must be newer than every input ────────────────────
inputs=("$SRC" "$LOGOSC")
for a in "$PKG/.lforge/$PROFILE/out"/*.a; do
    [ -e "$a" ] && inputs+=("$a")
done

bin_mt=$(stat -c %Y "$BIN" 2>/dev/null || echo 0)
for f in "${inputs[@]}"; do
    [ -e "$f" ] || continue
    f_mt=$(stat -c %Y "$f" 2>/dev/null || echo 0)
    if [ "$f_mt" -gt "$bin_mt" ]; then
        echo "conuco: '$STEM' has a STALE BINARY — it is older than $f."
        echo "conuco: lforge leaves the previous binary in place when a compile fails,"
        echo "conuco: so running it would report a PASS for code that no longer builds."
        echo "conuco: see the logos_26_conuco_build fixture's output for the diagnostics."
        exit 1
    fi
done

# THE WORKING DIRECTORY IS PART OF THE CONTRACT. Some tests read fixture files
# (tests/*.hex) through paths relative to the PACKAGE ROOT, which is where
# `lforge test` runs them — pdtbuf_fixtures, ssrle_codec and ssrle_pkd fail from
# anywhere else. The aggregate gate could not see this because it only ever ran
# them the one way; splitting the suite into per-test entries surfaced it
# immediately, which is most of the argument for splitting it.
cd "$PKG" || exit 1
exec "$BIN"
