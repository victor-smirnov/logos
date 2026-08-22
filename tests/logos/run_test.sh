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
    # ⚠ AN EMPTY EXPECTATION MATCHES EVERYTHING. `grep -F ""` succeeds on any
    # input including no input at all, so a fail test whose `.expected` is empty
    # (truncated, half-written, `git add`ed before it was filled in) passes on
    # every compiler, forever, while looking exactly like a test that holds a
    # diagnostic. The file is what makes the test exist at all — cmake globs
    # `.expected` — so an empty one is a registered assertion about nothing.
    if [ -z "$(printf '%s' "$WANT" | tr -d '[:space:]')" ]; then
        echo "FAIL: $EXPECTED is empty, so the diagnostic assertion is vacuous —"
        echo "      \`grep -F ''\` matches any stderr, and this test would pass on a"
        echo "      compiler that printed nothing at all."
        exit 1
    fi
    # ⚠ NOT `echo "$STDERR" | grep -qF`. Under `set -o pipefail`, `grep -q` exits
    # at the first match and `echo` — which for a compiler's whole stderr can far
    # exceed a pipe buffer — dies of SIGPIPE 141, which pipefail reports as the
    # pipeline's status. Here that direction fails CLOSED (a match reads as a
    # miss, so the test flakes red rather than green), but it is the same idiom
    # that lied on 07-30, and this runs for every fail test in the corpus.
    printf '%s' "$STDERR" > "$TMPD/stderr.txt"
    if grep -qF -- "$WANT" "$TMPD/stderr.txt"; then
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

# ── THE FACTS SIDE PRODUCT (task #85) ──────────────────────────────────────
# `LOGOS_FACTS_DIR` set  → the SAME single compile also writes its `--gen-dir`
# units, its whole stderr under `LOGOS_TRACE_PLAN=1`, its rc and a stamp into
# that durable directory, for the three `logos_09_*` census gates to fold over
# afterwards. The argument for this living here rather than in three sweep
# scripts is at the top of `facts_emit.sh`.
# `LOGOS_FACTS_DIR` unset → byte-for-byte the previous behaviour: one plain
# compile, no `--gen-dir`, no trace, nothing written outside `$TMPD`. A hand
# invocation of this script stays clean, which is why the emission is REQUESTED
# rather than always-on.
#
# ⚠ THE TRACE MUST NOT REACH THE FAILURE MESSAGE. `LOGOS_TRACE_PLAN=1` puts the
# plan trace on the SAME stderr as the diagnostics, and one `wql_*` fixture's
# trace is up to 41 KB. A compile failure here used to print stderr verbatim;
# printing it now would bury the diagnostic under the trace. `[plan] ` is the
# trace channel's ONLY spelling (`wql/codegen.logos::plan_trace` writes the
# prefix unconditionally), so the diagnostic is recovered by dropping exactly
# those lines — nothing else is filtered, and the non-facts path below is
# untouched.
if [ -n "${LOGOS_FACTS_DIR:-}" ]; then
    # ⚠ `if !`, NOT a bare call plus `$?`: this script runs under `set -e`, so a
    # bare call that failed would exit before the diagnostic was printed.
    if ! "$(dirname "$0")/facts_emit.sh" "$LOGOSC" "$TEST_LOGOS" \
            "$LOGOS_FACTS_DIR" "$OBJ" "${EXTRA[@]}"; then
        echo "FAIL: logosc failed:"
        grep -v '^\[plan\] ' "$LOGOS_FACTS_DIR/plan.err" 2>/dev/null || true
        exit 1
    fi
elif ! "$LOGOSC" "$TEST_LOGOS" -o "$OBJ" "${EXTRA[@]}" 2>"$TMPD/sema.err"; then
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
#   <bare body>    (no `exit:`/`stdout:` line anywhere → the WHOLE FILE is the
#                   expected stdout, like fail mode's `.expected`)
#
# ⚠ AND WHY THE BARE-BODY FORM IS NOT OPTIONAL. This parser used to recognise
# ONLY the two keys, and silently discarded anything else: a `.expected` holding
# four lines of intended program output set `WANT_STDOUT=""`, the comparison
# below is guarded by `[ -n "$WANT_STDOUT" ]`, and the test degenerated to
# "logosc exited 0 and the binary exited 0". MEASURED 2026-08-01 over the whole
# pass corpus (5422 files): 34 such files, asserting nothing about their output
# while LOOKING exactly like tests that pin it.
#
# That is not a hypothetical loss of strictness. `option_ptr_wrapper_niche`
# says `Option<Arc>=8` and the program PRINTED `Option<Arc>=4` — a live
# `sizeof` miscompile of every generic enum in a rendered context — green, for
# as long as the file has existed. Fail mode already refuses an empty
# expectation for exactly this reason ("`grep -F ''` matches any stderr"); this
# is the same defect on the other side of the harness, and it is now the same
# hard error. A `.expected` that states nothing is a registered assertion about
# nothing.
WANT_EXIT=0
WANT_STDOUT=""
HAVE_STDOUT=0
IN_STDOUT=0
# Pre-scan: does the file state ANY recognised key? The bare-body and vacuous
# forms are properties of the whole file, so they cannot be decided line by line.
HAS_KEY=0
while IFS= read -r raw_line || [[ -n "$raw_line" ]]; do
    case "${raw_line%%:*}" in
        exit|stdout) HAS_KEY=1; break ;;
    esac
