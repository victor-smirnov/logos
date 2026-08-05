#!/usr/bin/env bash
# type_apply_trap_gate.sh LOGOSC LEDGER FIXTURE_DIR [LIB_DIR]
#
# `type_apply` IS THE ONLY name→Type BRIDGE, AND IT CRASHES ON THE ONLY SHAPE
# THAT WOULD MAKE IT USEFUL.
#
# A metaprog handler holds its target's name as a runtime `str` read off the
# AST. `type_apply` accepts a name only as a source literal, and when handed
# anything else it calls `std::abort()` in mono — no file, no line, no span, a
# core dump. That is the entire wall between a handler and a capability question
# about the type it was handed, and it is the fact that prices the value-domain
# derivation. It is measured here rather than argued.
#
# WHY THIS IS NOT TWO CORPUS TESTS. Neither outcome survives `run_test.sh`:
#   * fail-mode asserts "logosc exited non-zero and stderr contained X". A
#     SIGABRT and a clean diagnostic are indistinguishable to it, and that
#     difference is the whole finding — the point is that the compiler CRASHES,
#     not that it refuses.
#   * pass-mode compiles, links, runs and compares an exit code. A compile that
#     returns 0 while emitting an object file whose demanded instance is a trap
#     reads as "the program returned 132", losing that the compiler approved it.
# So the exit codes ARE the measurement, and this gate reads them directly.
#
# HELD IN BOTH DIRECTIONS. Every row must match exactly; a row that improves is
# as red as a row that regresses, because a fixed defect must leave the ledger
# and land as an ordinary test. The population is DERIVED from the fixture
# directory, not listed here, so a fixture with no row is red and a row with no
# fixture is red.
#
# ⚠ AND THE GATE PROVES ITS OWN INSTRUMENT, IN THE SAME RUN, THROUGH THE SAME
# CODE. Three canaries, each feeding a deliberately-known input to the SAME
# function the real rows go through:
#   C1 (reader)     — a program that returns 7 must be measured as compile 0 /
#                     run 7. An `$?` that has stopped propagating, a linker that
#                     silently produced nothing, a `run_rc` hard-wired to 0:
#                     all of them answer "clean" on the real rows and fail here.
#   C2 (matcher)    — the stderr matcher must FIND a token that is in the text
#                     and MISS one that is not. A matcher degraded to
#                     `grep -F ""` matches every row forever.
#   C3 (comparator) — check_row fed a deliberately wrong expectation must report
#                     exactly one violation. A comparator that has stopped
#                     comparing reports a clean ledger exactly like a clean one.
#
# exit 0 = every recorded row still holds · 1 = a row moved, or a canary was not
#          caught · 2 = usage/IO error · 4 = malformed ledger.

set -uo pipefail

LOGOSC="${1:?logosc}"
LEDGER="${2:?ledger file}"
FIXDIR="${3:?fixture dir}"
LIBDIR="${4:-${LOGOS_LIB_DIR:-}}"

[ -x "$LOGOSC" ] || { echo "type-apply-gate: '$LOGOSC' is not executable"; exit 2; }
[ -f "$LEDGER" ] || { echo "type-apply-gate: no such ledger '$LEDGER'";    exit 2; }
[ -d "$FIXDIR" ] || { echo "type-apply-gate: no such dir '$FIXDIR'";       exit 2; }

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
# The fixtures abort and trap on purpose; a gate should not litter the tree
# with core files to prove it.
ulimit -c 0 2>/dev/null || true

