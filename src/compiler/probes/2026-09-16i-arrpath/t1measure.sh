#!/usr/bin/env bash
# Re-measure ONE tier-1 soundness row on a given binary: compile, link, run.
# Records cc rc, diag flag, run rc, stdout. Mirrors soundness_queue_gate.sh observe().
# usage: t1measure.sh <row_id> <outdir> <logosc> <libdir> <root>
set -uo pipefail
ID="$1"; OUT="$2"; LOGOSC="$3"; LIBDIR="$4"; ROOT="$5"
SRC="$ROOT/tests/soundness/open/$ID.logos"
D="$OUT/$ID"; mkdir -p "$D"
export LOGOS_VERIFY_LAYOUT=1
LINK=()
for a in "$LIBDIR"/liblstdlib*.a; do [ -f "$a" ] && LINK+=("$a"); done
for a in "$LIBDIR"/liblogos-*.a;  do [ -f "$a" ] && LINK+=("$a"); done
for a in "$LIBDIR"/*.a; do
  case "$(basename "$a")" in liblstdlib*|liblogos-*) ;; *) [ -f "$a" ] && LINK+=("$a") ;; esac
done
CC=0
"$LOGOSC" "$SRC" -o "$D/t.o" >"$D/cc.out" 2>"$D/cc.err" || CC=$?
DIAG=0; grep -q -E "error( \[|:)" "$D/cc.err" && DIAG=1
RUN="-"
if [ "$CC" -eq 0 ] && [ "$DIAG" -eq 0 ]; then
  if cc "$D/t.o" -Wl,--start-group "${LINK[@]}" -Wl,--end-group \
       -lpthread -lm -lstdc++ -Wl,--gc-sections -Wl,--allow-multiple-definition \
       -o "$D/t" 2>"$D/ld.err"; then
    RUN=0
    timeout 60 "$D/t" >"$D/stdout" 2>"$D/stderr" || RUN=$?
  else
    RUN="LINKFAIL"
  fi
fi
printf '%s cc=%s diag=%s run=%s\n' "$ID" "$CC" "$DIAG" "$RUN" > "$D/verdict"
