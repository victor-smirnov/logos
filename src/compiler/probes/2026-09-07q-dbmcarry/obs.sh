#!/usr/bin/env bash
# obs.sh <logosc> <outdir> [PROBE] — compile, link, RUN every hand program.
# Oracle: destructor COUNT on stdout (Rust = 1 for every c*/a* program) + rc.
LC=${1:?}; OUT=${2:?}; PROBE=${3:-}
cd "$(dirname "$0")/../../../.." || exit 2
LIB=${LOGOS_LIB_DIR:-$PWD/build/lib/logos}
mkdir -p "$OUT"
for f in src/compiler/probes/2026-09-07q-dbmcarry/hand/*.logos; do
  n=$(basename "$f" .logos)
  env_pre=(env "LOGOS_LIB_DIR=$LIB")
  [ -n "$PROBE" ] && env_pre+=("LOGOS_PROBE=$PROBE")
  if ! "${env_pre[@]}" "$LC" "$f" -o "$OUT/$n.o" > "$OUT/$n.err" 2>&1; then
     echo "$n REFUSED :: $(grep -m1 'error' "$OUT/$n.err" | cut -c1-160)"; continue
  fi
  ARCS=("$LIB"/liblstdlib*.a "$LIB"/liblogos-*.a "$LIB"/*.a)
  if ! cc "$OUT/$n.o" -Wl,--start-group "${ARCS[@]}" -Wl,--end-group -lpthread -lm -lstdc++ \
        -Wl,--gc-sections -Wl,--allow-multiple-definition -o "$OUT/$n.bin" 2>/dev/null; then
     echo "$n LINKFAIL"; continue
  fi
  so=$(timeout 30 "$OUT/$n.bin" 2>/dev/null); rc=$?
  nd=$(printf %s "$so" | grep -c '^D' )
  echo "$n rc=$rc drops=$nd out='$(printf %s "$so" | tr '\n' '|')'"
done
