#!/usr/bin/env bash
# plan_distinct_gate.sh LOGOSC TEST_LOGOS
#
# #726: a source's `distinct <rel>.<col> = <fn>;` reaches MacroParams — the plan
# trace line is printed from `rel_ndv_fn`, i.e. from the registry the Deem cost
# model reads, not from the natspec text. The fixture's rows are its own oracle;
# this gate asserts the declaration TRAVELLED.
set -euo pipefail
LOGOSC="$1"
TEST_LOGOS="$2"
TMPD=$(mktemp -d)
trap 'rm -rf "$TMPD"' EXIT
if ! LOGOS_TRACE_PLAN=1 "$LOGOSC" "$TEST_LOGOS" -o "$TMPD/test.o" 2>"$TMPD/err"; then
    echo "FAIL: logosc failed:"; cat "$TMPD/err"; exit 1
fi
if ! grep -Eq '^\[plan\] c -> distinct-value count on key   \(steps_ndv\)$' "$TMPD/err"; then
    echo "FAIL: no distinct-value trace for rel 'c' column 'key' naming 'steps_ndv'"
    grep '^\[plan\]' "$TMPD/err" | head -20
    exit 1
fi
echo "PASS: the distinct-value declaration reaches MacroParams"
