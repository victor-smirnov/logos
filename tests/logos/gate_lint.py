#!/usr/bin/env python3
"""gate_lint.py — the recorded lessons about LYING GATES, as rules a new artifact
cannot get past.

WHY THIS EXISTS, and it is the whole argument.

`bug_exit_code_gate_lies` is a written record, several paragraphs long, of eight
ways a gate reports "nothing is wrong" when what happened is "I could not look".
Every one of them was diagnosed, fixed at the class, and written down.

MEASURED 2026-08-01, five live violations — three of them in artifacts written
THIS WEEK to stop gates from lying:

  * `layout_lattice_gen.py` allocated 272 distinct failure codes and returned
    them through a process EXIT STATUS, which is 8 bits. Forced to fail at code
    255 the gate went red; forced at code 256 it went GREEN (256 & 0xFF == 0);
    257 came back as 1, i.e. named the first DST prefix shape. The identical
    class was fixed on 07-19 across all 33 conuco tests.
  * `wql_shadowed_column_gate.sh` grew two `awk … | grep -q` under
    `set -o pipefail` — the third recorded kind, fixed on 07-30 in `ff85443c`.
    One of the two is the NEGATIVE form, where a SIGPIPE inverts the verdict and
    a present forbidden string reads as absent. `run_test.sh`, which runs for
    EVERY fail test in the corpus, had a third.
  * `verdict.py` — the strict parser the layout gate reads every number
    through — accepted a DUPLICATED KEY, last wins. A verdict with the real
    `defs` at 0 and a second `defs` at 3676 met a floor of 3676 and exited 0,
    where the same 0 alone is a correct red.
  * The conuco suite was 33 tests when the ceiling was fixed there and
    mechanically re-verified. It is 67 now, and NINE of them returned a
    diagnostic code straight out of `main` again (max 78, so none was lying yet
    — which is what the record said about the rest of the suite in 07-19, just
    before the suite grew). R7.
  * The layout gate's own closing line said "NINE canaries caught" and listed
    nine names; `declined`, added the day before, was caught on every run and
    was in no list. Counted by an accumulator instead of a sentence: TWELVE.
    The number is now derived, and `MIN_CANARIES` is the only one maintained.

A written lesson that is not mechanized gets re-made. So each rule below is a
rule, not a paragraph, and each carries a CANARY: a snippet that violates it and
which this program must flag. If a rule stops catching its own canary, this
program exits 4 saying the LINT is broken — the same discipline the gates use on
themselves. A rule with no canary cannot be added: `--selftest` requires every
rule in ALL_RULES to appear at least once firing and at least once silent.

── THE SWEEP, 2026-08-01: every recorded lesson, and whether a NEW artifact can
   still violate it today ─────────────────────────────────────────────────────

CLOSED BY CONSTRUCTION — the mistake cannot be spelled
  · a diagnostic code colliding with another    `Codes` in layout_lattice_gen.py
    (07-19's second form: bases spaced 1 over   allocates; `chk` takes no number.
    a range of 4, 134 sites sharing a code)     `_audit_emitted_codes` then reads
                                                the emitted TEXT back — codes must
                                                be exactly 1..n, one probe each.
  · a floor of zero through verdict.py          `parse_floor` refuses it at the
                                                argument (exit 3). `--eq` is the
                                                honest spelling of "it is zero".
  · a duplicated key in a verdict               `object_pairs_hook` raises; a
                                                post-hoc check would read the
                                                already-collapsed dict.

CAUGHT BY A RULE HERE — expressible, and flagged
  R1  an exit status above 255 (or 126/127)     `exit N` in COMMAND POSITION at
                                                any indent, inside a function,
                                                after then/else/do, in a `case`
                                                arm, in `$((…))`; `sys.exit(N)`;
                                                a generated `return <code>i32`;
                                                an `.expected` declaring one. A
                                                status this cannot DECIDE
                                                (`exit $RC`, bare `exit`) is a
                                                finding, not a pass
  R2  `pipefail` + `| grep -q`                  read into a file, then match
  R3  a gate asking git instead of the build    the QUERY verbs (diff/status/log/
                                                rev-parse/…), since those are the
                                                ones that answer; `# lint:git-ok
                                                — <ground>` is the only way past,
                                                cheap to write truthfully and
                                                impossible to write honestly for
                                                a proxy
  R4  a relative `-newermt` (bfs errors → 0)    `@<epoch>` or `$VAR`
  R5  a gate script CTEST DOES NOT INVOKE       the population is asked of ctest,
                                                not grepped out of a CMakeLists:
                                                a name in a file is not a
                                                registration
  R6  `-ge 0` / `-gt -1` as a floor in shell    the spelling no parser sees
  R7  a conuco `main` returning a code          the 07-19 idiom, re-established

CAUGHT ELSEWHERE
  · a corpus `.logos` in no suite / an orphan   corpus_registration_gate.sh
    `.expected`
  · a chunk of a level that ran nothing         test-levels.sh: `--no-tests=error`
                                                and `tot_run >= COUNT`
  · an engine that DECLINED to answer           the layout verifier's `declined`
                                                field, with its own canary
  · a canary count restated in prose            the layout gate accumulates it

OPEN, WITH THE GROUND — and a wrong claim of coverage would be worse
  · AN ORACLE THAT SHARES A COMPARATOR WITH ITS SUBJECT. The defining question
    is what a name MEANS, not what a line looks like: `verdict.py` reading a
    number the compiler printed is independent; a Logos sort checked by the same
    comparator is not, and the two are textually identical. Mechanizing it needs
    a dependency notion this repo has no representation for. It stays a review
    question, and the standing answer is `feedback_oracle_must_be_independent`.
  · A FLOOR SET BELOW ITS MEASURED VALUE. `MIN_SCANNED=5000` against a measured
    6740 is arithmetic no scan can do — the measured value exists only in the
    run. Half-closed: a floor of 0 is refused (above), and every floor in the
    layout gate and here carries the measurement and the date beside it. The
    remaining hole is a floor that is positive and too low.
  · A NUMBER IN A COMMIT MESSAGE THAT NO GATE PINS (`f5688fb8`: "+8 bytes",
    remeasured at +1168). A commit-msg hook would have to decide which numbers
    in free prose are claims about the tree; the false-positive rate makes it a
    hook people disable, which is worse than none. Mechanized only in its
    in-repo form — a gate may not restate a number it computed — and that one
    IS closed, by derivation, in the artifact where it had recurred.
  · STATE DERIVED FROM A NUMERIC CODE RANGE (`why_axis_of`: 0–9 ⇒ AX_NONE, and
    `WG_AGG` got 6). This is C++ semantics, not an idiom: the fix is a separate
    field, and telling a legitimate range test from a state predicate needs the
    meaning of both numbers. Open.
  · A PYTHON GATE SHELLING OUT. R2/R3/R4/R6 read shell text; a helper doing
    a `subprocess` call with `shell=True` is outside them. MEASURED 2026-08-01:
    the only occurrences of that spelling under `tests/` and `scripts/` are the
    two in this paragraph, so there is nothing to find today — a stated gap, not
    a finding.

EXIT
  0  every file clean
  1  a rule fired — the TREE is what is wrong (the message names file:line)
  3  the POPULATION could not be derived (no --build-dir, ctest unavailable, an
     unparseable or empty answer). Nothing in the run is evidence about the
     tree; a smaller population reported clean is the defect, not the report.
  4  --selftest: a rule did not catch its own canary — THIS PROGRAM is wrong
"""
import json
import os
import re
import subprocess
import sys

