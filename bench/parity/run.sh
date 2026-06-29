#!/usr/bin/env bash
# rustc-parity micro-benchmark oracle (perf-based).
#
# For each <name>.logos / <name>.rs pair (algorithm-identical twins) builds:
#   lg-rel    logosc -O2 -C overflow-checks=off          (generic / SSE2 baseline)
#   rustc     rustc  -O                                   (generic / SSE2 baseline)
#   lg-nat    logosc -O2 -C overflow-checks=off -C target-cpu=native   (AVX-512…)
#   rc-nat    rustc  -O -C target-cpu=native
# and measures KERNEL cost with `perf` via the DIFFERENCE method count(2R)-count(R)
# (cancels each runtime's fixed process-startup). R is AUTO-SCALED per binary so
# T(R) ~= TARGET_CYCLES, large enough that cycle noise is <1% (the difference
# method is unreliable at small R — subtracting two near-equal large counts).
# Instructions are deterministic (one sample); cycles take the best of TRIALS.
#
# Columns: i-base = instruction ratio lg-rel/rustc (codegen density at baseline);
# c-base / c-nat = cycle ratio lg/rustc at baseline / native; nat-spd = how much
# native (AVX) speeds logos vs its own baseline. Ratio ~1.0 = parity; <1 = logos
# faster/denser.
set -u
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo="$(cd "$here/../.." && pwd)"
build="${BUILD:-$repo/build}"
logosc="$build/bin/logosc"; libd="$build/lib/logos"; cxx=clang++-20
TRIALS="${TRIALS:-7}"; TARGET_CYCLES="${TARGET_CYCLES:-300000000}"
flt(){ grep -vE 'version-mismatch|no ABI guarantee|may be unstable'; }
link(){ $cxx -Wl,--gc-sections "$1" -Wl,--start-group "$libd"/lib*.a -Wl,--end-group \
        -lpthread -lm -luring -o "$2" 2>&1 | grep -iE 'undefined|error'; }
ev(){ perf stat -x, -e "$2" "$1" "$3" 2>&1 | awk -F, -v e="$2" '$3==e{print $1}'; }
# auto-scale reps so a single run is ~TARGET_CYCLES
scale(){ local c; c=$(ev "$1" cycles 2000); local r=$(( 2000*TARGET_CYCLES/(c>0?c:1) ))
  [ "$r" -lt 2000 ] && r=2000; [ "$r" -gt 6000000 ] && r=6000000; echo "$r"; }
kins(){ local r="$2" lo hi; lo=$(ev "$1" instructions "$r"); hi=$(ev "$1" instructions $((r*2)))
  echo "scale=1;($hi-$lo)/$r"|bc; }
kcyc(){ local bin="$1" r="$2" lo=999999999999999 hi=999999999999999 t
  for _ in $(seq 1 $TRIALS); do t=$(ev "$bin" cycles "$r"); [ -n "$t" ]&&(( t<lo ))&&lo=$t; done
  for _ in $(seq 1 $TRIALS); do t=$(ev "$bin" cycles $((r*2))); [ -n "$t" ]&&(( t<hi ))&&hi=$t; done
  echo "scale=1;($hi-$lo)/$r"|bc; }

printf "%-11s %7s %7s %7s %8s\n" "bench" "i-base" "c-base" "c-nat" "nat-spd"
for lf in "$here"/*.logos; do b="$(basename "$lf" .logos)"
  "$logosc" -O2 -C overflow-checks=off -c "$lf" -o "$here/$b.o" 2>&1|flt|grep -iE error && continue
  link "$here/$b.o" "$here/$b.lr"
  "$logosc" -O2 -C overflow-checks=off -C target-cpu=native -c "$lf" -o "$here/$b.o" 2>&1|flt|grep -iE error && continue
  link "$here/$b.o" "$here/$b.ln"
  rustc -O "$here/$b.rs" -o "$here/$b.rr" 2>&1|grep -iE error && continue
  rustc -O -C target-cpu=native "$here/$b.rs" -o "$here/$b.rn" 2>&1|grep -iE error && continue
  R=$(scale "$here/$b.lr")
  ib=$(echo "scale=2;$(kins "$here/$b.lr" $R)/$(kins "$here/$b.rr" $R)"|bc)
  cb=$(echo "scale=2;$(kcyc "$here/$b.lr" $R)/$(kcyc "$here/$b.rr" $R)"|bc)
  Rn=$(scale "$here/$b.ln")
  cn=$(echo "scale=2;$(kcyc "$here/$b.ln" $Rn)/$(kcyc "$here/$b.rn" $Rn)"|bc)
  ns=$(echo "scale=2;$(kcyc "$here/$b.lr" $R)/$(kcyc "$here/$b.ln" $Rn)"|bc)
  printf "%-11s %6sx %6sx %6sx %7sx\n" "$b" "$ib" "$cb" "$cn" "$ns"
done
rm -f "$here"/*.o "$here"/*.lr "$here"/*.ln "$here"/*.rr "$here"/*.rn
