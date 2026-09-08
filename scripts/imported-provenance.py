#!/usr/bin/env python3
"""Stage 1 — PROVENANCE. For every imported port, resolve the upstream test it came
from, by header first and by name only as a fallback, and say WHICH method answered
and whether the port itself admits to having been modified.

An unresolved port is not a failure of this script, it is the finding: the fidelity
diff cannot run on it until a human or a later pass supplies the path."""
import os, re, subprocess, sys, collections

REPO = os.environ.get("LOGOS_REPO", os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
RUST = os.environ.get("RUSTC_CHECKOUT", "/home/logos/cxx/rust")
IMP = os.path.join(REPO, "tests/imported")

# upstream name index, per commit, built from the tree at that commit
def upstream_index(commit):
    out = subprocess.run(["git", "-C", RUST, "ls-tree", "-r", "--name-only", commit,
                          "tests/ui/"], capture_output=True, text=True, check=True).stdout
    paths = [p for p in out.splitlines() if p.endswith(".rs")]
    by_base = collections.defaultdict(list)
    for p in paths:
        by_base[os.path.basename(p)[:-3]].append(p)
    return set(paths), by_base

COMMITS = ["4b0c9d76ae7d387229caea55cfa73c280b08b8a7",
           "da5114692c9ebe46b869488c5f34f92eb10b98c1"]
IDX = {c: upstream_index(c) for c in COMMITS}

RE_RUSTC = re.compile(r"^//\s*rustc\s+((?:tests/)?ui/\S+?|\S+\.rs)(?:\s|$)")
RE_IMPFROM = re.compile(r"^//\s*Imported from rust-lang/rust[@\s]+(\S+)")
RE_UPSTREAM = re.compile(r"^//\s*upstream:\s*(\S+\.rs)")
RE_ORIGPATH = re.compile(r"^//\s*Original path:\s*(\S+\.rs)")
RE_BATCH = re.compile(r"@([0-9a-f]{40})")
# our own suffixes, stripped only as a LAST resort and recorded as such
RE_SUFFIX = re.compile(r"(--[a-z0-9_]+|-b1[0-9]{2}|-[a-z]?[0-9]{1,3})$")


def resolve(path):
    # Read the WHOLE leading comment block, not a fixed line count. Measured
    # 2026-09-07: a 25-line window recorded eleven ports that DO declare their
    # modifications as declaring none, because the block sat below line 25 — the
    # exact measurement this table exists to make mechanical, defeated by the
    # reader. Stop at the first line that is neither a comment nor blank, and
    # keep a floor so a file whose provenance sits after the `package` line is
    # still seen.
    head = []
    with open(path, errors="replace") as fh:
        for i, ln in enumerate(fh):
            if i >= 200:
                break
            if i >= 40 and ln.strip() and not ln.lstrip().startswith("//"):
                break
            head.append(ln)
    text = "".join(head)
    commit = None
    m = RE_BATCH.search(text)
    if m and m.group(1) in IDX:
        commit = m.group(1)
    modified = "yes" if re.search(r"^//\s*Modifications:", text, re.M) else "no"
    if re.search(r"\bdistilled\b|\bdistill", text, re.I):
        modified = "distilled"
    # A port that never mentions rust-lang and names no upstream path is not a port
    # at all: it is a test WRITTEN HERE that happens to live in tests/imported.
    is_port = bool(re.search(r"rust-lang/rust|^//\s*rustc\s|Original path:|upstream:",
                             text, re.M))

    cand = None
    method = None
    for ln in head:
        m = RE_RUSTC.match(ln) or RE_UPSTREAM.match(ln)
        if m:
            cand, method = m.group(1), "header-path"
            break
        m = RE_ORIGPATH.match(ln)
        if m:
            cand, method = m.group(1), "header-path"
            break
        m = RE_IMPFROM.match(ln)
        if m and m.group(1).endswith(".rs"):
            cand, method = m.group(1), "header-path"
            break
    if cand:
        cand = cand.rstrip(".,;")
        if not cand.endswith(".rs"):
            cand += ".rs"
        if not cand.startswith("tests/"):
            cand = ("tests/" + cand) if cand.startswith("ui/") \
                   else ("tests/ui/" + cand.lstrip("/"))

    order = [commit] if commit else []
    order += [c for c in COMMITS if c != commit]

    if cand:
        for c in order:
            if cand in IDX[c][0]:
                return cand, c, method, "high", modified
        # header names a path that is not in either tree — still the best evidence
        return cand, commit or "", method, "header-unverified", modified

    if not is_port:
        return "", commit or "", "native", "not-a-port", modified

    base = os.path.basename(path)[: -len(".logos")]
    for name, meth, conf in ((base, "basename", "medium"),
                             (RE_SUFFIX.sub("", base), "basename-destripped", "low")):
        for c in order:
            hits = IDX[c][1].get(name)
            if hits:
                return (hits[0] if len(hits) == 1 else "|".join(hits)), c, meth, \
                       (conf if len(hits) == 1 else "ambiguous"), modified
    return "", commit or "", "none", "none", modified


rows = []
for dirpath, _, files in os.walk(IMP):
    for fn in sorted(files):
        if not fn.endswith(".logos"):
            continue
        p = os.path.join(dirpath, fn)
        up, c, meth, conf, mod = resolve(p)
        rows.append((os.path.relpath(p, REPO)[: -len(".logos")], up, c, meth, conf, mod))

rows.sort()
with open(sys.argv[1], "w") as fh:
    fh.write("""# PROVENANCE.tsv — WHICH UPSTREAM TEST EACH IMPORTED PORT CAME FROM.
# GENERATED by scripts/imported-provenance.py from the port headers and the local
# rustc checkout. Do not hand-edit: fix the HEADER of the port, or the script.
#
# Stage 1 of the re-port plan, and it is BLOCKING: a port whose upstream is
# unknown cannot be diffed against the original, so it cannot be re-ported as-is
# and cannot be classified into the four buckets. An unresolved row is therefore
# not a failure of this table, it IS the finding.
#
# method       header-path  the port names its upstream, and the path exists in the
#                           checkout at the commit the port cites. Believe it.
#              native       the port never mentions rust-lang and names no upstream
#                           path: it is a test WRITTEN HERE that lives in
#                           tests/imported. It is not a port and must not be
#                           re-ported. ⚠ Some of these wear a port's header with an
#                           EMPTY path — the header is a template, not evidence.
#              basename*    matched by file name only. WEAK: a name is not an
#                           identity, and a destripped name even less so.
#              none         no upstream recovered. 345 of these carry
#                           `// Imported from rust-lang/rust@<commit>` and NO path
#                           line: the importer kept the commit and dropped the path.
# confidence   high | medium | low | ambiguous (several upstream files share the
#              base name) | header-unverified (the header names a path that is in
#              NEITHER checkout — upstream moved or the header is a glob) | not-a-port
# port_declares_modifications
#              yes        the port carries a `// Modifications:` block. The importer
#                         WROTE DOWN what it changed — read it before diffing.
#              distilled  the port says it distilled the original. It is by its own
#                         admission NOT as-is; re-porting it means starting over.
#              no         claims nothing. Says nothing about whether it is faithful.
#
# port\tupstream\tcommit\tmethod\tconfidence\tport_declares_modifications
""")
    for r in rows:
        fh.write("\t".join(r) + "\n")
    fh.write(f"# TOTAL {len(rows)}\n")

print(f"ports {len(rows)}")
for k in ("method", "confidence", "port_declares_modifications"):
    i = {"method": 3, "confidence": 4, "port_declares_modifications": 5}[k]
    c = collections.Counter(r[i] for r in rows)
    print(f"  {k}: " + "  ".join(f"{a}={b}" for a, b in c.most_common()))
