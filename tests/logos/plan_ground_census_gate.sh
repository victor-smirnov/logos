#!/usr/bin/env bash
# plan_ground_census_gate.sh LOGOSC PASS_DIR WHY_LOGOS ACCESS_PLAN_LOGOS
#
# THE REFUSAL CENSUS, RE-DERIVED BY MACHINE — ADR 0025 S2 ("the refusal census
# (why-vocabulary) re-derived, no silence where a drain happens").
#
# TWO VOCABULARIES, ONE CORPUS SWEEP. This compiler publishes justification
# sentences in two families and both are claims about queries that no test held:
#
#   THE REFUSAL VOCABULARY  `why::wg_words` / `why::rj_words` / `why::sz_*_words`
#                           — WHICH ANTECEDENT refused the order axis.
#   THE MATERIALIZATION     `access_plan::MG_*` — WHY a buffer exists (ADR 0025
#   VOCABULARY              §4: `Drain`/`Sort`/`Arrange`, and the two ways of
#                           building NOTHING).
#
# A ground sentence with no query that reaches it is a sentence nobody has ever
# read — the COVERAGE question, and the recorded rule is that coverage is
# measured against what the CORPUS has, not what the lattice admits. This gate
# measures it: it compiles the whole `wql_*`/`deem_*` pass corpus with
# `LOGOS_TRACE_PLAN=1` and `--gen-dir`, tallies every ground token EXTRACTED FROM
# THE SOURCE FILES (never a second copy of the list — a hand-kept copy is how a
# new ground escapes a census), and partitions the vocabulary into WITNESSED and
# UNWITNESSED against the pin block below.
#
# ⚠ THE PARTITION IS CHECKED IN BOTH DIRECTIONS. A token declared UNWITNESSED
# that the corpus DOES reach is a failure exactly like a witnessed one going
# quiet: an exemption nobody checks in the abuse direction is worse than no
# exemption, because the green then vouches for it. The UNWITNESSED list is the
# S2 DEBT LEDGER — `MG_REL_BLOCK`/`MG_UNDECIDED`/`MG_GPATH`/`MG_UNPROVEN` are the
# grounds `plan_mark_single_pass` carries that no fixture has yet driven through
# a node, and that function cannot be deleted until they are.
#
# ── NO SILENT DRAIN (the structural half, and the reason for the sweep) ───────
#
#   FACT A  NO SILENCE, CORPUS-WIDE. Per plan block, a rel that reports the
#           `materialize` verdict must ALSO carry a node line or the positive
#           "no node: already a buffer" ground. A materialization the plan
#           performed and did not name is the exact defect S2 closes, and the
#           four-fixture `logos_09_plan_nodes` sees it on four plans; this sees
#           it on every plan the corpus compiles.
#
#   FACT B  THE PRELUDE BUFFERS ARE THE DRAIN/SORT NODES, PER FIXTURE AND IN
#           TOTAL. #(drain+sort nodes) == #(`let mut __it_…` bindings in the
#           `--gen-dir` dump). ⚠ Since S2b the emitter CHOOSES that arm from the
#           node, so for the per-rel prelude this equality is true by
#           construction; it is kept because it still fails on a node dropped
#           past the list's capacity, an arrange node miscounted as a prelude
#           one, or a rel whose prelude arm is the container arm. The independent
#           oracle for that clause is the forcing control recorded in ADR 0025,
#           not this gate.
#
#   FACT C  THE ARRANGE DEFICIT IS ACCOUNTED, NOT SILENT — and this is what the
#           corpus sweep bought that the four fixtures could not see. MEASURED:
#           31 `arrange` nodes against 491 `hash join` strategy decisions and 594
#           emitted index bindings. The node layer covers the TOP-LEVEL chain's
#           steps; the remaining decisions are (i) a derived rel's / SCC member's
#           own chain, emitted per fixpoint variant, and (ii) the SECOND and
#           further carried nests of one chain, each of which emits its own build
#           phase. NEITHER IS NAMED BY A NODE YET, so the numbers are pinned:
#           they are the size of the debt, they can only move with an edit to
#           this block, and a new unnamed materialization class cannot enter the
#           corpus quietly. Per fixture the inequality `arrange <= hash joins`
#           holds and is asserted — arrange nodes over steps that no strategy
#           decision produced would be invention.
#
# ⚠ LIKE CENSUS FACT 5, THE EXACT TOTALS GO RED WHEN THE CORPUS GROWS, ON
# PURPOSE. A ground census measured against a stale corpus is worth nothing; the
# failure message prints every number so the fix is one edit to the pin block,
# made WITH the change that moved it and next to the sentence saying which class
# the new materialization belongs to.
#
# ⚠ NO PIPE INTO `wc`/`grep -q` ON A COUNT: under `pipefail` a legitimately-zero
# `grep` fails the command and zero is an expected answer here throughout. All
# tallying happens in the python pass, which reads files, not pipes.
set -uo pipefail

