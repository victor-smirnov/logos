#!/usr/bin/env bash
# gate-state.sh [L4|L4 bc|…] — the last recorded verdict for this tree state.
#
# ⚠ THIS EXISTS SO THE EXPENSIVE GATE IS NOT RE-RUN FOR AN ANSWER THAT IS
# ALREADY WRITTEN DOWN. Victor 2026-08-28: *"прогон теста должен просто писать в
# файл состояние, и следующий шаг его читает вместо полного прогона"*. Measured
# before it existed: 21 `L4` runs across one session's workflow phases, five of
# them opening baselines over a tree the previous phase had just certified
# green — about an hour spent re-reading a written answer.
#
# It prints the recorded rc and failing set, and exits:
#   0  the record is FOR THIS TREE — use it, do not run the gate
#   3  a record exists but is STALE (HEAD / worktree / logosc moved) — it is
#      about a different tree and you must run the gate to learn anything
#   4  no record at all
#
# ⚠ 3 AND 4 ARE DIFFERENT AND THE SCRIPT REFUSES TO CONFLATE THEM. "Stale" means
# somebody measured, then the tree moved; "absent" means nobody has measured.
# Reporting a stale record as if it were current is exactly the failure this
# whole arc has been about — an answer to a question nobody is asking any more.
set -uo pipefail
cd "$(dirname "$0")/.." || exit 2
ROOT=$PWD
BUILD=${LOGOS_BUILD:-$ROOT/build}
LEVEL=${1:-L4}; shift 2>/dev/null || true
BC=0; IMP=0
for a in "$@"; do
    [ "$a" = "bc" ] && BC=1
    [ "$a" = "imported-unreviewed" ] && IMP=1
done

# lint:git-ok — identity of the tree state this verdict belongs to; pure hygiene
head=$(git rev-parse HEAD 2>/dev/null || echo nogit)  # lint:git-ok — identity of the tree state this verdict belongs to; pure hygiene, no claim about the artefact
dirty=$(git status --porcelain 2>/dev/null | sha256sum | cut -c1-16)  # lint:git-ok — same ground
bin=$(stat -c %Y "$BUILD/bin/logosc" 2>/dev/null || echo 0)
key="$LEVEL-$BC-$IMP-$head-$dirty-$bin"
dir=${LOGOS_GATE_STATE:-$BUILD/gate-state}
f="$dir/$(printf '%s' "$key" | sha256sum | cut -c1-32)"

if [ -f "$f" ]; then
    echo "gate-state: CURRENT — this verdict is about the tree you are standing in."
    cat "$f"
    echo "gate-state: do NOT re-run $LEVEL $* for a baseline; this IS it."
    exit 0
fi

# A record for some OTHER state — say which part moved, because that is the
# actionable half. A caller who changed only the worktree needs a different
# answer from one who rebuilt.
latest=$(ls -t "$dir" 2>/dev/null | head -1)
if [ -n "$latest" ]; then
    echo "gate-state: STALE — a verdict exists but it is about a different tree." >&2
    echo "  recorded:" >&2; sed 's/^/    /' "$dir/$latest" | head -8 >&2
    echo "  now:      head=$head worktree=$dirty logosc_mtime=$bin" >&2
    ph=$(grep -oP '^head=\K\S+' "$dir/$latest" 2>/dev/null || echo "")
    pw=$(grep -oP '^worktree=\K\S+' "$dir/$latest" 2>/dev/null || echo "")
    pb=$(grep -oP '^logosc_mtime=\K\S+' "$dir/$latest" 2>/dev/null || echo "")
    [ "$ph" != "$head" ] && echo "  → HEAD moved: a different commit is checked out." >&2
    [ "$pw" != "$dirty" ] && echo "  → the WORKTREE moved: uncommitted files differ." >&2
    [ "$pb" != "$bin" ] && echo "  → logosc was REBUILT: the binary under test is not the one measured." >&2
    echo "  You must run the gate; nothing here answers your question." >&2
    exit 3
fi
echo "gate-state: NO RECORD for $LEVEL $* — nobody has measured this." >&2
echo "  That is not 'green'. Run the gate." >&2
exit 4
