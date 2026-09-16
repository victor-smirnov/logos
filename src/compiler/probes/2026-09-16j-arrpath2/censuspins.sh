#!/usr/bin/env bash
# censuspins.sh — re-derive the census REGISTRY pins BY DIRECT LISTING and show them beside the
# pinned literals, so the edit is a transcription and never an addition to the previous line.
#
# ⚠ THE PINS ARE ADDED TO, NOT RE-DERIVED, IS EXACTLY THE FAILURE population_pin_lint.py EXISTS FOR:
# 23 consecutive commits were red because a round computed "old + 1" instead of listing the tree.
# Every number below comes from `ctest -N` on the RECONFIGURED registry or from `ls | wc -l`.
#
# usage: censuspins.sh <dir-with-reg_*.txt>   (written by the round's l4chain.sh)
set -uo pipefail
L="${1:?usage: censuspins.sh <l4census dir>}"
cd /home/logos/devel/logos || exit 2

CENSUS=docs/deem-interpreter-deletion-census.md
pin() { grep -E "^$1 " "$CENSUS" | awk '{print $2}'; }

ALL=$(cat "$L/reg_all.txt" 2>/dev/null || echo "?")
NOIMP=$(cat "$L/reg_noimported.txt" 2>/dev/null || echo "?")
TIER=$(cat "$L/reg_tiercommit.txt" 2>/dev/null || echo "?")

printf '%-22s %-10s %-10s %s\n' PIN PINNED MEASURED DELTA
for row in "REGISTRY-ALL:$ALL" "REGISTRY-NOIMPORTED:$NOIMP" "REGISTRY-TIERCOMMIT:$TIER"; do
    k=${row%%:*}; m=${row#*:}
    p=$(pin "$k")
    if [ "$m" = "?" ] || [ -z "$p" ]; then d="?"; else d=$(( m - p )); fi
    printf '%-22s %-10s %-10s %+d\n' "$k" "${p:-MISSING}" "$m" "${d:-0}" 2>/dev/null \
      || printf '%-22s %-10s %-10s %s\n' "$k" "${p:-MISSING}" "$m" "$d"
done

echo
echo "population pins (direct listing, for direct_door_census_gate.sh):"
printf '  corpus  = %s\n' "$(cat "$L/corpus.txt" 2>/dev/null || ls tests/logos/pass/*.logos | wc -l)"
printf '  glob    = %s\n' "$(cat "$L/glob.txt" 2>/dev/null || ls tests/logos/pass/wql_*.logos tests/logos/pass/deem_*.logos 2>/dev/null | wc -l)"
echo "  nonglob = corpus - glob (the partition must close exactly)"
echo
echo "soundness queue, both directions:"
printf '  ledger rows   = %s\n' "$(cat "$L/qrows.txt" 2>/dev/null || awk '!/^#/ && NF' tests/logos/soundness_queue.ledger | wc -l)"
printf '  open programs = %s\n' "$(cat "$L/qprogs.txt" 2>/dev/null || ls tests/soundness/open/*.logos | wc -l)"
printf '  # TOTAL says  = %s\n' "$(grep '^# TOTAL' tests/logos/soundness_queue.ledger | awk '{print $3}')"
echo
echo "EXPECTED DELTAS for this round, stated BEFORE reading the measured column:"
echo "  +9 registered pass fixtures (7 hand-battery + 2 closed-row programs), all non-glob"
echo "  +3 net squeue tests (5 rows opened, 2 closed): REGISTRY-ALL +12, NOIMPORTED +12, TIERCOMMIT +3"
echo "  corpus 3810 -> 3819, glob unmoved at 191, nonglob 3619 -> 3628"
echo "  ledger rows = open programs = # TOTAL = 232"