# ── THE POPULATION IS DERIVED FROM THE ARTIFACT, NOT LISTED HERE ─────────────
#
# ⚠⚠ WHAT THIS REPLACED, AND WHY IT WAS THE SAME DEFECT ONE LEVEL UP.
#
# This file used to open with
#     SHELL_DIRS = ["tests/logos", "tests/exhaustive", "scripts"]
# walked NON-RECURSIVELY, and `MIN_SH = 23` — a floor which happened to equal
# exactly what that walk found, so it certified the walk against itself.
#
# MEASURED 2026-08-01: ctest registers 42 distinct shell scripts as test
# COMMANDs. 24 of them were outside those three directories — 21 in
# `tests/lforge/`, plus `tests/diag/json_format.sh`,
# `tools/check_typebuilder_lint.sh` and `tests/logos/ir/run_ir_snapshot.sh`
# (that last one one directory DOWN from a scanned directory, which is what
# "non-recursively" costs). Running this file's own rules over the 24 found 15
# R2 violations in 7 files, every one of them a registered ctest test inside L4,
# and the dangerous `if <pipe> | grep -q` form live in `archive_integrity.sh` at
# two sites where a successful match is reported as a failure.
#
# A gate whose population is a directory list checks the directories somebody
# thought of. So the population is now ASKED:
#
#   THE SET OF SHELL GATES = the scripts CTEST ACTUALLY INVOKES
#                          ∪ the scripts on disk that ctest does NOT invoke.
#
# The second half is not a leak: an unregistered script is either a gate nothing
# runs (R5, a finding) or a tool run by hand, and the rules are applied to it
# EITHER WAY — an idiom that lies lies whether ctest calls it or a person does.
# What derivation buys is that neither half can be short without the count
# saying so, and the count is floored below at a value measured off the
# derivation, not off a directory list.
#
# ⚠ The ctest half needs a CONFIGURED BUILD DIRECTORY, passed as `--build-dir`.
# Without it this program does NOT fall back to a directory walk and report
# clean over the smaller set — that is precisely the shape it exists to refuse.
# It exits 3, the same code verdict.py uses for "I could not look".
SHELL_ROOTS = ["tests", "scripts", "tools"]
PY_DIRS = ["tests/logos", "tests/exhaustive"]
# The memoria suite: standalone binaries whose `main` IS the verdict channel.
# This is where the 8-bit ceiling was first diagnosed and fixed, and where the
# fix decayed — see R7.
#
# ⚠ IT MOVED (2026-08-16), and this gate is why the move could not be silent: the
# population was `conuco/memoria/tests`, the tests are now corpus members named
# `memoria_*` under tests/logos/pass, and a path that no longer exists made this
# report CLEAN over zero files — which is exactly the blindness the floor below
# refuses. The population is a PREFIX now, not a directory, so a memoria test
# added to the corpus joins R7 by being named.
MEMORIA_DIR = "tests/logos/pass"
MEMORIA_PREFIX = "memoria_"

