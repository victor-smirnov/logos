#!/usr/bin/env bash
# run_ir_snapshot.sh  LOGOSC FILECHECK TEST_LOGOS CHECK_FILE [EXTRA_FLAGS...]
#
# Runs `logosc --emit-llvm <src> [extra]` and feeds its stdout to FileCheck
# against CHECK_FILE. FileCheck does pattern-style matching on the IR; any
# CHECK that fails to match makes the test fail.
#
# A FileCheck path of "-" disables the FileCheck step (used only for
# bring-up — every committed test should ship with a real CHECK file).

set -euo pipefail

LOGOSC="$1"
FILECHECK="$2"
TEST_LOGOS="$3"
CHECK_FILE="$4"
shift 4
EXTRA=("$@")

IR=$("$LOGOSC" --emit-llvm "$TEST_LOGOS" "${EXTRA[@]}" 2>&1) || {
    echo "FAIL: logosc --emit-llvm exited non-zero"
    echo "$IR"
    exit 1
}

if [ "$FILECHECK" = "-" ]; then
    echo "$IR"
    exit 0
fi

echo "$IR" | "$FILECHECK" --allow-unused-prefixes "$CHECK_FILE" || {
    echo "----- IR was: -----"
    echo "$IR"
    echo "----- end IR -----"
    exit 1
}
