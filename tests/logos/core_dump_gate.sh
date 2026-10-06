#!/usr/bin/env bash
# core_dump_gate.sh LOGOSC FIXTURE GOLDEN
#
# ADR 0031 R0 — THE CORE OF EVERY SURFACE SHAPE, AS TEXT.
#
# Compiles FIXTURE with `LOGOS_DEEM_DUMP=core` and compares the `[core]` blocks
# (a `[core] deem …` line and the indented lines under it) with GOLDEN. The
# golden was written BY HAND from the fixture's queries before the dump was
# first run, and matched it byte for byte: a change in how a shape LOWERS
# (where a condition lands, what stays inside a negation, which modifiers go to
# the envelope) moves a line here, and the reviewer re-derives it by hand.
set -u
LOGOSC="${1:?logosc}"; FIXTURE="${2:?fixture}"; GOLDEN="${3:?golden}"
for f in "$LOGOSC" "$FIXTURE" "$GOLDEN"; do
    [ -e "$f" ] || { echo "FAIL(2): missing input: $f"; exit 2; }
done
TMPD=$(mktemp -d); trap 'rm -rf "$TMPD"' EXIT
if ! LOGOS_DEEM_DUMP=core "$LOGOSC" "$FIXTURE" -o "$TMPD/t.o" > "$TMPD/out" 2>&1; then
    echo "FAIL(2): logosc failed on $FIXTURE:"; grep -v '^\[core\]\|^  ' "$TMPD/out" | head -10; exit 2
fi
awk '/^\[core\]/{p=1} /^\[core\]|^  /{if(p)print; next} {p=0}' "$TMPD/out" > "$TMPD/got"
if [ ! -s "$TMPD/got" ]; then
    echo "FAIL(2): no [core] block in the compile's output — the dump channel is off or renamed; no verdict."
    exit 2
fi
if ! diff -u "$GOLDEN" "$TMPD/got" > "$TMPD/diff"; then
    echo "FAIL(1): the core of $FIXTURE differs from the hand-written golden:"
    cat "$TMPD/diff"; exit 1
fi
echo "core dump: $(grep -c '^\[core\]' "$TMPD/got") deems match the hand-written core"
exit 0
