#!/usr/bin/env python3
"""ast_key_census.py — no AST node carries two fields in one key slot (ADR 0030 H0).

The AST node is a TinyObjectMap with 52 key codes, and %fields in logos.peg
REUSES codes across node kinds (QUAL_PARTS = 17 over USES, KEY = 12 over LHS,
the `group` blocks, …). A reuse is sound only while no node kind carries both
fields: otherwise one silently overwrites the other. This census derives, from
the grammar's own actions, which fields every node code is built with, and
fails when two differently-named fields of ONE ACTION (one node the parser
builds) resolve to the same key code. It also holds the GLOBAL keys (CODE, SRC_LINE, SRC_SPAN, stamped on
every node by peg_gen) free of any other field on any node.

It reads only what the parser builds; keys sema puts on nodes after parsing
are not seen, and a slot shared by two ALTERNATIVES of one node code (NAME_VAR
over WHERE: a quote body's FN has one, a plain FN the other) is a documented
reuse a consumer must tell apart by shape — neither is judged here (known
limits, named so nobody reads a green as covering them).

  ast_key_census.py <logos.peg>      exit 0 = sound, 1 = a collision
"""
import re
import sys

GLOBAL_KEYS = ("CODE", "SRC_LINE", "SRC_SPAN")


def parse_fields(text):
    body = text[text.index("%fields"):text.index("%nodes")]
    glob, groups, group = {}, {}, None
    for raw in body.splitlines():
        line = raw.split("//", 1)[0].strip()
        if not line:
            continue
        m = re.match(r"group\s+(\w+)\s*\{", line)
        if m:
            group = m.group(1)
            groups[group] = {}
            continue
        if line.startswith("}"):
            group = None
            continue
        m = re.match(r"([A-Z_][A-Z0-9_]*)\s*=\s*(\d+)", line)
        if m:
            (groups[group] if group else glob)[m.group(1)] = int(m.group(2))
    return glob, groups


def parse_actions(text):
    rules = text[text.index("%rules"):]
    actions = []  # (CODE name, rule group, [field names]) — one per action
    cur_group = None
    for raw in rules.splitlines():
        m = re.match(r"\s*\w+\s*:group\s+(\w+):\s*<-", raw)
        if m:
            cur_group = m.group(1)
        elif re.match(r"\s*\w+\s*<-", raw):
            cur_group = None
        for am in re.finditer(r"\{\s*CODE\s*:\s*([A-Z_0-9]+)((?:[^{}]|\{[^{}]*\})*)\}", raw):
            names = [fm.group(1) for fm in re.finditer(r"\b([A-Z_][A-Z0-9_]*)\s*:", am.group(2))]
            actions.append((am.group(1), cur_group, names))
    return actions


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    text = open(sys.argv[1]).read()
    glob, groups = parse_fields(text)
    actions = parse_actions(text)
    bad = 0
    for node, g, names in actions:
        by_code = {}
        for fname in names:
            code = groups.get(g, {}).get(fname, glob.get(fname)) if g else glob.get(fname)
            if code is None:
                print(f"FAIL: node {node} is built with field {fname}, which %fields does not declare")
                bad += 1
                continue
            by_code.setdefault(code, set()).add(fname)
        for code, fs in sorted(by_code.items()):
            if len(fs) > 1:
                print(f"FAIL: one {node} action sets {sorted(fs)} — all in key slot {code}; one overwrites the other")
                bad += 1
        for gk in GLOBAL_KEYS:
            gc = glob.get(gk)
            if gc is None:
                continue
            clash = sorted(n for n in by_code.get(gc, set()) if n != gk)
            if clash:
                print(f"FAIL: a {node} action sets {clash} in key slot {gc}, which is the global {gk}")
                bad += 1
    if bad:
        return 1
    print(f"OK: {len(actions)} actions over {len({a[0] for a in actions})} node codes: no action sets two fields in one key slot, "
          f"and {', '.join(GLOBAL_KEYS)} are free on every node.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