LOGOSC="$1"
PASS="$2"
WHY_SRC="$3"
AP_SRC="$4"

TMPD=$(mktemp -d)
trap 'rm -rf "$TMPD"' EXIT
export LC_ALL=C

shopt -s nullglob
FIXTURES=("$PASS"/wql_*.logos "$PASS"/deem_*.logos)
if [ "${#FIXTURES[@]}" -lt 150 ]; then
    echo "FAIL: only ${#FIXTURES[@]} corpus fixtures matched — the census is blind."
    exit 1
fi

# ── the sweep, one process per fixture, $(nproc) at a time ───────────────────
# Each worker writes THREE files and no shared state: the trace, and the two
# artifact counts. A worker that cannot compile writes its name into `_failed`.
mkdir -p "$TMPD/o"
cat > "$TMPD/one.sh" <<'WORKER'
#!/usr/bin/env bash
set -uo pipefail
f="$1"; LOGOSC="$2"; O="$3"
b=$(basename "$f" .logos)
d="$O/$b.d"
mkdir -p "$d"
if LOGOS_TRACE_PLAN=1 "$LOGOSC" "$f" --gen-dir "$d/gen" -o "$d/out.o" \
        > "$d/log" 2> "$O/$b.err"; then :; else echo "$b" >> "$O/_failed"; fi
shopt -s nullglob
# The USER module's dumps only: `logos.gen.*` holds the family DEFINITIONS, and
# a producer found there says nothing about what this query builds.
UD=("$d"/gen/test.*.gen.logos)
nit=0; nix=0
if [ "${#UD[@]}" -ge 1 ]; then
    grep -Eh 'let mut __it_[a-z_0-9]+:' "${UD[@]}" > "$d/it" 2>/dev/null
    grep -Eh 'let mut __(hm|hs|bt)[0-9]+:' "${UD[@]}" > "$d/ix" 2>/dev/null
    nit=$(wc -l < "$d/it")
    nix=$(wc -l < "$d/ix")
fi
echo "$b $nit $nix" > "$O/$b.count"
rm -rf "$d/gen" "$d/out.o"
WORKER
chmod +x "$TMPD/one.sh"

printf '%s\0' "${FIXTURES[@]}" \
  | xargs -0 -P "$(nproc)" -I{} "$TMPD/one.sh" {} "$LOGOSC" "$TMPD/o"
sweep_rc=$?
if [ "$sweep_rc" -ne 0 ]; then
    echo "FAIL: the corpus sweep itself failed (xargs rc $sweep_rc) — nothing was measured."
    exit 1
fi

# ── the census ───────────────────────────────────────────────────────────────
python3 - "$TMPD/o" "$WHY_SRC" "$AP_SRC" "${#FIXTURES[@]}" <<'PY'
import os, re, sys, glob

OD, WHY_SRC, AP_SRC, NFIX = sys.argv[1], sys.argv[2], sys.argv[3], int(sys.argv[4])
fail = []

