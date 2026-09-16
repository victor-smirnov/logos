#!/usr/bin/env bash
# run_one.sh LOGOSC FILE.logos  → "<name> CCRC=<n>" or "<name> exit=<n> out=[...]"
LOGOSC="$1"; F="$2"; N=$(basename "$F" .logos)
T=$(mktemp -d); LIB="${LOGOS_LIB_DIR:?}"
if ! "$LOGOSC" "$F" -o "$T/o.o" 2>"$T/e.txt"; then
  echo "$N CCRC=1 firstline=[$(head -c 200 "$T/e.txt" | head -1)]"; exit 0; fi
A=(); for a in "$LIB"/liblstdlib*.a "$LIB"/liblogos-*.a; do [ -f "$a" ] && A+=("$a"); done
for a in "$LIB"/*.a; do case "$(basename "$a")" in liblstdlib*|liblogos-*) ;; *) [ -f "$a" ] && A+=("$a");; esac; done
if ! cc "$T/o.o" -Wl,--start-group "${A[@]}" -Wl,--end-group -lpthread -lm -lstdc++ -Wl,--gc-sections -Wl,--allow-multiple-definition -o "$T/b" 2>/dev/null; then
  echo "$N LINKFAIL"; exit 0; fi
OUT=$("$T/b" 2>/dev/null); RC=$?
echo "$N exit=$RC out=[$(echo $OUT)]"
