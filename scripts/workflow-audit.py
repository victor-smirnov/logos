#!/usr/bin/env python3
"""workflow_audit.py <run-dir> — what the agents ACTUALLY did, against the protocol.

Written 2026-08-28 after Victor's point: an instruction is obeyed ~83% of the
time and the residue lands on the expensive commands, so "did they follow the
protocol" must be MEASURED after every run, not judged by reading the report.
The report is written by the agent; this reads the transcript.
"""
import json, re, sys, glob, os, datetime
from collections import Counter

WAIT = re.compile(r'pgrep|kill -0|seq 1 |until \[|while \[|sleep \d')
BUILD = re.compile(r'(?:^|[;&|]|\n)\s*(?:cd [^;]*; *)?(?:time )?cmake --build\b')
PROBE = re.compile(r'ceiling-probe\.sh\s+(\w+)')
PASSPR = re.compile(r'pass-probe\.sh\s+(\w+)')
# ⚠ ONLY AT A COMMAND POSITION. The first version matched the string anywhere,
# so `pgrep -f "test-levels.sh L4 bc"` inside a poll loop counted as a RUN and
# the auditor reported 9 L4 runs where there were 2. An instrument that
# over-counts is the defect this whole directory exists to find.
LEVEL = re.compile(r'(?:^|[;&|(]|\n)\s*(?:cd [^;]*;\s*)?(?:timeout \d+ )?bash\s+\S*test-levels\.sh\s+(L\d+(?:\s+\w+)?)')
CTEST = re.compile(r'(?:^|[;&|]|\n)\s*(?:timeout \d+ )?ctest\b([^\n;|]*)')

def audit(path):
    ev = []
    for line in open(path, errors="ignore"):
        try: o = json.loads(line)
        except Exception: continue
        t = o.get("timestamp")
        if not t: continue
        cmd = None
        for c in ((o.get("message") or {}).get("content") or []):
            if isinstance(c, dict) and c.get("type") == "tool_use":
                cmd = str(c.get("input", {}).get("command", ""))
        ev.append((datetime.datetime.fromisoformat(t.replace("Z", "+00:00")), cmd))
    if not ev: return None
    span = ev[-1][0] - ev[0][0]
    gaps = [((ev[i][0]-ev[i-1][0]).total_seconds(), ev[i-1][1])
            for i in range(1, len(ev)) if ev[i-1][1]]
    incmd = sum(d for d, _ in gaps)
    waited = sum(d for d, c in gaps if WAIT.search(c))
    builds = [c for _, c in gaps if BUILD.search(c)]
    probes = [m for _, c in gaps for m in PROBE.findall(c)] + \
             [m for _, c in gaps for m in PASSPR.findall(c)]
    levels = [m.strip() for _, c in gaps for m in LEVEL.findall(c)]
    nojflag = [s.strip()[:60] for _, c in gaps for s in CTEST.findall(c)
               if '-N' not in s and '-j' not in s and 'test-dir' not in s]
    return dict(span=span, n=len(ev), incmd=incmd, waited=waited,
                builds=len(builds), probes=probes, levels=levels,
                ctest_no_testdir=nojffix(nojflag))

def nojffix(x): return x

def report(rundir):
    files = sorted(glob.glob(os.path.join(rundir, "agent-*.jsonl")), key=os.path.getmtime)
    if not files:
        print("no agent transcripts in", rundir); return 1
    bad = 0
    for f in files:
        a = audit(f)
        if not a: continue
        name = os.path.basename(f)[:20]
        print(f"\n=== {name}  {a['span']}  ({a['n']} events)")
        print(f"    in commands {a['incmd']/60:.0f} min · WAITING {a['waited']/60:.0f} min"
              f" ({100*a['waited']/max(a['incmd'],1):.0f}%)")
        print(f"    builds {a['builds']} · probe runs {len(a['probes'])}"
              f" ({len(set(a['probes']))} distinct) · levels {Counter(a['levels'])}")
        # ── THE PROTOCOL, CHECKED ──────────────────────────────────────────
        if a['builds'] > 1 and len(set(a['probes'])) > 1:
            print(f"    ⚠ VIOLATION: {a['builds']} builds for a batch of "
                  f"{len(set(a['probes']))} probes. The batch protocol is ONE build.")
            bad += 1
        heavy = sum(v for k, v in Counter(a['levels']).items() if k.startswith("L4"))
        if heavy > 1:
            print(f"    ⚠ VIOLATION: L4 run {heavy} times. The ladder says ONCE per batch,"
                  f" last, before the final commit.")
            bad += 1
        if a['waited'] > 0.3 * a['incmd']:
            print(f"    ⚠ WASTE: {100*a['waited']/max(a['incmd'],1):.0f}% of command time in"
                  f" poll loops. Long gates belong in the background with an RC marker.")
            bad += 1
        if a['ctest_no_testdir']:
            print(f"    ⚠ {len(a['ctest_no_testdir'])} ctest calls without --test-dir")
            bad += 1
    print(f"\n{'='*60}\nVIOLATIONS: {bad}")
    return 1 if bad else 0

if __name__ == "__main__":
    sys.exit(report(sys.argv[1]))
