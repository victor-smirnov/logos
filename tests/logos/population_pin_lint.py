#!/usr/bin/env python3
"""population_pin_lint.py [--selftest] REPO [REV]

THE FOUR WIDEST-TRIGGER POPULATION PINS, READ EVERY COMMIT.

`logos_09_direct_door_census` and `logos_09_plan_ground_census` hold LITERAL
counts of the pass corpus.  Both are `tier_full`, so their only reader is
`test-levels.sh L4`.  MEASURED 2026-08-26 by replaying a re-derivation of those
literals against `git ls-tree` at each of the 31 commits 3c3899703..72b16e0f5:
the pin first disagreed with the tree at d2c5a1e1d and was repaired at
72b16e0f5 — 23 consecutive commits red, 15 h 54 m, every one of them reporting
itself green.  See population_pin_lint.known-answer.txt, which is the aggregated
output of that sweep and is reproduced by `--selftest`'s historical arm.

THE MECHANISM WAS TWO PREDICATES FOR ONE CONCEPT.  "How big is the pass corpus"
is ALREADY pinned per commit — `logos_00_census_pin` FACT 5 runs `ctest -N`
three ways and goes red whenever any test is added anywhere.  So each of those
23 commits WAS told the registered population had moved, re-derived census_pin's
number, and did not re-derive this one, because nothing connected the two
statements of one fact.  This program is that connection.  It is the reader that
forces the existing literals to agree with a DIRECT LISTING of the tree:

  direct_door_census_gate.sh  PIN['corpus']   == #(tests/logos/pass/*.logos)
                              PIN['glob']     == #(pass/{wql_*,deem_*}.logos)
                              PIN['nonglob']  == corpus - glob
  plan_ground_census_gate.sh  EXPECT_FIXTURES == glob

IT HOLDS NO NUMBER OF ITS OWN, deliberately.  A third copy of the population
would be a third thing to go stale; the pins stay where they are, because a pin
is the mechanism that makes a DROPPED fixture visible and must not become a
derivation.

WHAT IT DOES NOT DO, said plainly: 4 numbers over 2 of the 3 exact-pin gates.
NOT pull_shape's 27 pins, NOT the ~30 plan_ground EXPECT_* emitted-artifact
counts, NOT the 36 door cross-pins — those need the fact fold and still have
exactly one reader, L4.  This closes the four widest-trigger numbers; it does
not close the class.

Exit 0 all four agree · 1 a pin drifted · 2 COULD NOT LOOK.  The exit-2 path is
load-bearing: without the population floor below, a broken listing would report
four agreements over an empty tree, which is the shape gate_lint.py exists to
refuse.  (⚠ the prototype of this program spelled that path `sys.exit("...")`,
which exits 1, not 2 — a could-not-look reported as a drift.  It is spelled
`return 2` here and the selftest checks it.)
"""
import os
import re
import subprocess
import sys

# A listing that finds fewer than this many pass fixtures is a broken listing,
# not a shrunken corpus: the tree held 2398 when this was written and the pins
# themselves are four-digit.  Four agreements over an empty population assert
# nothing.
MIN_CORPUS = 1000

DOOR_GATE = "tests/logos/direct_door_census_gate.sh"
PLAN_GATE = "tests/logos/plan_ground_census_gate.sh"


class CannotLook(Exception):
    """Raised where the program cannot see the thing it judges."""


def read(repo, path, rev=None):
    if rev:
        r = subprocess.run(["git", "-C", repo, "show", f"{rev}:{path}"],
                           capture_output=True, text=True)
        if r.returncode:
            raise CannotLook(f"could not read {path} at {rev}")
        return r.stdout
    p = os.path.join(repo, path)
    if not os.path.isfile(p):
        raise CannotLook(f"{path} is not on disk under {repo}")
    with open(p, encoding="utf-8", errors="replace") as f:
        return f.read()


def listing(repo, rev=None):
    """#(pass/*.logos) and #(pass/{wql_*,deem_*}.logos), BY DIRECT LISTING."""
    if rev:
        r = subprocess.run(["git", "-C", repo, "ls-tree", "-r", "--name-only",
                            rev, "tests/logos/pass/"],
                           capture_output=True, text=True)
        if r.returncode:
            raise CannotLook(f"could not list tests/logos/pass at {rev}")
        names = [os.path.basename(n) for n in r.stdout.split()
                 if n.endswith(".logos")]
    else:
        d = os.path.join(repo, "tests/logos/pass")
        if not os.path.isdir(d):
            raise CannotLook(f"{d} is not a directory")
        names = [n for n in os.listdir(d) if n.endswith(".logos")]
    glob = [n for n in names if n.startswith("wql_") or n.startswith("deem_")]
    return len(names), len(glob)


def parse_door_pin(text):
    """The three PIN literals, read out of the gate SOURCE rather than copied."""
    try:
        i = text.index("PIN = {")
    except ValueError:
        raise CannotLook(f"no `PIN = {{` block in {DOOR_GATE}")
    pin = {}
    for line in text[i:].splitlines():
        s = line.split("#")[0]
        m = re.match(r"\s*'([a-z_]+)'\s*:\s*(\d+)\s*,", s)
        if m:
            pin[m.group(1)] = int(m.group(2))
        if s.strip() == "}":
            break
    for k in ("corpus", "glob", "nonglob"):
        if k not in pin:
            # ⚠ NOT a drift.  Renaming a key makes this program find nothing,
            # and a gate that reports agreement about a literal it could not
            # locate is the fifth recorded kind of lying gate in a new coat.
            raise CannotLook(f"PIN['{k}'] not found in {DOOR_GATE} — the key was "
                             "renamed or the block moved")
    return pin


