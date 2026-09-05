#!/usr/bin/env bash
# obs.sh LOGOSC SRC...  -> one line per src: "<name> cc=<rc> diag=<0|1> run=<rc|-> | first error line"
# Same reader as tests/logos/soundness_queue_gate.sh::observe (compile, link the
# same archive order, run under timeout). Sourced by every table in this round.
LOGOSC="${1:?logosc}"; shift
export LOGOS_VERIFY_LAYOUT=1
LIB="${LOGOS_LIB_DIR:?LOGOS_LIB_DIR}"
A=(); for a in "$LIB"/liblstdlib*.a; do [ -f "$a" ] && A+=("$a"); done
for a in "$LIB"/liblogos-*.a; do [ -f "$a" ] && A+=("$a"); done
for a in "$LIB"/*.a; do case "$(basename "$a")" in liblstdlib*|liblogos-*) ;; *) [ -f "$a" ] && A+=("$a");; esac; done
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
for src in "$@"; do
  d=$(mktemp -d -p "$T"); cc_rc=0
  "$LOGOSC" "$src" -o "$d/t.o" >"$d/cc.out" 2>"$d/cc.err" || cc_rc=$?
  dg=0; grep -q -E "error( \[|:)" "$d/cc.err" && dg=1
  run="-"; out=""
  if [ "$cc_rc" -eq 0 ] && [ "$dg" -eq 0 ]; then
    if cc "$d/t.o" -Wl,--start-group "${A[@]}" -Wl,--end-group -lpthread -lm -lstdc++ \
         -Wl,--gc-sections -Wl,--allow-multiple-definition -o "$d/t" 2>"$d/ld.err"; then
      run=0; timeout 60 "$d/t" >"$d/stdout" 2>/dev/null || run=$?
      out=$(head -c 200 "$d/stdout" | tr '\n' ' ')
    else run="LINKFAIL"; fi
  fi
  msg=$(grep -m1 -E "error( \[|:)" "$d/cc.err" | head -c 160)
  printf '%s cc=%s diag=%s run=%s | %s | %s\n' "$(basename "$src")" "$cc_rc" "$dg" "$run" "$msg" "$out"
done
