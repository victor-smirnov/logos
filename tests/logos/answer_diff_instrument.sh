#!/usr/bin/env bash
# answer_diff_instrument.sh OUTDIR [LOGOSC] [PASSDIR]
#
# THE CORPUS-WIDE ANSWER DIFF — the oracle an emitter change owes.
#
# WHY IT EXISTS. `criterion1_materialization_instrument.sh` reads the compiler's
# ARTIFACT and its TRACE: what shape was emitted and what the plan said about it.
# Neither channel can see a WRONG ANSWER. D4 (2026-08-15) is the measured proof:
# a miscompile made deem over a Vec-backed slice return every other row and then
# zeros, and every artifact-channel and trace-channel number was unchanged
# through it. What caught it was running the programs and comparing the values.
#
# So: for every `pass/wql_*` + `pass/deem_*` fixture this compiles it, links it
# exactly the way `run_test.sh` does, RUNS it, and records one line per fixture
#
#     <base> exit=<rc> out=<sha256 of stdout>
#
# into `$OUTDIR/answers`. Two trees, two OUTDIRs, `diff` them: any line that
# moves is an answer that moved. That is a stronger assertion than the corpus's
# own `.expected` files, which pin a rc and (for many fixtures) a coarse stdout
# — this pins the WHOLE of stdout, byte for byte, on every fixture at once.
#
# ⚠ IT IS AN INSTRUMENT, NOT A GATE, and the direction matters. An answer that
# MOVES is not automatically a defect: a stage that deliberately changes an
# emitted value must move it, and then the moved lines are exactly the review
# list. What the instrument refuses to allow is a change landing with NOBODY
# HAVING LOOKED. It gates only its own integrity (exit 2):
#
#   A1  one answer line per fixture — a probe that vanished (compile failure,
#       link failure, timeout) is recorded as such and COUNTED, never dropped.
#       A silently missing row is how a differential turns green by shrinking.
#   A2  the population is >= 20 fixtures — a glob that matched nothing must not
#       read as "no answers changed".
#
# The two fixtures that are KNOWN not to compile on this tree are recorded with
# `exit=CFAIL`; they are answers too, and a stage that makes one of them compile
# moves a line here rather than silently improving a count.
#
# EXIT 0 measured · 2 the instrument could not measure (A1/A2).
set -uo pipefail
OUT="${1:?outdir}"
REPO=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
LOGOSC="${2:-$REPO/build/bin/logosc}"
TESTS="${3:-$REPO/tests/logos/pass}"
export LOGOS_LIB_DIR=${LOGOS_LIB_DIR:-$REPO/build/lib/logos}
export LC_ALL=C

[ -x "$LOGOSC" ] || { echo "FAIL(2): no logosc at $LOGOSC"; exit 2; }
rm -rf "$OUT"; mkdir -p "$OUT/_rows"
shopt -s nullglob
FIXTURES=("$TESTS"/wql_*.logos "$TESTS"/deem_*.logos)
if [ "${#FIXTURES[@]}" -lt 20 ]; then
    echo "FAIL(2) A2: ${#FIXTURES[@]} fixtures — population lost"; exit 2
fi

# The link line, lifted from run_test.sh. Kept in one place here rather than
# sourced: run_test.sh is a per-test driver with its own argument contract, and
# a copy that DRIFTS would show up here as every answer moving at once, which is
# a loud failure rather than a quiet one.
LINK_ARCHIVES=()
for a in "$LOGOS_LIB_DIR"/liblstdlib*.a; do [ -f "$a" ] && LINK_ARCHIVES+=("$a"); done
for a in "$LOGOS_LIB_DIR"/liblogos-*.a;  do [ -f "$a" ] && LINK_ARCHIVES+=("$a"); done
for a in "$LOGOS_LIB_DIR"/*.a; do
    case "$(basename "$a")" in liblstdlib*|liblogos-*) ;; *) [ -f "$a" ] && LINK_ARCHIVES+=("$a");; esac
done
export LINK_LINE="${LINK_ARCHIVES[*]}"

one() {
    local f="$1" OUT="$2" LOGOSC="$3"
    local b; b=$(basename "$f" .logos)
    local d; d=$(mktemp -d)
    local rc sha
    if ! "$LOGOSC" "$f" -o "$d/o.o" >"$d/c.out" 2>"$d/c.err"; then
        echo "$b exit=CFAIL out=-" > "$OUT/_rows/$b"; rm -rf "$d"; return
    fi
    # shellcheck disable=SC2086
    if ! cc "$d/o.o" -Wl,--start-group $LINK_LINE -Wl,--end-group \
            -lpthread -lm -lstdc++ -Wl,--gc-sections \
            -Wl,--allow-multiple-definition -o "$d/bin" 2>/dev/null; then
        echo "$b exit=LFAIL out=-" > "$OUT/_rows/$b"; rm -rf "$d"; return
    fi
    timeout 120 "$d/bin" > "$d/stdout" 2>/dev/null
    rc=$?
    [ "$rc" = 124 ] && rc=TIMEOUT
    sha=$(sha256sum < "$d/stdout" | cut -c1-16)
    echo "$b exit=$rc out=$sha" > "$OUT/_rows/$b"
    cp "$d/stdout" "$OUT/$b.stdout"
    rm -rf "$d"
}
export -f one
printf '%s\0' "${FIXTURES[@]}" | xargs -0 -P "$(nproc)" -I{} bash -c 'one "$@"' _ {} "$OUT" "$LOGOSC"

ROWS=("$OUT"/_rows/*)
if [ "${#ROWS[@]}" -ne "${#FIXTURES[@]}" ]; then
    echo "FAIL(2) A1: ${#ROWS[@]} answer rows for ${#FIXTURES[@]} probes — a probe was lost."
    exit 2
fi
cat "${ROWS[@]}" | sort > "$OUT/answers"
echo "population: ${#FIXTURES[@]} fixtures"
echo "  compile failures: $(grep -c 'exit=CFAIL' "$OUT/answers")"
echo "  link failures:    $(grep -c 'exit=LFAIL' "$OUT/answers")"
echo "  timeouts:         $(grep -c 'exit=TIMEOUT' "$OUT/answers")"
echo "  ran:              $(grep -cE 'exit=[0-9]+' "$OUT/answers")"
echo "answers -> $OUT/answers"