def parse_expect_fixtures(text):
    m = re.search(r"^EXPECT_FIXTURES\s*=\s*(\d+)", text, re.M)
    if not m:
        raise CannotLook(f"EXPECT_FIXTURES not found in {PLAN_GATE}")
    return int(m.group(1))


def compare(pin, expect_fixtures, corpus, glob):
    return [("direct_door PIN['corpus']",   pin["corpus"],   corpus),
            ("direct_door PIN['glob']",     pin["glob"],     glob),
            ("direct_door PIN['nonglob']",  pin["nonglob"],  corpus - glob),
            ("plan_ground EXPECT_FIXTURES", expect_fixtures, glob)]


def run(repo, rev=None, out=sys.stdout):
    corpus, glob = listing(repo, rev)
    if corpus < MIN_CORPUS:
        raise CannotLook(
            f"only {corpus} pass fixtures listed (floor {MIN_CORPUS}). Four "
            "agreements over a population this small would be a statement about "
            "a listing that did not happen.")
    pin = parse_door_pin(read(repo, DOOR_GATE, rev))
    expect_fixtures = parse_expect_fixtures(read(repo, PLAN_GATE, rev))
    checks = compare(pin, expect_fixtures, corpus, glob)
    bad = [c for c in checks if c[1] != c[2]]
    for name, pinned, listed in checks:
        out.write(f"  {'OK   ' if pinned == listed else 'DRIFT'} {name:30s}"
                  f" pinned {pinned:6d}  listed {listed:6d}\n")
    if bad:
        out.write(
            "\nFAIL: a population pin no longer describes the corpus.\n"
            "      Re-derive it in the gate that HOLDS it, BY DIRECT LISTING, and\n"
            "      NAME the fixtures that moved and which half they joined. Do not\n"
            "      adjust the number until this passes.\n")
        return 1
    out.write(f"\nOK: 4 population pins agree with a direct listing of {corpus}"
              f" pass fixtures ({glob} wql_*/deem_*, {corpus - glob} other).\n"
              "    This says NOTHING about pull_shape's 27 pins, plan_ground's ~30\n"
              "    EXPECT_* artefact counts or the 36 door cross-pins: those still\n"
              "    have exactly one reader, `test-levels.sh L4`.\n")
    return 0


# ── THE PROGRAM PROVES IT STILL BITES, IN THE SAME RUN ───────────────────────
# A comparator that always says OK is indistinguishable from a tree that never
# drifts, and the whole finding here is that 23 commits reported themselves
# green. Both directions are checked, plus the could-not-look path that the
# prototype got wrong.
GATE_STUB = """
PIN = {
    'corpus': 2398,
    'glob': 191,
    'nonglob': 2207,
}
"""
PLAN_STUB = "EXPECT_FIXTURES   = 191  # a comment\n"


def selftest():
    broken = []
    pin = parse_door_pin(GATE_STUB)
    if pin != {"corpus": 2398, "glob": 191, "nonglob": 2207}:
        broken.append(f"  parse_door_pin read {pin} out of its own stub")
    if parse_expect_fixtures(PLAN_STUB) != 191:
        broken.append("  parse_expect_fixtures missed a literal in its own stub")
    agree = compare(pin, 191, 2398, 191)
    if [c for c in agree if c[1] != c[2]]:
        broken.append("  compare() reported DRIFT over an AGREEING population")
    # The historical answer, planted: the tree at dc4fdda52 listed 2386 pass
    # fixtures against a pin of 2381, and the +5 landed in `nonglob`.
    drift = compare(pin, 191, 2386, 191)
    got = sorted(c[0] for c in drift if c[1] != c[2])
    want = ["direct_door PIN['corpus']", "direct_door PIN['nonglob']"]
    if got != want:
        broken.append(f"  compare() over the dc4fdda52 population flagged {got},"
                      f" not {want}")
    for name, fn, arg in (("PIN key renamed", parse_door_pin, "PIN = {\n}\n"),
                          ("EXPECT_FIXTURES renamed", parse_expect_fixtures, "")):
        try:
            fn(arg)
        except CannotLook:
            pass
        else:
            broken.append(f"  {name} did not raise CannotLook — a missing literal"
                          " read as an agreement")
    if broken:
        sys.stderr.write("FAIL (population_pin_lint SELFTEST): the comparator no "
                         "longer bites.\n" + "\n".join(broken) + "\n")
        return 4
    return 0


def main(argv):
    if "--selftest" in argv:
        rc = selftest()
        if rc:
            return rc
        argv = [a for a in argv if a != "--selftest"]
    if not argv:
        sys.stderr.write(__doc__ + "\n")
        return 2
    repo = argv[0]
    rev = argv[1] if len(argv) > 1 else None
    try:
        return run(repo, rev)
    except CannotLook as e:
        sys.stderr.write(
            f"FAIL (population_pin_lint COULD NOT LOOK): {e}\n"
            "      Nothing above this line is evidence about the tree.\n")
        return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
