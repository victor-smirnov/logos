#!/usr/bin/env bash
# runhand.sh <probe-name-or-none> <outfile>
S=/tmp/claude-1004/-home-logos-devel-logos/25aa8421-fce1-4a11-8a89-5d2ba5981c88/scratchpad
R=/home/logos/devel/logos
P="$1"; OUT="$2"
: > "$OUT"
for f in $S/refmode/h*.logos; do
  n=$(basename "$f" .logos)
  if [ "$P" = none ]; then unset LOGOS_PROBE; else export LOGOS_PROBE="$P"; fi
  o=$(cd "$R" && LOGOS_LIB_DIR=$R/build/lib/logos bash tests/logos/run_test.sh pass \
        "$R/build/bin/logosc" "$f" "$S/refmode/exp0.expected" 2>&1)
  rc=$?
  # one-line summary: first meaningful diagnostic / failure line
  d=$(printf '%s' "$o" | grep -m1 -E 'error|FAIL|abort|Segmentation' | sed 's/^[^:]*logos://' | cut -c1-140)
  printf '%s\trc=%s\t%s\n' "$n" "$rc" "${d:-OK}" >> "$OUT"
done
