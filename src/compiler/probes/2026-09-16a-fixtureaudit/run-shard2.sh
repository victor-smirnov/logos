#!/bin/bash
# $1 = path to .rs ; compiles and runs; writes $D/out/<name>.txt
RS="$1"; D="$(dirname "$(dirname "$RS")")"; N="$(basename "$RS" .rs)"
RUSTC=/home/victor/.rustup/toolchains/stable-x86_64-unknown-linux-gnu/bin/rustc
O="$D/out/$N.txt"
{ echo "=== $N"; 
  if "$RUSTC" --edition 2024 "$RS" -o "$D/bin/$N" > "$D/out/$N.build" 2>&1; then
     echo "BUILD: ok"
     "$D/bin/$N" > "$D/out/$N.stdout" 2>&1; echo "RUNRC: $?"
     echo "STDOUT: $(head -c 300 "$D/out/$N.stdout" | tr '\n' '|')"
  else
     echo "BUILD: FAIL"
     grep -oE '^error\[E[0-9]+\]|^error:' "$D/out/$N.build" | sort | uniq -c | tr '\n' ' '; echo
     head -20 "$D/out/$N.build"
  fi
} > "$O" 2>&1
