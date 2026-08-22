#!/usr/bin/env bash
# facts_emit.sh LOGOSC SRC FACTS_DIR OBJ [EXTRA...]
#
# THE ONE PER-FIXTURE COMPILE, AND ITS FACTS AS A SIDE PRODUCT (task #85).
#
# WHY THIS FILE EXISTS. Three census gates — `pull_shape_gate.sh`,
# `plan_ground_census_gate.sh`, `direct_door_census_gate.sh` — each used to
# sweep a fixture corpus with a `one()` worker of its own, driven by
# `xargs -0 -P "$SWEEP_P"`, with `SWEEP_P` picked from `LOGOS_GATE_SWEEP_P` /
# `CTEST_INTERACTIVE_DEBUG_MODE` / `nproc`. That is a SECOND SCHEDULER living
# inside a test that ctest is already scheduling, and "SWEEP_P=1 under ctest"
# was a patch on the seam between the two: `ctest -j32 -R "logos_09_pull_shape|
# logos_09_plan_ground_census"` selected two tests, held 32 idle slots, and ran
# two `logosc` processes on a 32-core box for ~19 minutes.
#
# And every one of those compiles was a RECOMPILE. The ordinary per-fixture
# ctest test already compiles each of these fixtures; `run_test.sh` simply did
# not pass `--gen-dir`, so the artifact the gates want was produced and thrown
# away, then recomputed up to three more times.
#
# So the compile happens ONCE, here, driven by ctest's own scheduler through
# the per-fixture test, and what the gates want is written down beside it. Each
# gate becomes a pure serial FOLD over this directory.
#
# WHAT IS WRITTEN, and why each gate needs it:
#   gen/*.gen.logos  the `--gen-dir` units, UNIT BY UNIT and NOT concatenated.
#                    The three gates scope them DIFFERENTLY and the scoping is
#                    load-bearing: `pull_shape` and `plan_ground` drop
#                    `logos.gen.*` (a `next_batch()` there is the stdlib's own
#                    `BatchStream` impl, not a query pulling anything), while
#                    `direct_door` keeps every unit and scopes by the
#                    `// emitted by:` provenance header instead — 4 of that
#                    corpus's 36 doors live in `logos.gen.*` units. Concatenating
#                    here would pick one of those rules for all three.
#   plan.err         the compile's WHOLE stderr, which under `LOGOS_TRACE_PLAN=1`
#                    carries the plan trace (`[plan] …` lines, one per planner
#                    decision — `wql/codegen.logos::plan_trace`). `plan_ground`
#                    reads all of it; `direct_door` counts one sentence in it.
#   rc               the compiler's exit status, verbatim.
#   args             the EXTRA flags this compile used, one per line, NUL-safe
#                    by construction (they are cmake-generated paths and flags).
#                    Recorded so a fold can SAY what it folded over rather than
#                    assume.
#   stamp            see below. WRITTEN LAST.
#
# ⚠ THE STAMP, AND WHY IT IS THE WHOLE SAFETY OF THIS ARRANGEMENT. A facts
# directory is durable: it survives across runs, across `-R` selections, across
# rebuilds. A fold that reads it therefore cannot tell a fact PRODUCED BY THIS
# BUILD from one left behind by a previous one — and a census computed over a
# previous compiler's artifacts, reported green, is a gate that lies about the
# tree it was asked about. The stamp is the compiler binary's size and mtime
# AND the fixture source's size and mtime; the fold recomputes both from its own
# arguments and refuses LOUD (exit 2) on any mismatch. It is written LAST, after
# every other file, so a facts directory interrupted mid-write has no stamp at
# all and reads as MISSING rather than as stale-but-plausible.
#
# ⚠ AND IT IS `rm -rf`'d FIRST. Otherwise a fixture that stops emitting a unit
# leaves the old unit behind and the census counts a dump that no longer exists
# — the mirror image of the staleness above, inside one directory.
set -uo pipefail

LOGOSC="${1:?logosc}"
SRC="${2:?source}"
FACTS="${3:?facts dir}"
OBJ="${4:?object path}"
shift 4
EXTRA=("$@")

[ -x "$LOGOSC" ] || { echo "facts_emit: no logosc at $LOGOSC" >&2; exit 2; }
[ -f "$SRC" ]    || { echo "facts_emit: no source at $SRC" >&2; exit 2; }

rm -rf "$FACTS"
mkdir -p "$FACTS/gen" || { echo "facts_emit: cannot create $FACTS" >&2; exit 2; }

if [ "${#EXTRA[@]}" -gt 0 ]; then
    printf '%s\n' "${EXTRA[@]}" > "$FACTS/args"
else
    : > "$FACTS/args"
fi

LOGOS_TRACE_PLAN=1 "$LOGOSC" "$SRC" -o "$OBJ" "${EXTRA[@]}" \
    --gen-dir "$FACTS/gen" 2> "$FACTS/plan.err"
rc=$?
echo "$rc" > "$FACTS/rc"

# The stamp, last. THREE halves, because each alone leaves a hole: a rebuilt
# compiler over an unchanged fixture, an edited fixture under an unchanged
# compiler, and the SAME compiler over the same fixture invoked with DIFFERENT
# flags are all "facts that are not about this tree".
#
# The third line was added after a MEASURED hole. This round's verify wrote
# `-lTOTALLY_BOGUS` into a fixture's `args` and every fold stayed GREEN and
# byte-identical: `args` was recorded so a fold "can say what it folded over"
# and then nobody ever read it. A recorded fact no consumer checks is not a
# fact, it is a comment -- and this one carries the archive map that the door
# gate's whole argument now rests on. Hashing it INTO the stamp makes the
# consumer's existing staleness check cover it for free.
{
    echo "logosc $(stat -c '%s %Y' "$LOGOSC")"
    echo "src $(stat -c '%s %Y' "$SRC")"
    echo "args $(md5sum < "$FACTS/args" | cut -d' ' -f1)"
} > "$FACTS/stamp"

# `$rc` is a REAL PROCESS STATUS, captured by `rc=$?` on the line immediately
# after the compiler invocation and never arithmetic — already a byte, so the
# 8-bit ceiling this rule guards has nothing to truncate. It must be re-raised
# rather than swallowed: `run_test.sh` decides whether the fixture failed by
# asking THIS script, and the five `logos_09_facts_*` tests have no other
# verdict at all.
exit "$rc"  # lint:exit-ok — $rc is logosc's own wait status, captured at $?
