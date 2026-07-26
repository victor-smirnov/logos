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
# NOT `objdump … | grep -q`: under `set -o pipefail` that construct FAILS
# INTERMITTENTLY and blames the compiler for it. `grep -q` exits the moment it
# matches, which closes the pipe while objdump is still writing; objdump dies of
# SIGPIPE (141) and pipefail hands the pipeline that status. Whether the match
# comes before objdump finishes is a matter of scheduling, so it flakes only
# under load — 5 failures in 96 runs at 12-way, 0 in 120 with the read split
# from the match, and nothing wrong with the object either time.
objdump --dwarf=decodedline "$TMPD/test.o" > "$TMPD/dwarf" 2> "$TMPD/dwarf.err" || {
    echo "FAIL: objdump failed (rc=$?):"; cat "$TMPD/dwarf.err"; exit 1
}
if ! grep -q "gen\.logos" "$TMPD/dwarf"; then
    echo "FAIL: DWARF does not reference the gen dump"
    echo "  gen dumps:   $(ls "$TMPD"/gen/*.gen.logos 2>/dev/null | tr '\n' ' ')"
    echo "  dwarf lines: $(wc -l < "$TMPD/dwarf")"
    echo "  logosc stderr:"; sed -n '1,20p' "$TMPD/err"
    exit 1
fi
exit 0
