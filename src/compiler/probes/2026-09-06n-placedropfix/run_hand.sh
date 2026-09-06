#!/usr/bin/env bash
# run_hand.sh [ARM] — compile, link and RUN every hand program plus the three
# TARGET ROW programs, with LOGOS_PROBE=ARM (or nothing when ARM is empty).
#
# THE ORACLE IS STDOUT, NOT THE EXIT CODE. A destructor accumulator reaches 1011
# and an exit status is eight bits: 1011 & 0xFF = 243, so an rc oracle would have
# reported the tuple row's correct answer and its wrong one as different values of
# the same wrong thing. Each program prints `COUNT=<n>` and returns 0.
set -uo pipefail
cd "$(dirname "$0")/../../../.." || exit 2
ARM="${1:-}"
LOGOSC="${LOGOSC:-build/bin/logosc}"
LIB="${LOGOS_LIB_DIR:-$PWD/build/lib/logos}"
HAND="src/compiler/probes/2026-09-06n-placedropfix/hand"
ROWS="tests/soundness/open"
TMPD=$(mktemp -d); trap 'rm -rf "$TMPD"' EXIT
export LOGOS_VERIFY_LAYOUT=1
A=(); for a in "$LIB"/liblstdlib*.a; do [ -f "$a" ] && A+=("$a"); done
for a in "$LIB"/liblogos-*.a; do [ -f "$a" ] && A+=("$a"); done
for a in "$LIB"/*.a; do case "$(basename "$a")" in liblstdlib*|liblogos-*) ;; *) [ -f "$a" ] && A+=("$a");; esac; done

one() {
    local src="$1" d; d=$(mktemp -d -p "$TMPD"); local cc=0
    if [ -n "$ARM" ]; then export LOGOS_PROBE="$ARM"; else unset LOGOS_PROBE; fi
    "$LOGOSC" "$src" -o "$d/t.o" >"$d/cc.out" 2>"$d/cc.err" || cc=$?
    if grep -q -E "error( \[|:)" "$d/cc.err"; then
        printf '%-46s REFUSED   %s\n' "$(basename "$src" .logos)" \
            "$(grep -m1 -E 'error( \[|:)' "$d/cc.err" | cut -c1-140)"; return
    fi
    if [ "$cc" -ne 0 ]; then printf '%-46s CCFAIL rc=%s\n' "$(basename "$src" .logos)" "$cc"; return; fi
    if ! cc "$d/t.o" -Wl,--start-group "${A[@]}" -Wl,--end-group -lpthread -lm -lstdc++ \
            -Wl,--gc-sections -Wl,--allow-multiple-definition -o "$d/t" 2>"$d/ld.err"; then
        printf '%-46s LINKFAIL\n' "$(basename "$src" .logos)"; return; fi
    local rc=0; timeout 30 "$d/t" >"$d/out" 2>/dev/null || rc=$?
    printf '%-46s rc=%-4s %s\n' "$(basename "$src" .logos)" "$rc" \
        "$(grep -m1 'COUNT=' "$d/out" || head -c 60 "$d/out" | tr '\n' ' ')"
}
echo "=== ARM='${ARM:-<none>}'  logosc=$LOGOSC ==="
for f in "$HAND"/*.logos; do one "$f"; done
echo "--- TARGET ROWS (rc 0 = the defect is GONE; rc 1 = still wrong) ---"
for r in assign_tuple_elem_no_drop_old assign_index_elem_no_drop_old \
         assign_field_path_not_var_rooted_no_drop_old tuple_elem_reinit_after_move_never_dropped \
         nested_tuple_field_assign_unimplemented; do one "$ROWS/$r.logos"; done
