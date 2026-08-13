#!/usr/bin/env python3
"""callgrind_calls.py CALLGRIND_OUT — the CALL COUNT table, caller -> callee.

`callgrind_annotate` reports COST (instructions), not call counts, and cost is
not what ADR 0025 §5 claims: the claim is "one descent per LEAF, not per row",
which is a statement about how many times a function is ENTERED. This extracts
exactly that from the raw callgrind file and prints it as

    <count>\\t<caller>\\t<callee>

one edge per line, so a shell gate can assert on it with plain string
comparisons. Nothing is thresholded, filtered or rounded here: filtering is the
caller's business, and a helper that silently drops edges would make an absent
edge indistinguishable from a zero one.

FORMAT NOTE (callgrind_format.html, "name compression"). A name is spelled in
full ONCE — `fn=(7) some::name` — and afterwards by its number alone, `fn=(7)`.
A reader that only looks at lines carrying a name loses every repeat, which for
a hot function is nearly all of them. The id -> name map below is therefore
built as the file is read, and both `fn=` (the current caller) and `cfn=` (the
callee of the following `calls=` line) go through it.

⚠ THE FLOOR. An empty or unparseable file yields an empty table, and a gate that
asserts "no edge has count > X" over an empty table passes. So this program
exits 3 — verdict.py's "I could not look" code, distinct from any finding — when
it parsed fewer than MIN_EDGES edges, and prints why. A run of a real program
has thousands.
"""
import re
import sys
from collections import Counter

# Measured floor: the smallest program of interest here (build a container,
# walk it twice) produced ~1500 distinct call edges. 100 is far below that and
# far above anything a truncated or wrong-format file yields.
MIN_EDGES = 100

FN_RE = re.compile(r"^(c?fn)=\((\d+)\)(?:\s+(.*))?$")


def main(argv):
    if len(argv) != 2:
        print("usage: callgrind_calls.py CALLGRIND_OUT", file=sys.stderr)
        return 3
    names = {}
    counts = Counter()
    caller = None
    callee = None
    try:
        with open(argv[1], "r", errors="replace") as fh:
            for line in fh:
                line = line.rstrip("\n")
                m = FN_RE.match(line)
                if m:
                    kind, ident, nm = m.groups()
                    if nm:
                        names[ident] = nm
                    if kind == "fn":
                        caller = names.get(ident, "?" + ident)
                    else:
                        callee = names.get(ident, "?" + ident)
                    continue
                if line.startswith("calls="):
                    if caller is None or callee is None:
                        continue
                    counts[(caller, callee)] += int(line.split("=", 1)[1].split()[0])
    except OSError as exc:
        print("callgrind_calls: cannot read %s: %s" % (argv[1], exc),
              file=sys.stderr)
        return 3
    if len(counts) < MIN_EDGES:
        print("callgrind_calls: parsed only %d call edges from %s (floor %d) —"
              " the file is empty, truncated, or not callgrind output. Nothing"
              " in this run is evidence about the program."
              % (len(counts), argv[1], MIN_EDGES), file=sys.stderr)
        return 3
    for (a, b), n in sorted(counts.items(), key=lambda kv: -kv[1]):
        print("%d\t%s\t%s" % (n, a, b))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
