#!/usr/bin/env python3
"""arc-progress.py — derive the borrow-check arc's progress FROM THE TREE, not from a
tally anyone maintains by hand.

A hand-kept progress file is a carried-forward claim, and in this arc every carried
claim has been wrong at least once. So everything here is derived at read time:

  * the ledger totals come from the two ledger files, counted by direct listing;
  * the history comes from `git log` — a landing commit states `<n> -> <m>`, and that
    sentence is the only record of a row closing, written by the round that closed it;
  * the rate comes from the timestamps of those same commits.

Emits a table by default, `--json` for a machine.
"""
import json
import re
import subprocess
import sys
from datetime import datetime

TRANSITION = re.compile(r"(?<![\d.])(\d{2,3}) -> (\d{2,3})(?![\d.])")


def sh(*args: str) -> str:
    return subprocess.run(args, capture_output=True, text=True, check=True).stdout


def count_rows(path: str) -> int:
    """Count by DIRECT LISTING, the way the gate does — never trust a `# TOTAL` line."""
    try:
        with open(path) as fh:
            return sum(1 for ln in fh if ln.strip() and not ln.lstrip().startswith("#"))
    except FileNotFoundError:
        return 0


def declared_total(path: str) -> int | None:
    try:
        with open(path) as fh:
            for ln in fh:
                if ln.startswith("# TOTAL "):
                    return int(ln.split()[2])
    except FileNotFoundError:
        pass
    return None


def landings() -> list[dict]:
    """Every commit whose subject states a ledger transition, oldest first."""
    out = []
    raw = sh("git", "log", "--format=%H\x1f%ad\x1f%s", "--date=iso-strict")
    for line in raw.splitlines():
        h, date, subject = line.split("\x1f", 2)
        m = TRANSITION.search(subject)
        if not m:
            continue
        before, after = int(m.group(1)), int(m.group(2))
        # A transition must SHRINK and by a plausible amount; `7 -> 1` in prose is not one.
        if not 0 < before - after <= 40:
            continue
        # ⚠ A TRANSITION IS NOT ALWAYS A CLOSING. The 2026-09-04 split moved 25 rows
        # that no compiler fix can close into `bc_admits_blocked.ledger` — the total
        # dropped by 25 and not one defect was repaired. Counting that as progress
        # would have put the arc's best-ever rate on the day it did no compiler work.
        kind = "split" if "blocked" in sh("git", "show", "--stat", "--format=", h) else "fix"
        out.append({
            "sha": h[:9],
            "when": date,
            "before": before,
            "after": after,
            "closed": before - after,
            "kind": kind,
            "subject": subject,
        })
    out.reverse()
    return out


def rate_windows(rows: list[dict], n: int = 6) -> list[dict]:
    """Rows per hour over a sliding window — the honest unit is a window, not a round:
    a round that buys a DOOR closes no row, and the next one closes three."""
    win = []
    rows = [r for r in rows if r["kind"] == "fix"]
    for i in range(len(rows) - n + 1):
        chunk = rows[i:i + n]
        t0 = datetime.fromisoformat(chunk[0]["when"])
        t1 = datetime.fromisoformat(chunk[-1]["when"])
        hours = (t1 - t0).total_seconds() / 3600
        closed = sum(c["closed"] for c in chunk)
        win.append({
            "from": chunk[0]["when"][:16], "to": chunk[-1]["when"][:16],
            "closed": closed, "hours": round(hours, 1),
            "per_hour": round(closed / hours, 2) if hours > 0 else None,
        })
    return win


def main() -> int:
    rows = landings()
    actionable = count_rows("tests/logos/bc_admits.ledger")
    blocked = count_rows("tests/logos/bc_admits_blocked.ledger")
    data = {
        "actionable": actionable,
        "actionable_declared": declared_total("tests/logos/bc_admits.ledger"),
        "blocked": blocked,
        "blocked_declared": declared_total("tests/logos/bc_admits_blocked.ledger"),
        "start": rows[0]["before"] if rows else None,
        "closed_by_fixes": sum(r["closed"] for r in rows if r["kind"] == "fix"),
        "moved_to_blocked": sum(r["closed"] for r in rows if r["kind"] == "split"),
        "landings": rows,
        "rate": rate_windows(rows),
        "head": sh("git", "rev-parse", "--short", "HEAD").strip(),
    }

    if "--json" in sys.argv:
        print(json.dumps(data, indent=2))
        return 0

    print(f"HEAD {data['head']}")
    print(f"ledger   actionable {actionable:>4}   blocked {blocked:>3}"
          f"   (declared {data['actionable_declared']} / {data['blocked_declared']})")
    for name, listed, decl in (("actionable", actionable, data["actionable_declared"]),
                               ("blocked", blocked, data["blocked_declared"])):
        if decl is not None and listed != decl:
            print(f"  ⚠ {name}: listing says {listed}, '# TOTAL' says {decl}")
    print()
    print(f"{'date':16}  {'closed':>6}  {'left':>5}  subject")
    for r in rows[-14:]:
        mark = " (moved, not fixed)" if r["kind"] == "split" else ""
        print(f"{r['when'][:16]:16}  {r['closed']:>6}  {r['after']:>5}  {r['subject'][:60]}{mark}")
    print()
    print("rows/hour over a sliding 6-landing window:")
    for w in data["rate"][::max(1, len(data["rate"]) // 8)]:
        print(f"  {w['from']} → {w['to']}  {w['closed']:>3} rows / {w['hours']:>5} h"
              f"  = {w['per_hour']}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
