#!/usr/bin/env bash
# Round 2026-09-16j-arrpath2 — run BOTH batteries (16i's and mine) plus the two target row
# programs on a given binary, recording exit code AND the stdout destructor trace.
# usage: armmeasure.sh <outdir> <logosc> <libdir>
set -uo pipefail
OUT="$1"; LOGOSC="$2"; LIBDIR="$3"; ROOT=/home/logos/devel/logos
mkdir -p "$OUT/work"
export LOGOS_VERIFY_LAYOUT=1
LINK=()
for a in "$LIBDIR"/liblstdlib*.a; do [ -f "$a" ] && LINK+=("$a"); done
for a in "$LIBDIR"/liblogos-*.a;  do [ -f "$a" ] && LINK+=("$a"); done
for a in "$LIBDIR"/*.a; do case "$(basename "$a")" in liblstdlib*|liblogos-*) ;; *) [ -f "$a" ] && LINK+=("$a") ;; esac; done

run_one() { # run_one <id> <srcpath>
  local id="$1" src="$2" w="$OUT/work/$1"; mkdir -p "$w"
  local cc=0
  "$LOGOSC" "$src" -o "$w/t.o" >"$w/cc.out" 2>"$w/cc.err" || cc=$?
  local diag=0; grep -q -E "error( \[|:)" "$w/cc.err" && diag=1
  local rc="-" out=""
  if [ "$cc" = "0" ] && [ "$diag" = "0" ]; then
    if cc "$w/t.o" -Wl,--start-group "${LINK[@]}" -Wl,--end-group -lpthread -lm -lstdc++ \
         -Wl,--gc-sections -Wl,--allow-multiple-definition -o "$w/t" 2>"$w/ld.err"; then
      rc=0; timeout 60 "$w/t" >"$w/stdout" 2>/dev/null || rc=$?
      out=$(tr -d '\n' < "$w/stdout")
    else rc="LINKFAIL"; fi
  fi
  printf '%s\t%s\t%s\t%s\t%s\n' "$id" "$cc" "$diag" "$rc" "$out" >> "$OUT/ARM.tsv"
}

printf 'id\tcc\tdiag\trc\tstdout\n' > "$OUT/ARM.tsv"
# my own counter-examples
for f in "$ROOT"/src/compiler/probes/2026-09-16j-arrpath2/ybat/logos/*.logos; do
  [ -f "$f" ] && run_one "$(basename "$f" .logos)" "$f"
done
# 16i's battery, the shapes that already had rustc twins
for f in "$ROOT"/src/compiler/probes/2026-09-16i-arrpath/battery/*.logos; do
  run_one "i_$(basename "$f" .logos)" "$f"
done
# the two target rows
for r in match_array_nested_destructure_elem_double_drop let_array_pattern_field_base_unbound_elem_leak; do
  run_one "row_$r" "$ROOT/tests/soundness/open/$r.logos"
done
touch "$OUT/ARM_DONE"