done < "$EXPECTED"
if [ "$HAS_KEY" = 0 ]; then
    if [ -z "$(tr -d '[:space:]' < "$EXPECTED")" ]; then
        echo "FAIL: $EXPECTED states no expectation at all — no \`exit:\`, no"
        echo "      \`stdout:\`, no body. Such a file asserts only that the"
        echo "      program was built and returned 0, while reading as a test"
        echo "      that pins its behaviour. Write \`exit: 0\` if that IS the"
        echo "      whole assertion, or the expected stdout."
        exit 1
    fi
    # Bare body: the file IS the expected stdout (trailing newline stripped by $()).
    WANT_STDOUT="$(cat "$EXPECTED")"
    HAVE_STDOUT=1
fi
if [ "$HAS_KEY" = 1 ]; then
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
            HAVE_STDOUT=1
            if [ "$raw_line" = "stdout:" ]; then
                IN_STDOUT=1   # multi-line mode
            else
                WANT_STDOUT="$val"
            fi
            ;;
    esac
done < "$EXPECTED"
fi

BIN="$TMPD/test"
# Collect .a archives for static linking. Sources, in order:
#   1. ${LOGOS_LIB_DIR}/*.a — system module library (set by test harness;
#      mirrors the path baked into logosc, so the linker sees the same
#      archives the compiler used for symbol discovery).
#   2. -L flags in EXTRA — user-provided directories (globbed for *.a).
#   3. -l flags in EXTRA — specific archive files (passed verbatim).
LINK_ARCHIVES=()
if [ -n "${LOGOS_LIB_DIR:-}" ]; then
    # Phase 4 transition (three-layer split): order matters during the
    # migration window when liblstdlib.a (monolith) and liblogos-{lang,
    # mem,std}.a (layer archives) coexist. ld processes archives
    # lazily — symbols only pulled when still undefined — so putting the
    # monolith first lets it satisfy template-specialization references
    # before the layer archives are reached, avoiding "multiple
    # definition" errors for shared post-mono instantiations. Phase 7
    # cleanup removes the monolith and this ordering becomes moot.
    for a in "$LOGOS_LIB_DIR"/liblstdlib*.a; do
        [ -f "$a" ] && LINK_ARCHIVES+=("$a")
    done
    for a in "$LOGOS_LIB_DIR"/liblogos-*.a; do
        [ -f "$a" ] && LINK_ARCHIVES+=("$a")
    done
    for a in "$LOGOS_LIB_DIR"/*.a; do
        case "$(basename "$a")" in
            liblstdlib*|liblogos-*) ;; # already added above
            *) [ -f "$a" ] && LINK_ARCHIVES+=("$a") ;;
        esac
    done
fi
_take_next_dir=0
_take_next_file=0
for arg in "${EXTRA[@]}"; do
    if [ "$_take_next_dir" = 1 ]; then
        for a in "$arg"/*.a; do [ -f "$a" ] && LINK_ARCHIVES+=("$a"); done
        _take_next_dir=0
    elif [ "$_take_next_file" = 1 ]; then
        [ -f "$arg" ] && LINK_ARCHIVES+=("$arg")
        _take_next_file=0
    elif [ "$arg" = "-L" ] || [ "$arg" = "--libs" ]; then
        _take_next_dir=1
    elif [ "$arg" = "-l" ] || [ "$arg" = "--lib" ]; then
        _take_next_file=1
    else
        case "$arg" in
            -L?*) dir="${arg#-L}"; for a in "$dir"/*.a; do [ -f "$a" ] && LINK_ARCHIVES+=("$a"); done ;;
            -l?*) f="${arg#-l}"; [ -f "$f" ] && LINK_ARCHIVES+=("$f") ;;
        esac
    fi
done
if ! cc "$OBJ" -Wl,--start-group "${LINK_ARCHIVES[@]}" -Wl,--end-group -lpthread -lm -lstdc++ -Wl,--gc-sections -Wl,--allow-multiple-definition -o "$BIN" 2>/dev/null; then
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
    # ⚠ AND THE STDOUT, because a test whose exit code IS its verdict has already
    # written WHY on stdout and this check used to throw it away. Purely additive:
    # nothing about when the test fails changes, only what it tells you when it does.
    echo "  stdout was:"
    echo "$ACTUAL_STDOUT"
    exit 1
fi
# ⚠ `HAVE_STDOUT`, NOT `-n "$WANT_STDOUT"`. The old guard skipped the comparison
# whenever the expectation was the EMPTY STRING, so `stdout:` with nothing after
# it — a stated expectation that the program prints nothing — passed on a program
# that printed anything at all. "Stated" and "non-empty" are different facts.
if [ "$HAVE_STDOUT" = 1 ] && [ "$ACTUAL_STDOUT" != "$WANT_STDOUT" ]; then
    echo "FAIL: stdout mismatch"
    echo "  expected: $WANT_STDOUT"
    echo "  got:      $ACTUAL_STDOUT"
    exit 1
fi

exit 0
