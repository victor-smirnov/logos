#!/usr/bin/env bash
# cond_move_field_valgrind_gate.sh LOGOSC FIXTURE LIB_DIR
#
# THE SECOND ORACLE FOR THE #118/#121 DROP-FLAG KEYSPACE.
#
# The first oracle is the fixture's own printing: one `D<n>` per `M<n>`, checked
# by `run_test.sh` against the `.expected`. That oracle is DEMONSTRATED BLIND in
# one direction and blind-by-construction in another:
#
#   • blind to release. A user-`Drop` value held in a struct FIELD runs its
#     destructor and never releases its own owned sub-fields (task #133, 64 B
#     per value). An 18-program suite measured "all pairs balanced" while 12 of
#     them leaked. A printing destructor proves a CALL, not a release.
#   • blind to the payload. `D<n>` is printed by the destructor's first
#     statement; whether the second one freed a live block, a freed block, or a
#     block that was never allocated is not in the transcript.
#
# So the fixture allocates with `malloc` and its destructor `free`s, and this
# gate re-runs the SAME binary under valgrind:
#
#   a MISSED destructor  -> "definitely lost"   (the #118/#121 leak direction)
#   a DOUBLED destructor -> "Invalid free"      (the #121-A double-free)
#
# Both are demanded absent. `--error-exitcode` is not relied on alone — the
# summary is parsed and the leak total asserted to be exactly zero bytes, so a
# future valgrind that classifies a loss as "still reachable" cannot turn a leak
# into a pass.
#
# ⚠ NO VALGRIND, NO VERDICT. An absent instrument exits 2 (a red), never 0:
# "asked git, not the build" is the first recorded way a gate lies, and a gate
# that skips itself into green is the same lie wearing a different hat.
set -uo pipefail

LOGOSC="${1:?usage: $0 LOGOSC FIXTURE LIB_DIR}"
FIXTURE="${2:?}"
LIB_DIR="${3:?}"

if ! command -v valgrind > /dev/null 2>&1; then
    echo "FAIL(2): valgrind is not installed. This gate MEASURES allocation"
    echo "         balance; without the instrument it has no verdict. Install"
    echo "         valgrind (Debian/Ubuntu: apt install valgrind) or take this"
    echo "         test out of the suite deliberately — do NOT let it skip."
    exit 2
fi

TMPD="$(mktemp -d)"
trap 'rm -rf "$TMPD"' EXIT

export LOGOS_LIB_DIR="$LIB_DIR"
if ! "$LOGOSC" "$FIXTURE" -o "$TMPD/f.o" > "$TMPD/cc.log" 2>&1; then
    echo "FAIL(2): logosc failed on $FIXTURE"; tail -20 "$TMPD/cc.log"; exit 2
fi
# ⚠ THE COMPILER CAN WRITE AN OBJECT AND EXIT 0 AFTER SELF-DIAGNOSING (the 14th
# recorded gate lie, task #103). Read the log too.
if grep -qE '^(mlir_gen|sema|mono): ' "$TMPD/cc.log"; then
    echo "FAIL(2): logosc exited 0 but self-diagnosed:"; grep -E '^(mlir_gen|sema|mono): ' "$TMPD/cc.log" | head -5; exit 2
fi

