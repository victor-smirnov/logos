#!/usr/bin/env bash
# matrix.sh — every hand program x every probe name (plus unarmed):
# COMPILE rc, RUN rc, stdout sha. The oracle is a RUN, not a compile.
set -uo pipefail
ROOT=/home/logos/devel/logos
HAND="$ROOT/build/hand-2026-09-04five"
LOGOSC=$ROOT/build/bin/logosc
LIB_DIR=$ROOT/build/lib/logos
export LOGOS_LIB_DIR="$LIB_DIR"
ARCHIVES=()
for a in "$LIB_DIR"/liblstdlib*.a; do [ -f "$a" ] && ARCHIVES+=("$a"); done
for a in "$LIB_DIR"/liblogos-*.a;  do [ -f "$a" ] && ARCHIVES+=("$a"); done
for a in "$LIB_DIR"/*.a; do case "$(basename "$a")" in liblstdlib*|liblogos-*) ;; *) [ -f "$a" ] && ARCHIVES+=("$a");; esac; done
one() {  # $1=probe-or-"-"  $2=src
    local TMPD; TMPD="$(mktemp -d)"
    local ENVP=(); [ "$1" != "-" ] && ENVP=(env "LOGOS_PROBE=$1")
    "${ENVP[@]}" "$LOGOSC" "$2" -o "$TMPD/f.o" > "$TMPD/cc.log" 2>&1; local CR=$?
    if grep -qE '^(mlir_gen|sema|mono): ' "$TMPD/cc.log"; then CR=90; fi
    if [ $CR != 0 ]; then echo -e "$CR\t-\t-"; rm -rf "$TMPD"; return; fi
    if ! cc "$TMPD/f.o" -Wl,--start-group "${ARCHIVES[@]}" -Wl,--end-group -lpthread -lm -lstdc++ \
         -Wl,--gc-sections -Wl,--allow-multiple-definition -o "$TMPD/f.bin" 2>/dev/null; then
        echo -e "$CR\tLINKFAIL\t-"; rm -rf "$TMPD"; return; fi
    "$TMPD/f.bin" > "$TMPD/out" 2>/dev/null; local RR=$?
    echo -e "$CR\t$RR\t$(sha256sum < "$TMPD/out" | cut -c1-8)"
    rm -rf "$TMPD"
}
printf '%-22s %-26s %s\n' program probe "cc/run/sha"
for f in "$HAND"/*.logos; do
  b=$(basename "$f" .logos)
  for p in - bxfldmv dstrbind dropident clowndyn derefclos; do
    printf '%-22s %-26s %s\n' "$b" "$p" "$(one "$p" "$f")"
  done
done