# ── THE RESIDUE, AND ITS GROUND, ONE LINE EACH ───────────────────────────────
#
# Everything above is derived. This is what derivation CANNOT answer: whether a
# script that ctest does not invoke is a gate nobody runs (the fifth recorded
# kind) or a thing that legitimately runs elsewhere. No artifact records that —
# "elsewhere" is a person's procedure — so it is written here, with the ground
# on the line, and this program PRINTS the whole list on every clean run. A
# hand-written exception that nobody ever sees again is how a list becomes a
# blindfold; one that is printed beside the derived counts is a stated gap.
#
# Adding a name here is the only way past R5, and the ground is the cost.
NOT_GATES = {
    # A SOURCED LIBRARY, not a script anyone runs. `facts_fold.sh` defines one
    # function (`facts_require`) and is `.`-sourced by the three `logos_09_*`
    # census gates; it has no `main`, and a ctest test whose COMMAND is this file
    # would define a function and exit 0. It is exempt because it cannot be run,
    # not because nobody bothered: the verdict it carries — every population
    # member has facts, and they are this build's — is pronounced inside each of
    # the three gates that source it, and each of those IS registered. Bitten
    # 2026-08-21 on all three (a deleted facts dir, and a forged stamp): exit 2,
    # the missing/stale members named.
    "facts_fold.sh":           "a sourced bash LIBRARY (one function, no main); "
                               "its verdict is pronounced by the three "
                               "logos_09_* census gates that source it",
    # Reporters. They pronounce no verdict at all — this is also written down in
    # verdict.py's gate census.
    "ctest-summary.sh":        "reports a ctest run; asserts nothing",
    # TWO BARRIERS AND AN AUDITOR, added 2026-08-28 after Victor's point that an
    # instruction agents can read is not an instruction they execute — measured,
    # 6 builds for a batch of 9 probes where the protocol says ONE, and `L4 bc`
    # run 3 and 2 times where the ladder says once. None of the three is a gate:
    # `probe-batch.sh` prices ONE batch of hypotheses, so there is no population
    # to register it over (same ground as change-budget.sh and ceiling-probe.sh);
    # `workflow-audit.py` reads a workflow transcript, which is not a property of
    # the tree at all — it grades a RUN, and a run has no committed state to
    # assert against.
    "probe-batch.sh":          "installs one batch of probes, builds ONCE and "
                               "prices them; a hand-run tool for one hypothesis "
                               "set, with no population to register it over",
    "workflow-audit.py":       "grades a workflow RUN from its transcript "
                               "(builds, poll loops, gate repeats) — it asserts "
                               "nothing about the tree",
    # A READER, not a gate. It prints the verdict a previous L4 recorded for
    # THIS tree state, so the next step does not re-run a 12-minute gate for an
    # answer already written down. It asserts nothing itself: its three exits
    # are "this record is current", "a record exists but is about a different
    # tree", and "nobody has measured" — and it refuses to conflate the last two,
    # because reporting a stale verdict as current is the failure this whole
    # directory exists to prevent.
    "gate-state.sh":           "reads the verdict a previous L4 recorded for "
                               "this exact tree state; asserts nothing, and "
                               "distinguishes STALE from ABSENT",
    # THE RECORD AND ITS RUNNER, 2026-08-28. `gate-run.sh` runs a ctest filter
    # ONCE per (test set × compiler) and records the result; `gate_db.py` is the
    # SQLite store behind it. Neither asserts anything about the tree: the runner
    # returns whatever ctest returned, and the store answers questions ("what
    # failed", "when did this test last pass") that no fixed verdict could stand
    # in for. The KEY is the enumerated test list plus a content hash of logosc,
    # because an argument-keyed cache is wrong in both directions and an
    # mtime-keyed one throws away identical rebuilds.
    "gate-run.sh":             "runs a ctest filter once per (test set x "
                               "compiler) and records it; its own verdict is "
                               "ctest's, and it asserts nothing of its own",
    "gate_db.py":              "the SQLite store behind gate-run.sh; a reader "
                               "and a writer, with no verdict of its own",
    # A HAND-RUN CEILING PROBE, and it must not become a gate for the same
    # reason change-budget.sh must not: it answers a question about ONE
    # hypothesis, so there is no population to register it over. It reports how
    # many `bc_admits.ledger` rows a DELIBERATELY WRONG edit could close — the
    # edit ignores exemptions, over-refuses, and is never landed — so the number
    # is an UPPER BOUND used to decide what to fund, not a verdict about the
    # tree. Registering it would assert that some ceiling is the right one.
    # It does carry `--selftest`, whose verdict IS fixed (the sabotage probe
    # must close every row, which is what caught a broken reader on its first
    # run); that is the instrument grading itself, exactly like
    # tools/dlog/selftest.sh, and it is run by hand beside a probe session.
    "ceiling-probe.sh":        "a hand-run CEILING PROBE for one hypothesis at "
                               "a time; its number is an upper bound to spend "
                               "against, not a verdict, and there is no "
                               "population to register it over",
    # THE SIBLING OF ceiling-probe.sh, exempt for the same ground and one more.
    # ceiling-probe.sh reads closures off the acceptance ledger, where a
    # probe-induced FAILURE is good news because every row asserts the defect is
    # still there. Among programs that COMPILE there is no such assertion: an
    # armed probe's failures MIX programs wrongly admitted (the finding) with
    # legal programs wrongly refused (the cost), and NOTHING IN A COMPILE'S EXIT
    # CODE SEPARATES THEM. This script ranks them by which population asserted
    # the program legal — rustc's own verdict, the stdlib build, a fixture
    # author's word — and hands back the residue UNSORTED, on purpose. So it has
    # no fixed verdict about the tree and no population to be registered over,
    # twice over: the sort is a ranking for a human, not a claim. Its
    # `--selftest` verdict IS fixed and hand-run beside a probe session — two
    # poles, `selftest_refuse` must change hundreds of programs and
    # `selftest_inert` (a pure observer at the same site) must change none.
    "pass-probe.sh":           "a hand-run PROBE READER over the pass corpus and "
                               "the stdlib; it RANKS changed programs by whose "
                               "assertion of legality they rest on and leaves "
                               "the residue unsorted, so it pronounces no "
                               "verdict and has no population to register over",
    # A HAND-RUN MEASURING TOOL, and the one thing it must not become is a gate.
    # It reports the SIZE of a change (files / logic lines / new names / new
    # branches) against a budget the author declared BEFORE writing it — the
    # external half of "minimise the code you write" (Victor 2026-08-24). It has
    # no fixed verdict of its own: with no `--declare` it only measures, and with
    # one it answers a question about THAT change, so there is no population it
    # could be registered over. Registering it would also invert its purpose: a
    # budget that ctest re-checks every commit is a budget nobody had to predict.
    # A HAND-RUN FACT EXTRACTOR for the Datalog offloader (`tools/dlog`). It
    # pronounces no verdict at all — it emits `.facts` files that a Soufflé
    # program then reasons over, and the VERDICT is that program's. Registering
    # it would assert something about the extraction that only the rules can
    # decide. See tools/dlog/README.md for why the extractor, not the rules, is
    # the risk in that design.
    "gate.sh":                 "the tools/dlog findings baseline. NOT a ctest "
                               "test yet, and the baseline says why: 141 of 164 "
                               "rows are UNTRIAGED, so registering it would "
                               "assert that a pile of unexamined rows is the "
                               "correct state of the world. A ratchet first — it "
                               "stops the pile growing — a gate when the debt is "
                               "paid",
    "ask.sh":                  "the single entry point to tools/dlog: extracts "
                               "facts (content-keyed cache) and runs one .dl "
                               "question, printing where the answer landed. It "
                               "asserts NOTHING — the question holds the verdict "
                               "and none of them is a gate yet",
    "sweep.sh":                "runs tools/dlog over every compiler TU and prints "
                               "a REPORT to read, not a verdict; minutes of clang "
                               "and its rules are graded by selftest.sh",
    "make.sh":                 "builds tools/dlog/lir_facts against system LLVM; "
                               "a build script asserts nothing about this tree",
    "selftest.sh":             "the known-answer test for tools/dlog: it builds a "
                               "worktree of revision 28fc7c75 and needs souffle "
                               "and libclang-20-dev, so it grades a TOOL, not "
                               "this tree — run it by hand",
    "change-budget.sh":        "a hand-run measuring tool; its verdict is about "
                               "ONE change against a budget declared before that "
                               "change was written, so it has no fixed population "
                               "and cannot be a standing gate",

    # A HAND-RUN COVERAGE MAP, and the sibling of the two probe readers above —
    # exempt for the same ground and one more of its own. It rebuilds ONE
    # compiler TU with `-fprofile-instr-generate -fcoverage-mapping`, runs the
    # ctest corpus and the four stdlib layers under it, and reports which
    # regions never executed, which are near-dead and which are hot. It
    # pronounces NO verdict: a count-0 region is dead code OR a case the corpus
    # does not exercise OR a structurally unreachable guard, three situations
    # with three different responses, and nothing in the profile separates them.
    # There is therefore no population it could be registered over and no floor
    # on coverage that is the right one — a floor here would be a number
    # somebody guessed, which is the shape `verdict.py`'s census already refuses.
    # It is also a MAP, not an assertion: it says where a probe would have a
    # population behind it and never what the probe would find.
    "coverage-map.sh":         "a hand-run REGION MAP for one compiler TU: it "
                               "reports which regions the corpus executes and "
                               "how often, and a zero is dead code OR an "
                               "unexercised case OR an unreachable guard — three "
                               "answers it cannot separate, so it pronounces no "
                               "verdict and there is no floor on coverage it "
                               "could be registered to hold",
    "perf-slow.sh":            "lists the slowest tests; asserts nothing",
    "test-levels.sh":          "DRIVES ctest (L0–L4); being a ctest test would "
                               "be a recursion",
    # Gates that run OUTSIDE ctest, by the commit procedure. Each does pronounce
    # a verdict; what ctest cannot give them is their input.
    "abi-check.sh":            "the ABI gate — step 4 of the commit procedure; "
                               "needs a BUILT stdlib plus the committed spec "
                               "baseline, which a ctest test has no way to "
                               "sequence against the build",
    "abi-analyze.sh":          "qualifies an ABI delta BETWEEN TWO GIT REVISIONS "
                               "— its input is a revision range, not the tree",
    # Oracle harnesses and regenerators under tools/. Each builds two parsers
    # from a grammar and compares them; the checked-in artifact they produce is
    # what the suite gates, and building both backends is minutes, not seconds.
    "run.sh":                  "peg_gen cross-backend / AST-equality oracle — "
                               "builds two parser backends and diffs them; the "
                               "CHECKED-IN generated parser is what ctest gates",
    "run_wql.sh":              "peg_gen_cpp WQL cross-backend oracle; same",
    "regen.sh":                "REGENERATES the checked-in WQL/Trama parsers; a "
                               "producer, not a verdict",
    "trama_run.sh":            "peg_gen_logos Trama behaviour oracle; same as "
                               "run.sh",
    # Measurement instruments. They REPORT a number that is expected to move; the
    # properties of theirs that do NOT move are asserted inside them (exit 2).
    "criterion1_materialization_instrument.sh":
                               "ADR 0025 criterion 1's instrument — a whole-corpus "
                               "compile sweep (185 fixtures) that REPORTS "
                               "numerator/denominator. Its values are "
                               "corpus-size-dependent by construction: one added "
                               "fixture moved the denominator +10 and another +15, "
                               "so a ctest gate over them is either re-baselined "
                               "every commit (a number that always agrees) or a "
                               "number to tune — and the ADR's S6 writ control is "
                               "the measured case where tuning it moved it the "
                               "WRONG way (the text ratio 'improved' while the "
                               "artifact materialized 29 times more). The FOUR "
                               "properties that do NOT move with a slice (G1 one "
                               "population, G2 every plan head classified, G3 no "
                               "probe lost, G4 every ownership credit WITNESSED "
                               "by a nonzero fire count of the owning head in "
                               "the same sweep) are asserted inside the script "
                               "at exit 2. ⚠ G4 was added at R-B (2026-08-15) "
                               "and it is the one that changes what the script "
                               "MEANS: before it, the ACCOUNTED column summed a "
                               "hand-written name->prose table with nothing "
                               "connecting the credit to the trace, so the "
                               "number moved when the TABLE moved. R-B's "
                               "control tree measured the cost — 650 bindings "
                               "credited to four heads whose fire count was "
                               "ZERO on that same sweep, printed three lines "
                               "from the evidence against them. A tree older "
                               "than the table now exits 2 instead of printing "
                               "a flattering number, values first. Decision "
                               "recorded in "
                               "docs/adr/0025-criteria-and-instruments.md §1",
    "answer_diff_instrument.sh":
                               "ADR 0025 R-B's answer oracle — compiles, LINKS "
                               "and RUNS the whole wql_*/deem_* pass corpus and "
                               "records exit + a stdout hash per fixture. It "
                               "pronounces no verdict by itself: its output is "
                               "one tree's answers, and the verdict is a `diff` "
                               "of TWO trees, which no single ctest test can "
                               "hold (the other tree is a build that no longer "
                               "exists on disk when the test runs). What it "
                               "guards against is a stage landing with nobody "
                               "having looked at the values — the D4 class, "
                               "where every artifact-channel and trace-channel "
                               "number was unchanged through a wrong-answer "
                               "miscompile. Its own integrity IS asserted "
                               "inside it at exit 2 (A1 one row per fixture, "
                               "A2 population floor). The per-fixture answers "
                               "the SUITE pins are the `.expected` files, and "
                               "those do run under ctest",
}


# A file may hold DELIBERATE violations — this program's own canary table is
# nothing but violations. They live between these markers and are skipped, so
# the rest of such a file is still scanned. Blanket-excluding a whole file would
# make "the lint is clean" a statement about a smaller tree than it claims.
CANARY_BEGIN = "lint:canaries-begin"
CANARY_END = "lint:canaries-end"


class CannotLook(Exception):
    """The derivation failed. NOT a finding about the tree — a statement that
    this program could not obtain its own population, which must never be
    reported as a clean scan of a smaller one."""


