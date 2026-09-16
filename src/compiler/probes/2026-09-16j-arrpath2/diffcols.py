#!/usr/bin/env python3
"""diffcols.py — the three cost columns, diffed BOTH WAYS, base vs armed.

Rule 6: a ceiling bounds the COUNT, not the SET. So every column reports added AND removed AND
changed, and names the rows rather than only counting them.

usage: diffcols.py run   <base.tsv> <armed.tsv>
       diffcols.py fail  <base.tsv> <armed.tsv>
       diffcols.py vg    <base_results.tsv> <armed_results.tsv>
"""
import sys

# run_oracle.py row: name \t ccrc \t runrc \t stdout_sha
# ⚠ SUBTRACTED BY NAME: this fixture prints a STACK ADDRESS, so its stdout sha differs run to run
# and is not evidence of damage. Named, not pattern-matched, so it cannot silently swallow a sibling.
RUN_SUBTRACT = {"logos_02_semantic_core_pass_cast-region-to-uint"}

def load(path, keycol=0, valcols=None):
    d = {}
    for line in open(path):
        f = line.rstrip("\n").split("\t")
        if len(f) <= keycol:
            continue
        d[f[keycol]] = tuple(f[i] for i in valcols) if valcols else tuple(f[1:])
    return d

def report(kind, base, armed, subtract=frozenset()):
    kb, ka = set(base), set(armed)
    added, removed = ka - kb, kb - ka
    common = kb & ka
    changed = [k for k in common if base[k] != armed[k]]
    sub = [k for k in changed if k in subtract]
    changed = [k for k in changed if k not in subtract]
    print("%s: common=%d added=%d removed=%d CHANGED=%d (subtracted by name: %d)"
          % (kind, len(common), len(added), len(removed), len(changed), len(sub)))
    for k in sorted(sub):
        print("  SUBTRACTED %s  base=%s armed=%s  (prints a stack address)" % (k, base[k], armed[k]))
    for k in sorted(added):
        print("  ADDED      %s  armed=%s" % (k, armed[k]))
    for k in sorted(removed):
        print("  REMOVED    %s  base=%s" % (k, base[k]))
    for k in sorted(changed):
        print("  CHANGED    %s  base=%s -> armed=%s" % (k, base[k], armed[k]))
    return len(changed), len(added), len(removed)

def main():
    kind, bp, ap = sys.argv[1], sys.argv[2], sys.argv[3]
    if kind == "run":
        # name, (ccrc, runrc, sha)
        report("run_oracle", load(bp), load(ap), RUN_SUBTRACT)
    elif kind == "fail":
        # name, (rc, stderr_sha, expected_match, path)
        report("fail_text", load(bp), load(ap))
    elif kind == "vg":
        # sweep results.tsv: path, status, allocs, frees, definite, indirect, reachable, invalid, rc
        # The SETS that matter are LEAK and CORRUPT, diffed both ways — a count alone cannot say
        # whether the same fixtures leaked.
        b, a = load(bp, 0, [1]), load(ap, 0, [1])
        report("valgrind(status)", b, a)
        for tag in ("LEAK", "CORRUPT", "LEAK+CORRUPT", "TIMEOUT", "NOVG", "CFAIL", "RUNFAIL"):
            sb = {k for k, v in b.items() if v[0] == tag}
            sa = {k for k, v in a.items() if v[0] == tag}
            if sb or sa:
                print("  %-12s base=%d armed=%d  entered=%s  left=%s"
                      % (tag, len(sb), len(sa), sorted(sa - sb) or "-", sorted(sb - sa) or "-"))
    else:
        sys.exit("unknown column %r" % kind)

main()
