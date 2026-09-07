#!/usr/bin/env bash
# obs.sh SRC [LOGOSC] [LIBDIR] -> "name CC DIAG RUN | first-error"
src="$1"; LOGOSC="${2:-/home/logos/devel/logos/build/bin/logosc}"; LIB="${3:-/home/logos/devel/logos/build/lib/logos}"
export LOGOS_VERIFY_LAYOUT=1
d=$(mktemp -d); trap 'rm -rf "$d"' EXIT
A=(); for a in "$LIB"/liblstdlib*.a; do [ -f "$a" ] && A+=("$a"); done
for a in "$LIB"/liblogos-*.a; do [ -f "$a" ] && A+=("$a"); done
for a in "$LIB"/*.a; do case "$(basename "$a")" in liblstdlib*|liblogos-*) ;; *) [ -f "$a" ] && A+=("$a");; esac; done
cc_rc=0; "$LOGOSC" "$src" -o "$d/t.o" >"$d/cc.out" 2>"$d/cc.err" || cc_rc=$?
diag=0; grep -q -E "error( \[|:)" "$d/cc.err" && diag=1
n=$(basename "$src" .logos)
if [ "$cc_rc" -ne 0 ] || [ "$diag" -ne 0 ]; then
  echo "$n CC=$cc_rc DIAG=$diag RUN=- | $(grep -m1 -E 'error( \[|:)' "$d/cc.err" | head -c 160)"; exit 0; fi
if ! cc "$d/t.o" -Wl,--start-group "${A[@]}" -Wl,--end-group -lpthread -lm -lstdc++ -Wl,--gc-sections -Wl,--allow-multiple-definition -o "$d/t" 2>"$d/ld.err"; then
  echo "$n CC=0 DIAG=0 RUN=LINKFAIL |"; exit 0; fi
r=0; timeout 60 "$d/t" >"$d/so" 2>/dev/null || r=$?
echo "$n CC=0 DIAG=0 RUN=$r | out=$(head -c 60 "$d/so" | tr '\n' ' ')"
