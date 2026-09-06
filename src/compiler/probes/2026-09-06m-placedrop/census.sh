#!/usr/bin/env bash
# census.sh OUT — compile (no link, no run) every fixture the pass tier owns with
# LOGOS_CENSUS armed, then sum the per-process buckets. The census sites are
# UNCONDITIONAL in this build, so an UNARMED run is the arrival population of the
# site BEFORE any arm — rule 17 answered on the same binary that carries the arms.
set -uo pipefail
cd "$(dirname "$0")/../../../.." || exit 2
OUT="${1:?usage: census.sh OUT}"; : > "$OUT"
export LOGOS_CENSUS="$OUT"
export LOGOS_VERIFY_LAYOUT=1
find tests -name '*.logos' -not -path 'tests/lattice/*' -print0 \
  | xargs -0 -P "$(nproc)" -n 1 -I{} sh -c \
      'build/bin/logosc "{}" -o /dev/null >/dev/null 2>&1 || true'
echo "--- files offered: $(find tests -name '*.logos' -not -path 'tests/lattice/*' | wc -l)"
awk -F'\t' '{s[$1]+=$2} END {for (k in s) printf "%-28s %8d\n", k, s[k]}' "$OUT" | sort
