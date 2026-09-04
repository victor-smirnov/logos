#!/usr/bin/env bash
# run1.sh [-v] <src.logos> [probe] — compile, link, RUN. Prints cc rc, run rc, stdout.
ROOT=/home/logos/devel/logos
LOGOSC=$ROOT/build/bin/logosc
LIB_DIR=$ROOT/build/lib/logos
export LOGOS_LIB_DIR="$LIB_DIR"
VG=0; if [ "$1" = "-v" ]; then VG=1; shift; fi
SRC=$1; P=${2:--}
ARCHIVES=()
for a in "$LIB_DIR"/liblstdlib*.a; do [ -f "$a" ] && ARCHIVES+=("$a"); done
for a in "$LIB_DIR"/liblogos-*.a;  do [ -f "$a" ] && ARCHIVES+=("$a"); done
for a in "$LIB_DIR"/*.a; do case "$(basename "$a")" in liblstdlib*|liblogos-*) ;; *) [ -f "$a" ] && ARCHIVES+=("$a");; esac; done
TMPD=$(mktemp -d)
ENVP=(); [ "$P" != "-" ] && ENVP=(env "LOGOS_PROBE=$P")
"${ENVP[@]}" "$LOGOSC" "$SRC" -o "$TMPD/f.o" > "$TMPD/cc.log" 2>&1; CR=$?
if grep -qE '^(mlir_gen|sema|mono): ' "$TMPD/cc.log"; then CR=90; fi
if [ $CR != 0 ]; then echo "CC=$CR"; sed -n '1,6p' "$TMPD/cc.log"; rm -rf "$TMPD"; exit 0; fi
if ! cc "$TMPD/f.o" -Wl,--start-group "${ARCHIVES[@]}" -Wl,--end-group -lpthread -lm -lstdc++ \
     -Wl,--gc-sections -Wl,--allow-multiple-definition -o "$TMPD/f.bin" 2>"$TMPD/ld.log"; then
  echo "CC=$CR LINKFAIL"; sed -n '1,6p' "$TMPD/ld.log"; rm -rf "$TMPD"; exit 0; fi
"$TMPD/f.bin" > "$TMPD/out" 2>"$TMPD/err"; RR=$?
echo "CC=$CR RUN=$RR"
echo "--stdout--"; cat "$TMPD/out"
if [ -s "$TMPD/err" ]; then echo "--stderr--"; head -4 "$TMPD/err"; fi
if [ $VG = 1 ]; then
  echo "--valgrind--"
  valgrind --error-exitcode=97 "$TMPD/f.bin" 2>&1 >/dev/null | grep -E "ERROR SUMMARY|Invalid|definitely lost|total heap usage" | head -8
fi
rm -rf "$TMPD"
