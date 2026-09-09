#!/bin/bash
# run_hand.sh — the hand matrix for round 2026-09-09g-closureenv.
# Every cell is rc + valgrind ERROR SUMMARY + definitely-lost, over four
# binaries/arms: base, unarmed, capfat, capretesc, and both.
# Usage: run_hand.sh <outdir>
set -u
R=/home/logos/devel/logos
O="${1:?usage: run_hand.sh <outdir>}"
OBS=/tmp/claude-1004/-home-logos-devel-logos/25aa8421-fce1-4a11-8a89-5d2ba5981c88/scratchpad/obs.sh
PROGS=$(ls $R/src/compiler/probes/2026-09-09g-closureenv/hand/*.logos)
ROWS="boxed_move_closure_fat_capture_env_overflow closure_owned_dyn_capture \
boxed_escaping_fnonce_capture_double_free closure_fnonce_cond_call_untaken_leak \
closure_fnonce_call_in_loop_multi_free impl_fn_return_stack_env_dangles \
generic_drop_body_calling_closure_param_corrupts_callers_closure"
for r in $ROWS; do PROGS="$PROGS $R/tests/soundness/open/$r.logos"; done
PROGS="$PROGS $R/tests/logos/pass/bc_objlt_str_literal.logos"
for arm in base unarmed capfat capretesc both; do
  case $arm in
    base)      BIN=$R/build/bin/logosc.base09g; PV="" ;;
    unarmed)   BIN=$R/build/bin/logosc;         PV="" ;;
    capfat)    BIN=$R/build/bin/logosc;         PV="capfat" ;;
    capretesc) BIN=$R/build/bin/logosc;         PV="capretesc" ;;
    both)      BIN=$R/build/bin/logosc;         PV="__both__" ;;
  esac
  for f in $PROGS; do
    n=$(basename $f .logos)
    d="$O/$arm/$n"; mkdir -p "$d"
    if [ "$PV" = "__both__" ]; then
      # probe::on arms EXACTLY ONE name per process; LOGOS_PROBE=a,b arms NEITHER.
      echo "$arm $n SKIPPED_TWO_PROBES_ONE_PROCESS" >> "$O/summary.txt"; continue
    fi
    line=$(LOGOS_PROBE="$PV" $OBS "$f" "$d" "$BIN" --valgrind 2>/dev/null)
    err=$(grep -oE "ERROR SUMMARY: [0-9]+" "$d/vg.txt" 2>/dev/null | grep -oE "[0-9]+" | head -1)
    lost=$(grep -oE "definitely lost: [0-9,]+ bytes" "$d/vg.txt" 2>/dev/null | head -1)
    rc=$(echo "$line" | grep -oE "RUN=[^ ]+" | head -1)
    cc=$(echo "$line" | grep -oE "CC=[0-9]+ DIAG=[0-9]+" | head -1)
    echo -e "$arm\t$n\t$cc\t${rc:-RUN=?}\terrs=${err:-na}\t${lost:-}" >> "$O/summary.txt"
  done
done
