#!/usr/bin/env bash
# ctest-summary.sh — run full ctest once and emit a fixed-size, agent-friendly
# summary so a single invocation gives both the pass/fail count and the
# offending tests with their output. Use from the build directory:
#
#   bash ../tests/logos/ctest-summary.sh [extra ctest args...]
#
# Output layout (always present, regardless of pass/fail):
#   === Failures ===     — per-failure block with name + last 60 lines of stderr
#                          ("(none)" when all green)
#   === Summary ===      — the trailing 30 lines (pass count + label table +
#                          total time)
#
# Tail-friendly: even if the harness truncates the tool result to the last
# ~10 lines, those lines belong to === Summary === which has what we need.

set -u
LOG=$(mktemp -t ctest.XXXXXX.log)
trap 'rm -f "$LOG"' EXIT

ctest -j8 --output-on-failure "$@" 2>&1 | tee "$LOG" > /dev/null
EXIT=${PIPESTATUS[0]}

echo "=== Failures ==="
# `ctest --output-on-failure` prints each failing test as:
#     N/M Test #K: name ........***Failed   T sec
#     <stderr block>
# Grab those blocks (name line + up to 60 stderr lines).
awk '
    /\*\*\*Failed/ { in_fail=1; print; ctr=0; next }
    in_fail {
        print
        ctr++
        if (ctr >= 60) { in_fail=0; print "---" }
    }
' "$LOG" | head -800 > /tmp/ctest_failures.$$
if [ -s /tmp/ctest_failures.$$ ]; then
    cat /tmp/ctest_failures.$$
else
    echo "(none)"
fi
rm -f /tmp/ctest_failures.$$

echo
echo "=== Summary ==="
tail -30 "$LOG"

exit "$EXIT"
