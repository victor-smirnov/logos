#!/usr/bin/env bash
# facts_fold.sh — the shared PRELUDE of the three census folds (task #85).
#
# Sourced, never executed. It provides one function, `facts_require`, which
# answers the single question every fold must answer before it counts anything:
#
#     ARE THE FACTS FOR *EVERY* MEMBER OF MY POPULATION PRESENT, AND ARE THEY
#     ABOUT *THIS* TREE?
#
# ⚠ THE GATE-LIE THIS EXISTS TO PREVENT, and it is the whole risk of moving
# these gates off their own sweeps. A gate that reads a facts directory cannot,
# by looking at the directory, tell
#
#     "no facts for this fixture because its test was not selected under `-R`"
#   from
#     "no facts for this fixture because there were none to have"
#
# and the recorded class is SILENCE READ AS ZERO. A census that silently
# measured 3 of 191 fixtures and reported green would be strictly worse than
# the slow sweep it replaced — the slow one at least compiled all 191. So each
# fold derives its expected fixture list EXACTLY as its sweep did (the same
# glob, in the same directory), hands it to this function, and this function
# refuses LOUD — `exit 2`, "the census is not measurable", every missing member
# NAMED — rather than returning a smaller population.
#
# ctest is what makes the list complete rather than a hope: the three gates
# declare `FIXTURES_REQUIRED`, so selecting a gate by `-R` or by `-L` pulls
# every per-fixture test that produces its facts, whatever the selection was.
# Measured on ctest 3.28.3 (2026-08-21): `-R '^consumer$'` over 20 setups ran
# 21 tests; `-L gate` likewise; a FAILING setup left the consumer `***Not Run`
# and FAILED, never green and never smaller.
#
# ⚠ AND STALENESS IS THE SAME LIE ONE DAY LATER. The facts tree is durable, so
# a directory left by a previous build looks exactly like one written by this
# one. `facts_emit.sh` stamps each fixture with the compiler binary's size and
# mtime AND the fixture source's size and mtime, written LAST; this function
# recomputes both from its own arguments and refuses on any mismatch. Both
# halves are needed: a rebuilt compiler over an unchanged fixture and an edited
# fixture under an unchanged compiler are both "facts that are not about this
# tree", and either alone leaves the other's hole open.
#
# CONTRACT
#   facts_require <FACTS_ROOT> <LOGOSC> <SUBJECT> <fixture .logos path>...
# On success: prints nothing, returns 0. On any failure: prints the refusal and
# EXITS 2 from the calling script.

# ⚠ NOT `wc`/`grep -c` ANYWHERE HERE: zero is a legitimate answer for several
# of these lists and `grep` exits 1 on it, which under `pipefail` would turn a
# clean census into a refusal (fail-closed, but for the wrong reason and with
# the wrong message). Counting is done by array length.
facts_require() {
    local FROOT="$1" LOGOSC="$2" SUBJECT="$3"
    shift 3
    local -a FIX=("$@")

    if [ ! -d "$FROOT" ]; then
        echo "FAIL(2): no facts tree at $FROOT — the $SUBJECT census is not"
        echo "         measurable. The facts are written by the per-fixture"
        echo "         ctest tests (run_test.sh -> facts_emit.sh); this gate"
        echo "         declares FIXTURES_REQUIRED so ctest runs them first."
        exit 2
    fi

    local want_logosc
    want_logosc="logosc $(stat -c '%s %Y' "$LOGOSC")"

    local -a missing=() stale=()
    local f b d want got
    for f in "${FIX[@]}"; do
        b=$(basename "$f" .logos)
        d="$FROOT/$b"
        # `stamp` is written LAST by facts_emit.sh, so its absence covers both
        # "never written" and "interrupted mid-write". `rc` is asked for
        # separately because every fold reads it and a facts dir without one is
        # not usable even if stamped.
        # `plan.err` and `args` are in this list because of a MEASURED hole,
        # not for symmetry: this round's verify deleted a single fixture's
        # `plan.err` and `direct_door` -- whose plan census READS that file --
        # stayed rc 0 and green, with only a `grep: No such file` on a stderr
        # nobody reads. Every file a fold consumes must be required by the
        # function that vouches for completeness, or the vouching is partial.
        if [ ! -f "$d/stamp" ] || [ ! -f "$d/rc" ] || [ ! -d "$d/gen" ] ||
           [ ! -f "$d/plan.err" ] || [ ! -f "$d/args" ]; then
            missing+=("$b")
            continue
        fi
        want="$want_logosc
src $(stat -c '%s %Y' "$f")
args $(md5sum < "$d/args" | cut -d' ' -f1)"
        got=$(cat "$d/stamp")
        if [ "$want" != "$got" ]; then
            stale+=("$b")
        fi
    done

    if [ "${#missing[@]}" -ne 0 ]; then
        echo "FAIL(2): ${#missing[@]} of ${#FIX[@]} $SUBJECT fixtures have NO facts —"
        echo "         the census is not measurable and MUST NOT report a smaller"
        echo "         one. Missing (facts dir under $FROOT):"
        printf '           %s\n' "${missing[@]}"
        echo "         Each is produced by that fixture's own ctest test. If you"
        echo "         ran this gate by hand, run its ctest name instead — the"
        echo "         FIXTURES_REQUIRED declaration is what makes the list whole."
        exit 2
    fi

    if [ "${#stale[@]}" -ne 0 ]; then
        echo "FAIL(2): ${#stale[@]} of ${#FIX[@]} $SUBJECT fixtures carry facts from a"
        echo "         DIFFERENT tree — a stale fact counted as this build's is a"
        echo "         census about a compiler that is no longer here. Stale:"
        printf '           %s\n' "${stale[@]}"
        echo "         Expected stamp: $want_logosc + the fixture's own size/mtime."
        exit 2
    fi
}
