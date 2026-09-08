#!/usr/bin/env bash
# run1.sh <logosc> <libdir> <src>  -> prints "CC=<rc> RUN=<rc> OUT=<stdout first line>"
set -uo pipefail
LOGOSC="$1"; LIB="$2"; SRC="$3"
d=$(mktemp -d); trap 'rm -rf "$d"' EXIT
A=()
for a in "$LIB"/liblstdlib*.a; do [ -f "$a" ] && A+=("$a"); done
for a in "$LIB"/liblogos-*.a;  do [ -f "$a" ] && A+=("$a"); done
for a in "$LIB"/*.a; do case "$(basename "$a")" in liblstdlib*|liblogos-*) ;; *) [ -f "$a" ] && A+=("$a");; esac; done
cc_rc=0
LOGOS_VERIFY_LAYOUT=1 "$LOGOSC" "$SRC" -o "$d/t.o" >"$d/cc.out" 2>"$d/cc.err" || cc_rc=$?
if [ $cc_rc -ne 0 ] || grep -q -E "error( \[|:)" "$d/cc.err"; then
  echo "CC=$cc_rc RUN=- OUT=$(grep -m1 -E 'error( \[|:)' "$d/cc.err" | head -c 120)"; exit 0
fi
if ! cc "$d/t.o" -Wl,--start-group "${A[@]}" -Wl,--end-group -lpthread -lm -lstdc++ \
     -Wl,--gc-sections -Wl,--allow-multiple-definition -o "$d/t" 2>"$d/ld.err"; then
  echo "CC=$cc_rc RUN=LINKFAIL OUT=$(head -c 120 "$d/ld.err" | tr '\n' ' ')"; exit 0
fi
r=0; timeout 60 "$d/t" >"$d/out" 2>/dev/null || r=$?
echo "CC=$cc_rc RUN=$r OUT=$(head -1 "$d/out" | head -c 120)"
