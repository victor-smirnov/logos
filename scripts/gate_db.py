#!/usr/bin/env python3
"""gate_db.py — the DB is the source of truth; ctest is only a runner.

Victor 2026-08-28: *"использовать sqlite DB как источник истины, а ctest только
как runner"*. The three questions are:

    what tests exist   → `ctest -N`, names only: one stable line per test
    what is measured   → this DB, per (build, test)
    what is missing    → the difference, which is what actually gets run

⚠ AN EARLIER DESIGN PARSED `ctest -N -V` FOR LABELS and tried to reproduce
ctest's filter algebra in SQL so groups could be selected from the store. That
was fragile for no gain: we never need to know WHY a test is in a set, only
whether its verdict for this build is already recorded.

THE UNIT IS (build, test). A build is identified by what the compiler says about
itself — `logosc --version`, which already carries branch, commit, a dirty flag
and a build timestamp — plus a hash of the libraries, because a stdlib rebuild
leaves logosc byte-identical and changes what every test does.

Subcommands:
  build     <db> <version> <libhash> <head>      → register/lookup a build, print its id
  inventory <db> <build_id>                       ← names on stdin (from `ctest -N`)
  missing   <db> <build_id>                       → names with no verdict yet, one per line
  ingest    <db> <build_id> <junit.xml> <args>    → record verdicts
  verdicts  <db> <build_id> [regex]               → what is recorded
  history   <db> <test>                           → this test across builds, newest first
  compare   <db> <build_a> <build_b>              → what changed verdict, over the common set
"""
import sqlite3, sys, os, re
import xml.etree.ElementTree as ET

SCHEMA = """
CREATE TABLE IF NOT EXISTS builds (
  id INTEGER PRIMARY KEY, version TEXT, lib_hash TEXT, head TEXT, first_seen TEXT,
  UNIQUE(version, lib_hash));
CREATE TABLE IF NOT EXISTS known (
  build_id INTEGER, name TEXT, PRIMARY KEY(build_id, name));
CREATE TABLE IF NOT EXISTS verdicts (
  build_id INTEGER, name TEXT, status TEXT, seconds REAL, at TEXT, args TEXT,
  PRIMARY KEY(build_id, name));
CREATE INDEX IF NOT EXISTS verdicts_name ON verdicts(name);
"""

def conn(db):
    d = os.path.dirname(db)
    if d: os.makedirs(d, exist_ok=True)
    c = sqlite3.connect(db); c.executescript(SCHEMA); return c

def build(db, version, libhash, head):
    c = conn(db)
    c.execute("INSERT OR IGNORE INTO builds(version,lib_hash,head,first_seen)"
              " VALUES(?,?,?,datetime('now'))", (version, libhash, head))
    c.commit()
    print(c.execute("SELECT id FROM builds WHERE version=? AND lib_hash=?",
                    (version, libhash)).fetchone()[0])
    return 0

def inventory(db, bid):
    names = [l.strip() for l in sys.stdin if l.strip()]
    c = conn(db)
    c.executemany("INSERT OR IGNORE INTO known(build_id,name) VALUES(?,?)",
                  [(bid, n) for n in names])
    c.commit()
    print(f"gate-db: {len(names)} tests known for build {bid}")
    return 0

def missing(db, bid):
    c = conn(db)
    rows = c.execute("SELECT k.name FROM known k LEFT JOIN verdicts v"
                     " ON v.build_id=k.build_id AND v.name=k.name"
                     " WHERE k.build_id=? AND v.name IS NULL ORDER BY k.name", (bid,)).fetchall()
    for (n,) in rows: print(n)
    return 0

