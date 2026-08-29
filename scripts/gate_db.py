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
CREATE INDEX IF NOT EXISTS runs_list ON runs(list_hash);
CREATE INDEX IF NOT EXISTS runs_bin ON runs(bin_hash);
-- ⚠ THE BUILD IS A FIRST-CLASS ROW, not just a column. Victor 2026-08-28:
-- "надо еще ключевать кэшем сборки данные в таблице, так можно будет быстро
-- сравнивать прогоны между собой". With the build named, "which tests changed
-- verdict between these two compilers, over the same test set" is one join
-- instead of a diff of two logs — and that is the question every round of this
-- session has been asking by hand.
CREATE TABLE IF NOT EXISTS builds (
  bin_hash TEXT PRIMARY KEY, first_seen TEXT, head TEXT, note TEXT);
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
            # ⚠ READ THE STATUS, DO NOT INFER IT FROM A MISSING CHILD. The first
            # version said "no <failure> and no <skipped> ⇒ passed", and ctest
            # records a DISABLED test as `status="disabled"` with neither child
            # — so three tests disabled on purpose were ingested as PASSED, a
            # false green in the very store built to stop false greens.
            st = tc.get("status") or ""
            if not st:
                if tc.find("failure") is not None: st = "failed"
                elif tc.find("skipped") is not None: st = "skipped"
                else: st = "passed"
            if st in ("run", "completed"): st = "passed"
            tests.append((tc.get("name"), st, float(tc.get("time") or 0)))
    p = sum(1 for _, s, _ in tests if s == "passed")
    f = sum(1 for _, s, _ in tests if s == "failed")
    k = sum(1 for _, s, _ in tests if s in ("skipped", "disabled", "notrun"))
    other = sorted({s for _, s, _ in tests} - {"passed", "failed", "skipped", "disabled", "notrun"})
    if other:
        print(f"gate-db: ⚠ statuses this store does not classify: {other} — counted"
              f" as neither passed nor failed. Fix the vocabulary before trusting a"
              f" green from this run.")
    cur = c.execute(
        "INSERT INTO runs(key,list_hash,bin_hash,head,dirty,args,at,seconds,rc,total,passed,failed,skipped)"
        " VALUES(?,?,?,?,?,?,datetime('now'),?,?,?,?,?,?)",
        (key, lh, bh, head, dirty, args, float(secs), int(rc), len(tests), p, f, k))
    rid = cur.lastrowid
    c.execute("INSERT OR IGNORE INTO builds(bin_hash,first_seen,head,note)"
              " VALUES(?,datetime('now'),?,'')", (bh, head))
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

def builds(db):
    c = conn(db)
    rows = c.execute(
        "SELECT b.bin_hash, b.first_seen, b.head, count(r.id), "
        "       sum(r.failed), sum(r.total) "
        "FROM builds b LEFT JOIN runs r ON r.bin_hash=b.bin_hash "
        "GROUP BY b.bin_hash ORDER BY b.first_seen DESC").fetchall()
    print(f"{'build':18} {'first seen':20} {'head':11} {'runs':>5} {'failed':>7} {'tests':>7}")
    for bh, at, head, n, f, t in rows:
        print(f"{bh:18} {at:20} {head or '-':11} {n:5} {f or 0:7} {t or 0:7}")
    return 0

def compare(db, a, b):
    """Two BUILDS, one test set: what changed verdict. The set is the
    intersection of what both actually ran — comparing over a list one side
    never ran would report a 'change' that is an absence."""
    c = conn(db)
    q = ("SELECT res.name, res.status FROM results res JOIN runs r ON r.id=res.run_id"
         " WHERE r.bin_hash LIKE ? AND r.id=(SELECT max(id) FROM runs r2"
         "   WHERE r2.bin_hash=r.bin_hash AND r2.list_hash=r.list_hash)")
    A = dict(c.execute(q, (a + "%",)).fetchall())
    B = dict(c.execute(q, (b + "%",)).fetchall())
    if not A or not B:
        print(f"gate-db: no runs for build {a if not A else b}", file=sys.stderr); return 4
    both = set(A) & set(B)
    onlyA, onlyB = set(A) - both, set(B) - both
    changed = [(n, A[n], B[n]) for n in sorted(both) if A[n] != B[n]]
    print(f"builds {a} -> {b}: {len(both)} tests in common, {len(changed)} changed verdict")
    for n, x, y in changed:
        mark = "REGRESSION" if (x == "passed" and y == "failed") else \
               "FIXED     " if (x == "failed" and y == "passed") else "changed   "
        print(f"  {mark} {n}: {x} -> {y}")
    # ⚠ NOT-IN-COMMON IS NOT A CHANGE, and saying so is the point: a test the
    # other build never ran has no verdict to differ from.
    if onlyA: print(f"  ({len(onlyA)} tests only in {a} — not run by {b}, so not a change)")
    if onlyB: print(f"  ({len(onlyB)} tests only in {b} — not run by {a}, so not a change)")
    return 0

if __name__ == "__main__":
    cmd = sys.argv[1]
    if cmd == "ingest": sys.exit(ingest(*sys.argv[2:]) or 0)
    if cmd == "lookup": sys.exit(lookup(sys.argv[2], sys.argv[3]))
    if cmd == "history": sys.exit(history(sys.argv[2], sys.argv[3]))
    if cmd == "builds": sys.exit(builds(sys.argv[2]))
    if cmd == "compare": sys.exit(compare(sys.argv[2], sys.argv[3], sys.argv[4]))
    print(__doc__); sys.exit(2)
