#!/usr/bin/env python3
"""verdict.py ARMFILE — each hand program's verdict against rustc: OK (Rust's answer), WRONG, REFUSED-LEGAL, ADMITTED-ILLEGAL, REFUSED-ILLEGAL.
Oracle: this round and 2026-09-15f -> stdout equality with the rustc run; 2026-09-15e -> the program's own exit code (0 = Rust's count)."""
import re, sys, os
ROOT = "/home/logos/devel/logos/src/compiler/probes"
rust = {}
for l in open(f"{ROOT}/2026-09-15g-aggtemp/rust/RUSTC_VERDICTS.txt"):
    m = re.match(r"(\S+) rustc=(\d+) run=(\S+)(?: out=(.*))?", l.rstrip("\n"))
    if m: rust[m.group(1)] = (m.group(2) == "0", (m.group(4) or "").strip())
for l in open(f"{ROOT}/2026-09-15f-consumeland/rust/RUSTC_VERDICTS.txt"):
    m = re.match(r"(\S+) RUSTC (ok|REFUSED)\s*(?:exit=\d+ stdout=(.*))?", l.rstrip("\n"))
    if m: rust.setdefault(m.group(1), (m.group(2) == "ok", (m.group(3) or "").strip()))
erc = {}
for l in open(f"{ROOT}/2026-09-15e-consume/rust/RUSTC_VERDICTS.txt"):
    m = re.match(r"(\S+)\s+RUSTC (ok|REFUSED)", l)
    if m: erc[m.group(1)] = m.group(2) == "ok"
for l in open(sys.argv[1]):
    m = re.match(r"(\S+) cc=(\d+) run=(\S+)(?: out=(.*?))?( vg=\[.*\])?(?: diag=.*)?$", l.rstrip("\n"))
    if not m: continue
    n, cc, run, out, vg = m.group(1), m.group(2), m.group(3), (m.group(4) or "").strip(), (m.group(5) or "")
    refused = cc != "0" or run == "-"
    if n in rust:
        legal, rout = rust[n]
        if not legal: v = "REFUSED-ILLEGAL" if refused else "ADMITTED-ILLEGAL"
        elif refused: v = "REFUSED-LEGAL"
        else: v = "OK" if (out == rout and run == "0") else "WRONG"
    elif n in erc:
        legal = erc[n]
        if not legal: v = "REFUSED-ILLEGAL" if refused else "ADMITTED-ILLEGAL"
        elif refused: v = "REFUSED-LEGAL"
        else: v = "OK" if run == "0" else "WRONG"
    else: v = "NO-TWIN"
    es = re.search(r"ERROR SUMMARY: (\d+) errors", vg)
    print(f"{n}\t{v}\trun={run}\tout={out}\tvgerr={es.group(1) if es else '-'}")
