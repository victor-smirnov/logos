#!/usr/bin/env bash
# gate-run.sh <ctest args…> — run a gate ONCE per (test set × binary), and record it.
#
# ⚠ THE KEY IS WHAT WAS ACTUALLY RUN. Victor 2026-08-28: enumerate with `ctest -N`
# first, hash THAT list, and mix in a CONTENT hash of the compiler. A key made of
# command-line arguments is wrong in both directions — `-L bc` selects a
# different set as fixtures land, and two different command lines can select the
# same set. A key made of mtime is wrong too: a rebuild producing an identical
# binary invalidates nothing, and `touch` invalidates nothing.
#
# If a run with this exact key is already recorded, it PRINTS IT and exits 0.
# That is the point: the answer is an artefact, not a thing to re-earn.
# Measured before this existed: 21 `L4` runs in one session's workflow phases,
# five of them opening baselines over a tree already certified green.
#
#   gate-run.sh -L bc                 # the borrow-check oracle
#   gate-run.sh -R '^logos_00_bc_admit_'
#   FORCE=1 gate-run.sh -L bc         # re-measure anyway (say why in your report)
set -uo pipefail
cd "$(dirname "$0")/.." || exit 2
ROOT=$PWD
BUILD=${LOGOS_BUILD:-$ROOT/build}
DB=${LOGOS_GATE_DB:-$BUILD/gate-state/runs.db}
BIN=$BUILD/bin/logosc
[ -x "$BIN" ] || { echo "gate-run: no $BIN — build first" >&2; exit 2; }

LIST=$(ctest --test-dir "$BUILD" -N "$@" 2>/dev/null | grep -oP '^\s+Test\s+#\d+: \K\S+' | sort)
N=$(printf '%s\n' "$LIST" | grep -c . || true)
[ "$N" -eq 0 ] && { echo "gate-run: that filter selects NO tests — a filter is not a count" >&2; exit 2; }
LH=$(printf '%s\n' "$LIST" | sha256sum | cut -c1-16)
# ⚠ THE COMPILER ALONE IS NOT THE BUILD. Victor 2026-08-28: "хэша бинарника тут
# недостаточно, нужны еще библиотеки". A rebuild that changes only the stdlib
# leaves logosc byte-identical and changes what every test does. The set below
# is DERIVED, not guessed: `ctest -N -V` prints each test's real command line,
# and the only build paths those commands name are `bin/logosc`, the `lib/logos`
# search DIRECTORY (so its contents matter, not just its name), and the `.a`
# archives under `tests/logos`. Measured cost of hashing all of it: 0.10 s.
BH=$( { sha256sum "$BIN"
        find "$BUILD/lib/logos" -type f 2>/dev/null | sort | xargs -r sha256sum
        find "$BUILD/tests/logos" -maxdepth 1 -name '*.a' 2>/dev/null | sort | xargs -r sha256sum
      } | sha256sum | cut -c1-16)
# lint:git-ok — identity of the tree state; hygiene, no claim about the artefact
HEAD=$(git rev-parse --short HEAD 2>/dev/null || echo nogit)  # lint:git-ok — identity of the tree state this verdict belongs to; hygiene, no claim about the artefact
# ⚠ THE WORKTREE IS PART OF THE KEY, AND MY FIRST VERSION ARGUED IT SHOULD NOT BE.
# That argument — "a test depends on the BINARY and the LIST; a doc edit changes
# no verdict" — was written into this file and was refuted within minutes of its
# first use: the cache returned `logos_00_gate_lint FAILED` for a tree where I
# had just FIXED gate_lint, because the fix was in `gate_lint.py` and the
# compiler had not moved. Most `logos_00_*` gates are scripts; their behaviour
# is defined in the tree, not in logosc. So the key includes the worktree, and a
# doc edit costs a re-run. That is the conservative direction on purpose: a
# false invalidation costs one run, a false cache HIT reports a wrong verdict.
# ⚠ CONTENT, NOT STATUS — and the first version got this wrong too, in the same
# direction. `git status --porcelain` prints `?? path` for an untracked file
# whatever is inside it, and `git ls-files -s` hashes the INDEX, not the
# worktree. So neither saw an edit to an untracked script, nor to a tracked one
# before `git add`. MEASURED: appending a line to this very file changed neither
# hash and the cache handed back the previous verdict. What is hashed now is the
# diff against HEAD (every tracked modification, by content) plus the bytes of
# every untracked file.
# lint:git-ok — every git call in this block asks the same hygiene question:
# WHICH TREE STATE this verdict belongs to. None of them claims the artefact is
# right; that is what the test run itself is for.
_tree_state() {  # lint:git-ok — see the block comment above
    git diff HEAD 2>/dev/null
    git status --porcelain -unormal 2>/dev/null | sed -n 's/^?? //p' \
        | while read -r u; do [ -f "$u" ] && sha256sum "$u"; done
}
DIRTY=$(_tree_state | sha256sum | cut -c1-16)
KEY=$(printf '%s-%s-%s' "$LH" "$BH" "$DIRTY" | sha256sum | cut -c1-32)

if [ "${FORCE:-0}" != "1" ] && python3 scripts/gate_db.py lookup "$DB" "$KEY"; then
    echo "gate-run: ↑ ALREADY MEASURED for this exact test set ($N tests) and this exact"
    echo "  compiler. Nothing has changed that a test run could see. Use it."
    echo "  (FORCE=1 re-measures; say in your report why the record was not enough.)"
    exit 0
fi

echo "gate-run: $N tests, list $LH, binary $BH — measuring"
JU=$(mktemp --suffix=.xml)
S=$(date +%s)
ctest --test-dir "$BUILD" --output-junit "$JU" "$@"; RC=$?
E=$(( $(date +%s) - S ))
python3 scripts/gate_db.py ingest "$DB" "$JU" "$RC" "$E" "$KEY" "$LH" "$BH" "$HEAD" "$DIRTY" "$*"
rm -f "$JU"
exit $RC  # lint:exit-ok — the gate's own status, which is the point of running it
