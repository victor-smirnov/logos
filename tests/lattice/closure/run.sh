#!/usr/bin/env bash
# run.sh <progdir> <outdir> : compile+link+run every .logos, one line per program:
#   name|cc_rc|diag|run_rc|stdout(one line)|first_error_line
set -u
R=${LOGOS_ROOT:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)}
LOGOSC=${LOGOSC:-$R/build/bin/logosc}
LIB=${LOGOS_LIB:-$R/build/lib/logos}
PD=$1; OD=$2; mkdir -p "$OD"
export LOGOS_VERIFY_LAYOUT=1
ARCH=()
for a in "$LIB"/liblstdlib*.a; do [ -f "$a" ] && ARCH+=("$a"); done
for a in "$LIB"/liblogos-*.a;  do [ -f "$a" ] && ARCH+=("$a"); done
for a in "$LIB"/*.a; do case "$(basename "$a")" in liblstdlib*|liblogos-*) ;; *) [ -f "$a" ] && ARCH+=("$a");; esac; done
one() {
  local src="$1" n d cc=0 diag=0 run="-" msg="" so=""
  local ARCH=($ARCHSTR)
  n=$(basename "$src" .logos); d="$OD/$n"; rm -rf "$d"; mkdir -p "$d"
  timeout 120 "$LOGOSC" "$src" -o "$d/t.o" >"$d/cc.out" 2>"$d/cc.err" || cc=$?
  grep -q -E "error( \[|:)" "$d/cc.err" && diag=1
  if [ "$cc" -eq 0 ] && [ "$diag" -eq 0 ]; then
    if cc "$d/t.o" -Wl,--start-group "${ARCH[@]}" -Wl,--end-group -lpthread -lm -lstdc++ \
        -Wl,--gc-sections -Wl,--allow-multiple-definition -o "$d/t" 2>"$d/ld.err"; then
      run=0; timeout 60 "$d/t" >"$d/stdout" 2>"$d/run.err" || run=$?
      so=$(tr '\n' ';' < "$d/stdout" | head -c 200)
    else run=LINKFAIL; msg=$(head -1 "$d/ld.err" | head -c 160); fi
  else
    msg=$(grep -m1 -E "error( \[|:)|syntax error" "$d/cc.err" | head -c 160)
    [ -z "$msg" ] && msg=$(head -1 "$d/cc.err" | head -c 160)
  fi
  printf '%s|%s|%s|%s|%s|%s\n' "$n" "$cc" "$diag" "$run" "$so" "$msg"
}
export -f one; export OD LOGOSC LOGOS_VERIFY_LAYOUT
export ARCHSTR="${ARCH[*]}"
ls "$PD"/*.logos | xargs -P "$(nproc)" -I{} bash -c 'one "$@"' _ {} | sort
