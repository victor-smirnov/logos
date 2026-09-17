#!/usr/bin/env bash
# measure.sh BASE_BIN ARMED_BIN PROBE OUTDIR — the armed-vs-unarmed read.
#
# ⚠ EVERY SET IS DIFFED BOTH WAYS (rule 6: a ceiling bounds the count, not the
# set). A row that CLOSES and a row that OPENS are both reported; a row that
# moves and is not in PREDICTIONS.md refutes the grouping.
#
# ⚠ THE EXIT CODE IS NOT THE ORACLE for a closed over-refusal. A tier-3 row is
# closed only if the program COMPILES, LINKS, RUNS and gives the exit code its
# rustc twin gives. `run` is recorded here; the twin's number is in rust/.
set -uo pipefail
BASE="${1:?base logosc}"; ARMED="${2:?armed logosc}"; PROBE="${3:?probe name}"
OUT="${4:?outdir}"; mkdir -p "$OUT"
ROOT=/home/logos/devel/logos
LIB="$ROOT/build/lib/logos"

verdict() {  # verdict BIN SRC [PROBEENV] -> "<cc>/<diag>/<run>"
    local bin="$1" src="$2" pr="${3:-}" d cc=0 diag=0 run="-"
    d=$(mktemp -d -p "$OUT")
    if [ -n "$pr" ]; then
        LOGOS_PROBE="$pr" LOGOS_VERIFY_LAYOUT=1 "$bin" "$src" -o "$d/t.o" >"$d/cc.out" 2>"$d/cc.err" || cc=$?
    else
        LOGOS_VERIFY_LAYOUT=1 "$bin" "$src" -o "$d/t.o" >"$d/cc.out" 2>"$d/cc.err" || cc=$?
    fi
    grep -q -E "error( \[|:)" "$d/cc.err" && diag=1
    if [ "$cc" -eq 0 ] && [ "$diag" -eq 0 ]; then
        local ars=()
        for a in "$LIB"/liblstdlib*.a; do [ -f "$a" ] && ars+=("$a"); done
        for a in "$LIB"/liblogos-*.a;  do [ -f "$a" ] && ars+=("$a"); done
        for a in "$LIB"/*.a; do case "$(basename "$a")" in liblstdlib*|liblogos-*) ;; *) [ -f "$a" ] && ars+=("$a") ;; esac; done
        if cc "$d/t.o" -Wl,--start-group "${ars[@]}" -Wl,--end-group -lpthread -lm -lstdc++ \
              -Wl,--gc-sections -Wl,--allow-multiple-definition -o "$d/t" 2>"$d/ld.err"; then
            run=0; timeout 60 "$d/t" >"$d/stdout" 2>/dev/null || run=$?
        else run="LINKFAIL"; fi
    fi
    printf '%s/%s/%s' "$cc" "$diag" "$run"
    cp "$d/cc.err" "$OUT/$(basename "$src" .logos).$(basename "$bin").err" 2>/dev/null
}

echo "== TARGET QUEUE ROWS (predicted by name in PREDICTIONS.md) =="
printf '%-62s %-12s %-12s %s\n' ROW BASE ARMED MOVED
for r in mutptr_region_param_elided_let_arg_refused \
         refptr_inner_region_elision_demands_static_refused \
         fnptr_call_result_region_param_reads_static_refused \
         static_arg_pins_shared_callee_region_refused \
         generic_type_param_two_regions_first_wins_refused; do
    s="$ROOT/tests/soundness/open/$r.logos"
    b=$(verdict "$BASE" "$s"); a=$(verdict "$ARMED" "$s" "$PROBE")
    m=$([ "$b" = "$a" ] && echo . || echo MOVED)
    printf '%-62s %-12s %-12s %s\n' "$r" "$b" "$a" "$m"
done

echo
echo "== HAND BATTERY =="
printf '%-34s %-12s %-12s %s\n' ID BASE ARMED MOVED
for s in "$ROOT"/src/compiler/probes/2026-09-17a-ptrmention/hand/*.logos; do
    b=$(verdict "$BASE" "$s"); a=$(verdict "$ARMED" "$s" "$PROBE")
    m=$([ "$b" = "$a" ] && echo . || echo MOVED)
    printf '%-34s %-12s %-12s %s\n' "$(basename "$s" .logos)" "$b" "$a" "$m"
done

echo
echo "== WHOLE QUEUE, ARMED — the only ceiling that can see a queue row =="
echo "   (ceiling-probe.sh cannot; 16m read 0 with two rows one door away)"
