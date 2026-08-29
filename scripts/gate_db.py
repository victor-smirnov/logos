#!/usr/bin/env python3
"""gate_db.py — the record of what the gates found, keyed on what was actually run.

⚠ THE KEY IS THE TEST LIST AND THE BINARY, NOT THE COMMAND LINE. Victor
2026-08-28. `-L bc` selects a different SET as fixtures land, and two different
command lines can select the same set, so a key made of arguments answers the
wrong question in both directions. `ctest -N` enumerates exactly what would run;
that list is hashed. The binary is hashed by CONTENT, not mtime — a rebuild that
produced an identical logosc invalidates nothing, and a touch invalidates
nothing either.

⚠ THE WORKTREE IS PART OF THE KEY, AND THE FIRST VERSION OF THIS COMMENT ARGUED
IT SHOULD NOT BE. The argument was "a test depends on the BINARY and the LIST; a
doc edit changes no verdict". It was refuted on the first real use: the cache
handed back `logos_00_gate_lint FAILED` for a tree where that gate had just been
FIXED — the fix lived in `gate_lint.py` and logosc had not moved. Most
`logos_00_*` gates ARE scripts; their behaviour is defined in the tree. A false
invalidation costs one run; a false cache hit reports a wrong verdict, so the
key takes the conservative side.

Subcommands:
  ingest <db> <junit.xml> <rc> <secs> <key> <listhash> <binhash> <head> <dirty> <args>
  lookup <db> <key>              → CURRENT record, exit 0; nothing, exit 4
  history <db> <test-name>       → every recorded verdict for one test, newest first
  failing <db> <key>             → the failing set of that run, one per line
"""
import sqlite3, sys, os, json
import xml.etree.ElementTree as ET

SCHEMA = """
CREATE TABLE IF NOT EXISTS runs (
  id INTEGER PRIMARY KEY, key TEXT, list_hash TEXT, bin_hash TEXT,
  head TEXT, dirty TEXT, args TEXT, at TEXT, seconds REAL,
  rc INTEGER, total INTEGER, passed INTEGER, failed INTEGER, skipped INTEGER);
CREATE INDEX IF NOT EXISTS runs_key ON runs(key);
CREATE TABLE IF NOT EXISTS results (
  run_id INTEGER, name TEXT, status TEXT, seconds REAL);
CREATE INDEX IF NOT EXISTS results_name ON results(name);
CREATE INDEX IF NOT EXISTS results_run ON results(run_id);
"""

def conn(db):
    os.makedirs(os.path.dirname(db), exist_ok=True)
    c = sqlite3.connect(db); c.executescript(SCHEMA); return c

def ingest(db, junit, rc, secs, key, lh, bh, head, dirty, args):
    c = conn(db)
    tests = []
    if os.path.exists(junit):
        root = ET.parse(junit).getroot()
        for tc in root.iter("testcase"):
            st = "passed"
            if tc.find("failure") is not None: st = "failed"
            elif tc.find("skipped") is not None: st = "skipped"
            tests.append((tc.get("name"), st, float(tc.get("time") or 0)))
    p = sum(1 for _, s, _ in tests if s == "passed")
    f = sum(1 for _, s, _ in tests if s == "failed")
    k = sum(1 for _, s, _ in tests if s == "skipped")
    cur = c.execute(
        "INSERT INTO runs(key,list_hash,bin_hash,head,dirty,args,at,seconds,rc,total,passed,failed,skipped)"
        " VALUES(?,?,?,?,?,?,datetime('now'),?,?,?,?,?,?)",
        (key, lh, bh, head, dirty, args, float(secs), int(rc), len(tests), p, f, k))
    rid = cur.lastrowid
    c.executemany("INSERT INTO results(run_id,name,status,seconds) VALUES(?,?,?,?)",
                  [(rid, n, s, t) for n, s, t in tests])
    c.commit()
    print(f"gate-db: recorded run {rid} — rc={rc} {p} passed / {f} failed / {k} skipped, {len(tests)} tests")

def lookup(db, key):
    if not os.path.exists(db): return 4
    c = conn(db)
    r = c.execute("SELECT id,at,rc,total,passed,failed,skipped,seconds,head,dirty,args"
                  " FROM runs WHERE key=? ORDER BY id DESC LIMIT 1", (key,)).fetchone()
    if not r: return 4
    rid, at, rc, tot, p, f, k, secs, head, dirty, args = r
    print(f"rc={rc}")
    print(f"args={args}")
    print(f"at={at}  ({secs:.0f}s)")
    print(f"counts={p} passed / {f} failed / {k} skipped, {tot} total")
    print(f"head={head}")
    print(f"worktree_when_measured={dirty}")
    fails = c.execute("SELECT name FROM results WHERE run_id=? AND status='failed'"
                      " ORDER BY name", (rid,)).fetchall()
    print(f"failing_count={len(fails)}")
    for (n,) in fails: print(f"  FAILED {n}")
    return 0

def history(db, name):
    c = conn(db)
    rows = c.execute(
        "SELECT r.at, res.status, r.head, r.args FROM results res JOIN runs r ON r.id=res.run_id"
        " WHERE res.name=? ORDER BY r.id DESC LIMIT 40", (name,)).fetchall()
    if not rows:
        print(f"gate-db: no record of '{name}' in any run", file=sys.stderr); return 4
    for at, st, head, args in rows:
        print(f"{at}  {st:8} {head[:9]}  {args}")
    return 0

if __name__ == "__main__":
    cmd = sys.argv[1]
    if cmd == "ingest": sys.exit(ingest(*sys.argv[2:]) or 0)
    if cmd == "lookup": sys.exit(lookup(sys.argv[2], sys.argv[3]))
    if cmd == "history": sys.exit(history(sys.argv[2], sys.argv[3]))
    print(__doc__); sys.exit(2)
