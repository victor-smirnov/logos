#!/usr/bin/env bash
# run1.sh <logosc> <src>  -> prints "<cc-rc>/<diag>/<run-rc> first-error-line"
L=$1; SRC=$2; LIB=/home/logos/devel/logos/build/lib/logos
d=$(mktemp -d); export LOGOS_VERIFY_LAYOUT=1
cc=0; "$L" "$SRC" -o $d/t.o >$d/out 2>$d/err || cc=$?
diag=0; grep -q -E "error( \[|:)" $d/err && diag=1
run="-"
if [ $cc -eq 0 ] && [ $diag -eq 0 ]; then
  A=(); for a in $LIB/liblstdlib*.a $LIB/liblogos-*.a; do [ -f "$a" ] && A+=("$a"); done
  if cc $d/t.o -Wl,--start-group "${A[@]}" -Wl,--end-group -lpthread -lm -lstdc++ -Wl,--gc-sections -Wl,--allow-multiple-definition -o $d/t 2>$d/ld; then run=0; timeout 60 $d/t >$d/so 2>/dev/null || run=$?; else run=LINKFAIL; fi
fi
printf '%s\t%s/%s/%s\t%s\n' "$(basename $SRC .logos)" $cc $diag $run "$(grep -m1 -E 'error( \[|:)' $d/err | cut -c1-110)"
rm -rf $d