def ctest_shell_commands(build_dir, root):
    """Every `.sh` ctest invokes, repo-relative. THE ARTIFACT IS ASKED.

    `--show-only=json-v1` runs no test; it prints the registered command line of
    each, which is the only place that knows what the suite is. A CMakeLists
    grep would find the strings somebody wrote, not the commands cmake built:
    an `add_test` behind an `if()` that is false registers nothing and greps the
    same."""
    try:
        p = subprocess.run(["ctest", "--show-only=json-v1"], cwd=build_dir,
                           capture_output=True, text=True, timeout=180)
    except (OSError, subprocess.SubprocessError) as e:
        raise CannotLook(f"could not run ctest in {build_dir}: {e}")
    if p.returncode != 0:
        raise CannotLook(f"ctest --show-only failed in {build_dir} "
                         f"(exit {p.returncode}): {p.stderr.strip()[:300]}")
    try:
        d = json.loads(p.stdout)
    except ValueError as e:
        raise CannotLook(f"ctest's json was not parseable: {e}")
    tests = d.get("tests")
    if not tests:
        raise CannotLook("ctest reported ZERO tests — a configured build "
                         "registers thousands, so this is a build directory "
                         "that has not been configured, not an empty suite.")
    out = {}
    for t in tests:
        for a in t.get("command", []):
            if a.endswith(".sh"):
                out.setdefault(os.path.relpath(a, root), set()).add(t.get("name", "?"))
    return out, len(tests)


def on_disk_shell(root):
    """Every `.sh` under the roots, RECURSIVELY. `tests/logos/ir/` is one
    directory below a directory the old walk listed, and its `run_ir_snapshot.sh`
    is registered eight times."""
    out = set()
    for d in SHELL_ROOTS:
        full = os.path.join(root, d)
        if not os.path.isdir(full):
            continue
        for base, _dirs, names in os.walk(full):
            for n in sorted(names):
                if n.endswith(".sh"):
                    out.add(os.path.relpath(os.path.join(base, n), root))
    return out


def _mask_canary_regions(text):
    """Blank the lines inside canary regions, KEEPING the line numbering."""
    out, inside = [], False
    for ln in text.splitlines():
        if CANARY_BEGIN in ln:
            inside = True
        elif CANARY_END in ln:
            inside = False
        out.append("" if inside else ln)
    return "\n".join(out)


class Finding:
    def __init__(self, rule, path, line, text, why):
        self.rule, self.path, self.line, self.text, self.why = \
            rule, path, line, text, why

    def render(self):
        return (f"{self.path}:{self.line}: [{self.rule}] {self.why}\n"
                f"    {self.text.strip()[:160]}")


# ── R1  A PROCESS EXIT STATUS IS EIGHT BITS ──────────────────────────────────
# Two forms. (a) a literal `exit N` outside 0..125 — 126/127/128+ are the
# shell's own (not-executable / not-found / killed-by-signal), and anything
# above 255 wraps, so 256 is a SUCCESSFUL exit. (b) a generator that emits a
# COMPUTED code into a generated program's `main` return — which is the same
# channel one level of indirection away, and is exactly how 272 codes ended up
# in 8 bits after the class had been fixed.
#
# ⚠⚠ AND THE REGEX SAW 15% OF ITS OWN CORPUS. The previous form was
#     r'(?:^|[;&|]\s*|\bthen\s+|\belse\s+|\bdo\s+)exit\s+(\d+)'
# which anchors at COLUMN 0 or immediately after `;`/`&`/`|`/then/else/do.
# MEASURED 2026-08-01 over the derived population: 13 `exit N` at column 0
# against 75 INDENTED. It was silent on
#     if [ 1 ]; then\n    exit 256\nfi        (indented under `then`)
#     fail() { exit 256; }                    (inside a function body)
#     \texit 256                              (tab-indented)
#     exit $((250+6))                         (arithmetic)
#     RC=256; exit $RC                        (through a variable)
# A rule must see every form the LANGUAGE admits, not the form its author wrote.
#
# So the test is now COMMAND POSITION, computed: `exit` is a finding wherever a
# command can start — after nothing but whitespace, after `;` `&` `|` `(` `{`,
# after `&&` `||` `;;`, or after the keywords then/else/elif/do — at ANY indent,
# and NOT inside a quoted string (which is what made `echo "exit=$rc"` 21 false
# positives when the anchor was simply dropped).
#
# THREE VERDICTS, NOT TWO. A literal and an arithmetic expression are DECIDED. A
# status that comes from a variable or from a bare `exit` (which re-raises `$?`)
# CANNOT BE, and the rule says so instead of passing: the line carries
# `# lint:exit-ok — <ground>` or it is a finding. That is the same discipline
# R3 uses, and it is what stops "no findings" from meaning "the rule looked away".
RE_PY_EXIT = re.compile(r'\bsys\.exit\(\s*(\d+)\s*\)')
RE_EMIT_CODE = re.compile(r'return\s*\{[^}]*\}\s*i(?:8|16|32|64)\b')
RE_EXPECTED_EXIT = re.compile(r'^exit:\s*(-?\d+)\s*$')
RE_ARITH = re.compile(r'^\$\(\((?P<e>[0-9+\-*/ ()]+)\)\)$')
# What may sit immediately before an `exit` that is a COMMAND. Everything else
# (a `=`, a letter, a `"`) means this `exit` is a word inside something.
_CMD_LEAD = re.compile(r'(?:^|[;&|(){}]|&&|\|\||;;|\bthen\b|\belse\b|\belif\b|\bdo\b)\s*$')
# The tail of a CASE LABEL whose first branch is the word we matched:
# `|stdout)`, `|stdout|status)`. A pattern branch contains no whitespace and no
# command separator, which is what tells it from a pipeline `exit | tee`.
RE_CASE_ALT = re.compile(r'^(?:\|[^\s|)(&;]+)+\)')


def _sh_unquoted_spans(line):
    """The [start, end) spans of `line` that are NOT inside '…' or "…".

    Approximate on purpose and deliberately CONSERVATIVE about its own limits:
    it tracks single/double quotes and backslash escapes, which is what
    separates `exit 256` from `echo "exit=$rc"`. A heredoc body is not tracked —
    those show up as unquoted and are therefore SCANNED, which errs toward
    reporting rather than toward silence."""
    spans, start, q, i = [], 0, None, 0
    while i < len(line):
        c = line[i]
        if q is None:
            if c == "\\":
                i += 2
                continue
            if c in "'\"":
                spans.append((start, i))
                q = c
        elif c == q:
            q = None
            start = i + 1
        elif c == "\\" and q == '"':
            i += 2
            continue
        i += 1
    if q is None:
        spans.append((start, len(line)))
    return spans


def sh_exit_sites(line):
    """(column, argument-text) for every `exit` in COMMAND POSITION on `line`.

    The argument is everything up to the next command terminator; `''` means a
    bare `exit`, which exits with `$?`."""
    if line.lstrip().startswith("#"):
        return []
    out = []
    for lo, hi in _sh_unquoted_spans(line):
        seg = line[lo:hi]
        for m in re.finditer(r'exit\b', seg):
            if not _CMD_LEAD.search(seg[:m.start()]):
                continue
            rest = seg[m.end():]
            # `exit)` is a CASE LABEL, not a command — `case "$key" in exit) …`
            # sits in command position by every other test. A subshell `( exit 1 )`
            # has a space, so the immediate `)` is what distinguishes them.
            #
            # ⚠ AND A CASE LABEL MAY BE AN ALTERNATION: `exit|stdout)` is one
            # pattern with two branches, and only the FIRST branch was
            # recognised. MEASURED 2026-08-01 on `tests/logos/run_test.sh`, the
            # first file written after this rule landed that reads two keys in
            # one arm: the rule reported a bare `exit` (undecidable) on
            # `exit|stdout) HAS_KEY=1; break ;;` — a false finding, which costs
            # exactly what a missed one does, because the fix on offer is to
            # annotate a line that never exits.
            # The distinguishing fact is the same as above: pattern branches
            # carry no whitespace and no command separator before the `)`.
            if rest.startswith(")") or RE_CASE_ALT.match(rest):
                continue
            arg = re.split(r'[;&|)]|\}|&&|\|\|', rest, maxsplit=1)[0]
            out.append((lo + m.start(), arg.strip()))
    return out


