#!/usr/bin/env bash
# run_test.sh  MODE LOGOSC TEST_LOGOS EXPECTED [EXTRA_FLAGS...]
#
# MODE=pass  — logosc must succeed; compiled binary must match expected exit/stdout
# MODE=fail  — logosc must fail; its stderr must contain the expected string
#
# EXTRA_FLAGS: passed verbatim to logosc (e.g. -I /path/to/stdlib)

set -euo pipefail

MODE="$1"
LOGOSC="$2"
TEST_LOGOS="$3"
EXPECTED="$4"
shift 4
EXTRA=("$@")

TMPD=$(mktemp -d)
trap 'rm -rf "$TMPD"' EXIT

# ── Fail mode ──────────────────────────────────────────────────────────────
if [ "$MODE" = fail ]; then
    STDERR=$("$LOGOSC" "$TEST_LOGOS" -o /dev/null "${EXTRA[@]}" 2>&1 || true)
    WANT=$(cat "$EXPECTED")
    if echo "$STDERR" | grep -qF "$WANT"; then
        exit 0
    fi
    echo "FAIL: stderr did not contain:"
    echo "  $WANT"
    echo "Actual stderr:"
    echo "$STDERR"
    exit 1
fi

# ── Pass mode ──────────────────────────────────────────────────────────────
OBJ="$TMPD/test.o"

if ! "$LOGOSC" "$TEST_LOGOS" -o "$OBJ" "${EXTRA[@]}" 2>"$TMPD/sema.err"; then
    echo "FAIL: logosc failed:"
    cat "$TMPD/sema.err"
    exit 1
fi

# Parse expected file: lines of the form "key: value"
WANT_EXIT=0
WANT_STDOUT=""
while IFS=: read -r key val; do
    val="${val# }"      # strip one leading space
    case "$key" in
        exit)   WANT_EXIT="$val" ;;
        stdout) WANT_STDOUT="$val" ;;
    esac
done < "$EXPECTED"

BIN="$TMPD/test"
if ! cc "$OBJ" -o "$BIN" 2>/dev/null; then
    echo "FAIL: cc link failed"
    exit 1
fi

set +e
"$BIN" > "$TMPD/stdout" 2>/dev/null
ACTUAL_EXIT=$?
set -e
# Strip trailing whitespace for comparison (programs may print trailing space before \n)
ACTUAL_STDOUT=$(cat "$TMPD/stdout" | sed 's/[[:space:]]*$//')
WANT_STDOUT=$(printf '%s' "$WANT_STDOUT" | sed 's/[[:space:]]*$//')

if [ "$ACTUAL_EXIT" != "$WANT_EXIT" ]; then
    echo "FAIL: exit code $ACTUAL_EXIT (expected $WANT_EXIT)"
    exit 1
fi
if [ -n "$WANT_STDOUT" ] && [ "$ACTUAL_STDOUT" != "$WANT_STDOUT" ]; then
    echo "FAIL: stdout mismatch"
    echo "  expected: $WANT_STDOUT"
    echo "  got:      $ACTUAL_STDOUT"
    exit 1
fi

exit 0
