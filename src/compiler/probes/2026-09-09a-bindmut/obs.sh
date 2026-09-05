#!/usr/bin/env bash
# obs.sh LOGOSC SRC...  — mirrors soundness_queue_gate.sh observe(): prints "name cc/diag/run  <first error line>"
LOGOSC="$1"; shift
export LOGOS_VERIFY_LAYOUT=1
LIB=/home/logos/devel/logos/build/lib/logos
A=(); for a in $LIB/liblstdlib*.a; do [ -f "$a" ] && A+=("$a"); done; for a in $LIB/liblogos-*.a; do [ -f "$a" ] && A+=("$a"); done
for a in $LIB/*.a; do case "$(basename $a)" in liblstdlib*|liblogos-*) ;; *) [ -f "$a" ] && A+=("$a");; esac; done
for src in "$@"; do
  d=$(mktemp -d); cc_=0; diag=0; run="-"
  "$LOGOSC" "$src" -o "$d/t.o" >"$d/cc.out" 2>"$d/cc.err" || cc_=$?
  grep -q -E "error( \[|:)" "$d/cc.err" && diag=1
  if [ $cc_ -eq 0 ] && [ $diag -eq 0 ]; then
    if cc "$d/t.o" -Wl,--start-group "${A[@]}" -Wl,--end-group -lpthread -lm -lstdc++ -Wl,--gc-sections -Wl,--allow-multiple-definition -o "$d/t" 2>"$d/ld.err"; then
      run=0; timeout 60 "$d/t" >"$d/stdout" 2>/dev/null || run=$?
    else run=LINKFAIL; fi
  fi
  printf '%-40s %s/%s/%s  %s\n' "$(basename $src .logos)" "$cc_" "$diag" "$run" "$(grep -E 'error( \[|:)' $d/cc.err | head -1 | sed -E "s/^.*error \[[^]]*\]: //; s/^error: //" | cut -c1-110)"
  [ -n "${OBS_KEEP:-}" ] && echo "$d" || rm -rf "$d"
done