def _mask_embedded(text):
    """Blank out SINGLE-quoted regions, which span lines and hold ANOTHER
    LANGUAGE — an embedded awk or python program.

    ⚠ MEASURED 2026-08-26. R1 reddened `tools/dlog/extract.sh` for
        if (started && depth <= 0) { print body; exit }
    which is awk's `exit`, inside an awk program, eight lines into a
    single-quoted argument. The rule keys on the WORD and cannot see which
    language the word belongs to — the same shape as the one-scheduler lint
    matching the word "parallel" inside an `echo`, fixed the day before.
    Double-quoted strings were already stripped there; single quotes are what
    embedded programs actually use, and they cross lines, so a per-line strip
    cannot do it. Tracked as state instead.

    Detection is not blunted: a real shell `exit` is never inside single
    quotes, and `xargs -P '$N'` still leaves `xargs -P` once the argument is
    blanked.

    ⚠ AND THE FIRST CUT OF THIS FUNCTION WAS ITSELF WRONG, measured the same
    hour: it toggled on every `'`, so an APOSTROPHE IN PROSE desynced it — eleven
    of them (`r's`, `m's`, `e's`) in one gate's comments left the rest of the file
    masked backwards, and the lint then reported THREE new violations that were
    all awk. Comments and quotes cannot be resolved in two passes, because a `#`
    inside a quoted string is not a comment and a quote inside a comment is not a
    quote. One pass, both states.
    """
    out = []
    in_sq = in_dq = False          # ⚠ ACROSS lines: an embedded awk or python
                                   # program is one single-quoted region that
                                   # spans many. Resetting per line was the
                                   # SECOND wrong cut of this function and it
                                   # let the awk `exit` through again.
    for ln in text.splitlines():
        buf, i = [], 0
        while i < len(ln):
            ch = ln[i]
            if not in_sq and not in_dq and ch == "#":
                buf.append(" " * (len(ln) - i))     # comment: blank to EOL
                break
            if ch == "'" and not in_dq:
                in_sq = not in_sq; buf.append(" ")
            elif ch == '"' and not in_sq:
                in_dq = not in_dq; buf.append(ch)   # keep: R3/R4 read these
            elif in_sq:
                buf.append(" ")
            else:
                buf.append(ch)
            i += 1
        out.append("".join(buf))
    return "\n".join(out)


def r1_exit_ceiling(path, text):
    out = []
    is_py = path.endswith(".py")
    # ⚠ SCAN THE MASKED LINE, READ THE ANNOTATION OFF THE RAW ONE. Masking blanks
    # comments — including the `# lint:*-ok` grounds the rules look for — so a
    # rule fed only the masked text is blind to its own exemptions. The selftest
    # caught exactly that: the canary `exit $RC  # lint:exit-ok — …` FIRED when
    # it must not. Two views of one line, and each rule must say which it means.
    raws   = text.splitlines()
    masked = _mask_embedded(text).splitlines()
    for i, ln in enumerate(masked, 1):
        raw  = raws[i - 1] if i - 1 < len(raws) else ln
        code = ln.split("#", 1)[0] if is_py else ln
        if is_py:
            sites = [(m.start(), m.group(1)) for m in RE_PY_EXIT.finditer(code)]
        else:
            sites = sh_exit_sites(code)
        for _col, arg in sites:
            n = None
            if re.fullmatch(r'-?\d+', arg):
                n = int(arg)
            elif not is_py:
                am = RE_ARITH.match(arg)
                if am:
                    try:
                        n = int(eval(am.group("e"), {"__builtins__": {}}, {}))
                    except Exception:
                        n = None
                elif "lint:exit-ok" in raw:
                    continue
                else:
                    out.append(Finding(
                        "R1-exit-undecidable", path, i, ln,
                        f"`exit {arg or '(bare — re-raises $?)'}` — this rule "
                        f"CANNOT DECIDE the status statically, and a rule that "
                        f"cannot look must not report clean. A real process "
                        f"status is already a byte; a computed one is the 8-bit "
                        f"ceiling waiting to happen (`RC=256; exit $RC` exits 0). "
                        f"Say which it is: `# lint:exit-ok — <ground>`."))
                    continue
            if n is not None and not (0 <= n <= 125):
                out.append(Finding(
                    "R1-exit-ceiling", path, i, ln,
                    f"exit status {n} — a process exit status is EIGHT BITS "
                    f"({n} & 0xFF == {n & 0xFF}) and 126..127 are the shell's "
                    f"own. Put the diagnosis in the OUTPUT and return a fixed "
                    f"byte."))
        if is_py and RE_EMIT_CODE.search(code):
            out.append(Finding(
                "R1-emitted-code", path, i, ln,
                "this emits a COMPUTED failure code into a generated program's "
                "`main` return, i.e. into an 8-bit exit status. The code must "
                "travel in the program's OUTPUT; the status carries one bit."))
    return out


def r1c_expected_exit(path, text):
    out = []
    for i, ln in enumerate(text.splitlines(), 1):
        m = RE_EXPECTED_EXIT.match(ln)
        if m and not (0 <= int(m.group(1)) <= 255):
            out.append(Finding(
                "R1-expected-exit", path, i, ln,
                f"an .expected declaring exit {m.group(1)} asserts something the "
                f"OS cannot deliver — the runner compares against a truncated "
                f"status."))
    return out


# ── R2  `pipefail` + `grep -q`: A MATCH REPORTED AS A FAILURE ────────────────
# `grep -q` exits at its first match and closes the pipe; the writer dies of
# SIGPIPE (141) and `pipefail` gives that status to the pipeline. In the
# `if ! …` form the check flakes red under load; in the `if …` form a present
# string reads as ABSENT, which is a gate going green on the thing it forbids.
RE_PIPE_Q = re.compile(r'\|\s*(?:LC_ALL=\S+\s+)?u?grep\s+(?:-\w*\s+)*-\w*q')


def r2_pipefail_grep_q(path, text):
    if "pipefail" not in text:
        return []
    out = []
    for i, ln in enumerate(text.splitlines(), 1):
        if ln.lstrip().startswith("#"):
            continue
        if RE_PIPE_Q.search(ln):
            out.append(Finding(
                "R2-pipefail-grep-q", path, i, ln,
                "`… | grep -q` under `set -o pipefail`: grep exits at the first "
                "match, the writer takes SIGPIPE 141, and pipefail reports the "
                "MATCH as a failed pipeline. Read into a file, then match it."))
    return out


# ── R3  A GATE ASKS THE BUILD, NOT git ───────────────────────────────────────
# `1abed428`: the ABI gate asked `git diff` ("did you edit the file?") where the
# question was whether the spec matches the BUILT stdlib, so it answered
# PRESERVING to someone who had forgotten to regenerate. git is legitimate for
# what only git knows — what the BASE commit held, whether a file is committed —
# and each such use must say so on the line, which is the whole point: the
# marker is cheap for a real use and impossible to write honestly for a proxy.
#
# ⚠ IT IS THE QUERY THAT CAN LIE, NOT THE MUTATION. Widening the population to
# every registered gate brought in `tests/lforge/{git_dep,lockfile,mvs_update,
# build_cache,replace_and_floor}.sh`, which BUILD a throwaway git repository in a
# temp directory because lforge's git-dependency resolution is the thing under
# test. `git init` / `add` / `commit` / `tag` / `config` produce no answer to
# anything — they cannot be a proxy for the build because they return nothing to
# read. MEASURED 2026-08-01: 34 hits across those five files, 30 of them
# construction, 4 of them `git rev-parse HEAD` capturing the SHA the fixture just
# made — and those four are queries and DO carry the marker.
#
# So the rule fires on the verbs that ANSWER. Narrowing a predicate to what it
# was always about is not weakening it: the same run scans 24 files the old
# predicate never opened.
GIT_QUERY_VERBS = ("diff", "status", "log", "show", "rev-parse", "rev-list",
                   "ls-files", "ls-tree", "describe", "cat-file", "blame",
                   "merge-base", "symbolic-ref", "name-rev", "shortlog",
                   "for-each-ref", "check-ignore", "grep")
RE_GIT = re.compile(r'(?:^|[;&|(`$]\s*|\bthen\s+|\bif\s+!?\s*)git\s+(?:-C\s+\S+\s+)?'
                    r'(' + "|".join(GIT_QUERY_VERBS) + r')\b')


def r3_gate_asks_git(path, text):
    out = []
    for i, ln in enumerate(text.splitlines(), 1):
        if ln.lstrip().startswith("#"):
            continue
        if RE_GIT.search(ln) and "lint:git-ok" not in ln:
            out.append(Finding(
                "R3-gate-asks-git", path, i, ln,
                "a gate asking git. git knows about HYGIENE (was it committed, "
                "what did the base hold); it does not know whether the ARTEFACT "
                "is right. If this is one of the things only git knows, say so "
                "on the line: `# lint:git-ok — <ground>`."))
    return out


