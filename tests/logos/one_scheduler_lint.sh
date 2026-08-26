#!/usr/bin/env bash
# one_scheduler_lint.sh REPO_ROOT
#
# ONE SCHEDULER. A test registered with ctest MAY NOT FAN OUT ITS OWN.
#
# ── WHY ─────────────────────────────────────────────────────────────────────
# ctest is a scheduler: it is told `-j N` and it fills N slots with tests. A
# gate that runs `xargs -P $(nproc)` from inside one of those slots is a SECOND
# scheduler nested in the first, and the two cannot see each other. Both failure
# directions were measured on this tree:
#
#   OVERSUBSCRIPTION — the full suite fills 32 slots and each of three gates
#   forks 32 more workers. That is #82: gates that timed out "at random" under
#   the suite while finishing in 10-33 s alone. The ceiling was not the problem.
#
#   IDLE — the opposite, and it is the one that hides. `ctest -j32 -R <two
#   gates>` selects two tests, so ctest holds 32 slots with nothing to put in
#   them while each gate, seeing `CTEST_INTERACTIVE_DEBUG_MODE`, drops its own
#   fan-out to 1: two `logosc` on a 32-core box, load 4, ~19 minutes. The rule
#   "parallelism = 1 under ctest" is correct only when ctest is filling the box
#   with the whole suite, and a gate cannot see how many tests ctest was asked
#   to run. It was answering a PROXY question.
#
# Both directions come from the same shape, so the fix is not a better budget —
# it is that the shape stops existing. Victor, 2026-08-21: «короче, xargs тебе
# вообще не надо». The three corpus-census gates were converted in that round:
# the per-fixture compile that the fixture's OWN ctest test already performs
# emits its facts as a side product, and the gate became a serial fold over
# them. `xargs`, `SWEEP_P` and the `CTEST_INTERACTIVE_DEBUG_MODE` branch were
# deleted rather than tuned.
#
# ── WHY A LINT AND NOT A NOTE ───────────────────────────────────────────────
# The population is a `grep` — four hits when this file was written — so the
# class is MECHANICALLY ENUMERABLE, and the recorded rule for that case is to
# sweep it and leave a gate that reds a new one at birth. A comment saying "do
# not do this" is what the three converted gates each had, in the form of a
# careful paragraph about `SWEEP_P`, while the shape kept being written.
#
# ── THE PIN ─────────────────────────────────────────────────────────────────
# Every fan-out primitive inside a script REGISTERED AS A CTEST TEST is a row.
# A script that is not registered is not this lint's business: run by hand it
# owns the box, which is exactly the case the rule always allowed.
#
# Rows are OPEN (a known instance, carrying its task number) or the file is
# clean. A NEW hit in a registered script that is not an OPEN row is RED. An
# OPEN row whose hit is GONE is also RED — a converted gate must retire its row
# deliberately, so the ledger cannot rot into a list nobody rereads.
set -u

ROOT="${1:-.}"
T="$ROOT/tests/logos"
[ -d "$T" ] || { echo "FAIL(2): no tests/logos under $ROOT — nothing to measure."; exit 2; }
CM="$T/CMakeLists.txt"
[ -f "$CM" ] || { echo "FAIL(2): $CM does not resolve — registration is unreadable."; exit 2; }

# ── OPEN ROWS: registered gates that still fan out, each with its task ───────
# These are the residue of the 2026-08-21 conversion round, which converted the
# three CORPUS-CENSUS gates. These three are not pure folds — `plan_independence`
# recompiles each fixture a second time with forced flags, which is a DIFFERENT
# compile from the one the fixture's own test runs, so it needs producers of its
# own rather than the shared facts. Sized as task #101.
OPEN_plan_independence_gate_sh="#101"
OPEN_drain_read_once_pair_gate_sh="#101"
OPEN_drain_import_pair_gate_sh="#101"

