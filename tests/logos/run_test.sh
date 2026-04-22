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

# Parse expected file.
# Supported formats:
#   exit: N
#   stdout: single line value
#   stdout:
#   line1
#   line2          (multi-line: everything after a bare "stdout:" to EOF)
WANT_EXIT=0
WANT_STDOUT=""
IN_STDOUT=0
while IFS= read -r raw_line || [[ -n "$raw_line" ]]; do
    if [ "$IN_STDOUT" = 1 ]; then
        # accumulate multi-line stdout body
        if [ -n "$WANT_STDOUT" ]; then
            WANT_STDOUT="${WANT_STDOUT}"$'\n'"${raw_line}"
        else
            WANT_STDOUT="$raw_line"
        fi
        continue
    fi
    key="${raw_line%%:*}"
    val="${raw_line#*: }"
    case "$key" in
        exit)   WANT_EXIT="$val" ;;
        stdout)
            if [ "$raw_line" = "stdout:" ]; then
                IN_STDOUT=1   # multi-line mode
            else
                WANT_STDOUT="$val"
            fi
            ;;
    esac
done < "$EXPECTED"

BIN="$TMPD/test"
# Collect .a archives from -L flags in EXTRA so stdlib symbols resolve.
# Handles both -L/path and -L /path (separate arg) forms.
LINK_ARCHIVES=()
_take_next=0
for arg in "${EXTRA[@]}"; do
    if [ "$_take_next" = 1 ]; then
        for a in "$arg"/*.a; do [ -f "$a" ] && LINK_ARCHIVES+=("$a"); done
        _take_next=0
    elif [ "$arg" = "-L" ]; then
        _take_next=1
    else
        case "$arg" in
            -L?*) dir="${arg#-L}"; for a in "$dir"/*.a; do [ -f "$a" ] && LINK_ARCHIVES+=("$a"); done ;;
        esac
    fi
done
if ! cc "$OBJ" "${LINK_ARCHIVES[@]}" -o "$BIN" 2>/dev/null; then
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
