#!/usr/bin/env bash
# ledger_sync_gate.sh — the OFFLINE half of "these ledgers are generated, not edited".
#
# Since 2026-09-17 the GitHub issues are the source of truth and the counted ledgers
# are regenerated from them by `scripts/issues-to-ledger.py`. The files stay in the
# tree because they are BUILD INPUTS — `tests/logos/CMakeLists.txt` reads the queue at
# configure time and generates a per-row ctest test from it.
#
# This gate does NOT ask the issues anything. A build gate that needs a token and a
# network is a gate that goes red when GitHub has a bad day, and `cmake` must work
# offline. What it asks instead is answerable locally and is exactly the rule Victor
# set: has anyone edited these files by hand since the generator wrote them? Each file
# carries `# SYNC-HASH <digest>` over its row region; a hand edit moves the digest.
#
# ⚠ It cannot see a bad GENERATED write — a rewrite recomputes the digest over whatever
# it produced. That direction is guarded in the generator (empty-channel, large-shrink
# and closure rails), not here. Said plainly so nobody reads this gate as more than it is.
#
#   ledger_sync_gate.sh <repo-root>
#   exit 0 = every ledger matches its digest · 1 = a file was hand-edited · 2 = IO error
set -uo pipefail

ROOT="${1:-.}"
PY="${PYTHON:-python3}"

# ⚠ RUN FROM THE REPO ROOT. `scripts/issues-to-ledger.py` resolves the ledger paths
# RELATIVE to the working directory, and ctest runs a test from the BUILD dir, where
# `tests/logos/*.ledger` does not exist. Invoked by hand from the root it passed; the
# first ctest run failed for exactly this reason — the same lesson as a gate called
# with three arguments where CMake passes four: an invocation that differs from
# CMake's is a different measurement.
cd "$ROOT" || { echo "ledger-sync: cannot enter '$ROOT'"; exit 2; }
SCRIPT="scripts/issues-to-ledger.py"
[ -f "$SCRIPT" ] || { echo "ledger-sync: $SCRIPT not found under '$ROOT'"; exit 2; }

fail=0
for list in backlog bc-admits squeue; do
    out="$("$PY" "$SCRIPT" --list "$list" --verify 2>&1)"; rc=$?
    printf '%s\n' "$out"
    [ "$rc" -eq 0 ] || fail=1
done

# CANARY, in the same run and through the same checker. A verifier that has stopped
# verifying cannot be enumerated into — it can only be caught by feeding it something
# it MUST reject. Corrupt a copy of a ledger and require the digest to move.
# ⚠ THE CANARY MUST RIDE THE REAL CODE PATH. The first version computed the digest
# itself and compared two numbers; it printed "caught" in a run where all three real
# checks had crashed with FileNotFoundError and never verified anything. A canary that
# does not go through the checker cannot report on the checker. This one corrupts a
# COPY and feeds it to the SAME `--verify`, and requires it to REJECT.
# ⚠ AND IT NEEDS BOTH DIRECTIONS IN THE SAME RUN, FOR A REASON MEASURED TWICE.
# v1 computed the digest itself and said "caught" in a run where all three real checks
# had crashed. v2 rode the real checker but only required a NON-ZERO rc — and
# `FileNotFoundError` is also non-zero, so it still said "caught" when nothing could be
# read at all. A canary that cannot tell "rejected the planted edit" from "could not
# look" certifies exactly the failure it exists to catch. So: the SAME call must pass
# on a clean copy AND fail on a corrupted one, with the hand-edit sentence.
canary_dir="$(mktemp -d)"
cp tests/logos/unrowed_backlog.ledger "$canary_dir/clean.ledger"
cp tests/logos/unrowed_backlog.ledger "$canary_dir/dirty.ledger"
printf '%s\n' "canary_planted_row canary 1 tests/logos/NOT_A_REAL_ROW" >> "$canary_dir/dirty.ledger"

"$PY" "$SCRIPT" --list backlog --verify --file "$canary_dir/clean.ledger" >/dev/null 2>&1
clean_rc=$?
dirty_out="$("$PY" "$SCRIPT" --list backlog --verify --file "$canary_dir/dirty.ledger" 2>&1)"
dirty_rc=$?

if [ "$clean_rc" -ne 0 ]; then
    echo "ledger-sync: CANARY INCONCLUSIVE — the checker rejected a CLEAN copy (rc $clean_rc);"
    echo "             it is not discriminating, so treat every OK above as unproven"
    fail=1
elif [ "$dirty_rc" -eq 0 ] || ! printf '%s' "$dirty_out" | grep -q 'HAND-EDITED'; then
    echo "ledger-sync: CANARY DID NOT CATCH a planted row (rc $dirty_rc, no HAND-EDITED verdict)"
    echo "             treat every OK above as unproven"
    fail=1
else
    echo "ledger-sync: canary caught — clean copy accepted, planted row reported HAND-EDITED"
fi

[ "$fail" -eq 0 ] && echo "ledger-sync: OK (3 ledgers match their digests)"
exit "$fail"
