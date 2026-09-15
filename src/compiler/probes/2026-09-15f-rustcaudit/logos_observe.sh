#!/usr/bin/env bash
# obs.sh SRC OUTDIR — the soundness_queue_gate observe() reader, by hand
src=$1; d=$(mktemp -d "$2/o.$(basename $src .logos).XXXX")
LIB=/home/logos/devel/logos/build/lib/logos
A=(); for a in $LIB/liblstdlib*.a; do [ -f "$a" ] && A+=("$a"); done; for a in $LIB/liblogos-*.a; do [ -f "$a" ] && A+=("$a"); done
for a in $LIB/*.a; do case "$(basename $a)" in liblstdlib*|liblogos-*) ;; *) A+=("$a");; esac; done
cc_rc=0; LOGOS_VERIFY_LAYOUT=1 /home/logos/devel/logos/build/bin/logosc "$src" -o "$d/t.o" >"$d/cc.out" 2>"$d/cc.err" || cc_rc=$?
diag=0; grep -q -E "error( \[|:)" "$d/cc.err" && diag=1
run=-; out=
if [ $cc_rc -eq 0 ] && [ $diag -eq 0 ]; then
  if cc "$d/t.o" -Wl,--start-group "${A[@]}" -Wl,--end-group -lpthread -lm -lstdc++ -Wl,--gc-sections -Wl,--allow-multiple-definition -o "$d/t" 2>"$d/ld.err"; then
    run=0; timeout 60 "$d/t" >"$d/stdout" 2>/dev/null || run=$?; out=$(head -c 100 "$d/stdout"|tr '\n' ' ')
  else run=LINKFAIL; fi
fi
printf '%s\tcc=%s diag=%s run=%s\tstdout=%s\terr=%s\n' "$(basename $src .logos)" $cc_rc $diag $run "$out" "$(grep -E 'error|internal' $d/cc.err | head -2 | tr '\n' '|')"
