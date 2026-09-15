#!/usr/bin/env bash
# one.sh <rs> <outdir> : metadata verdict, then build + run
RUSTC=/home/victor/.rustup/toolchains/stable-x86_64-unknown-linux-gnu/bin/rustc
f=$1; O=$2; id=$(echo "$f" | sed 's#.*/stage/rust/##; s#/#__#g; s#\.rs$##')
d=$(mktemp -d "$O/w.$id.XXXX")
$RUSTC --edition 2024 --crate-type bin --emit=metadata --out-dir "$d" "$f" >"$d/meta.err" 2>&1; mrc=$?
codes=$(grep -oE '^error(\[E[0-9]+\])?' "$d/meta.err" | sed 's/^error//; s/\[//; s/\]//' | sed 's/^$/nocode/' | sort | uniq -c | awk '{printf "%s%sx%s", (NR>1?",":""), $2, $1}')
brc=-; run=-; out=-
if [ $mrc -eq 0 ]; then
  $RUSTC --edition 2024 -o "$d/bin" "$f" >"$d/build.err" 2>&1; brc=$?
  if [ $brc -eq 0 ]; then timeout 20 "$d/bin" >"$d/stdout" 2>"$d/stderr"; run=$?; out=$(head -c 80 "$d/stdout" | tr '\n\t' '  '); fi
fi
printf '%s\t%s\t%s\t%s\t%s\t%s\n' "$id" "$mrc" "${codes:--}" "$brc" "$run" "$out"
