#!/usr/bin/env bash
# census.sh <outfile> <jobs> — compile every pass/fail .logos fixture with LOGOS_CENSUS armed.
R=/home/logos/devel/logos
OUT="$1"; J="${2:-8}"
: > "$OUT"
export LOGOS_CENSUS="$OUT"
export LOGOS_LIB_DIR=$R/build/lib/logos
find $R/tests/logos -name '*.logos' -print0 \
  | xargs -0 -P "$J" -I{} sh -c "$R/build/bin/logosc {} -o /dev/null >/dev/null 2>&1" 
