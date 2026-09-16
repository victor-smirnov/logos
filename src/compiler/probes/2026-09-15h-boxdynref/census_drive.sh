#!/usr/bin/env bash
# census_drive.sh <logosc> <outdir> — arrival census at the three &VarRef owning arms.
# Compiles every pass/fail/spec fixture under LOGOS_CENSUS; per-FILE arm hits.
set -uo pipefail
LOGOSC="${1:?logosc}"
OUT="${2:?outdir}"
mkdir -p "$OUT/per"
export LOGOS_LIB_DIR=/home/logos/devel/logos/build/lib/logos
cd /home/logos/devel/logos
one() {
  f="$1"; n=$(echo "$f" | tr '/.' '__')
  LOGOS_CENSUS="$OUT/per/$n.census" timeout 120 "$LOGOSC" "$f" -o /dev/null > /dev/null 2>&1
  if [ -s "$OUT/per/$n.census" ]; then
    if grep -q '^boxdyn\.arm\.' "$OUT/per/$n.census"; then
      awk -v F="$f" '/^boxdyn\.arm\./ {print F"\t"$1"\t"$2}' "$OUT/per/$n.census"
    fi
  fi
}
export -f one; export OUT LOGOSC
ls tests/logos/pass/*.logos tests/logos/fail/*.logos tests/spec/pass/*.logos 2>/dev/null \
  | xargs -P "$(nproc)" -I{} bash -c 'one "$@"' _ {} > "$OUT/arrivals.tsv" 2>/dev/null
echo "=== arrivals by arm (FILES)"
awk -F'\t' '{print $2}' "$OUT/arrivals.tsv" | sort | uniq -c
echo "=== arrivals by arm (HITS)"
awk -F'\t' '{s[$2]+=$3} END {for (k in s) print s[k], k}' "$OUT/arrivals.tsv"
echo "=== files taking the owning_trait_object arm"
awk -F'\t' '$2=="boxdyn.arm.owning_trait_object" {print $1}' "$OUT/arrivals.tsv" | sort -u
