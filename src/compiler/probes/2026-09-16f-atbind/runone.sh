#!/usr/bin/env bash
# runone.sh <logosc> <prog.logos> — compile, link, run; print "<id>\t<ccrc>\t<runrc>\t<stdout>"
LOGOSC="$1"; PROG="$2"; LIB="${LOGOS_LIB_DIR:-/home/logos/devel/logos/build/lib/logos}"
id=$(basename "$PROG" .logos)
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
if ! "$LOGOSC" "$PROG" -o "$T/o.o" > "$T/cc.err" 2>&1; then
  echo -e "$id\tCCFAIL\t-\t$(grep -m1 -E 'error|Error' "$T/cc.err" | cut -c1-120)"; exit 0
fi
A=(); for a in "$LIB"/liblstdlib*.a "$LIB"/liblogos-*.a; do [ -f "$a" ] && A+=("$a"); done
for a in "$LIB"/*.a; do case "$(basename "$a")" in liblstdlib*|liblogos-*) ;; *) [ -f "$a" ] && A+=("$a");; esac; done
if ! cc "$T/o.o" -Wl,--start-group "${A[@]}" -Wl,--end-group -lpthread -lm -lstdc++ -Wl,--gc-sections -Wl,--allow-multiple-definition -o "$T/bin" 2>/dev/null; then
  echo -e "$id\tLINKFAIL\t-\t-"; exit 0
fi
out=$("$T/bin" 2>/dev/null); rc=$?
echo -e "$id\tOK\t$rc\t$(echo "$out" | tr '\n' ' ')"