# ── R4  `find` HERE IS bfs: A RELATIVE -newermt IS AN ERROR, NOT A MISS ──────
# bfs 4.1.1 rejects GNU's relative time strings; the idiom
# `find … -newermt '-15 minutes' 2>/dev/null | wc -l` therefore prints 0, and a
# zero reads as "nothing changed". Sensor failure indistinguishable from an
# observation. `@<epoch>` is accepted by both.
RE_NEWERMT = re.compile(r'-newermt\s+(["\']?)([^"\'\s]+)')


def r4_find_newermt(path, text):
    out = []
    for i, ln in enumerate(text.splitlines(), 1):
        if ln.lstrip().startswith("#"):
            continue
        m = RE_NEWERMT.search(ln)
        if m and not m.group(2).startswith(("@", "$")):
            out.append(Finding(
                "R4-find-newermt", path, i, ln,
                f"`find` here is bfs 4.1.1, which takes only `@<epoch>` and "
                f"ISO-8601 — {m.group(2)!r} is an ERROR that, with stderr "
                f"silenced, reads as 'no matches'. Use `@$(date -d … +%s)`."))
    return out


# ── R5  A GATE NOTHING RUNS ──────────────────────────────────────────────────
# The fifth recorded kind is a TEST MISSING FROM THE SUITE — an orphaned
# `.expected` that cmake warned about and skipped, caught only by the test count
# being one short. `corpus_registration_gate.sh` closed that for corpus
# fixtures. This is the same sentence one level up: a gate script that no
# CMakeLists registers runs never, and unlike a fixture it usually has no
# sibling to make its absence visible.
#
# ⚠ THIS USED TO GREP `tests/logos/CMakeLists.txt` FOR THE FILENAME, over the
# scripts in ONE directory. Two things were wrong with that and both are the
# slice's subject: a NAME appearing in a file is not a registration (an
# `add_test` inside a false `if()` greps the same), and one directory is not the
# tree. It now compares the scripts ON DISK against the commands CTEST ACTUALLY
# INVOKES — cmake's own answer, after every conditional has been evaluated.
def r5_unregistered_gates(on_disk, registered):
    out = []
    for rel in sorted(on_disk - set(registered)):
        name = os.path.basename(rel)
        if name in NOT_GATES or name.startswith("run_"):
            continue
        out.append(Finding(
            "R5-unregistered-gate", rel, 1, name,
            "ctest invokes no test with this script as its command, so nothing "
            "runs it. A gate outside the suite is indistinguishable from a "
            "passing one at every level — that is the fifth recorded kind. If "
            "it is a hand-run tool and not a gate, it belongs in NOT_GATES with "
            "its ground."))
    return out


# ── R6  A FLOOR OF ZERO IS NOT A MEASUREMENT ─────────────────────────────────
# `grep -c` yields 0, which is a number, so a floor of 0 reads as a measurement
# and holds for the empty run, the silenced channel and the deleted corpus
# alike. `verdict.py` refuses `--floor X 0` at the argument; this catches the
# shell spelling, which no parser sees.
RE_ZERO_FLOOR = re.compile(r'-(?:ge|gt)\s+(-?\d+)\b')


def r6_vacuous_floor(path, text):
    out = []
    for i, ln in enumerate(text.splitlines(), 1):
        if ln.lstrip().startswith("#"):
            continue
        for m in RE_ZERO_FLOOR.finditer(ln):
            n = int(m.group(1))
            if (m.group(0).startswith("-ge") and n <= 0) or \
               (m.group(0).startswith("-gt") and n < 0):
                out.append(Finding(
                    "R6-vacuous-floor", path, i, ln,
                    f"`{m.group(0)}` on a count holds for a run that measured "
                    f"NOTHING. Floor it at the value you observed, or say "
                    f"`-eq {n}` if zero is the assertion."))
    return out


# ── R7  A CONUCO TEST'S `main` RETURNS A BOOLEAN ─────────────────────────────
# THE LESSON DECAYED, AND THIS IS THE MEASUREMENT. On 07-19 the 8-bit ceiling
# was diagnosed here and fixed across ALL 33 conuco tests, mechanically verified
# ("in every one of the 33 files `main` returns only 0i32/1i32"). The suite is
# 67 tests now. MEASURED 2026-08-01, before this rule: NINE of them return a
# diagnostic code straight out of `main` — alg_entry up to 53, btfl_vle and
# btfl_tristream up to 78. None crosses 255 today, so none is lying yet; the
# recorded note said exactly that about the rest of the suite in 07-19 ("the
# others are safe for now, but the hazard is structural and the suite grows"),
# and then the suite grew. A verification that was true when it was made and is
# re-established by nothing is a claim with a shelf life.
#
# What this asserts is the FIXED idiom, not a bound: `main` returns 0i32 or
# 1i32, and any diagnosis travels in stderr. A bound ("no code above 125") would
# be the renumbering-into-a-safe-band that the record calls a deferral.
RE_CONUCO_MAIN = re.compile(r'\bfn\s+main\s*\(\s*\)\s*->\s*i32\s*\{')


def _fn_body(src, m):
    """The text between `m`'s `{` and its match. Braces only — no strings in
    these files carry an unbalanced one, and a false positive here is a finding
    a human then reads, not a silent pass."""
    i, depth = m.end(), 1
    while i < len(src) and depth:
        if src[i] == "{":
            depth += 1
        elif src[i] == "}":
            depth -= 1
        i += 1
    return src[m.end():i]


def r7_conuco_main_boolean(path, text):
    m = RE_CONUCO_MAIN.search(text)
    if not m:
        return [Finding(
            "R7-conuco-no-main", path, 1, "",
            "a conuco test with no `fn main() -> i32` — this rule could not "
            "look, and a rule that cannot look must not report clean.")]
    body = _fn_body(text, m)
    line0 = text[:m.end()].count("\n") + 1
    out = []
    for rm in re.finditer(r'\breturn\b([^;]*);', body):
        val = rm.group(1).strip()
        if val in ("0i32", "1i32"):
            continue
        ln = line0 + body[:rm.start()].count("\n")
        out.append(Finding(
            "R7-conuco-code-in-exit", path, ln, rm.group(0),
            f"`main` returns {val} — a DIAGNOSTIC CODE in an 8-bit exit status. "
            f"That is the class fixed here on 07-19 (`881939d3`) across all 33 "
            f"tests that then existed. Move the body to `run_all()` and let "
            f"`main` print `FAIL <test>: diagnostic code N` and return 1."))
    return out


SHELL_RULES = [r1_exit_ceiling, r2_pipefail_grep_q, r3_gate_asks_git,
               r4_find_newermt, r6_vacuous_floor]
PY_RULES = [r1_exit_ceiling]
# ⚠ EVERY rule, not just the two per-file lists. `r1c_expected_exit`,
# `r5_unregistered_gates` and `r7_conuco_main_boolean` run over their own
# corpora; leaving them out of the completeness check is how a rule comes to sit
# in the file with nothing proving it still bites — the shape this file exists
# to refuse.
ALL_RULES = SHELL_RULES + PY_RULES + [r1c_expected_exit, r5_unregistered_gates,
                                      r7_conuco_main_boolean]


