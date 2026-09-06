#!/usr/bin/env bash
# leak_gate.sh LOGOSC FIXTURE LIB_DIR [MIN_ALLOCS]
#
# THE LEAK ORACLE FOR A FIXTURE THAT PRINTS.
#
# `tests/logos/cond_move_field_valgrind_gate.sh` is the precedent in this tree
# and this gate is deliberately NOT a copy of it. That one asserts
# `allocs == frees`, which is exact for a fixture that touches the heap only
# through its own malloc/free pairs and prints nothing. It is WRONG for any
# program that calls `println!`: the runtime's own stdout state is two blocks
# that are never freed and are reported as `still reachable`, so a perfectly
# clean printing program has allocs = frees + 2 and would red that gate.
#
# ⚠ AND `still reachable` IS NOT A LEAK. A block a pointer still reaches at
# exit was not lost; asserting it away would be asserting that the runtime
# tears down state it has no reason to tear down. So the balance is stated
# with the reachable blocks CARRIED, not dropped:
#
#       allocs - frees == still-reachable blocks
#
# which is the same destructor-count claim as the precedent's equality — every
# block that was not deliberately kept alive at exit was released — and it is
# the equality itself when nothing is reachable. Nothing is weakened: a missed
# destructor still shows up as `definitely lost` AND as a broken balance, and a
# doubled one as `Invalid free`.
#
# ⚠ NO VALGRIND, NO VERDICT: an absent instrument exits 2, never 0.
# ⚠ MIN_ALLOCS is the "the program actually ran" floor. A gate whose subject
# allocated nothing has measured coverage, not evidence.
set -uo pipefail

LOGOSC="${1:?usage: $0 LOGOSC FIXTURE LIB_DIR [MIN_ALLOCS]}"
FIXTURE="${2:?}"
LIB_DIR="${3:?}"
MIN_ALLOCS="${4:-1}"

if ! command -v valgrind > /dev/null 2>&1; then
    echo "FAIL(2): valgrind is not installed. This gate MEASURES release;"
    echo "         without the instrument it has no verdict. Install valgrind"
    echo "         or take this test out deliberately — do NOT let it skip."
    exit 2
fi

TMPD="$(mktemp -d)"
trap 'rm -rf "$TMPD"' EXIT

export LOGOS_LIB_DIR="$LIB_DIR"
if ! "$LOGOSC" "$FIXTURE" -o "$TMPD/f.o" > "$TMPD/cc.log" 2>&1; then
    echo "FAIL(2): logosc failed on $FIXTURE"; tail -20 "$TMPD/cc.log"; exit 2
fi
# ⚠ THE COMPILER CAN WRITE AN OBJECT AND EXIT 0 AFTER SELF-DIAGNOSING (task #103).
if grep -qE '^(mlir_gen|sema|mono): ' "$TMPD/cc.log"; then
    echo "FAIL(2): logosc exited 0 but self-diagnosed:"
    grep -E '^(mlir_gen|sema|mono): ' "$TMPD/cc.log" | head -5; exit 2
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

valgrind --leak-check=full --show-leak-kinds=definite,indirect \
         --errors-for-leak-kinds=definite,indirect \
         --error-exitcode=97 "$TMPD/f.bin" > "$TMPD/out" 2> "$TMPD/vg"
VG_RC=$?
if [ "$VG_RC" != 0 ] && [ "$VG_RC" != 97 ]; then
    echo "FAIL(3): the fixture exited $VG_RC under valgrind (expected 0). A"
    echo "         crash leaves allocations unreleased, so the leak numbers"
    echo "         below would be measuring the crash."
    tail -30 "$TMPD/vg"; exit 3
fi

INVALID=$(grep -cE 'Invalid free|Invalid read|Invalid write|Mismatched free' "$TMPD/vg")
DEF=$(sed -n 's/.*definitely lost: *\([0-9,]*\) bytes.*/\1/p'  "$TMPD/vg" | tr -d ',')
IND=$(sed -n 's/.*indirectly lost: *\([0-9,]*\) bytes.*/\1/p'  "$TMPD/vg" | tr -d ',')
RCH=$(sed -n 's/.*still reachable: *[0-9,]* bytes in *\([0-9,]*\) blocks.*/\1/p' "$TMPD/vg" | tr -d ',')
# A wholly clean run prints "All heap blocks were freed" INSTEAD of a LEAK
# SUMMARY, so absent numbers are admissible only beside that sentence.
if [ -z "$DEF" ] && [ -z "$IND" ] && grep -q 'All heap blocks were freed' "$TMPD/vg"; then
    DEF=0; IND=0; RCH=0
fi
: "${DEF:=MISSING}"; : "${IND:=MISSING}"; : "${RCH:=0}"

ALLOCS=$(sed -n 's/.*total heap usage: *\([0-9,]*\) allocs, *\([0-9,]*\) frees.*/\1/p' "$TMPD/vg" | tr -d ',')
FREES=$( sed -n 's/.*total heap usage: *\([0-9,]*\) allocs, *\([0-9,]*\) frees.*/\2/p' "$TMPD/vg" | tr -d ',')

# ⚠ ASSERT THE INSTRUMENT REPORTED. Unparsed numbers would compare as green.
if [ "$DEF" = MISSING ] || [ "$IND" = MISSING ] || [ -z "$ALLOCS" ] || [ -z "$FREES" ]; then
    echo "FAIL(4): valgrind produced no heap summary — the gate has no verdict"
    tail -30 "$TMPD/vg"; exit 4
fi
if [ "$ALLOCS" -lt "$MIN_ALLOCS" ]; then
    echo "FAIL(4): only $ALLOCS allocations recorded, floor is $MIN_ALLOCS — the"
    echo "         fixture cannot have run its cells, and a clean valgrind on a"
    echo "         program that never called malloc is coverage, not evidence."
    exit 4
fi

FAIL=0
if [ "$INVALID" -ne 0 ]; then
    echo "FAIL(5): $INVALID invalid-access record(s) — a block used or released twice"
    grep -E 'Invalid free|Invalid read|Invalid write|Mismatched free' "$TMPD/vg" | head -5
    FAIL=1
fi
if [ "$DEF" -ne 0 ] || [ "$IND" -ne 0 ]; then
    echo "FAIL(6): definitely lost $DEF bytes, indirectly lost $IND bytes — a"
    echo "         MISSED destructor. rc 0 and silent to every other oracle in"
    echo "         this tree, which is exactly why this gate exists."
    grep -E 'definitely lost|indirectly lost' "$TMPD/vg" | head -5
    FAIL=1
fi
if [ "$((ALLOCS - FREES))" -ne "$RCH" ]; then
    echo "FAIL(6): $ALLOCS allocs - $FREES frees = $((ALLOCS - FREES)), but only"
    echo "         $RCH block(s) are still reachable at exit. Every block not"
    echo "         deliberately kept alive must have been released."
    FAIL=1
fi
[ "$FAIL" != 0 ] && exit 5

echo "PASS: $(basename "$FIXTURE") — $ALLOCS allocs / $FREES frees, $RCH still"
echo "      reachable (not a leak, not asserted away), 0 invalid accesses,"
echo "      0 bytes definitely or indirectly lost (valgrind rc $VG_RC)"
exit 0
