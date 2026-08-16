#!/usr/bin/env bash
# THE CONUCO FIXTURE — builds `conuco/memoria` and compiles all of its tests, so
# the per-test ctest entries have prebuilt binaries to run.
#
# WHY A FIXTURE AND NOT ONE AGGREGATE TEST: `conuco/memoria` builds with lforge,
# not the repo CMake, and nothing in the repository ran it — that is how 30 of
# its 67 tests rotted through the D1 borrow-checker arc with the in-tree L4
# green (47e34581). One aggregate ctest closed the hole but reported a single
# verdict: a failure named no file, because lforge's borrow diagnostics carry no
# location either. Per-test entries make each failure say its own name.
#
# `lforge test` takes no name filter — it compiles every stale test and runs all
# of them. So this fixture pays the compile once and the per-test entries re-run
# the (cheap) binaries. The double run of the binaries is the price of the
# missing compile-only command, and it is small: compilation dominates.
#
# The exit code of `lforge test` is DELIBERATELY IGNORED — a failing test is the
# per-test entries' verdict to render, not this fixture's. What this fixture
# fails on is the package not BUILDING, which would leave every per-test entry
# reporting the same thing 67 times.
#
# Usage: conuco_setup.sh <lforge> <logosc> <LOGOS_LIB_DIR>

set -uo pipefail

LFORGE="${1:?lforge path}"
LOGOSC="${2:?logosc path}"
LIB="${3:?LOGOS_LIB_DIR}"

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO=$(cd "$SCRIPT_DIR/../.." && pwd)
PKG="$REPO/conuco/memoria"

if [ ! -d "$PKG" ]; then
    echo "conuco setup: $PKG is missing — the package moved or was removed;"
    echo "conuco setup: tests/logos/CMakeLists.txt names it explicitly and must move with it."
    exit 1
fi

cd "$PKG" || exit 1

if ! LOGOSC="$LOGOSC" LOGOS_LIB_DIR="$LIB" "$LFORGE" build; then
    echo "conuco setup: FAIL — the package does not BUILD"
    exit 1
fi

# Compile (and, unavoidably, run) every test. Failures here are reported by the
# per-test entries; see the note above.
LOGOSC="$LOGOSC" LOGOS_LIB_DIR="$LIB" "$LFORGE" test
echo "conuco setup: OK — package built, tests compiled"
exit 0
