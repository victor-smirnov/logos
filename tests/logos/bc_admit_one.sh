#!/usr/bin/env bash
# bc_admit_one.sh LOGOSC SRC [ROOT_ID]
#
# ONE admit program, ONE verdict. This is the reader that `bc_admits_ledger_gate.sh`
# used to run in a loop over every row; it is now a registered ctest test per
# program, and the gate keeps only the roster.
#
# ── WHY THE LOOP HAD TO GO ──────────────────────────────────────────────────
#
# ctest is the only scheduler (task #85): a registered test may not fan out its
# own workers. The corollary is the half that bites — a test that CANNOT fan out
# and still has N programs to get through simply runs them serially. The ledger
# gate compiled 462 programs in one slot: ~7 minutes with one core busy and
# thirty-one idle, and it sits in `tier_commit`, so every gates run paid it.
# Three rounds did; in one, eight gate runs cost ~56 minutes and nearly all of
# it was this. Registering one test per program hands the work to the scheduler
# that owns it — and a failure then NAMES the program instead of saying the fold
# disagreed.
#
# ── WHAT THE VERDICT IS ─────────────────────────────────────────────────────
#
# These programs are ones rustc REFUSES and we ACCEPT. The assertion is that the
# defect is STILL THERE: the compile is silent. Nothing is run — the programs are
# defective by definition and their runtime behaviour is not a claim anyone makes.
#
# EXIT 0 = still admitted (the hole is still open, as the ledger says).
# EXIT 1 = NO LONGER ADMITTED — the hole CLOSED. That is a round SUCCEEDING, and
#          the fix is to delete the row from `bc_admits.ledger`, decrement its
#          `# TOTAL`, and reland the program as a fail fixture with the
#          diagnostic pinned.
# EXIT 2 = cannot measure (missing source, no compiler).
set -uo pipefail

LOGOSC="${1:?usage: bc_admit_one.sh <logosc> <src.logos> [root-id]}"
SRC="${2:?usage: bc_admit_one.sh <logosc> <src.logos> [root-id]}"
ROOT="${3:-<unknown>}"

[ -x "$LOGOSC" ] || { echo "FAIL(2): no compiler at $LOGOSC"; exit 2; }
[ -f "$SRC" ]    || { echo "FAIL(2): no source at $SRC — a ledger entry for a program that is gone is a claim nobody can check."; exit 2; }

TMPD=$(mktemp -d); trap 'rm -rf "$TMPD"' EXIT

rc=0
"$LOGOSC" "$SRC" -o "$TMPD/out.o" >"$TMPD/out.txt" 2>"$TMPD/err.txt" || rc=$?

# ⚠ NOT `<compiler stderr> | grep -q`: under `pipefail` a `grep` that exits early
# turns the producer's SIGPIPE into the pipeline's status — the recorded gate-lie
# form. Materialise, then match. And `error( \[|:)` rather than a bare "error",
# which would also match the word inside a WARNING's own text.
if [ "$rc" -eq 0 ] && ! grep -q -E "error( \[|:)" "$TMPD/err.txt"; then
    exit 0
fi

echo "NO LONGER ADMITTED — the hole is CLOSED, and that is a round succeeding."
echo "  program: $SRC"
echo "  root id: $ROOT"
echo "  rc=$rc, first diagnostic:"
grep -m2 -E "error( \[|:)" "$TMPD/err.txt" 2>/dev/null | sed 's/^/    /'
echo "  Delete its row from tests/logos/bc_admits.ledger, decrement the '# TOTAL'"
echo "  line, and reland the program under tests/imported/fail/ with the"
echo "  diagnostic pinned — the row leaving IS the proof the class closed."
exit 1  # lint:exit-ok — a literal, not a captured status or a count
