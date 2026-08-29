#!/usr/bin/env python3
"""workflow_audit.py <run-dir> — what the agents ACTUALLY did, against the protocol.

Written 2026-08-28 after Victor's point: an instruction is obeyed ~83% of the
time and the residue lands on the expensive commands, so "did they follow the
protocol" must be MEASURED after every run, not judged by reading the report.
The report is written by the agent; this reads the transcript.
"""
import json, re, sys, glob, os, datetime
from collections import Counter

# ⚠ TWO KINDS OF WAITING, AND ONLY ONE IS WASTE. This metric was written when a
# poll loop was pathological — an agent spinning on `pgrep` because a foreground
# gate could not finish. Then the barriers landed and MANDATED backgrounding, so
# waiting on an `RC=` marker became the CORRECT form, and the metric started
# reporting correct behaviour as 99% waste. A measure that condemns the practice
# its own system requires is measuring the wrong thing.
#   POLL  — pgrep / kill -0 / a bounded retry loop: the old pathology, still bad
#   BLOCK — `until grep -q '^RC='` on a marker: the prescribed form, not waste
POLL = re.compile(r'pgrep|kill -0|seq 1 \d')
BLOCK = re.compile(r"until\s+grep\s+-q|until\s+\[\s*-f|RC_MARKER|grep -q '\^RC=")
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
    polled = sum(d for d, c in gaps if POLL.search(c) and not BLOCK.search(c))
    blocked = sum(d for d, c in gaps if BLOCK.search(c))
    builds = [c for _, c in gaps if BUILD.search(c)]
    probes = [m for _, c in gaps for m in PROBE.findall(c)] + \
             [m for _, c in gaps for m in PASSPR.findall(c)]
    levels = [m.strip() for _, c in gaps for m in LEVEL.findall(c)]
    nojflag = [s.strip()[:60] for _, c in gaps for s in CTEST.findall(c)
               if '-N' not in s and '-j' not in s and 'test-dir' not in s]
    return dict(span=span, n=len(ev), incmd=incmd, polled=polled, blocked=blocked,
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
        print(f"    in commands {a['incmd']/60:.0f} min · blocked-on-marker {a['blocked']/60:.0f} min"
              f" · POLLING {a['polled']/60:.0f} min"
              f" ({100*a['polled']/max(a['incmd'],1):.0f}%)")
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
        if a['polled'] > 0.15 * a['incmd']:
            print(f"    ⚠ WASTE: {100*a['polled']/max(a['incmd'],1):.0f}% of command time SPINNING"
                  f" (pgrep / bounded retries). Block on an RC marker instead.")
            bad += 1
        if a['ctest_no_testdir']:
            print(f"    ⚠ {len(a['ctest_no_testdir'])} ctest calls without --test-dir")
            bad += 1
    print(f"\n{'='*60}\nVIOLATIONS: {bad}")
    return 1 if bad else 0

if __name__ == "__main__":
    sys.exit(report(sys.argv[1]))
