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
  R1  an exit status above 255 (or 126/127)     literal `exit N`, `sys.exit(N)`,
                                                a generated `return <code>i32`,
                                                an `.expected` declaring one
  R2  `pipefail` + `| grep -q`                  read into a file, then match
  R3  a gate asking git instead of the build    `# lint:git-ok — <ground>` is the
                                                only way past, and it is cheap to
                                                write truthfully and impossible
                                                to write honestly for a proxy
  R4  a relative `-newermt` (bfs errors → 0)    `@<epoch>` or `$VAR`
  R5  a gate script no CMakeLists runs          comments stripped first, so being
                                                DESCRIBED is not being registered
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
  4  --selftest: a rule did not catch its own canary — THIS PROGRAM is wrong
"""
import os
import re
import sys

# ── the corpus: the artifacts that PRONOUNCE VERDICTS ────────────────────────
# Gates and their helpers. Corpus fixtures are not here: they assert nothing on
# their own, and `corpus_registration_gate.sh` is what watches them.
SHELL_DIRS = ["tests/logos", "tests/exhaustive", "scripts"]
PY_DIRS = ["tests/logos", "tests/exhaustive"]
# The conuco suite: 67 standalone binaries whose `main` IS the verdict channel.
# This is where the 8-bit ceiling was first diagnosed and fixed, and where the
# fix decayed — see R7.
CONUCO_TESTS = "conuco/memoria/tests"

# `ctest-summary.sh` and `perf-slow.sh` pronounce no verdict — they report. That
# is written down in verdict.py's gate census, and it is why R5 lets them be
# unregistered. Any OTHER unregistered gate is a gate nothing runs.
NOT_GATES = {"ctest-summary.sh", "perf-slow.sh", "test-levels.sh"}


# A file may hold DELIBERATE violations — this program's own canary table is
# nothing but violations. They live between these markers and are skipped, so
# the rest of such a file is still scanned. Blanket-excluding a whole file would
# make "the lint is clean" a statement about a smaller tree than it claims.
CANARY_BEGIN = "lint:canaries-begin"
CANARY_END = "lint:canaries-end"


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
RE_SH_EXIT = re.compile(r'(?:^|[;&|]\s*|\bthen\s+|\belse\s+|\bdo\s+)exit\s+(\d+)')
RE_PY_EXIT = re.compile(r'\bsys\.exit\(\s*(\d+)\s*\)')
RE_EMIT_CODE = re.compile(r'return\s*\{[^}]*\}\s*i(?:8|16|32|64)\b')
RE_EXPECTED_EXIT = re.compile(r'^exit:\s*(-?\d+)\s*$')


def r1_exit_ceiling(path, text):
    out = []
    is_py = path.endswith(".py")
    for i, ln in enumerate(text.splitlines(), 1):
        code = ln.split("#", 1)[0] if is_py else ln
        for m in (RE_PY_EXIT if is_py else RE_SH_EXIT).finditer(code):
            n = int(m.group(1))
            if n > 125:
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
RE_GIT = re.compile(r'(?:^|[;&|(`$]\s*|\bthen\s+|\bif\s+!?\s*)git\s+[a-z-]')


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
def r5_unregistered_gates(root):
    out = []
    cml = os.path.join(root, "tests/logos/CMakeLists.txt")
    # ⚠ COMMENT LINES ARE STRIPPED FIRST. A substring hit anywhere in the file
    # would let a script be "registered" by being NAMED in a comment that
    # explains why it exists — which is precisely a gate whose only evidence of
    # running is prose about it.
    reg = "\n".join(
        ln for ln in open(cml, encoding="utf-8", errors="replace").read()
                         .splitlines()
        if not ln.lstrip().startswith("#"))
    d = os.path.join(root, "tests/logos")
    for name in sorted(os.listdir(d)):
        if not name.endswith("_gate.sh") and not name.endswith(".sh"):
            continue
        if name in NOT_GATES or name.startswith("run_"):
            continue
        if name not in reg:
            out.append(Finding(
                "R5-unregistered-gate", f"tests/logos/{name}", 1, name,
                "no CMakeLists registers this script, so nothing runs it. A "
                "gate outside the suite is indistinguishable from a passing "
                "one at every level — that is the fifth recorded kind."))
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
    """R5 asks the FILESYSTEM, so its canary is a filesystem: a two-file tree
    with one registered gate and one that only a COMMENT mentions. Without this
    R5 would be the one rule nothing proves is alive, which this file forbids."""
    import tempfile
    with tempfile.TemporaryDirectory() as td:
        d = os.path.join(td, "tests", "logos")
        os.makedirs(d)
        open(os.path.join(d, "CMakeLists.txt"), "w").write(
            "add_test(NAME t COMMAND ${D}/registered_gate.sh)\n"
            "# orphan_gate.sh is described here and registered nowhere\n")
        for n in ("registered_gate.sh", "orphan_gate.sh"):
            open(os.path.join(d, n), "w").write("#!/usr/bin/env bash\nexit 0\n")
        got = {f.path for f in r5_unregistered_gates(td)}
        fired.add("r5_unregistered_gates")
        silent.add("r5_unregistered_gates")
        if "tests/logos/orphan_gate.sh" not in got:
            broken.append("  r5_unregistered_gates: a gate named ONLY by a "
                          "comment read as registered.")
        if "tests/logos/registered_gate.sh" in got:
            broken.append("  r5_unregistered_gates: flagged a gate that IS in "
                          "an add_test COMMAND.")


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


def walk(root):
    findings, n_sh, n_py, n_exp, n_cnc = [], 0, 0, 0, 0
    for d in SHELL_DIRS:
        full = os.path.join(root, d)
        if not os.path.isdir(full):
            continue
        for name in sorted(os.listdir(full)):
            if not name.endswith(".sh"):
                continue
            p = os.path.join(full, name)
            text = _mask_canary_regions(
                open(p, encoding="utf-8", errors="replace").read())
            n_sh += 1
            for rule in SHELL_RULES:
                findings += rule(f"{d}/{name}", text)
    for d in PY_DIRS:
        full = os.path.join(root, d)
        if not os.path.isdir(full):
            continue
        for name in sorted(os.listdir(full)):
            if not name.endswith(".py"):
                continue
            p = os.path.join(full, name)
            text = _mask_canary_regions(
                open(p, encoding="utf-8", errors="replace").read())
            n_py += 1
            for rule in PY_RULES:
                findings += rule(f"{d}/{name}", text)
    # RECURSIVE: `tests/imported/pass` alone holds 120 subdirectories, and a
    # walk that only lists the top level would report "no bad exit expectation"
    # about a corpus it never opened.
    for base, _dirs, names in os.walk(os.path.join(root, "tests")):
        for name in sorted(names):
            if not name.endswith(".expected"):
                continue
            n_exp += 1
            p = os.path.join(base, name)
            findings += r1c_expected_exit(
                os.path.relpath(p, root),
                open(p, encoding="utf-8", errors="replace").read())
    # THE CONUCO SUITE: 67 standalone binaries whose `main` IS the channel.
    cnc = os.path.join(root, CONUCO_TESTS)
    if os.path.isdir(cnc):
        for name in sorted(os.listdir(cnc)):
            if not name.endswith(".logos"):
                continue
            n_cnc += 1
            findings += r7_conuco_main_boolean(
                f"{CONUCO_TESTS}/{name}",
                open(os.path.join(cnc, name), encoding="utf-8",
                     errors="replace").read())
    findings += r5_unregistered_gates(root)
    return findings, n_sh, n_py, n_exp, n_cnc


# ⚠ A FLOOR ON THE WALK ITSELF. "0 findings" over 0 files is the defect this
# whole file is about, so the population is asserted before the verdict is.
# MEASURED 2026-08-01 by this program: 23 shell scripts under tests/logos,
# tests/exhaustive and scripts; 8 python helpers; 6728 `.expected` under tests/.
MIN_SH, MIN_PY, MIN_EXPECTED, MIN_CONUCO = 23, 8, 6728, 67


def main(argv):
    if "--selftest" in argv:
        rc = selftest()
        if rc:
            return rc
        argv = [a for a in argv if a != "--selftest"]
    root = argv[0] if argv else "."
    findings, n_sh, n_py, n_exp, n_cnc = walk(root)
    if n_sh < MIN_SH or n_py < MIN_PY or n_exp < MIN_EXPECTED \
            or n_cnc < MIN_CONUCO:
        sys.stderr.write(
            f"FAIL (gate_lint SAW TOO LITTLE): {n_sh} shell (floor {MIN_SH}), "
            f"{n_py} python (floor {MIN_PY}), {n_exp} .expected (floor "
            f"{MIN_EXPECTED}), {n_cnc} conuco tests (floor {MIN_CONUCO}) under "
            f"{os.path.abspath(root)}.\n"
            f"      A clean report over a corpus that shrank is the blindness "
            f"this program checks for.\n")
        return 1
    if findings:
        sys.stderr.write(
            f"FAIL (gate_lint): {len(findings)} recorded lesson(s) violated. "
            f"Each of these was diagnosed, fixed at the class and written down "
            f"BEFORE the line below was written:\n\n"
            + "\n".join(f.render() for f in findings) + "\n")
        return 1
    sys.stderr.write(
        f"[gate-lint] OK — {n_sh} gate scripts, {n_py} gate helpers, {n_exp} "
        f"corpus expectations, {n_cnc} conuco tests: no exit status carrying "
        f"more than a byte, no `| grep -q` under pipefail, no gate asking git "
        f"without its ground, no relative `-newermt`, no vacuous floor, no "
        f"unregistered gate, and every conuco `main` a boolean.\n")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
