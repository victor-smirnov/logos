#!/usr/bin/env bash
# THE CONUCO GATE — `conuco/memoria` is a real lforge package in this tree, and
# until now NOTHING in the repository ran it.
#
# It builds with lforge, not the repo CMake, so no ctest, no script and no
# workflow touched it. The consequence was measured on 2026-08-16: the D1
# borrow-checker arc (134ce9c4…6c793b8e) hardened `&mut` receivers over thirteen
# rounds with the in-tree L4 green throughout, while 30 of the package's 67
# tests had stopped compiling. The rot was invisible because the only party who
# would have seen it was never asked.
#
# WHAT THIS GATE ASSERTS — three things, not one:
#
#   1. every test the roster does NOT except passes;
#   2. the TOTAL equals EXPECTED_TOTAL. "All tests passed" is worthless if a
#      test can vanish from the run: a deleted/renamed/unbuildable .logos would
#      silently shrink the population and the gate would still read green. The
#      count is the population's own witness.
#   3. every roster entry still FAILS. An expected-failure that starts passing
#      is a gate failure too — otherwise the roster outlives the defect and the
#      next reader believes a bug is open that was fixed months ago.
#
# THE ROSTER IS NOT A SUPPRESSION LIST. Each entry names a defect that is FILED
# and OPEN, with its diagnosis. Adding an entry to make a red gate green is the
# one use this file forbids.
#
# Usage: conuco_memoria.sh <lforge> <logosc> <LOGOS_LIB_DIR>

set -uo pipefail

LFORGE="${1:?lforge path}"
LOGOSC="${2:?logosc path}"
LIB="${3:?LOGOS_LIB_DIR}"

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO=$(cd "$SCRIPT_DIR/../.." && pwd)
PKG="$REPO/conuco/memoria"

# ── The population, and the two known-open compiler defects ─────────────────
#
# EXPECTED_TOTAL is the number of tests/*.logos files the package ships. Bump it
# in the same commit that adds or removes one — that is the point of the number.
EXPECTED_TOTAL=67

# name → the OPEN defect it stands on. Both are compiler defects, not stale
# tests; both are reproducible standalone (see the commit that filed them).
EXPECTED_FAIL=(
    # logosc ABORTS with no diagnostic: `munmap_chunk(): invalid pointer`.
    #
    # THE NAME IS THE WRONG DIAGNOSIS. Neither genericity nor two families is
    # required: the trigger is a `bool` VALUE column, and the 8-line repro is
    #
    #     package test;
    #     use logos.lcm.canon.container_item; use logos.lcm.canon.metaclass;
    #     use logos.mem.pkd; use logos.mem.bt.map;
    #     container M { kind ordered_map; entry { key: u64, val: bool } measure max(key); }
    #     fn fid<C: CtrFamily>() -> u64 { return C::family_id(); }
    #     fn main() -> i32 { if fid::<typeof(M)>() == 0u64 { return 1i32; } return 0i32; }
    #
    # `kind vector; entry { elem: bool }` SIGSEGVs on the same defect. `u8`/`i8`
    # values are fine, so it is not narrowness; `key: bool` is refused CLEANLY
    # (the key path has no bool arm), so only the VALUE path is affected.
    #
    # ATTRIBUTED by disabling the arms one at a time and rebuilding: the trigger
    # is the `vconv` arm — `parse_expr("__raw != 0u64")` at container_item.logos
    # 1730 (ordered_map) / 3262 (vector), spliced as `#(vconv)` into the
    # leaf-batch producer. With both disabled, both kinds emit a clean `cannot
    # cast u64 to bool` instead of crashing; with only the `val_at` arm (595)
    # enabled, no crash.
    #
    # WHAT IT IS: memory corruption inside `logos_emit_item_blob_subst_in`'s
    # `subst_walk` (src/compiler/main.cpp:1720) — valgrind reports two
    # uninitialised-value reads and then an invalid free of a .text address (the
    # `_M_manager` of the `replace_in_parent` std::function) while destroying
    # the frame's `elems` vector at main.cpp:1901.
    #
    # REFUTED, so nobody re-walks them: (a) main-thread stack exhaustion —
    # `ulimit -s 262144` still aborts; (b) unbounded recursion — a 32x stack
    # does not change the time to abort; (c) the pre-reserve bound at
    # main.cpp:~1100 being too small, i.e. the documented arena-move hazard —
    # +64 MB of reserve still aborts, and the arena-moved post-check at
    # main.cpp:2282 is unreachable because the crash happens mid-walk.
    gen_generic_distinct
    # `mlir_gen: internal: `return` value lowered to no value … the RETURN was
    # silently discarded`, on `return unsafe { … }` inside an `impl<K: ?Sized>`.
    gendrop_probe
)

