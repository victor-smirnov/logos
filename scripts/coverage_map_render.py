#!/usr/bin/env python3
"""coverage_map_render.py — turn an `llvm-cov export` JSON into a REGION MAP for
one translation unit: which regions never executed, which are near-dead, which
are hot, grouped by enclosing function.

Called by scripts/coverage-map.sh. Not a gate: it pronounces no verdict. See
that script's header for what a zero does and does not mean.

Inputs : --json  llvm-cov export output (already filtered to one source file)
         --source  absolute path of the source file the map is about
         --population / --date / --binary  provenance strings, printed verbatim
Outputs: --out-md   the narrative map (committed artefact)
         --out-csv  every region, one row, for grep next round
"""
import argparse
import csv
import json
import re
import subprocess
import sys

KIND_CODE = 0

# ── WHAT A REGION IS, said in words a next round can aim with ────────────────
# A line number alone is not usable. These patterns read the SOURCE TEXT at the
# region's first line and name the syntactic thing the region is the body of.
# They are a reading aid, not a verdict: the source line is printed beside every
# classification so a wrong guess is visible.
CLASSIFIERS = [
    (re.compile(r'^\s*case\b|^\s*default\s*:'),        "switch arm"),
    (re.compile(r'^\s*\}?\s*else\s+if\b'),             "else-if guard"),
    (re.compile(r'^\s*\}?\s*else\b'),                  "else branch"),
    (re.compile(r'^\s*if\s*\(|^\s*if\s+constexpr'),    "if guard"),
    (re.compile(r'^\s*(for|while|do)\b'),              "loop body"),
    (re.compile(r'^\s*return\b'),                      "early return"),
    (re.compile(r'^\s*continue\s*;|^\s*break\s*;'),    "loop exit"),
    (re.compile(r'^\s*throw\b|^\s*std::abort|^\s*assert\b'), "abort/assert"),
    (re.compile(r'\bdiag|\berror\(|\breport|E0\d\d\d'), "diagnostic emission"),
    (re.compile(r'^\s*\[?\[?[A-Za-z_].*\)\s*\{?\s*$'), "call / statement"),
]


def classify(text: str) -> str:
    for rx, name in CLASSIFIERS:
        if rx.search(text):
            return name
    return "statement"


def demangle(names):
    """One llvm-cxxfilt call for the whole set; a failure degrades to mangled."""
    uniq = sorted(set(names))
    if not uniq:
        return {}
    try:
        out = subprocess.run(["llvm-cxxfilt-20", "-n"], input="\n".join(uniq),
                             capture_output=True, text=True, check=True).stdout
        got = out.rstrip("\n").split("\n")
        if len(got) == len(uniq):
            return dict(zip(uniq, got))
    except (OSError, subprocess.CalledProcessError):
        pass
    return {n: n for n in uniq}


