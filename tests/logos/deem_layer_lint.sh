#!/usr/bin/env bash
# deem_layer_lint.sh REPO_ROOT
#
# ADR 0031 R8 — THE DEEM LAYERS ARE A GATE, NOT PROSE.
#
# The surface (the parse tree: `RQProgram`, `RQuery`, `RQSimple`, … — every
# `RQ*` schema of plan.logos) is read by the parser, the lowering into the core,
# the handler that holds the parsed root, and the diagnostics' position
# resolver. Every other Deem module — the checks, the core rewrites, the
# planner, the plan, the emitters — reads the CORE (`CProgram`) and the plan.
# A surface type named in their CODE (comments excluded) is a layer violation:
# it is how the compiler's no-HIR mistake (ADR 0030) starts, one convenient
# read of the syntax at a time.
#
# CANARY: the check is first run over a copy of the tree with a violation
# planted in the emitter; it must refuse that copy, or a check that cannot
# fire would pass vacuously.
set -euo pipefail
ROOT="$1"
python3 - "$ROOT" <<'EOF'
import os, re, shutil, sys, tempfile
root = sys.argv[1]
DIRS = ["stdlib/mem/wql", "stdlib/mem/deem"]
# The modules that may name the surface: its schemas, the generated parser,
# the lowering (core.logos), the handler (wql.logos) and the position resolver.
ALLOWED = {"plan.logos", "wql_surface_parser.logos", "core.logos", "wql.logos", "srcloc.logos"}
SURF = re.compile(r"\bRQ[A-Z][A-Za-z]*\b")

def code(line):
    # strip a `//` comment outside a string literal
    out, ins, i = [], False, 0
    while i < len(line):
        c = line[i]
        if c == '"' and (i == 0 or line[i-1] != '\\'): ins = not ins
        if not ins and line.startswith("//", i): break
        out.append(c); i += 1
    return "".join(out)

def check(base):
    bad = []
    for d in DIRS:
        p = os.path.join(base, d)
        for f in sorted(os.listdir(p)):
            if not f.endswith(".logos") or f in ALLOWED: continue
            for n, line in enumerate(open(os.path.join(p, f)), 1):
                c = code(line)
                if c.lstrip().startswith("use "): continue   # an import is not a read
                m = SURF.search(c)
                if m: bad.append(f"{d}/{f}:{n}: `{m.group(0)}` — a surface type outside the parse/lower layer")
    return bad

# CANARY
tmp = tempfile.mkdtemp(prefix="deem_layer_lint_")
for d in DIRS:
    shutil.copytree(os.path.join(root, d), os.path.join(tmp, d))
with open(os.path.join(tmp, "stdlib/mem/wql/rexpr_walk.logos"), "a") as fh:
    fh.write("\nfn __layer_canary(q: RQSimple) -> i64 { return 0i64; }\n")
if not any("rexpr_walk.logos" in b and "RQSimple" in b for b in check(tmp)):
    print("FAIL: the planted canary (an `RQSimple` parameter in rexpr_walk.logos) was NOT caught — the lint cannot fire")
    sys.exit(1)
shutil.rmtree(tmp, ignore_errors=True)

bad = check(root)
for b in bad: print("FAIL: " + b)
if bad:
    print(f"FAIL: {len(bad)} surface read(s) outside the parse/lower layer (ADR 0031 R8)")
    sys.exit(1)
print("deem_layer_lint: OK (canary caught; no surface type outside parse/lower)")
EOF