if [ ! -d "$PKG" ]; then
    echo "conuco gate: $PKG is missing — the package moved or was removed;"
    echo "conuco gate: this gate names it explicitly and must move with it."
    exit 1
fi

cd "$PKG" || exit 1

LOG=$(mktemp)
trap 'rm -f "$LOG"' EXIT

# Build first: `lforge test` builds the libs on demand, but a build failure and
# a test failure must not arrive as the same message.
if ! LOGOSC="$LOGOSC" LOGOS_LIB_DIR="$LIB" "$LFORGE" build > "$LOG" 2>&1; then
    echo "conuco gate: FAIL — the package does not BUILD"
    tail -40 "$LOG"
    exit 1
fi

LOGOSC="$LOGOSC" LOGOS_LIB_DIR="$LIB" "$LFORGE" test > "$LOG" 2>&1

# ── Read the population off the run, not off an assumption ──────────────────
SUMMARY=$(grep -E "^lforge: tests passed=" "$LOG" | tail -1)
if [ -z "$SUMMARY" ]; then
    echo "conuco gate: FAIL — no summary line; the run did not complete"
    tail -40 "$LOG"
    exit 1
fi
PASSED=$(echo "$SUMMARY" | sed -n 's/.*passed=\([0-9]*\).*/\1/p')
FAILED=$(echo "$SUMMARY" | sed -n 's/.*failed=\([0-9]*\).*/\1/p')
TOTAL=$((PASSED + FAILED))

FAIL_NAMES=$(grep -E "^lforge: test .* FAIL" "$LOG" | awk '{print $3}' | sort)

rc=0

# (2) the count — a vanished test cannot hide behind a green run.
if [ "$TOTAL" -ne "$EXPECTED_TOTAL" ]; then
    echo "conuco gate: FAIL — population is $TOTAL, expected $EXPECTED_TOTAL"
    echo "conuco gate:   a test was added, removed or renamed. If deliberate,"
    echo "conuco gate:   bump EXPECTED_TOTAL in this script in the SAME commit."
    rc=1
fi

# (1) unexpected failures.
for name in $FAIL_NAMES; do
    known=0
    for e in "${EXPECTED_FAIL[@]}"; do [ "$name" = "$e" ] && known=1; done
    if [ "$known" -eq 0 ]; then
        echo "conuco gate: FAIL — '$name' fails and is not a known-open defect"
        rc=1
    fi
done

# (3) expected failures that now pass — the roster must not outlive the defect.
for e in "${EXPECTED_FAIL[@]}"; do
    if ! echo "$FAIL_NAMES" | grep -qx "$e"; then
        echo "conuco gate: FAIL — '$e' PASSES but is listed as a known-open defect"
        echo "conuco gate:   the defect was fixed: delete the entry from EXPECTED_FAIL."
        rc=1
    fi
done

if [ "$rc" -ne 0 ]; then
    echo "conuco gate: ── failing tests' diagnostics ──"
    grep -E "^error|^warning \[[a-z]|malfunction|^lforge: test .* FAIL" "$LOG" | head -60
    exit 1
fi

echo "conuco gate: OK — $PASSED/$TOTAL pass, ${#EXPECTED_FAIL[@]} known-open defect(s) still failing as recorded"
exit 0
