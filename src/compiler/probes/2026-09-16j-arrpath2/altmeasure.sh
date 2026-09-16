#!/usr/bin/env bash
# altmeasure.sh <outdir> <logosc> <libdir> — the A2-alt scorecard's five shapes in ONE pass.
# y11/y12/y13 are the SEPARATING shapes; x01/x03 are the pricing round's own two programs, the
# CONTROL rows that I claim cannot tell the two forms apart.
set -uo pipefail
OUT="$1"; LOGOSC="$2"; LIBDIR="$3"; ROOT=/home/logos/devel/logos
mkdir -p "$OUT/work"; export LOGOS_VERIFY_LAYOUT=1
LINK=()
for a in "$LIBDIR"/liblstdlib*.a "$LIBDIR"/liblogos-*.a; do [ -f "$a" ] && LINK+=("$a"); done
for a in "$LIBDIR"/*.a; do case "$(basename "$a")" in liblstdlib*|liblogos-*) ;; *) [ -f "$a" ] && LINK+=("$a") ;; esac; done
printf 'id\tcc\tdiag\trc\tstdout\n' > "$OUT/ALT.tsv"
one() { local id="$1" src="$2" w="$OUT/work/$1"; mkdir -p "$w"; local cc=0
  "$LOGOSC" "$src" -o "$w/t.o" >"$w/cc.out" 2>"$w/cc.err" || cc=$?
  local diag=0; grep -q -E "error( \[|:)" "$w/cc.err" && diag=1
  local rc="-" out=""
  if [ "$cc" = 0 ] && [ "$diag" = 0 ]; then
    if cc "$w/t.o" -Wl,--start-group "${LINK[@]}" -Wl,--end-group -lpthread -lm -lstdc++ \
         -Wl,--gc-sections -Wl,--allow-multiple-definition -o "$w/t" 2>/dev/null; then
      rc=0; timeout 60 "$w/t" >"$w/stdout" 2>/dev/null || rc=$?; out=$(tr -d '\n' < "$w/stdout")
    else rc=LINKFAIL; fi
  fi
  printf '%s\t%s\t%s\t%s\t%s\n' "$id" "$cc" "$diag" "$rc" "$out" >> "$OUT/ALT.tsv"
  printf '  %-5s cc=%-4s rc=%-4s %s\n' "$id" "$cc" "$rc" "$out"; }
echo "SEPARATING shapes (y11/y12/y13):"
for id in y11 y12 y13; do one "$id" "$ROOT/src/compiler/probes/2026-09-16j-arrpath2/ybat/logos/$id.logos"; done
echo "CONTROL rows — the pricing round's OWN two programs (x01/x03):"
for id in x01 x03; do one "$id" "$ROOT/src/compiler/probes/2026-09-16i-arrpath/battery/$id.logos"; done
echo "ALSO: the two rows this round closed, on the ALT form:"
one row_T1 "$ROOT/tests/logos/pass/bc_16j_row_nested_destructure_admit.logos"
one row_T2 "$ROOT/tests/logos/pass/bc_16j_row_letfield_unbound_admit.logos"
touch "$OUT/ALT_DONE"
