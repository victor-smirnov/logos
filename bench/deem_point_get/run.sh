#!/usr/bin/env bash
# ADR 0026 F4: instructions per point get, Deem query vs the source's lookup.
# Callgrind counts instructions, so the numbers hold on a loaded box.
# usage: bench/deem_point_get/run.sh [-O0|-O1|-O2|-O3]   (default -O2)
set -euo pipefail
trap 'echo "run.sh: line $LINENO exited $?" >&2' ERR
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
OPT=${1:--O2}
LOGOSC=$ROOT/build/bin/logosc
export LOGOS_LIB_DIR=$ROOT/build/lib/logos
OUT=$ROOT/bench/bin/deem_point_get
mkdir -p "$OUT"
ARCH=$(ls "$LOGOS_LIB_DIR"/*.a)
CALLS=20000
"$LOGOSC" --version
for src in btree memoria; do
    ir=()
    for mode in 0 1 2; do
        sed "s/\bMODE\b/$mode/g" "$ROOT/bench/deem_point_get/$src.logos" > "$OUT/${src}_$mode.logos"
        "$LOGOSC" "$OUT/${src}_$mode.logos" "$OPT" -o "$OUT/${src}_$mode.o" 2>/dev/null
        cc "$OUT/${src}_$mode.o" -Wl,--start-group $ARCH -Wl,--end-group -lpthread -lm -lstdc++ \
           -Wl,--gc-sections -Wl,--allow-multiple-definition -o "$OUT/${src}_$mode"
        "$OUT/${src}_$mode" || { echo "$src mode $mode: wrong answer (exit $?)"; exit 1; }
        ir[$mode]=$(valgrind --tool=callgrind --callgrind-out-file="$OUT/${src}_$mode.cg" \
                    "$OUT/${src}_$mode" 2>&1 | sed -n 's/.*Collected : //p')
    done
    deem=$(( (ir[1] - ir[0]) / CALLS )); direct=$(( (ir[2] - ir[0]) / CALLS ))
    echo "$src $OPT: deem $deem instr/call, direct $direct instr/call (control ${ir[0]})"

done
