#!/usr/bin/env bash
# rerun.sh INPUT_LIST OUT_LOG [JOBS]
# Compile every input with the new borrow checker in SHADOW mode
# (LOGOS_DL_SHADOW=bc): both checkers run, the compile acts on the old one, and
# every disagreement plus a per-input census is appended to OUT_LOG.
#   bc / bc-census / bc-skip     post-mono functions
#   bcg / bcg-census / bcg-skip  generic templates, pre-mono (#434)
# Read the log with summ.py (totals, classes) and top.py (one class by function).
# JOBS defaults to 12, the box's virtual cores; never run it beside a build —
# it executes build/bin/logosc.
set -u
LIST=$1; OUT=$2; JOBS=${3:-12}
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
rm -f "$OUT"
export LOGOS_DL_SHADOW=bc LOGOS_DL_SHADOW_LOG="$OUT"
xargs -a "$LIST" -P "$JOBS" -I{} sh -c "timeout 120 '$ROOT/build/bin/logosc' '{}' -o /dev/null >/dev/null 2>&1"