# ── THE PIN BLOCK ────────────────────────────────────────────────────────────
# Measured 2026-08-13 on the S2/S2b tree, over the whole wql_/deem_ pass corpus.
EXPECT_FIXTURES   = 175
# The two fixtures that cannot compile ALONE: each `use`s a companion package the
# suite supplies through a lib path (LOCAL_PUBLIB_USERS / LOCAL_WQLMAP_USERS in
# CMakeLists.txt). Named, so that a THIRD compile failure — or one of these two
# starting to compile, which would mean the pin is stale — is red.
EXPECT_FAILED     = {"wql_mapping_cross_module_e2e", "wql_wref_field_pkg"}
EXPECT_DRAIN_SORT = 7     # drain 4 + sort 3, corpus-wide
EXPECT_IT         = 7     # `let mut __it_…` prelude bindings in the artifacts
EXPECT_ARRANGE    = 31    # Arrange nodes
EXPECT_HASHJOIN   = 491   # `hash join` strategy decisions
EXPECT_INDEX      = 594   # emitted `__hm`/`__hs`/`__bt` bindings
EXPECT_NOMAT      = {"container": 187, "readonce": 15}
# The DEBT LEDGER: ground tokens the corpus does not reach. Checked in BOTH
# directions — a token here that IS witnessed fails just as loudly.
UNWITNESSED = {
    # the order axis: refusals no corpus query provokes
    "WG_NO_STEP", "WG_UNDECIDED", "WG_ONE_FLOAT", "WG_MAX_FL", "WG_CROSS",
    "WG_NO_SIZE",
    "RJ_PREDBASE", "RJ_PREDPIN", "RJ_SHAPE",
    "SZ_RUN_STREAMS", "SZ_PREP_STREAMS",
    # ADR 0025 §4: the materialization grounds `plan_mark_single_pass` carries
    # and no fixture drives through a node. S2 cannot delete that function until
    # this set is empty.
    "MG_REL_BLOCK", "MG_UNDECIDED", "MG_GPATH", "MG_UNPROVEN",
}

def bad(msg):
    fail.append(msg)

# ── the vocabularies, EXTRACTED FROM THE SOURCE ──────────────────────────────
def fn_body(src, name):
    i = src.index("fn " + name + "(")
    j = src.index("\n}\n", i)
    return src[i:j]

why = open(WHY_SRC, encoding="utf-8").read()
apl = open(AP_SRC, encoding="utf-8").read()

vocab = {}   # token -> probe sentence prefix
for fname, pfx in (("wg_words", ""), ("rj_words", ""),
                   ("sz_run_words", "SZ_RUN_"), ("sz_no_prepare_words", "SZ_PREP_")):
    body = fn_body(why, fname)
    pairs = re.findall(r'if \w+ == (\w+)\(\)[^\n]*\{\s*\n?\s*return "(.*?)";', body, re.S)
    if not pairs:
        bad(f"the vocabulary extractor read ZERO sentences out of `{fname}` — "
            f"the census would have been vacuously green")
    for tok, sent in pairs:
        # `SZ_RUN_` + `SZ_STREAMS` would name one ground twice. The channel
        # prefix replaces the token's own, so the census prints the name the ADR
        # and this file both use: `SZ_RUN_STREAMS`.
        key = pfx + tok[3:] if pfx and tok.startswith("SZ_") else (pfx + tok if pfx else tok)
        vocab[key] = sent[:55]
for tok, sent in re.findall(r'pub fn (MG_\w+)\(\) -> str\s*\{ return "(.*?)"; \}', apl):
    vocab[tok] = sent
if len([k for k in vocab if k.startswith("MG_")]) < 8:
    bad("fewer than 8 MG_* grounds extracted — the materialization vocabulary "
        "was not read")

