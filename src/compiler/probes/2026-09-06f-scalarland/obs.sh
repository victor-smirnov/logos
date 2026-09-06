#!/usr/bin/env bash
# obs.sh <logosc> <outfile> [srcdir]  — one line per program: name cc diag run "first error"
set -u
LOGOSC="$1"; OUT="$2"; SRCDIR="${3:-$(dirname "$0")/hand}"
ROOT=/home/logos/devel/logos
export LOGOS_VERIFY_LAYOUT=1
LIB=$ROOT/build/lib/logos
LINK=()
for a in "$LIB"/liblstdlib*.a; do [ -f "$a" ] && LINK+=("$a"); done
for a in "$LIB"/liblogos-*.a;  do [ -f "$a" ] && LINK+=("$a"); done
for a in "$LIB"/*.a; do case "$(basename "$a")" in liblstdlib*|liblogos-*) ;; *) [ -f "$a" ] && LINK+=("$a") ;; esac; done
: > "$OUT"
for f in "$SRCDIR"/*.logos; do
  n=$(basename "$f" .logos)
  d=$(mktemp -d)
  cc_rc=0
  "$LOGOSC" "$f" -o "$d/t.o" >"$d/cc.out" 2>"$d/cc.err" || cc_rc=$?
  diag=0; grep -q -E "error( \[|:)" "$d/cc.err" && diag=1
  run="-"
  if [ "$cc_rc" -eq 0 ] && [ "$diag" -eq 0 ]; then
    if cc "$d/t.o" -Wl,--start-group "${LINK[@]}" -Wl,--end-group -lpthread -lm -lstdc++ \
        -Wl,--gc-sections -Wl,--allow-multiple-definition -o "$d/t" 2>"$d/ld.err"; then
      run=0; timeout 60 "$d/t" >"$d/out" 2>/dev/null || run=$?
    else run=LINKFAIL; fi
  fi
  msg=$(grep -m1 -E "error( \[|:)" "$d/cc.err" | cut -c1-140 | tr -d '\n')
  printf '%-6s cc=%-3s diag=%s run=%-8s %s\n' "$n" "$cc_rc" "$diag" "$run" "$msg" >> "$OUT"
  rm -rf "$d"
done