def ingest(db, bid, junit, args):
    c = conn(db); tests = []
    if os.path.exists(junit):
        for tc in ET.parse(junit).getroot().iter("testcase"):
            # ⚠ READ THE STATUS, DO NOT INFER IT FROM A MISSING CHILD. ctest
            # records a DISABLED test as status="disabled" with neither a
            # <failure> nor a <skipped> child; the first version said "no marker
            # ⇒ passed" and ingested five deliberately-disabled ports as PASSING.
            st = tc.get("status") or ""
            if not st:
                st = ("failed" if tc.find("failure") is not None else
                      "skipped" if tc.find("skipped") is not None else "passed")
            # ctest's own vocabulary, read off `--output-junit` and not guessed:
            #   run → the test passed        fail → it failed
            #   disabled → DISABLED property  notrun → selected but never started
            # ⚠ The guard below stays: an unrecognised status must NOT default to
            # green. It earned itself immediately — the first version knew "run"
            # and not "fail", and three genuinely failing rows were recorded as
            # neither passed nor failed, with a warning, instead of silently
            # counting as a clean run.
            st = {"run": "passed", "completed": "passed", "fail": "failed"}.get(st, st)
            tests.append((tc.get("name"), st, float(tc.get("time") or 0)))
    unknown = sorted({s for _, s, _ in tests} - {"passed","failed","skipped","disabled","notrun"})
    if unknown:
        print(f"gate-db: ⚠ unclassified statuses {unknown} — counted as neither passed"
              f" nor failed. Fix the vocabulary before trusting a green from this run.")
    c.executemany("INSERT OR REPLACE INTO verdicts(build_id,name,status,seconds,at,args)"
                  " VALUES(?,?,?,?,datetime('now'),?)",
                  [(bid, n, s, t, args) for n, s, t in tests])
    c.executemany("INSERT OR IGNORE INTO known(build_id,name) VALUES(?,?)",
                  [(bid, n) for n, _, _ in tests])
    c.commit()
    p = sum(1 for _, s, _ in tests if s == "passed")
    f = sum(1 for _, s, _ in tests if s == "failed")
    o = len(tests) - p - f
    print(f"gate-db: build {bid} — {p} passed / {f} failed / {o} other, {len(tests)} recorded")
    return 0

def verdicts(db, bid, rx=None):
    c = conn(db)
    rows = c.execute("SELECT name,status,seconds FROM verdicts WHERE build_id=? ORDER BY name",
                     (bid,)).fetchall()
    if rx: rows = [r for r in rows if re.search(rx, r[0])]
    bad = [r for r in rows if r[1] == "failed"]
    print(f"build {bid}: {len(rows)} recorded, {len(bad)} failed")
    for n, s, _ in rows:
        if s != "passed": print(f"  {s:9} {n}")
    return 0

def history(db, name):
    c = conn(db)
    rows = c.execute("SELECT b.id,b.version,b.head,v.status,v.at FROM verdicts v"
                     " JOIN builds b ON b.id=v.build_id WHERE v.name=?"
                     " ORDER BY b.id DESC LIMIT 40", (name,)).fetchall()
    if not rows:
        print(f"gate-db: no verdict for '{name}' under any build", file=sys.stderr); return 4
    for bid, ver, head, st, at in rows:
        print(f"build {bid:3}  {st:9} {at}  {head or '-':10} {ver}")
    return 0

def compare(db, a, b):
    c = conn(db)
    A = dict(c.execute("SELECT name,status FROM verdicts WHERE build_id=?", (a,)).fetchall())
    B = dict(c.execute("SELECT name,status FROM verdicts WHERE build_id=?", (b,)).fetchall())
    if not A or not B:
        print(f"gate-db: build {a if not A else b} has no verdicts", file=sys.stderr); return 4
    both = set(A) & set(B)
    ch = [(n, A[n], B[n]) for n in sorted(both) if A[n] != B[n]]
    print(f"build {a} -> {b}: {len(both)} tests measured under both, {len(ch)} changed")
    for n, x, y in ch:
        m = ("REGRESSION" if (x, y) == ("passed", "failed") else
             "FIXED     " if (x, y) == ("failed", "passed") else "changed   ")
        print(f"  {m} {n}: {x} -> {y}")
    # ⚠ NOT-IN-COMMON IS NOT A CHANGE: a test one build never ran has no verdict
    # to differ from. Reported so the reader sees what was left out.
    for tag, s in (("only in %s" % a, set(A)-both), ("only in %s" % b, set(B)-both)):
        if s: print(f"  ({len(s)} {tag} — not measured by the other, so not a change)")
    return 0

if __name__ == "__main__":
    cmd = sys.argv[1]; rest = sys.argv[2:]
    fn = {"build": build, "inventory": inventory, "missing": missing, "ingest": ingest,
          "verdicts": verdicts, "history": history, "compare": compare}.get(cmd)
    if not fn: print(__doc__); sys.exit(2)
    sys.exit(fn(*rest) or 0)
