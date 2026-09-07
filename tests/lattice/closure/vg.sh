#!/usr/bin/env bash
OD=$1
one(){ d="$1"; n=$(basename "$d"); [ -x "$d/t" ] || { echo "$n|NOBIN|||"; return; }
  valgrind --leak-check=full --error-exitcode=97 -q "$d/t" >/dev/null 2>"$d/vg.txt"; rc=$?
  errs=$(grep -c "^==" "$d/vg.txt")
  lost=$(grep -oE "definitely lost: [0-9,]+ bytes" "$d/vg.txt" | head -1)
  inv=$(grep -oE "Invalid (read|write) of size [0-9]+" "$d/vg.txt" | sort -u | tr '\n' ',')
  echo "$n|$rc|$errs|$lost|$inv"; }
export -f one
ls -d "$OD"/*/ | xargs -P $(nproc) -I{} bash -c 'one "$@"' _ {} | sort