# ── EVERY RULE PROVES IT STILL BITES, IN THE SAME RUN ────────────────────────
# (rule, filename, source, must-fire). A rule with no canary cannot be added:
# `--selftest` requires each rule below to appear here at least once firing and
# at least once NOT firing, so "no findings" is never "the regex stopped
# matching".
# lint:canaries-begin — every line below is a DELIBERATE violation
CANARIES = [
    (r1_exit_ceiling, "c.sh", "set -e\nif [ 1 ]; then exit 256; fi\n", True),
    (r1_exit_ceiling, "c.sh", "set -e\nexit 137\n", True),
    (r1_exit_ceiling, "c.sh", "set -e\nexit 1\nexit 0\nexit 2\n", False),
    # ── THE FORMS THE COLUMN-0 ANCHOR COULD NOT SEE (2026-08-01) ────────────
    # Each of these is a real spelling from the tree the widened population
    # brought in, and the old regex was silent on every one.
    (r1_exit_ceiling, "c.sh", "set -e\nif [ 1 ]; then\n    exit 256\nfi\n", True),
    (r1_exit_ceiling, "c.sh", "set -e\n\texit 256\n", True),
    (r1_exit_ceiling, "c.sh", "fail() { exit 256; }\n", True),
    (r1_exit_ceiling, "c.sh", "set -e\nfor f in a; do\n  exit 300\ndone\n", True),
    (r1_exit_ceiling, "c.sh", "set -e\ncase $x in a) exit 256 ;; esac\n", True),
    (r1_exit_ceiling, "c.sh", "set -e\n[ -f x ] && exit 256\n", True),
    (r1_exit_ceiling, "c.sh", "set -e\nexit $((250+6))\n", True),
    (r1_exit_ceiling, "c.sh", "set -e\n  exit 2\n    exit 0\n", False),
    # …and the forms it must NOT read as an exit at all: the word inside a
    # string. Dropping the anchor without the quote test made 21 of these fire.
    (r1_exit_ceiling, "c.sh", 'echo "exit=$rc, want 256"\n', False),
    (r1_exit_ceiling, "c.sh", "echo 'exit 256 is what it printed'\n", False),
    # UNDECIDABLE: the rule says so rather than passing.
    (r1_exit_ceiling, "c.sh", "RC=256\nexit $RC\n", True),
    (r1_exit_ceiling, "c.sh", 'exit "$EXIT"\n', True),
    (r1_exit_ceiling, "c.sh", "trap 'exit' INT\nexit\n", True),
    (r1_exit_ceiling, "c.sh", "exit $RC  # lint:exit-ok — $RC is a real wait status\n",
     False),
    (r1_exit_ceiling, "c.sh", 'case "$k" in\n  exit)   W="$v" ;;\nesac\n', False),
    # A case label with ALTERNATION branches — the form the single-`)` test
    # could not see, found live in `run_test.sh` the day after the rule landed.
    (r1_exit_ceiling, "c.sh", 'case "$k" in\n  exit|stdout) H=1 ;;\nesac\n', False),
    (r1_exit_ceiling, "c.sh", 'case "$k" in\n  exit|stdout|status) H=1 ;;\nesac\n', False),
    # …and the alternation form must not swallow a real one: a pattern branch
    # carries no whitespace, so `exit | tee` stays a pipeline whose `exit` is a
    # command, and `exit|stdout) exit 256` still reports the 256.
    (r1_exit_ceiling, "c.sh", 'case "$k" in\n  exit|stdout) exit 256 ;;\nesac\n', True),
    (r1_exit_ceiling, "c.sh", "set -e\nexit | tee log\n", True),
    (r1_exit_ceiling, "c.py", "sys.exit(300)\n", True),
    (r1_exit_ceiling, "c.py", "sys.exit(4)\n", False),
    (r1_exit_ceiling, "c.py", 'out.append(f"  if x {{ return {code}i32; }}")\n', True),
    (r1_exit_ceiling, "c.py", 'out.append(f"  return {acc};")\n', False),
    (r2_pipefail_grep_q, "c.sh",
     "set -euo pipefail\nif objdump -x t.o | grep -q foo; then :; fi\n", True),
    (r2_pipefail_grep_q, "c.sh",
     "set -euo pipefail\nawk '/x/' f | grep -qE 'y'\n", True),
    (r2_pipefail_grep_q, "c.sh",
     "set -euo pipefail\nobjdump -x t.o > d\nif grep -q foo d; then :; fi\n", False),
    (r2_pipefail_grep_q, "c.sh", "set -e\nobjdump -x t.o | grep -q foo\n", False),
    (r3_gate_asks_git, "c.sh", "if ! git diff --quiet -- spec; then exit 1; fi\n", True),
    (r3_gate_asks_git, "c.sh",
     "if ! git diff --quiet -- spec; then exit 1; fi  # lint:git-ok — is it committed\n",
     False),
    (r3_gate_asks_git, "c.sh", "SHA=$(git rev-parse HEAD)\n", True),
    (r3_gate_asks_git, "c.sh", "if git log -1 --format=%H; then :; fi\n", True),
    # CONSTRUCTION, not interrogation: these five build the throwaway repo that
    # lforge's git-dependency tests resolve against. They return no answer, so
    # they cannot be a proxy for one.
    (r3_gate_asks_git, "c.sh",
     "git init --quiet\ngit add .\ngit commit --quiet -m v1\ngit tag v1.0.0\n"
     "git config user.email t@example.com\n", False),
    (r4_find_newermt, "c.sh", "find . -newermt '-15 minutes' | wc -l\n", True),
    (r4_find_newermt, "c.sh", 'find . -newermt "15 minutes ago"\n', True),
    (r4_find_newermt, "c.sh", 'B=$(date -d "-15 min" +%s); find . -newermt "@$B"\n',
     False),
    (r6_vacuous_floor, "c.sh", 'if [ "$n" -ge 0 ]; then echo ok; fi\n', True),
    (r6_vacuous_floor, "c.sh", 'if [ "$n" -gt -1 ]; then echo ok; fi\n', True),
    (r6_vacuous_floor, "c.sh", 'if [ "$n" -ge 3676 ]; then echo ok; fi\n', False),
    (r7_conuco_main_boolean, "c.logos",
     "fn main() -> i32 {\n  if x { return 15i32; }\n  return 0i32;\n}\n", True),
    (r7_conuco_main_boolean, "c.logos",
     "fn main() -> i32 {\n  return 20i32 + (c % 10i32);\n}\n", True),
    (r7_conuco_main_boolean, "c.logos", "fn run_all() -> i32 { return 9i32; }\n",
     True),   # …because a file with no `main` cannot be judged, and says so
    (r7_conuco_main_boolean, "c.logos",
     "fn run_all() -> i32 { return 9i32; }\n"
     "fn main() -> i32 {\n  let c: i32 = run_all();\n"
     "  if c == 0i32 { return 0i32; }\n  return 1i32;\n}\n", False),
    (r1c_expected_exit, "c.expected", "exit: 256\n", True),
    (r1c_expected_exit, "c.expected", "exit: -1\n", True),
    (r1c_expected_exit, "c.expected", "exit: 1\nstdout:\nhi\n", False),
]
# lint:canaries-end


def _selftest_r5(broken, fired, silent):
    """R5 now compares two SETS, so its canary is two sets: one script ctest
    invokes and one it does not. Without this R5 would be the one rule nothing
    proves is alive, which this file forbids."""
    on_disk = {"tests/logos/registered_gate.sh", "tests/logos/orphan_gate.sh",
               "tests/logos/ctest-summary.sh"}
    registered = {"tests/logos/registered_gate.sh": {"t"}}
    got = {f.path for f in r5_unregistered_gates(on_disk, registered)}
    fired.add("r5_unregistered_gates")
    silent.add("r5_unregistered_gates")
    if "tests/logos/orphan_gate.sh" not in got:
        broken.append("  r5_unregistered_gates: a script ctest does not invoke "
                      "read as registered.")
    if "tests/logos/registered_gate.sh" in got:
        broken.append("  r5_unregistered_gates: flagged a script that IS a "
                      "ctest command.")
    if "tests/logos/ctest-summary.sh" in got:
        broken.append("  r5_unregistered_gates: flagged a declared NOT_GATES "
                      "reporter, so the grounded exception does not hold.")


def _selftest_derivation(broken):
    """THE DERIVATION ITSELF, PROVED ABLE TO FAIL. Every number this program
    floors comes from `ctest --show-only`; if that call could silently yield an
    empty or unparseable answer and be read as a small population, the whole
    conversion would be decoration. Three broken answers, each of which MUST
    raise CannotLook rather than return a short list."""
    import tempfile
    cases = [
        ("a directory with no CMakeCache", None),
        ("ctest answering with zero tests", '{"tests": []}'),
        ("ctest answering with non-json", 'Total Tests: 0\n'),
    ]
    for label, payload in cases:
        with tempfile.TemporaryDirectory() as td:
            if payload is not None:
                # A fake `ctest` on PATH that prints the broken answer and
                # succeeds — the shape that would otherwise read as a real,
                # small population.
                bindir = os.path.join(td, "bin")
                os.makedirs(bindir)
                fake = os.path.join(bindir, "ctest")
                with open(fake, "w") as fh:
                    fh.write("#!/bin/sh\ncat <<'XEOF'\n" + payload + "\nXEOF\n")
                os.chmod(fake, 0o755)
                saved = os.environ["PATH"]
                os.environ["PATH"] = bindir + os.pathsep + saved
            else:
                saved = None
            try:
                ctest_shell_commands(td, td)
            except CannotLook:
                pass
            else:
                broken.append(f"  ctest_shell_commands: {label} did NOT raise "
                              f"CannotLook — a failed derivation would be read "
                              f"as a small population.")
            finally:
                if saved is not None:
                    os.environ["PATH"] = saved