# ── THE POPULATION, DERIVED ─────────────────────────────────────────────────
# Registered = named in an `add_test` COMMAND in tests/logos/CMakeLists.txt. Read
# from the artifact, never a list here: a gate registered tomorrow is in scope
# tomorrow with no edit to this file.
mapfile -t REG < <(grep -oE '[A-Za-z0-9_]+\.sh' "$CM" | sort -u)
if [ "${#REG[@]}" -lt 20 ]; then
    echo "FAIL(2): only ${#REG[@]} registered scripts parsed out of $CM."
    echo "         This tree has scores of them; reading a handful means the"
    echo "         parse broke, not that the gates went away. Refusing to vouch."
    exit 2
fi

# A FAN-OUT PRIMITIVE. `xargs -P`, `parallel`, `&`-backgrounded loops, and an
# explicit `-j` handed to a sub-make. Comments are stripped first: a paragraph
# explaining why the shape is wrong must not itself read as the shape.
# The trailing-`&` arm requires a SINGLE `&`: `&&` at a line end is a continued
# boolean, not a background job, and matching it fired on an awk one-liner in
# flat_body_gate.sh — a measured false positive, fixed here rather than by
# exempting the file, since an exemption would have hidden a real `&` in it too.
FANOUT='xargs[^|]*-P|(^|[^A-Za-z_])parallel[[:space:]]|[^&|]&[[:space:]]*$|make[^|]*-j[0-9]'

rc=0
found=0
for s in "${REG[@]}"; do
    f="$T/$s"
    [ -f "$f" ] || continue
    # ⚠ STRIP DOUBLE-QUOTED STRINGS AS WELL AS COMMENTS. A fan-out primitive is
    # CODE; the same word inside an `echo` is PROSE. Measured 2026-08-26: this
    # lint reddened `bc_admits_ledger_gate.sh` for the line
    #   echo "    462 of them, MEASURED 33.5 s in parallel against the ~7 min"
    # — a message describing that the work had just been MOVED OUT of the gate
    # and handed to ctest, i.e. the very thing this lint exists to obtain. It
    # matched by SPELLING where the property is "does this script schedule".
    # Stripping quotes does not blunt the detection: `xargs -P "$N"` still
    # leaves `xargs -P`, and `parallel "$x"` still leaves `parallel` — the
    # primitives survive their arguments being blanked, which is the point.
    hits=$(sed 's/#.*//; s/"[^"]*"//g' "$f" | grep -nE "$FANOUT" | grep -v '^\s*$' || true)
    key="OPEN_$(echo "$s" | tr '.-' '__')"
    open="${!key:-}"
    if [ -n "$hits" ]; then
        found=$((found + 1))
        if [ -z "$open" ]; then
            echo "FAIL: $s is registered as a ctest test AND fans out its own workers:"
            printf '%s\n' "$hits" | sed 's/^/         /'
            echo "      ctest is already the scheduler. Two nested schedulers cannot"
            echo "      see each other: under the full suite this oversubscribes (that"
            echo "      was #82), and under a narrow -R it goes SERIAL for nothing (two"
            echo "      logosc on a 32-core box). Emit per-fixture facts from the"
            echo "      compile the fixture's own test already runs and fold them"
            echo "      serially — see tests/logos/facts_emit.sh and facts_fold.sh."
            rc=1
        fi
    elif [ -n "$open" ]; then
        echo "FAIL: $s carries an OPEN row ($open) and no longer fans out."
        echo "      A converted gate must RETIRE its row in this file, deliberately."
        echo "      Left standing it is a row that can never go red again."
        rc=1
    fi
done

if [ "$found" = 0 ]; then
    echo "FAIL(2): zero fan-out sites over ${#REG[@]} registered scripts."
    echo "         Three are known OPEN as of this file's writing; reading zero"
    echo "         means the matcher stopped matching, not that the class closed."
    exit 2
fi

if [ "$rc" = 0 ]; then
    echo "one-scheduler lint: ${#REG[@]} registered scripts; every fan-out site is a"
    echo "  declared OPEN row (#101). ctest is the only scheduler for the rest."
fi
# `rc` is a LITERAL, set to 0 once and to 1 at the two sites above — never a
# captured process status, so the 8-bit ceiling that turns `exit 256` green is
# unreachable. Every unmeasurable path already exited 2.
exit $rc  # lint:exit-ok
