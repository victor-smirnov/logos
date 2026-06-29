#!/usr/bin/env bash
# rustc-parity micro-benchmark oracle (perf-based).
#
# For each <name>.logos / <name>.rs pair, builds logos-safe (overflow checks on),
# logos-release (-C overflow-checks=off) and rustc -O, then measures KERNEL cost
# with `perf stat` using the DIFFERENCE method: count(2R) - count(R) isolates the
# work of exactly R kernel iterations, cancelling each runtime's fixed
# process-startup instructions/cycles. Instructions are deterministic (one
# sample); cycles take the best of TRIALS. Reports per-rep instructions and
# cycles plus logos-release/rustc ratios — instructions = codegen density,
# cycles = real speed (vectorization shows up as fewer cycles / higher IPC).
#
#   instr-ratio ~1.0  => codegen at parity with rustc
#   cycles-ratio      => actual runtime parity (lower is better; >1 = slower)
set -u
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo="$(cd "$here/../.." && pwd)"
build="${BUILD:-$repo/build}"
logosc="$build/bin/logosc"
libd="$build/lib/logos"
cxx=clang++-20
R="${R:-4000}"; R2=$((R*2)); TRIALS="${TRIALS:-5}"
flt(){ grep -vE 'version-mismatch|no ABI guarantee|may be unstable'; }

link_logos(){ # $1=obj $2=out
  $cxx -Wl,--gc-sections "$1" -Wl,--start-group \
    "$libd/liblogos-lang.a" "$libd/liblogos-mem.a" "$libd/liblogos-std.a" \
    "$libd/liblstdlib_rt.a" "$libd/liblstdlib_fibers.a" "$libd/liblstdlib_uring.a" \
    -Wl,--end-group -Wl,--allow-multiple-definition -lpthread -lm -luring -o "$2" 2>&1 | grep -iE 'undefined|error'
}
# echo "<instructions> <cycles>" for one run of `$1 $2`
pcount(){ local out; out=$(perf stat -x, -e instructions,cycles "$1" "$2" 2>&1)
  local ins cyc
  ins=$(echo "$out" | awk -F, '$3=="instructions"{print $1}')
  cyc=$(echo "$out" | awk -F, '$3=="cycles"{print $1}')
  echo "${ins:-0} ${cyc:-0}"; }
# kernel instructions/rep (deterministic — single sample, difference method)
kinsn(){ local lo hi; lo=$(pcount "$1" $R | cut -d' ' -f1); hi=$(pcount "$1" $R2 | cut -d' ' -f1)
  echo "scale=1; ($hi - $lo)/$R" | bc; }
# kernel cycles/rep (best-of-TRIALS at each R, difference method)
kcyc(){ local bin="$1" lob=999999999999 hib=999999999999 t
  for _ in $(seq 1 $TRIALS); do t=$(pcount "$bin" $R | cut -d' ' -f2); (( $(echo "$t<$lob"|bc) )) && lob=$t; done
  for _ in $(seq 1 $TRIALS); do t=$(pcount "$bin" $R2 | cut -d' ' -f2); (( $(echo "$t<$hib"|bc) )) && hib=$t; done
  echo "scale=1; ($hib - $lob)/$R" | bc; }

printf "%-12s | %-26s | %-22s | %s\n" "" "instructions/rep" "cycles/rep" ""
printf "%-12s | %8s %8s %8s | %8s %8s | %s\n" "bench" "lg-safe" "lg-rel" "rustc" "lg-rel" "rustc" "i-ratio c-ratio"
for lf in "$here"/*.logos; do
  b="$(basename "$lf" .logos)"
  "$logosc" -O2 -c "$lf" -o "$here/$b.safe.o" 2>&1|flt|grep -iE error && continue
  link_logos "$here/$b.safe.o" "$here/$b.safe"
  "$logosc" -O2 -C overflow-checks=off -c "$lf" -o "$here/$b.rel.o" 2>&1|flt|grep -iE error && continue
  link_logos "$here/$b.rel.o" "$here/$b.rel"
  rustc -O "$here/$b.rs" -o "$here/$b.rs.bin" 2>&1|grep -iE 'error' && continue
  is=$(kinsn "$here/$b.safe"); ir=$(kinsn "$here/$b.rel"); ix=$(kinsn "$here/$b.rs.bin")
  cr=$(kcyc "$here/$b.rel");  cx=$(kcyc "$here/$b.rs.bin")
  iratio=$(echo "scale=2; $ir/$ix"|bc 2>/dev/null); cratio=$(echo "scale=2; $cr/$cx"|bc 2>/dev/null)
  printf "%-12s | %8s %8s %8s | %8s %8s | %5sx %5sx\n" "$b" "$is" "$ir" "$ix" "$cr" "$cx" "$iratio" "$cratio"
done
rm -f "$here"/*.o "$here"/*.safe "$here"/*.rel "$here"/*.rs.bin