def selftest():
    broken = []
    fired, silent = set(), set()
    for rule, name, src, want in CANARIES:
        got = bool(rule(name, src))
        (fired if want else silent).add(rule.__name__)
        if got != want:
            broken.append(
                f"  {rule.__name__} on {src.strip()[:70]!r}: "
                f"{'did NOT fire, and must' if want else 'FIRED, and must not'}")
    _selftest_r5(broken, fired, silent)
    _selftest_derivation(broken)
    for rule in ALL_RULES:
        if rule.__name__ not in fired:
            broken.append(f"  {rule.__name__}: no canary that MUST fire. A rule "
                          f"with no canary is a rule nothing proves is alive.")
        if rule.__name__ not in silent:
            broken.append(f"  {rule.__name__}: no case that must NOT fire — a "
                          f"rule that flags everything proves nothing.")
    if broken:
        sys.stderr.write(
            "FAIL (gate_lint SELFTEST): a rule no longer catches its own "
            "canary.\n      THE LINT THAT GUARDS THE GATES IS BROKEN.\n"
            + "\n".join(broken) + "\n")
        return 4
    sys.stderr.write(
        f"[gate-lint] selftest: {len(CANARIES)} canaries through the same rules "
        f"this run uses — every violation flagged, every clean form left "
        f"alone.\n")
    return 0


def walk(root, build_dir):
    """Every rule over a population ASKED FOR, with the derivation's own counts.

    Returns (findings, census) where census is a dict of numbers the caller
    floors. Raises CannotLook when the population could not be obtained — which
    is not a clean scan and must not be reported as one."""
    registered, n_ctest = ctest_shell_commands(build_dir, root)
    on_disk = on_disk_shell(root)
    # THE UNION, and the two halves are counted separately so a shrink in either
    # is visible. A registered script that is not on disk would be a ctest entry
    # pointing at nothing — reported here rather than skipped.
    missing = sorted(set(registered) - on_disk)
    corpus = sorted(on_disk | set(registered))
    findings = []
    for rel in missing:
        findings.append(Finding(
            "R5-registered-but-absent", rel, 1, rel,
            "ctest registers a test whose command is this script, and the file "
            "is not on disk. The test cannot run; nothing distinguishes that "
            "from a test that passes."))
    n_sh = 0
    for rel in corpus:
        p = os.path.join(root, rel)
        if not os.path.isfile(p):
            continue
        text = _mask_canary_regions(
            open(p, encoding="utf-8", errors="replace").read())
        n_sh += 1
        for rule in SHELL_RULES:
            findings += rule(rel, text)
    n_py = 0
    for d in PY_DIRS:
        full = os.path.join(root, d)
        if not os.path.isdir(full):
            continue
        for name in sorted(os.listdir(full)):
            if not name.endswith(".py"):
                continue
            text = _mask_canary_regions(
                open(os.path.join(full, name), encoding="utf-8",
                     errors="replace").read())
            n_py += 1
            for rule in PY_RULES:
                findings += rule(f"{d}/{name}", text)
    # RECURSIVE: `tests/imported/pass` alone holds 120 subdirectories, and a
    # walk that only lists the top level would report "no bad exit expectation"
    # about a corpus it never opened.
    n_exp = 0
    for base, _dirs, names in os.walk(os.path.join(root, "tests")):
        for name in sorted(names):
            if not name.endswith(".expected"):
                continue
            n_exp += 1
            p = os.path.join(base, name)
            findings += r1c_expected_exit(
                os.path.relpath(p, root),
                open(p, encoding="utf-8", errors="replace").read())
    # THE CONUCO SUITE: standalone binaries whose `main` IS the channel.
    n_cnc = 0
    cnc = os.path.join(root, MEMORIA_DIR)
    if os.path.isdir(cnc):
        for name in sorted(os.listdir(cnc)):
            if not (name.startswith(MEMORIA_PREFIX) and name.endswith(".logos")):
                continue
            n_cnc += 1
            findings += r7_conuco_main_boolean(
                f"{MEMORIA_DIR}/{name}",
                open(os.path.join(cnc, name), encoding="utf-8",
                     errors="replace").read())
    findings += r5_unregistered_gates(on_disk, registered)
    return findings, {"sh": n_sh, "sh_registered": len(registered),
                      "sh_on_disk": len(on_disk), "ctest_tests": n_ctest,
                      "py": n_py, "expected": n_exp, "conuco": n_cnc}


# ⚠ FLOORS ON THE DERIVATION, NOT ON A DIRECTORY LIST. The old `MIN_SH = 23`
# equalled exactly what the old walk found, so it asserted the walk against
# itself and said nothing about the 24 registered gates outside it. These are
# read off the DERIVED numbers, MEASURED 2026-08-01 by this program:
#   [gate-lint] population: 42 scripts registered by ctest (6807 tests),
#               54 on disk under tests/ scripts/ tools/, 54 scanned
#   8 python helpers; 6728 `.expected` under tests/; 67 conuco tests.
# `sh_registered` is the number that matters: it is the one a directory list
# cannot produce, and the one that drops when a gate falls out of the suite.
MIN_SH, MIN_SH_REGISTERED, MIN_PY = 54, 42, 8
MIN_EXPECTED, MIN_CONUCO, MIN_CTEST_TESTS = 6728, 67, 6807
FLOORS = {"sh": MIN_SH, "sh_registered": MIN_SH_REGISTERED, "py": MIN_PY,
          "expected": MIN_EXPECTED, "conuco": MIN_CONUCO,
          "ctest_tests": MIN_CTEST_TESTS}


def main(argv):
    if "--selftest" in argv:
        rc = selftest()
        if rc:
            return rc
        argv = [a for a in argv if a != "--selftest"]
    build_dir = None
    rest = []
    for a in argv:
        if a.startswith("--build-dir="):
            build_dir = a.split("=", 1)[1]
        else:
            rest.append(a)
    root = rest[0] if rest else "."
    if not build_dir:
        sys.stderr.write(
            "FAIL (gate_lint COULD NOT LOOK): no --build-dir=<configured build>.\n"
            "      The population of shell gates is the set of scripts CTEST\n"
            "      INVOKES, and that answer lives in the build directory. This\n"
            "      program does NOT fall back to walking a list of directories:\n"
            "      the previous form did exactly that and reported clean over 23\n"
            "      of the 42 registered gates, with 15 live violations in the\n"
            "      other 24.\n")
        return 3
    try:
        findings, census = walk(root, build_dir)
    except CannotLook as e:
        sys.stderr.write(
            f"FAIL (gate_lint COULD NOT LOOK): {e}\n"
            f"      Nothing above this line is evidence about the tree.\n")
        return 3
    short = [(k, census[k], v) for k, v in FLOORS.items() if census[k] < v]
    if short:
        sys.stderr.write(
            "FAIL (gate_lint SAW TOO LITTLE): "
            + ", ".join(f"{k}={got} (floor {want})" for k, got, want in short)
            + f" under {os.path.abspath(root)}.\n"
              "      A clean report over a corpus that shrank is the blindness\n"
              "      this program checks for.\n")
        return 1
    if findings:
        sys.stderr.write(
            f"FAIL (gate_lint): {len(findings)} recorded lesson(s) violated. "
            f"Each of these was diagnosed, fixed at the class and written down "
            f"BEFORE the line below was written:\n\n"
            + "\n".join(f.render() for f in findings) + "\n")
        return 1
    sys.stderr.write(
        "[gate-lint] the residue, printed so it cannot become a blindfold — "
        f"{len(NOT_GATES)} scripts ctest does not invoke, each with its ground:\n"
        + "".join(f"              {k:26s} {v}\n"
                  for k, v in sorted(NOT_GATES.items())))
    sys.stderr.write(
        f"[gate-lint] population DERIVED, not listed: {census['sh_registered']} "
        f"shell scripts are the command of a ctest test (out of "
        f"{census['ctest_tests']} tests), {census['sh_on_disk']} are on disk "
        f"under {'/ '.join(SHELL_ROOTS)}, {census['sh']} were scanned.\n"
        f"[gate-lint] OK — {census['sh']} gate scripts, {census['py']} gate "
        f"helpers, {census['expected']} corpus expectations, {census['conuco']} "
        f"conuco tests: no exit status carrying more than a byte and none this "
        f"rule could not decide, no `| grep -q` under pipefail, no gate asking "
        f"git for an answer without its ground, no relative `-newermt`, no "
        f"vacuous floor, no unregistered gate, and every conuco `main` a "
        f"boolean.\n")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