ARCHIVES=()
for a in "$LIB_DIR"/liblstdlib*.a; do [ -f "$a" ] && ARCHIVES+=("$a"); done
for a in "$LIB_DIR"/liblogos-*.a;  do [ -f "$a" ] && ARCHIVES+=("$a"); done
for a in "$LIB_DIR"/*.a; do
    case "$(basename "$a")" in
        liblstdlib*|liblogos-*) ;;
        *) [ -f "$a" ] && ARCHIVES+=("$a") ;;
    esac
done
if ! cc "$TMPD/f.o" -Wl,--start-group "${ARCHIVES[@]}" -Wl,--end-group \
        -lpthread -lm -lstdc++ -Wl,--gc-sections -Wl,--allow-multiple-definition \
        -o "$TMPD/f.bin" 2> "$TMPD/link.log"; then
    echo "FAIL(2): cc link failed"; tail -20 "$TMPD/link.log"; exit 2
fi

valgrind --leak-check=full --errors-for-leak-kinds=definite,indirect \
         --error-exitcode=97 "$TMPD/f.bin" > "$TMPD/out" 2> "$TMPD/vg"
VG_RC=$?

# The program itself must have succeeded — a crash under valgrind would leave
# allocations unreleased and the leak numbers would then be measuring the crash.
if [ "$VG_RC" != 0 ] && [ "$VG_RC" != 97 ]; then
    echo "FAIL(3): the fixture exited $VG_RC under valgrind (expected 0)"
    tail -30 "$TMPD/vg"; exit 3
fi

INVALID=$(grep -cE 'Invalid free|Invalid read|Invalid write|Mismatched free' "$TMPD/vg")
DEF=$(sed -n 's/.*definitely lost: *\([0-9,]*\) bytes.*/\1/p'  "$TMPD/vg" | tr -d ',')
IND=$(sed -n 's/.*indirectly lost: *\([0-9,]*\) bytes.*/\1/p' "$TMPD/vg" | tr -d ',')
# A clean run prints "All heap blocks were freed" INSTEAD of a LEAK SUMMARY, so
# an absent pair of numbers is only admissible beside that sentence.
if [ -z "$DEF" ] && [ -z "$IND" ] && grep -q 'All heap blocks were freed' "$TMPD/vg"; then
    DEF=0; IND=0
fi
: "${DEF:=MISSING}"; : "${IND:=MISSING}"

# THE ALLOCATION BALANCE, read from the line valgrind ALWAYS prints. This is the
# gate's primary two-direction number and it does not depend on the leak
# classifier: a missed destructor leaves allocs > frees, a doubled one raises
# frees (and trips "Invalid free" above).
ALLOCS=$(sed -n 's/.*total heap usage: *\([0-9,]*\) allocs, *\([0-9,]*\) frees.*/\1/p' "$TMPD/vg" | tr -d ',')
FREES=$( sed -n 's/.*total heap usage: *\([0-9,]*\) allocs, *\([0-9,]*\) frees.*/\2/p' "$TMPD/vg" | tr -d ',')

# ⚠ ASSERT THE INSTRUMENT REPORTED. Unparsed numbers would compare as green.
if [ "$DEF" = MISSING ] || [ "$IND" = MISSING ] || [ -z "$ALLOCS" ] || [ -z "$FREES" ]; then
    echo "FAIL(4): valgrind produced no heap summary — the gate has no verdict"
    tail -30 "$TMPD/vg"; exit 4
fi
if [ "$ALLOCS" -lt 40 ]; then
    echo "FAIL(4): only $ALLOCS allocations recorded — the fixture cannot have"
    echo "         run its cells; the gate would be measuring an empty program"
    exit 4
fi

FAIL=0
if [ "$INVALID" -ne 0 ]; then
    echo "FAIL(5): $INVALID invalid-access record(s) — a DOUBLE FREE (a place"
    echo "         destroyed twice: an ancestor's guarded drop taking a subtree"
    echo "         AS A UNIT over a descendant already moved out, #121-A)"
    grep -E 'Invalid free|Invalid read|Invalid write|Mismatched free' "$TMPD/vg" | head -5
    FAIL=1
fi
if [ "$ALLOCS" -ne "$FREES" ]; then
    echo "FAIL(6): $ALLOCS allocations vs $FREES frees — the destructor count"
    echo "         and the release count disagree"
    FAIL=1
fi
if [ "$DEF" -ne 0 ] || [ "$IND" -ne 0 ]; then
    echo "FAIL(6): definitely lost $DEF bytes, indirectly lost $IND bytes — a"
    echo "         MISSED destructor (the #118/#121 leak direction; rc 0 and"
    echo "         silent, which is exactly why this gate exists)"
    grep -E 'definitely lost|indirectly lost' "$TMPD/vg" | head -5
    FAIL=1
fi
[ "$FAIL" != 0 ] && exit 5

echo "PASS: $(basename "$FIXTURE") — $ALLOCS allocs / $FREES frees, 0 invalid"
echo "      accesses, 0 bytes definitely or indirectly lost (valgrind rc $VG_RC)"
exit 0
