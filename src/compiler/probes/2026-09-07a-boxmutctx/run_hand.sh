#!/usr/bin/env bash
# run_hand.sh LOGOSC DIR [names...] — compile, link, run each hand program; print name cc run firstdiag
LOGOSC="$1"; DIR="$2"; shift 2
ROOT=/home/logos/devel/logos
LIB=$ROOT/build/lib/logos
A=()
for a in "$LIB"/liblstdlib*.a; do [ -f "$a" ] && A+=("$a"); done
for a in "$LIB"/liblogos-*.a;  do [ -f "$a" ] && A+=("$a"); done
for a in "$LIB"/*.a; do case "$(basename "$a")" in liblstdlib*|liblogos-*) ;; *) [ -f "$a" ] && A+=("$a") ;; esac; done
export LOGOS_VERIFY_LAYOUT=1
T=$(mktemp -d)
for f in "$DIR"/*.logos; do
  n=$(basename "$f" .logos)
  if [ $# -gt 0 ]; then case " $* " in *" ${n%%_*} "*) ;; *) continue;; esac; fi
  d=$T/$n; mkdir -p $d
  cc=0; "$LOGOSC" "$f" -o $d/t.o >$d/out 2>$d/err || cc=$?
  diag=$(grep -E "error( \[|:)" $d/err | head -1 | sed 's#.*error#error#')
  run='-'
  if [ $cc -eq 0 ] && [ -z "$diag" ]; then
    if cc $d/t.o -Wl,--start-group "${A[@]}" -Wl,--end-group -lpthread -lm -lstdc++ -Wl,--gc-sections -Wl,--allow-multiple-definition -o $d/t 2>$d/ld; then
      run=0; timeout 60 $d/t >$d/stdout 2>/dev/null || run=$?
    else run=LINKFAIL; fi
  fi
  nerr=$(grep -cE "error( \[|:)" $d/err)
  printf '%-40s cc=%-3s run=%-8s errs=%s  %s\n' "$n" "$cc" "$run" "$nerr" "$diag"
done
rm -rf $T
