#!/usr/bin/env bash
# run_gendir_test.sh LOGOSC TEST_LOGOS
#
# --gen-dir round-trip gate: compiles TEST_LOGOS with -g --gen-dir <tmp>,
# asserts (a) the compile succeeds with NO renderer-fidelity fallbacks (the
# render→reparse swap must hold for the quote corpus), (b) at least one
# .gen.logos dump exists, (c) DWARF debug info references the dump file —
# i.e. a debugger will list generated code from the dump.
set -euo pipefail

LOGOSC="$1"
TEST_LOGOS="$2"

TMPD=$(mktemp -d)
trap 'rm -rf "$TMPD"' EXIT

if ! "$LOGOSC" -g "$TEST_LOGOS" --gen-dir "$TMPD/gen" -o "$TMPD/test.o" \
        2>"$TMPD/err"; then
    echo "FAIL: logosc failed:"; cat "$TMPD/err"; exit 1
fi
if grep -q "fidelity" "$TMPD/err"; then
    echo "FAIL: renderer fidelity fallback (dump did not round-trip):"
    grep "fidelity" "$TMPD/err"
    exit 1
fi
GEN_COUNT=$(ls "$TMPD"/gen/*.gen.logos 2>/dev/null | wc -l)
if [ "$GEN_COUNT" -lt 1 ]; then
    echo "FAIL: no .gen.logos dump produced"; exit 1
fi
if ! objdump --dwarf=decodedline "$TMPD/test.o" | grep -q "gen\.logos"; then
    echo "FAIL: DWARF does not reference the gen dump"; exit 1
fi
exit 0
