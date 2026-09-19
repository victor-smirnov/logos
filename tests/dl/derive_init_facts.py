#!/usr/bin/env python3
"""Derive the initialisation inputs of polonius.dl for an oracle case.

The borrow checker computes maybe-(un)initialised paths as bitset dataflow in
C++ (borrow_bir.inc, BirFacts::initialization) and hands the rules three
restricted inputs. An oracle case is a directory of .facts files; this script
derives those three from the case's base facts with an independent, set-based
implementation of the same dataflow, so a case's hand-derived expect/ answers
(move_errors, assign_twice, ...) check the C++ as well:

  path_maybe_uninitialized_on_entry(X, Q)    X accessed at Q
  path_maybe_initialized_on_entry(X, P)      a variable's root path assigned at P
  var_maybe_partly_initialized_on_exit(V, P) V has a drop that touches an origin

  derive_init_facts.py CASE_DIR...
"""
import os, sys

def read(d, rel):
    p = os.path.join(d, rel + ".facts")
    if not os.path.exists(p):
        return []
    with open(p) as f:
        return [tuple(l.rstrip("\n").split("\t")) for l in f if l.strip()]

def derive(d):
    edges = read(d, "cfg_edge")
    preds, succs, points = {}, {}, set()
    for a, b in edges:
        preds.setdefault(b, set()).add(a); succs.setdefault(a, set()).add(b)
        points |= {a, b}
    parent = {c: p for c, p in read(d, "child_path")}
    var_of = {x: v for x, v in read(d, "path_is_var")}
    paths = set(parent) | set(parent.values()) | set(var_of)
    def root(x):
        while x in parent: x = parent[x]
        return x
    kids = {}
    for c, p in parent.items(): kids.setdefault(p, set()).add(c)
    def sub(x):
        out, st = set(), [x]
        while st:
            y = st.pop(); out.add(y); st.extend(kids.get(y, ()))
        return out
    var_paths = {}
    for x in paths:
        v = var_of.get(root(x))
        if v is not None: var_paths.setdefault(v, set()).add(x)
    asg, mov, dead = {}, {}, {}
    for x, q in read(d, "path_assigned_at_base"): asg.setdefault(q, set()).update(sub(x))
    for x, q in read(d, "path_moved_at_base"):    mov.setdefault(q, set()).update(sub(x))
    for v, q in read(d, "var_dead_at"):           dead.setdefault(q, set()).update(var_paths.get(v, ()))
    def solve(gen, kill):
        ex = {q: set() for q in points}
        changed = True
        while changed:
            changed = False
            for q in points:
                inn = set().union(*(ex[p] for p in preds.get(q, ())))
                new = gen.get(q, set()) | (inn - kill.get(q, set()) - dead.get(q, set()))
                if new != ex[q]: ex[q] = new; changed = True
        return ex
    init = solve(asg, mov)
    uninit = solve(mov, asg)
    def on_entry(ex, q, x): return any(x in ex[p] for p in preds.get(q, ()))
    unin = sorted({(y, q) for x, q in read(d, "path_accessed_at_base") for y in sub(x)
                   if on_entry(uninit, q, y)})
    ini = sorted({(x, q) for x, q in read(d, "path_assigned_at_base")
                  if x not in parent and on_entry(init, q, x)})
    drop_vars = {v for v, _ in read(d, "drop_of_var_derefs_origin")}
    part = sorted({(v, q) for v in drop_vars for q in points
                   if init[q] & var_paths.get(v, set())})
    for rel, rows in (("path_maybe_uninitialized_on_entry", unin),
                      ("path_maybe_initialized_on_entry", ini),
                      ("var_maybe_partly_initialized_on_exit", part)):
        with open(os.path.join(d, rel + ".facts"), "w") as f:
            for r in rows: f.write("\t".join(r) + "\n")

if __name__ == "__main__":
    for d in sys.argv[1:]:
        derive(d)
