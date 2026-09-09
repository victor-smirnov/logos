#!/usr/bin/env bash
# run_hand.sh <logosc> [probe-name] — verdict matrix over the 9 hand shapes and
# the 8 ledger rows. ⚠ `probe::on` arms EXACTLY ONE name per process, so this
# takes ONE name; `LOGOS_PROBE=a,b` arms NEITHER.
set -uo pipefail
cd "$(dirname "$0")/../../../.." || exit 2
CC="${1:?usage: run_hand.sh <logosc> [probe]}"; P="${2:-}"
export LOGOS_LIB_DIR="$PWD/build/lib/logos"
[ -n "$P" ] && export LOGOS_PROBE="$P"
T=$(mktemp -d)
row() { printf '%-48s rc=%s  %s\n' "$1" "$2" "$3"; }
for f in src/compiler/probes/2026-09-09i-cesc/hand/*.logos \
         tests/imported/admit/borrowck/anonymous-region-in-apit--closure-param-escapes.logos \
         tests/imported/admit/borrowck/borrowed-data-escapes-closure-148392.logos \
         tests/imported/admit/borrowck/issue-95079-missing-move-in-nested-closure.logos \
         tests/imported/admit/nll/issue-40510-1.logos \
         tests/imported/admit/nll/issue-40510-3.logos \
         tests/imported/admit/nll/issue-42574-diagnostic-in-nested-closure--b.logos \
         tests/imported/admit/nll/issue-42574-diagnostic-in-nested-closure--t15.logos \
         tests/imported/admit/nll/issue-48697--t16.logos; do
    out=$("$CC" --emit-obj -o "$T/o.o" "$f" 2>&1); rc=$?
    row "$(basename "$f" .logos)" "$rc" "$(printf '%s' "$out" | grep -m1 'error' || echo ADMITTED)"
done
rm -rf "$T"
