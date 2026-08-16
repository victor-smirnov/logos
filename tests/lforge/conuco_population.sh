#!/usr/bin/env bash
# The population witness for the per-test conuco entries.
#
# The per-test ctest entries are enumerated by `file(GLOB)` AT CONFIGURE TIME.
# That is the hole this test closes: a .logos added, removed or renamed after
# the last cmake run changes what the directory holds and NOT what ctest runs,
# so the suite would keep reporting green over a population it no longer covers.
# "All tests passed" says nothing about tests that were never registered.
#
# The expected number is not a hand-maintained constant — it is the count CMake
# actually registered, passed in here. So a deliberate add needs a re-cmake and
# nothing else; there is no number to remember to bump.
#
# Usage: conuco_population.sh <registered-count>

set -uo pipefail

REGISTERED="${1:?registered test count}"

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO=$(cd "$SCRIPT_DIR/../.." && pwd)
DIR="$REPO/conuco/memoria/tests"

if [ ! -d "$DIR" ]; then
    echo "conuco population: $DIR is missing — the package moved or was removed."
    exit 1
fi

LIVE=$(find "$DIR" -maxdepth 1 -name '*.logos' | wc -l)

if [ "$LIVE" -ne "$REGISTERED" ]; then
    echo "conuco population: FAIL — $LIVE test file(s) on disk, $REGISTERED registered with ctest"
    echo "conuco population:   a test was added, removed or renamed since the last cmake."
    echo "conuco population:   Re-run cmake so the per-test entries match the directory."
    exit 1
fi

echo "conuco population: OK — $LIVE test file(s), all registered"
exit 0
