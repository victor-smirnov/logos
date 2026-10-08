#!/usr/bin/env bash
# scc_index_hoist_gate.sh LOGOSC FIXTURE
#
# ADR 0031 R7.2 — the semi-naive driver builds the index over a source its loop
# does not change ONCE, before the loop. Structural, not timed: in the dumped
# SCC fn of FIXTURE the hash index's declaration precedes `loop {`.
set -euo pipefail
LOGOSC="$1"; FIXTURE="$2"
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
"$LOGOSC" "$FIXTURE" -o "$T/t.o" --gen-dir "$T/gen" > "$T/cc.log" 2>&1 || { echo "FAIL: logosc failed"; cat "$T/cc.log"; exit 1; }
F=$(grep -l 'fn __wql_[a-z_]*_scc[0-9]' "$T"/gen/* | head -1)
[ -n "$F" ] || { echo "FAIL: no SCC fn in the dump"; exit 1; }
HM=$(grep -n 'let mut __hm[0-9]*: HashMap' "$F" | head -1 | cut -d: -f1)
LP=$(grep -n '^    loop {' "$F" | head -1 | cut -d: -f1)
[ -n "$HM" ] && [ -n "$LP" ] || { echo "FAIL: no hash index or no fixpoint loop in $F"; exit 1; }
if [ "$HM" -ge "$LP" ]; then
    echo "FAIL: the index over the loop-invariant source is built INSIDE the fixpoint loop (line $HM, loop at $LP) — rebuilt every round"
    exit 1
fi
echo "scc_index_hoist_gate: OK (index at line $HM, loop at line $LP)"