def short(name: str) -> str:
    """Drop argument lists and template arguments: the map is read by eye."""
    name = re.sub(r'\(.*', '', name, count=1)
    depth, out = 0, []
    for ch in name:
        if ch == '<':
            depth += 1
        elif ch == '>':
            depth = max(0, depth - 1)
        elif depth == 0:
            out.append(ch)
    s = "".join(out).strip()
    return s or name


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--json", required=True)
    ap.add_argument("--source", required=True)
    ap.add_argument("--out-md", required=True)
    ap.add_argument("--out-csv", required=True)
    ap.add_argument("--population", default="(unstated)")
    ap.add_argument("--date", default="(unstated)")
    ap.add_argument("--binary", default="(unstated)")
    ap.add_argument("--runs", default="(unstated)")
    ap.add_argument("--near-dead-max", type=int, default=9)
    a = ap.parse_args()

    with open(a.json) as fh:
        data = json.load(fh)
    export = data["data"][0]
    src_lines = open(a.source, errors="replace").read().split("\n")

    # ── AGGREGATE BY SOURCE SPAN, ACROSS INSTANTIATIONS ──────────────────────
    # A template or a lambda appears once per instantiation. The question this
    # map answers is about the SOURCE, so counts for one span are summed and the
    # owning function is the first one seen for that span.
    regions = {}          # (l0,c0,l1,c1) -> [count, owner mangled]
    fn_count = {}         # mangled -> summed entry count
    fn_span = {}          # mangled -> (min line, max line)
    for fn in export["functions"]:
        files = fn["filenames"]
        if a.source not in files:
            continue
        fid_ok = {i for i, f in enumerate(files) if f == a.source}
        name = fn["name"]
        touched = False
        for r in fn["regions"]:
            l0, c0, l1, c1, count, fid, _exp, kind = r[:8]
            if fid not in fid_ok or kind != KIND_CODE:
                continue
            touched = True
            key = (l0, c0, l1, c1)
            slot = regions.setdefault(key, [0, name])
            slot[0] += count
            lo, hi = fn_span.get(name, (l0, l1))
            fn_span[name] = (min(lo, l0), max(hi, l1))
        if touched:
            fn_count[name] = fn_count.get(name, 0) + fn["count"]

    if not regions:
        print("coverage_map_render: no regions for %s — the mapping did not "
              "resolve, and an empty map is not a clean one" % a.source,
              file=sys.stderr)
        return 3

    names = demangle(list(fn_count.keys()))
    rows = []
    for (l0, c0, l1, c1), (count, owner) in regions.items():
        text = src_lines[l0 - 1].rstrip() if 0 < l0 <= len(src_lines) else ""
        rows.append(dict(line=l0, col=c0, end_line=l1, end_col=c1, count=count,
                         fn=short(names.get(owner, owner)),
                         kind=classify(text), text=text.strip()))
    rows.sort(key=lambda r: (r["line"], r["col"]))

    with open(a.out_csv, "w", newline="") as fh:
        w = csv.DictWriter(fh, fieldnames=["count", "line", "col", "end_line",
                                           "end_col", "kind", "fn", "text"])
        w.writeheader()
        for r in rows:
            w.writerow({k: r[k] for k in w.fieldnames})

    # ── PER-FUNCTION ROLL-UP ─────────────────────────────────────────────────
    per_fn = {}
    for r in rows:
        d = per_fn.setdefault(r["fn"], dict(total=0, zero=0, hot=0, line=r["line"]))
        d["total"] += 1
        d["line"] = min(d["line"], r["line"])
        if r["count"] == 0:
            d["zero"] += 1
        d["hot"] = max(d["hot"], r["count"])

    dead_fns = sorted((n for n, d in per_fn.items() if d["hot"] == 0),
                      key=lambda n: per_fn[n]["line"])
    zeros = [r for r in rows if r["count"] == 0 and per_fn[r["fn"]]["hot"] > 0]
    near = sorted((r for r in rows if 0 < r["count"] <= a.near_dead_max),
                  key=lambda r: (r["count"], r["line"]))
    hot = sorted(rows, key=lambda r: -r["count"])[:30]

    # ── PROBE SITES: the arrival count at every logos::probe::on in the TU ────
    # The count wanted is the ENCLOSING region's, not the probe's own guarded
    # body — that body is 0 by construction because the probe was disarmed for
    # this run. So: among the regions containing the probe's line, take the
    # SMALLEST one that does not START on that line.
    probes = []
    for i, text in enumerate(src_lines, start=1):
        m = re.search(r'probe::on\("([a-z_0-9]+)"\)', text)
        if not m:
            continue
        cands = [r for r in rows if r["line"] < i <= r["end_line"]]
        cands.sort(key=lambda r: (r["end_line"] - r["line"]))
        probes.append((m.group(1), i, cands[0]["count"] if cands else None,
                       cands[0]["fn"] if cands else "?"))

    tot = len(rows)
    z = sum(1 for r in rows if r["count"] == 0)
    with open(a.out_md, "w") as o:
        p = lambda *s: print(*s, file=o)
        p("# borrow_check.cpp — region execution map")
        p()
        p("| | |")
        p("|---|---|")
        p("| taken | %s |" % a.date)
        p("| subject | `%s` |" % a.source)
        p("| binary | %s |" % a.binary)
        p("| population | %s |" % a.population)
        p("| compiler runs | %s |" % a.runs)
        p("| reproduce | `scripts/coverage-map.sh` |")
        p()
        p("## ⚠ A ZERO IN COVERAGE IS NOT A DEFECT")
        p()
        p("A region with count 0 is exactly one fact: **nothing in the population")
        p("above executed it**. That is three different situations and they take")
        p("three different responses:")
        p()
        p("1. **dead code** — no input can reach it. Delete it.")
        p("2. **an unexercised case** — reachable, but nothing we test reaches it.")
        p("   That is a CORPUS gap, and the response is a fixture.")
        p("3. **structurally unreachable** — a guard whose condition cannot hold")
        p("   given its callers' invariants. The response is to say so in the")
        p("   code, or to remove the guard.")
        p()
        p("This map cannot tell them apart and does not try. Reading a zero as a")
        p("defect would repeat the error this arc is about — the census's 28 915")
        p("\"arrivals\" were real and meant something other than what was read off")
        p("them. Equally: a zero is not permission to delete.")
        p()
        p("**And coverage is not a probe.** Coverage says which code executed and")
        p("how often. A probe says what CHANGES if the code behaves differently,")
        p("which coverage cannot answer at all. This map says where a probe would")
        p("have a population behind it; it never says what the probe would find.")
        p()
        p("## Totals")
        p()
        p("| | count |")
        p("|---|---:|")
        p("| code regions in the TU | %d |" % tot)
        p("| never executed (count 0) | %d (%.1f%%) |" % (z, 100.0 * z / tot))
        p("| near-dead (1..%d) | %d |" % (a.near_dead_max, len(near)))
        p("| functions with regions here | %d |" % len(per_fn))
        p("| functions never entered | %d |" % len(dead_fns))
        p()
        p("Every region is in `%s`, one row each, sorted by line and column."
          % a.out_csv.rsplit("/", 1)[-1])
        p()
        p("\u26a0 Counts are summed ACROSS INSTANTIATIONS: a template or a lambda has")
        p("one entry per instantiation in the raw profile, and the question here is")
        p("about the SOURCE. So the function count above is smaller than the one")
        p("`llvm-cov report` prints, which counts instantiations. Two regions can")
        p("share a line and differ only in column \u2014 a sub-expression of an `&&`")
        p("chain is its own region \u2014 so line:col is the identity, not line.")
        p()

        p("## A. Functions never entered (%d)" % len(dead_fns))
        p()
        p("No region in these executed. A probe placed in any of them reports")
        p("ceiling 0 for the reason that has nothing to do with the hypothesis.")
        p()
        p("| first line | regions | function |")
        p("|---:|---:|---|")
        for n in dead_fns:
            p("| %d | %d | `%s` |" % (per_fn[n]["line"], per_fn[n]["total"], n))
        p()

        p("## B. Never-executed regions inside functions that DO run (%d)"
          % len(zeros))
        p()
        p("The interesting half: the function is live, this branch of it is not.")
        p("Grouped by enclosing function, ranked by how much of the function is")
        p("cold. Full list in the CSV; here, every function with at least one.")
        p()
        by_fn = {}
        for r in zeros:
            by_fn.setdefault(r["fn"], []).append(r)
        order = sorted(by_fn, key=lambda n: (-len(by_fn[n]), per_fn[n]["line"]))
        for n in order:
            g = by_fn[n]
            p("### `%s` — %d of %d regions cold (hottest %d)"
              % (n, len(g), per_fn[n]["total"], per_fn[n]["hot"]))
            p()
            p("| line:col | what it is | source |")
            p("|---:|---|---|")
            for r in g:
                p("| %d:%d | %s | `%s` |" % (r["line"], r["col"], r["kind"],
                                             r["text"].replace("|", "\\|")[:110]))
            p()

        p("## C. Near-dead regions — count 1..%d (%d)" % (a.near_dead_max, len(near)))
        p()
        p("**This is the class that wasted three probe slots.** A site here is")
        p("live, so a probe on it is not \"never fired\" — it fires, twice, and")
        p("reports a ceiling of 0 that reads exactly like a refuted hypothesis.")
        p("Before spending a slot here, ask whether a population of this size")
        p("could show the effect at all.")
        p()
        p("| count | line:col | function | what it is | source |")
        p("|---:|---:|---|---|---|")
        for r in near:
            p("| %d | %d:%d | `%s` | %s | `%s` |"
              % (r["count"], r["line"], r["col"], r["fn"], r["kind"],
                 r["text"].replace("|", "\\|")[:90]))
        p()

        p("## D. The hottest 30 regions, for contrast")
        p()
        p("| count | line:col | function | source |")
        p("|---:|---:|---|---|")
        for r in hot:
            p("| %d | %d:%d | `%s` | `%s` |"
              % (r["count"], r["line"], r["col"], r["fn"],
                 r["text"].replace("|", "\\|")[:90]))
        p()

        p("## E. Where the probes are aimed")
        p()
        p("Every `logos::probe::on(...)` site in the TU with the execution count")
        p("of its ENCLOSING region — the number of times the probe's condition")
        p("was evaluated in this population. The probe's own body is 0 by")
        p("construction here: no probe was armed for the mapping run.")
        p()
        p("| arrivals | line | probe | enclosing function |")
        p("|---:|---:|---|---|")
        for name, line, cnt, fn in sorted(probes, key=lambda t: (t[2] is None, t[2])):
            p("| %s | %d | `%s` | `%s` |"
              % ("?" if cnt is None else cnt, line, name, fn))
        p()
    print("map: %d regions, %d zero, %d near-dead, %d dead functions"
          % (tot, z, len(near), len(dead_fns)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
