#!/usr/bin/env bash
LOGOSC=/home/logos/devel/logos/build/bin/logosc; PROG="$1"; LIB=/home/logos/devel/logos/build/lib/logos
id=$(basename "$PROG" .logos); T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
"$LOGOSC" "$PROG" -o "$T/o.o" >/dev/null 2>&1 || { echo -e "$id\tCCFAIL"; exit 0; }
A=(); for a in "$LIB"/liblstdlib*.a "$LIB"/liblogos-*.a; do [ -f "$a" ] && A+=("$a"); done
for a in "$LIB"/*.a; do case "$(basename "$a")" in liblstdlib*|liblogos-*) ;; *) [ -f "$a" ] && A+=("$a");; esac; done
cc "$T/o.o" -Wl,--start-group "${A[@]}" -Wl,--end-group -lpthread -lm -lstdc++ -Wl,--gc-sections -Wl,--allow-multiple-definition -o "$T/bin" 2>/dev/null || { echo -e "$id\tLINKFAIL"; exit 0; }
out=$(timeout 120 valgrind --error-exitcode=0 "$T/bin" 2>&1); rc=$?
echo -e "$id\trc=$rc\t$(echo "$out" | grep -E 'ERROR SUMMARY|definitely lost' | tr '\n' ' ')"
