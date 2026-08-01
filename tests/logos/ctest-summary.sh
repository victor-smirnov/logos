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

# ⚠ `--no-tests=error` IS NOT OPTIONAL. `ctest -R <pattern>` that matches NOTHING
# prints "No tests were found!!!" and exits 0 — measured on ctest 3.28.3: without
# the flag `ctest -R '^zzz_no_such_test$'` exits 0, with it 8 — so a selection
# regex that stopped matching (a renamed test, a mangled alternation, a build
# that registered no tests at all) reported success having run nothing. A gate
# that observed no test may not report on any.
ctest --no-tests=error -j"$(nproc)" --output-on-failure "$@" 2>&1 | tee "$LOG" > /dev/null
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
tail -40 "$LOG"
# Always re-print the pass-count line LAST so it survives the agent
# harness's tail-of-tail truncation. ctest emits it before the
# Label Time Summary block, so without this it can fall off-screen.
echo
echo "=== Pass/fail ==="
grep -E "tests passed|tests failed" "$LOG" | tail -2 || true
# ⚠ AND THE COUNT MUST EXIST. If ctest never printed a pass/fail line there is no
# number above and nothing to read — belt to `--no-tests=error`'s braces, because
# "the summary is missing" and "everything passed" look identical to a reader who
# only checks the exit code.
if ! grep -qE "tests passed" "$LOG"; then
    echo "FAIL: ctest emitted no pass/fail summary — no test was observed."
    tail -20 "$LOG"
    exit 1
fi

exit "$EXIT"  # lint:exit-ok — ${PIPESTATUS[0]} of ctest: a real wait status, already a byte