# ── the sweep's output ───────────────────────────────────────────────────────
errs = sorted(glob.glob(os.path.join(OD, "*.err")))
if len(errs) != NFIX:
    bad(f"{len(errs)} traces for {NFIX} fixtures — a worker died silently")
if NFIX != EXPECT_FIXTURES:
    bad(f"corpus is {NFIX} fixtures, pin says {EXPECT_FIXTURES} — re-derive the "
        f"census (every number below is measured against the corpus)")

failed = set()
fp = os.path.join(OD, "_failed")
if os.path.exists(fp):
    failed = {l.strip() for l in open(fp) if l.strip()}
if failed != EXPECT_FAILED:
    bad(f"standalone compile failures {sorted(failed)} != pinned "
        f"{sorted(EXPECT_FAILED)}")

NODE = re.compile(r'^\[plan\] ([a-z_0-9]+) -> (drain|sort|arrange) on ([^(]*?)\s+\(')
VERB = re.compile(r'^\[plan\] ([a-z_0-9]+) -> ([a-z ]+?)\s')

tot = dict(drain=0, sort=0, arrange=0, it=0, ix=0, hj=0,
           container=0, readonce=0, materialize=0, stream=0)
witness = {k: 0 for k in vocab}
silent = []

for e in errs:
    b = os.path.basename(e)[:-4]
    text = open(e, encoding="utf-8", errors="replace").read()
    # ⚠ TWO PROBES, BECAUSE THE TWO VOCABULARIES ARE SHAPED DIFFERENTLY. A `WG_*`
    # / `RJ_*` / `SZ_*` sentence is a paragraph and a substring probe over the
    # trace is exact enough to be an identity. An `MG_*` ground is a TOKEN of two
    # or three words — `order by` occurs in 290 lines of this corpus that have
    # nothing to do with a Sort node — so those are counted by EXACT EQUALITY on
    # the ground field of a node line (and, for `MG_CONTAINER`, of the
    # "no materialization" line), which is the only place a ground token is ever
    # written. A short probe read as a sentence is how a census counts the
    # question instead of the answer.
    for tok, probe in vocab.items():
        if not tok.startswith("MG_"):
            witness[tok] += text.count(probe)
    nd = dict(drain=0, sort=0, arrange=0, hj=0)
    mat, named = set(), set()
    for line in text.splitlines():
        m = NODE.match(line)
        if m:
            rel, kind, gnd = m.group(1), m.group(2), m.group(3).strip()
            nd[kind] += 1
            named.add(rel)
            for tok, probe in vocab.items():
                if tok.startswith("MG_") and probe == gnd:
                    witness[tok] += 1
            if not gnd:
                bad(f"[{b}] a `{kind}` node carries an EMPTY ground")
            continue
        if line.startswith("[plan] ") and " -> hash join on " in line:
            nd["hj"] += 1
        if " -> materialize " in line or line.endswith(" -> materialize"):
            mat.add(line.split()[1])
        if " -> no materialization" in line:
            rel = line.split()[1]
            if "already a buffer" in line:
                named.add(rel); tot["container"] += 1
                witness["MG_CONTAINER"] += 1
            else:
                tot["readonce"] += 1
        if " -> prepared plan on " in line:
            for r in mat - named:
                silent.append((b, r))
            mat, named = set(), set()
    for r in mat - named:
        silent.append((b, r))
    tot["materialize"] += text.count(" -> materialize   (")
    tot["stream"] += text.count(" -> stream   (")
    for k in ("drain", "sort", "arrange", "hj"):
        tot[k] += nd[k]

    cf = os.path.join(OD, b + ".count")
    nit = nix = 0
    if os.path.exists(cf):
        _, a, c = open(cf).read().split()
        nit, nix = int(a), int(c)
    else:
        bad(f"[{b}] no artifact count file — the emitted side was not read")
    tot["it"] += nit
    tot["ix"] += nix

    # FACT B, per fixture
    if b not in EXPECT_FAILED and nd["drain"] + nd["sort"] != nit:
        bad(f"[{b}] {nd['drain']+nd['sort']} drain/sort nodes vs {nit} `__it_` "
            f"prelude bindings in the artifact")
    # FACT C, per fixture: an arrangement over a step no strategy decided
    if nd["arrange"] > nd["hj"]:
        bad(f"[{b}] {nd['arrange']} arrange nodes over {nd['hj']} hash-join "
            f"decisions — a node was invented")

