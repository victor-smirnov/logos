#!/usr/bin/env bash
# hrun.sh LOGOSC LIBDIR SRC [vg] — compile (LOGOS_VERIFY_LAYOUT=1), link like the queue gate, run; print one line:
#   <name> cc=<rc> run=<rc|-> out=<stdout one line> [vg=<ERROR SUMMARY + leak line>] diag=<first error line>
LOGOSC="$1"; LIB="$2"; SRC="$3"; VG="${4:-}"
export LOGOS_VERIFY_LAYOUT=1
A=()
for a in "$LIB"/liblstdlib*.a; do [ -f "$a" ] && A+=("$a"); done
for a in "$LIB"/liblogos-*.a; do [ -f "$a" ] && A+=("$a"); done
for a in "$LIB"/*.a; do case "$(basename "$a")" in liblstdlib*|liblogos-*) ;; *) [ -f "$a" ] && A+=("$a");; esac; done
d=$(mktemp -d -p "${HR_TMP:?HR_TMP}")
n=$(basename "$SRC" .logos)
cc_rc=0; LOGOS_LIB_DIR="$LIB" "$LOGOSC" "$SRC" -o "$d/t.o" >"$d/cc.out" 2>"$d/cc.err" || cc_rc=$?
diag=$(grep -m1 -E "error( \[|:)|\]: " "$d/cc.err" | cut -c1-160)
if [ "$cc_rc" -ne 0 ] || grep -q -E "error( \[|:)" "$d/cc.err"; then echo "$n cc=$cc_rc run=- diag=$diag"; exit 0; fi
if ! cc "$d/t.o" -Wl,--start-group "${A[@]}" -Wl,--end-group -lpthread -lm -lstdc++ -Wl,--gc-sections -Wl,--allow-multiple-definition -o "$d/t" 2>"$d/ld.err"; then echo "$n cc=0 run=LINKFAIL"; exit 0; fi
r=0; timeout 60 "$d/t" >"$d/stdout" 2>/dev/null || r=$?
out=$(tr '\n' ' ' < "$d/stdout" | cut -c1-80)
vgs=""
if [ -n "$VG" ]; then
  timeout 120 valgrind --leak-check=full --error-exitcode=99 "$d/t" >/dev/null 2>"$d/vg.err"
  es=$(grep -m1 'ERROR SUMMARY' "$d/vg.err" | sed 's/^==[0-9]*== //' | cut -c1-40)
  al=$(grep -m1 'total heap usage' "$d/vg.err" | sed 's/^==[0-9]*== *//')
  vgs=" vg=[$es; $al]"
fi
echo "$n cc=0 run=$r out=$out$vgs"
