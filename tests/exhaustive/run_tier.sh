#!/usr/bin/env bash
# run_tier.sh <smoke|full> <logosc> <lib-dir> — one tier of the enumerator.
#
# Three assertions, in order, and the FIRST is about the corpus itself:
#
#   1. THE CORPUS DIGEST matches the committed one. The generated text is never
#      checked in — it is regenerated from the axes, and a generated file that
#      could be edited would stop being a spec — so what is checked in is a
#      sha256 over (program name, program source, case id, expected value) for
#      every program in the tier. If the generator no longer produces the same
#      corpus, the run stops here: the numbers below would be about a different
#      corpus than the one they were measured on.
#   2. ZERO WRONG ANSWERS, absolutely. No ledger, no tier exemption.
#   3. THE REFUSAL SET IS EXACTLY the ledger, checked in BOTH directions.
#
# `--jobs 12` matches the PROCESSORS 12 the CMake entry reserves, so this does
# not oversubscribe a `ctest -j12`.
set -euo pipefail

TIER=${1:?usage: run_tier.sh <smoke|full> <logosc> <lib-dir>}
LOGOSC_BIN=${2:?}
LIB_DIR=${3:?}
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)

export LOGOSC="$LOGOSC_BIN"
export LOGOS_LIB_DIR="$LIB_DIR"

exec python3 "$HERE/harness.py" \
    --all --tier "$TIER" --jobs 12 \
    --digest-file "$HERE/corpus.$TIER.sha256" \
    --ledger "$HERE/refusals.ledger"