# FACT A
if silent:
    for b, r in silent[:20]:
        bad(f"[{b}] rel `{r}` reports `materialize` and names no node — a "
            f"materialization with no ground")

# FACT B / FACT C totals
for key, want, what in (
        ("drain", None, None),
        ("it", EXPECT_IT, "`__it_` prelude bindings"),
        ("arrange", EXPECT_ARRANGE, "Arrange nodes"),
        ("hj", EXPECT_HASHJOIN, "`hash join` strategy decisions"),
        ("ix", EXPECT_INDEX, "emitted index bindings"),
        ("container", EXPECT_NOMAT["container"], "`already a buffer` grounds"),
        ("readonce", EXPECT_NOMAT["readonce"], "`read once, consumed where it stands` grounds")):
    if want is None:
        continue
    if tot[key] != want:
        bad(f"corpus total: {tot[key]} {what}, pinned {want}")
if tot["drain"] + tot["sort"] != EXPECT_DRAIN_SORT:
    bad(f"corpus total: {tot['drain']}+{tot['sort']} drain/sort nodes, pinned "
        f"{EXPECT_DRAIN_SORT}")

# FACT D — the partition, both directions
for tok in sorted(vocab):
    n = witness[tok]
    if tok in UNWITNESSED and n > 0:
        bad(f"ground `{tok}` is pinned UNWITNESSED and the corpus reaches it "
            f"{n} times — the debt ledger is stale (this is a WIN: move it out "
            f"of UNWITNESSED)")
    if tok not in UNWITNESSED and n == 0:
        bad(f"ground `{tok}` is a published justification sentence that NO "
            f"corpus query reaches — either a fixture died or the vocabulary "
            f"grew without one")
for tok in sorted(UNWITNESSED):
    if tok not in vocab:
        bad(f"the pin block declares `{tok}` UNWITNESSED and no such ground "
            f"exists in the vocabulary — a stale exemption")

# ── the census, printed whatever the verdict ─────────────────────────────────
print("── MATERIALIZATION PLANE ─────────────────────────────────────────────")
print(f"  drain {tot['drain']}  sort {tot['sort']}  arrange {tot['arrange']}"
      f"   |  artifact: __it_ {tot['it']}  index {tot['ix']}")
print(f"  hash-join decisions {tot['hj']}   materialize {tot['materialize']}"
      f"   stream {tot['stream']}")
print(f"  no materialization: already-a-buffer {tot['container']}, "
      f"read-once {tot['readonce']}   |  silent {len(silent)}")
print("── GROUND VOCABULARY ─────────────────────────────────────────────────")
for tok in sorted(vocab):
    mark = "·" if tok in UNWITNESSED else " "
    print(f"  {mark} {tok:20s} {witness[tok]:6d}")
print(f"  ({len(UNWITNESSED)} pinned UNWITNESSED — the S2 debt ledger)")

if fail:
    print("---- FAILURES ----")
    for m in fail:
        print("FAIL: " + m)
    sys.exit(1)
print("OK: the refusal census re-derived over %d corpus fixtures — no plan "
      "materializes without a named ground, the drain/sort nodes are the "
      "prelude buffers the artifact builds, and every published ground sentence "
      "is either witnessed or pinned as debt." % NFIX)
sys.exit(0)
PY
# ⚠ NO `exit $?` HERE, DELIBERATELY. The python pass is the LAST command, so this
# script's status IS its status — a real process byte rather than a number this
# file computed and could truncate. The census exits 0 or 1 and nothing else.