# ── THE READER ───────────────────────────────────────────────────────────────
# measure SRC TAG → prints "<compile_rc> <run_rc>"; leaves compiler stderr in
# $WORK/$TAG.err. run_rc is "-" when there is no binary to run.
#
# ⚠ NO PIPES ON THE MEASURED COMMANDS. The exit code is the measurement here,
# and a pipeline reports its LAST component's status: `logosc … | head` would
# read a SIGABRT as 141 or as 0 depending on where the pipe broke.
measure() {
    local src="$1" tag="$2"
    local obj="$WORK/$tag.o" bin="$WORK/$tag.bin"
    local crc rrc
    "$LOGOSC" "$src" -o "$obj" >"$WORK/$tag.out" 2>"$WORK/$tag.err"
    crc=$?
    if [ "$crc" -ne 0 ] || [ ! -f "$obj" ]; then
        printf '%s -\n' "$crc"
        return 0
    fi
    local archives=()
    if [ -n "$LIBDIR" ]; then
        local a
        for a in "$LIBDIR"/liblstdlib*.a;  do [ -f "$a" ] && archives+=("$a"); done
        for a in "$LIBDIR"/liblogos-*.a;   do [ -f "$a" ] && archives+=("$a"); done
        for a in "$LIBDIR"/*.a; do
            case "$(basename "$a")" in
                liblstdlib*|liblogos-*) ;;
                *) [ -f "$a" ] && archives+=("$a") ;;
            esac
        done
    fi
    if [ "${#archives[@]}" -eq 0 ]; then
        echo "GATE BROKEN: no stdlib archives under '${LIBDIR:-<unset>}' — every"      >&2
        echo "             row would fail to link and the gate would report the stand" >&2
        echo "             instead of the compiler."                                    >&2
        exit 2
    fi
    if ! cc "$obj" -Wl,--start-group "${archives[@]}" -Wl,--end-group \
            -lpthread -lm -lstdc++ -Wl,--gc-sections -Wl,--allow-multiple-definition \
            -o "$bin" 2>"$WORK/$tag.link"; then
        echo "GATE BROKEN: link failed for $tag — the stand, not the compiler:" >&2
        cat "$WORK/$tag.link" >&2
        exit 2
    fi
    "$bin" >"$WORK/$tag.run" 2>&1
    rrc=$?
    printf '%s %s\n' "$crc" "$rrc"
}

# ── THE MATCHER ──────────────────────────────────────────────────────────────
# has_text FILE NEEDLE → 0 if NEEDLE occurs in FILE.
# ⚠ Materialise, then match. `cat file | grep -q` under `pipefail` turns the
# producer's SIGPIPE into the pipeline's status.
has_text() {
    local file="$1" needle="$2"
    [ -n "$needle" ] || return 1     # an empty needle matches everything: refuse
    grep -qF -- "$needle" "$file"
}

# ── THE COMPARATOR ───────────────────────────────────────────────────────────
# check_row NAME SRC WANT_CRC WANT_RRC WANT_ERR → prints violations, returns
# the number of them (0 = the row holds).
check_row() {
    local name="$1" src="$2" want_crc="$3" want_rrc="$4" want_err="$5"
    local tag="row_$name" bad=0 got crc rrc
    got="$(measure "$src" "$tag")"
    if [ -z "$got" ]; then
        # `measure` runs in a command substitution, so its `exit 2` on a broken
        # stand only kills the subshell. An empty reading is that, and it must
        # not be reported as a row that moved.
        echo "GATE BROKEN: no reading for $name — see the message above." >&2
        exit 2
    fi
    crc="${got%% *}"; rrc="${got##* }"

    if [ "$crc" != "$want_crc" ]; then
        echo "FAIL: $name — compile exit code $crc, ledger records $want_crc."
        bad=$((bad + 1))
    fi
    if [ "$rrc" != "$want_rrc" ]; then
        echo "FAIL: $name — run exit code $rrc, ledger records $want_rrc."
        bad=$((bad + 1))
    fi
    if ! has_text "$WORK/$tag.err" "$want_err"; then
        echo "FAIL: $name — compiler stderr does not contain:"
        echo "        $want_err"
        echo "      it was:"
        sed 's/^/        /' "$WORK/$tag.err"
        bad=$((bad + 1))
    fi
    return $bad
}

fail=0

# ── CANARY 1: the reader distinguishes exit codes ────────────────────────────
cat > "$WORK/canary_reader.logos" <<'EOF'
package type_apply_gate_canary_reader;
fn main() -> i32 { return 7i32; }
EOF
c1="$(measure "$WORK/canary_reader.logos" canary_reader)"
if [ "$c1" != "0 7" ]; then
    echo "GATE BROKEN (C1): a program that compiles clean and returns 7 measured"
    echo "                  as '$c1', expected '0 7'. The exit codes this gate"
    echo "                  reports about the real rows are not being read."
    cat "$WORK/canary_reader.err"
    exit 1
fi

# ── CANARY 2: the matcher discriminates ──────────────────────────────────────
printf 'alpha beta gamma\n' > "$WORK/canary_match.txt"
if ! has_text "$WORK/canary_match.txt" "beta gamma"; then
    echo "GATE BROKEN (C2): the stderr matcher missed a token that is present."
    exit 1
fi
if has_text "$WORK/canary_match.txt" "delta"; then
    echo "GATE BROKEN (C2): the stderr matcher found a token that is absent —"
    echo "                  it would match every ledger row forever."
    exit 1
fi
if has_text "$WORK/canary_match.txt" ""; then
    echo "GATE BROKEN (C2): the matcher accepted an EMPTY needle, so a ledger row"
    echo "                  with a blank stderr field would assert nothing."
    exit 1
fi

# ── THE LEDGER ───────────────────────────────────────────────────────────────
n_rows=0
declare -a SEEN_NAMES=()
FIRST_SRC=""
FIRST_CRC=""
FIRST_RRC=""
FIRST_ERR=""

while IFS= read -r line || [ -n "$line" ]; do
    case "${line#"${line%%[![:space:]]*}"}" in ''|'#'*) continue ;; esac
    IFS='|' read -r f_name f_crc f_rrc f_err <<< "$line"
    # trim
    f_name="$(printf '%s' "$f_name" | sed 's/^[[:space:]]*//; s/[[:space:]]*$//')"
    f_crc="$( printf '%s' "$f_crc"  | sed 's/^[[:space:]]*//; s/[[:space:]]*$//')"
    f_rrc="$( printf '%s' "$f_rrc"  | sed 's/^[[:space:]]*//; s/[[:space:]]*$//')"
    f_err="$( printf '%s' "$f_err"  | sed 's/^[[:space:]]*//; s/[[:space:]]*$//')"
    if [ -z "$f_name" ] || [ -z "$f_crc" ] || [ -z "$f_rrc" ] || [ -z "$f_err" ]; then
        echo "GATE BROKEN: malformed ledger line (want '<name> | <crc> | <rrc> | <stderr>'):"
        echo "  $line"
        exit 4
    fi
    src="$FIXDIR/$f_name.logos"
    if [ ! -f "$src" ]; then
        echo "FAIL: ledger names '$f_name' but $src does not exist."
        echo "      A row about a program that is gone is a claim nobody can check."
        fail=1
        continue
    fi
    n_rows=$((n_rows + 1))
    SEEN_NAMES+=("$f_name")
    if [ -z "$FIRST_SRC" ]; then
        FIRST_SRC="$src"; FIRST_CRC="$f_crc"; FIRST_RRC="$f_rrc"; FIRST_ERR="$f_err"
    fi

    check_row "$f_name" "$src" "$f_crc" "$f_rrc" "$f_err"
    if [ $? -ne 0 ]; then
        echo "      → If this row IMPROVED, the defect is fixed: delete the row and"
        echo "        land the new behaviour as an ordinary corpus test. If it got"
        echo "        worse, re-measure — do not re-point the expectation."
        fail=1
    fi
done < "$LEDGER"

# ── THE POPULATION IS DERIVED FROM THE ARTIFACT ──────────────────────────────
n_fixtures=0
for f in "$FIXDIR"/*.logos; do
    [ -f "$f" ] || continue
    n_fixtures=$((n_fixtures + 1))
    b="$(basename "$f" .logos)"
    found=0
    for s in ${SEEN_NAMES+"${SEEN_NAMES[@]}"}; do
        [ "$s" = "$b" ] && { found=1; break; }
    done
    if [ "$found" -eq 0 ]; then
        echo "FAIL: $f is a fixture no ledger row names, so nothing measures it."
        fail=1
    fi
done
if [ "$n_rows" -eq 0 ] || [ "$n_fixtures" -eq 0 ]; then
    echo "GATE BROKEN: $n_rows ledger row(s) and $n_fixtures fixture(s) — a gate"
    echo "             with an empty population reports 'clean' about nothing."
    exit 1
fi

# ── CANARY 3: the comparator still compares ──────────────────────────────────
# Feed the FIRST real row a deliberately wrong compile code and a needle that is
# not in its stderr, through the same check_row the ledger went through.
bogus_crc=$((FIRST_CRC + 1))
c3_out="$(check_row "canary_comparator" "$FIRST_SRC" "$bogus_crc" "$FIRST_RRC" \
          "no-such-text-in-any-diagnostic-XYZZY" 2>&1)"
c3_bad=$?
if [ "$c3_bad" -ne 2 ]; then
    echo "GATE BROKEN (C3): check_row reported $c3_bad violation(s) for an"
    echo "                  expectation that is wrong in two places (compile code"
    echo "                  and stderr). It has stopped comparing, and every row"
    echo "                  above passed for that reason."
    echo "$c3_out"
    exit 1
fi

if [ "$fail" -ne 0 ]; then
    echo "type-apply-gate: RED — $n_rows recorded row(s), $n_fixtures fixture(s)."
    exit 1
fi
echo "type-apply-gate: OK — $n_rows recorded row(s) still hold, $n_fixtures fixture(s), 3 canaries caught."
exit 0
