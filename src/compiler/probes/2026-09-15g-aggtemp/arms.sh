#!/usr/bin/env bash
# arms.sh OUTDIR LOGOSC LIBDIR ARM... — run every hand-battery program (this round's + 2026-09-15f-consumeland's + 2026-09-15e-consume's)
# under each arm ("-" = nothing armed), valgrind on; one sorted file per arm: OUTDIR/<arm>.txt
O="$1"; LOGOSC="$2"; LIB="$3"; shift 3
ROOT=/home/logos/devel/logos
mkdir -p "$O"
ls $ROOT/src/compiler/probes/2026-09-15g-aggtemp/battery/*.logos $ROOT/src/compiler/probes/2026-09-15f-consumeland/battery/*.logos \
   $ROOT/src/compiler/probes/2026-09-15e-consume/battery/*.logos > "$O/population.txt"
for arm in "$@"; do
  H=$(mktemp -d "$O/hr.$arm.XXXX")
  if [ "$arm" = "-" ]; then
    HR_TMP=$H xargs -P 24 -I{} $ROOT/src/compiler/probes/2026-09-15g-aggtemp/hrun.sh "$LOGOSC" "$LIB" {} vg < "$O/population.txt" | sort > "$O/unarmed.txt"
  else
    LOGOS_PROBE=$arm HR_TMP=$H xargs -P 24 -I{} $ROOT/src/compiler/probes/2026-09-15g-aggtemp/hrun.sh "$LOGOSC" "$LIB" {} vg < "$O/population.txt" | sort > "$O/$arm.txt"
  fi
done
touch "$O/done.$(date +%s)"
