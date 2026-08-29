#!/usr/bin/env python3
"""probe-log-lint.py — every `site:` in src/compiler/PROBES.md must still resolve.

⚠ THE POINT IS THE LINK, NOT THE PROSE. Victor 2026-08-29 agreed to move probe
measurements out of `.cpp` comments — where recording them costs a rebuild —
into a versioned file beside the sources, on one condition: *"лишь бы модель
могла правильно соотносить текст из файла с исходниками"*. A record whose link
has silently rotted is worse than no record, because it is read as current.

So the link is a SYMBOL, never a line number, and this checks it. A renamed or
deleted function reds the gate at the moment of the rename, in the commit that
caused it, instead of leaving a note pointing at nothing.

⚠ IT CHECKS THE LINK, NOT THE NUMBERS. Whether a ceiling of 5 is still 5 is a
question only a re-run answers; this cannot and does not claim it. What it can
say is that the thing the number is ABOUT still exists.
"""
import re, sys, os

def main(root="."):
    log = os.path.join(root, "src/compiler/PROBES.md")
    if not os.path.exists(log):
        print(f"probe-log-lint: {log} is missing", file=sys.stderr); return 1
    text = open(log, encoding="utf-8").read()
    recs = re.findall(r'^## (\S+)\n(.*?)(?=^## |\Z)', text, re.M | re.S)
    bad, checked = [], 0
    seen = set()
    for name, body in recs:
        if name in ("Format",):  # the format section is prose, not a record
            continue
        if name in seen:
            bad.append(f"{name}: duplicate record — two measurements under one name "
                       f"cannot be told apart, which is the defect this file records")
        seen.add(name)
        m = re.search(r'^site:\s*(\S+)::(\S+)', body, re.M)
        if not m:
            bad.append(f"{name}: no `site:` line — a measurement with no link to the "
                       f"code is a number nobody can act on")
            continue
        path, sym = m.group(1), m.group(2)
        full = os.path.join(root, path)
        if not os.path.exists(full):
            bad.append(f"{name}: site file {path} does not exist"); continue
        src = open(full, encoding="utf-8", errors="replace").read()
        # Word-boundary match: `record_borrow` must not be satisfied by
        # `record_borrow_thing`, or the check certifies what it cannot see.
        if not re.search(r'\b' + re.escape(sym) + r'\b', src):
            bad.append(f"{name}: symbol `{sym}` is gone from {path} — the record now "
                       f"points at nothing; move it or delete it, do not leave it")
        checked += 1
        # A record with no fire count cannot be read: a zero ceiling and an
        # unreached site look identical without it.
        if not re.search(r'^fires:\s*\S', body, re.M):
            bad.append(f"{name}: no `fires:` line — a ceiling without a fire count "
                       f"cannot distinguish a refutation from an unreached site")
    if bad:
        print(f"FAIL (probe-log-lint): {len(bad)} broken record(s) in src/compiler/PROBES.md")
        for b in bad: print(f"  {b}")
        return 1
    print(f"probe-log-lint: {checked} records, every site symbol resolves")
    return 0

if __name__ == "__main__":
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else "."))
