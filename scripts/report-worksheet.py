#!/usr/bin/env python3
"""Stage 2 — the RE-PORT WORKSHEET for one imported port.

Prints, side by side, everything a re-port as-is needs and nothing else: the
upstream test exactly as it stands at the commit the port cites, the lines its
`//~` annotations point AT (the construct the test exists to exercise), our
port as it stands, and the ledger row the port answers to.

⚠ This tool does NOT judge fidelity. Deciding whether the port kept the
construct is the reader's job — the tool that guesses it is the tool that
produced the ports being re-done here.
"""
import os, re, subprocess, sys

REPO = os.environ.get("LOGOS_REPO",
                      os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
RUST = os.environ.get("RUSTC_CHECKOUT", "/home/logos/cxx/rust")


def provenance(port):
    for ln in open(os.path.join(REPO, "tests/imported/PROVENANCE.tsv")):
        if ln.startswith("#") or not ln.strip():
            continue
        f = ln.rstrip("\n").split("\t")
        if f[0] == port or f[0].endswith("/" + port):
            return f
    return None


def ledger_row(name):
    for f in ("tests/logos/bc_admits.ledger", "tests/logos/bc_admits_blocked.ledger"):
        for ln in open(os.path.join(REPO, f)):
            if ln.strip() and not ln.startswith("#") and ln.split()[0] == name:
                return f, ln.rstrip("\n")
    return None, None


def main(port):
    p = provenance(port)
    if not p:
        print(f"NO PROVENANCE for {port} — it cannot be re-ported as-is; that IS the finding.")
        return 2
    portpath, up, commit, method, conf, mods = p
    name = os.path.basename(portpath)
    src = subprocess.run(["git", "-C", RUST, "show", f"{commit}:{up}"],
                         capture_output=True, text=True)
    print(f"═══ PORT      {portpath}")
    print(f"═══ UPSTREAM  {up}  @ {commit[:9]}   ({method}/{conf}, declares mods: {mods})")
    lf, row = ledger_row(name)
    print(f"═══ LEDGER    {row if row else '(no row)'}" + (f"   [{lf}]" if lf else ""))
    if src.returncode:
        print("\n!! upstream file not readable at that commit")
        return 3
    lines = src.stdout.splitlines()
    ann = [(i + 1, l) for i, l in enumerate(lines) if "//~" in l or "error-pattern" in l]
    print(f"\n─── THE CONSTRUCT ({len(ann)} annotated line(s)) ───")
    for n, l in ann:
        print(f"  {n:>4}: {l.rstrip()}")
    print(f"\n─── UPSTREAM SOURCE ({len(lines)} lines) ───")
    for i, l in enumerate(lines, 1):
        print(f"  {i:>4}| {l.rstrip()}")
    print("\n─── OUR PORT ───")
    with open(os.path.join(REPO, portpath + ".logos")) as fh:
        for i, l in enumerate(fh, 1):
            print(f"  {i:>4}| {l.rstrip()}")
    return 0


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("usage: report-worksheet.py <port path or basename>", file=sys.stderr)
        sys.exit(1)
    sys.exit(main(sys.argv[1]))
