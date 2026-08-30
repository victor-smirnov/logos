#!/usr/bin/env python3
"""fail_text_oracle.py <outfile> — the FAIL half of the cost population, read
by TEXT and not only by exit code.

⚠ WHY THIS EXISTS. `ceiling-probe.sh` priced COST over `-L bc -L pass` plus
three `pass` directories. ctest ANDs its `-L` filters, so that selection holds
NO `fail` fixture, and on 2026-08-30 the statement-spelling `guardmovearm`
probe priced COST 0 while regressing five pinned diagnostics. That is the
first of the three damage shapes the old population could not see.

⚠ AND ADDING `-L bc -L fail` TO ctest WOULD ONLY CATCH TWO OF THE THREE.
`tests/logos/run_test.sh` in fail mode asserts `grep -qF "$(cat .expected)"`
— a SUBSTRING. A probe that appends a second diagnostic, re-words a note, or
moves a line number the `.expected` does not pin leaves the ctest verdict
GREEN while changing what the compiler says. Rule 15. So the unit recorded
here is not a verdict but a TRIPLE:

    rc      the exit code                    (shape: a fail fixture's rc moved)
    sha     sha256 of the NORMALISED stderr  (shape: text-only change)
    match   does `.expected` still occur     (what ctest would say)

A diff of these three across an unarmed and an armed run reports each shape
separately, and the text column is the one ctest cannot produce at all.

The population and its per-test flags are read from `ctest -N -V`, which prints
each registered test's real command line — never from a glob, because the `bc`
label is decided in CMake (a directory rule for imported ports, a filename
prefix rule for native fixtures) and a glob here would be a second, drifting
copy of that decision.
"""
import subprocess, shlex, re, sys, os, hashlib, concurrent.futures

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD = os.environ.get("LOGOS_BUILD", os.path.join(ROOT, "build"))
SEL = os.environ.get("LOGOS_FAIL_ORACLE_SEL", "-L bc -L fail").split()

def population():
    out = subprocess.run(["ctest", "--test-dir", BUILD, "-N", "-V"] + SEL,
                         capture_output=True, text=True).stdout
    rows, cmd, env = [], None, {}
    for l in out.splitlines():
        if "Test command:" in l:
            cmd = shlex.split(l.split("Test command:", 1)[1].strip()); env = {}
        m = re.match(r'\s*\d+:\s+(LOGOS_\w+)=(.*)$', l)
        if m: env[m.group(1)] = m.group(2)
        m = re.match(r'\s*Test\s+#\d+:\s+(\S+)\s*$', l)
        if m and cmd:
            # runner mode logosc logos expected extra...
            rows.append((m.group(1), cmd[3], cmd[4], cmd[5:], dict(env)))
            cmd = None
    return rows

# ⚠ NORMALISE, OR THE ORACLE REPORTS ITSELF. A raw stderr carries the mktemp
# object path the driver invented for this run; on the full-suite oracle that
# alone reported 6433 changes out of 8648.
_NORM = [(re.compile(r'/tmp/[A-Za-z0-9_.\-]+'), '/tmp/<T>'),
         (re.compile(r'^logosc: wrote .*$', re.M), 'logosc: wrote <O>')]

# ⚠ `match` MUST MEAN WHAT ctest MEANS, OR IT REPORTS DIFFERENCES THAT ARE NOT
# THERE. `run_test.sh` fail mode is `grep -qF -- "$(cat .expected)"`, and a
# MULTI-LINE pattern in `grep -F` is an ALTERNATION: it succeeds when ANY one
# line occurs. Seven `nll_*_outlives_scope` fixtures use the multi-key spelling
# (`exit: 1` / `stderr:` / the diagnostic), and a strict whole-string `in` test
# called all seven changed on an unarmed baseline — a reader inventing seven
# differences before any probe was armed. Reproduced here line for line.
def expected_match(path, err):
    try:
        want = open(path, encoding="utf-8", errors="replace").read().rstrip("\n")
    except OSError:
        return 0
    lines = want.split("\n")
    # `grep -F` with an empty pattern line matches ANY input. run_test.sh refuses
    # an all-whitespace `.expected`; a file with ONE blank line among others is
    # still vacuous and is reported as such rather than silently counted green.
    if any(l == "" for l in lines):
        return 2
    return 1 if any(l in err for l in lines) else 0

def one(row):
    name, logos, expected, extra, env = row
    e = dict(os.environ); e.update(env)
    p = subprocess.run([os.path.join(BUILD, "bin", "logosc"), logos, "-o", "/dev/null"] + extra,
                       capture_output=True, text=True, env=e, cwd=BUILD)
    err = p.stderr + p.stdout
    for rx, rep in _NORM: err = rx.sub(rep, err)
    match = expected_match(expected, err)
    return (name, p.returncode, hashlib.sha256(err.encode()).hexdigest()[:16], match, logos)

def main():
    out = sys.argv[1]
    rows = population()
    if not rows:
        print("fail-oracle: the selection %r names NO fail fixture — a filter is not a"
              " population" % " ".join(SEL), file=sys.stderr)
        return 2
    with concurrent.futures.ThreadPoolExecutor(max_workers=os.cpu_count()) as ex:
        res = sorted(ex.map(one, rows))
    with open(out, "w") as f:
        for name, rc, sha, match, logos in res:
            f.write("%s\t%d\t%s\t%d\t%s\n" % (name, rc, sha, match, os.path.relpath(logos, ROOT)))
    print("fail-oracle: %d fail fixtures recorded (rc, stderr sha, .expected match) -> %s"
          % (len(res), out), file=sys.stderr)
    return 0

if __name__ == "__main__":
    sys.exit(main())
