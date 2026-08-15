#!/usr/bin/env bash
# criterion1_materialization_instrument.sh OUTDIR [LOGOSC] [PASSDIR]
#
# ADR 0025 CRITERION 1'S INSTRUMENT — PROMOTED FROM THE SANDBOX, WITH ITS
# NUMERATOR AND DENOMINATOR DEFINED HERE RATHER THAN IN A COMMIT MESSAGE.
#
# Criterion 1 (Victor, 2026-08-11): «No intermediate materialization inside
# Deem.» Operationally, as S2 fixed it: EVERY compiler-inserted materialization
# is a NAMED PLAN NODE WITH A GROUND — the identity a reader can ask about, and
# the identity `explain()` prints.
#
# ── WHY THIS FILE EXISTS ───────────────────────────────────────────────────
#
# S2-S5 each recorded "instrument validated by reproducing its recorded
# denominator exactly", and the S5 audit found (F6, CONFIRMED) that the
# instrument was NOWHERE IN THE REPOSITORY: `609/4,023 = 15.14%` lived only in
# commit prose, and the auditor could not reproduce 4,023 under any reading of
# "emitted collection bindings" (nearest 3,312). A number no one can re-derive
# is not a measurement. This script IS the definition; the numbers it prints are
# whatever this tree says today.
#
# ── THE TWO CHANNELS. THEY ARE NOT THE SAME MEASUREMENT ────────────────────
#
# TRACE CHANNEL (`LOGOS_TRACE_PLAN=1`, stderr): what the plan SAYS. One
#   `[plan] <binding> -> <head>   (<ground>)` line per decision.
#     NUMERATOR   N1 = `[plan]` lines whose HEAD is a materializing node kind
#                      (the MAT list below) — a compiler-inserted collection that
#                      has a name and a ground.
#     DENOMINATOR D1 = all `[plan]` lines — every decision the plan narrates.
#   N1/D1 is the share of the plan's own sentences that name a materialization.
#   It is NOT "the share of materializations that are named": nothing on this
#   channel can see a materialization the plan never mentions.
#
# ARTIFACT CHANNEL (`--gen-dir` dumps, family definitions `logos.gen.*`
#   EXCLUDED): what the emitter BUILT.
#     DENOMINATOR D2 = emitted collection bindings — `let [mut] <n>: (Vec |
#                      Buffer | HashMap | BTreeMap)<…>` in the user dumps.
#   D2 is partitioned by BINDING-NAME CLASS into ACCOUNTED (a class whose owner
#   plan node is named, listed in ACC below) and UNACCOUNTED. The unaccounted
#   classes, printed largest-first, ARE the criterion-1 worklist — that list is
#   the honest reading of "no intermediate materialization", and each entry is
#   either an owner or an admission.
#
# ⚠ THE HISTORICAL NUMBER IS REPRODUCED AND THEN TAKEN APART. `609` was
#   `[plan]` lines matching the TEXT "materializ". This script prints that number
#   too (T), with its composition — because the majority of it is not
#   materializations at all: it mixes POSITIVE `-> materialize` nodes with
#   `-> no materialization` ABSENCES and with `key vector` grounds whose
#   parenthetical says "not a second materialization". A text ratio over ground
#   SENTENCES is not a count of materializations, and the ADR's S6 writ control
#   is the measured case where the two moved in OPPOSITE directions (the text
#   ratio "improved" 668 -> 638 while the artifact materialized 29 times more).
#
# ── GATE OR INSTRUMENT? INSTRUMENT, AND HERE IS THE REASON ─────────────────
#
# N1, D1 and D2 are corpus-size-dependent counts: one added fixture moved D1 by
# +10 and N1 by +3 within S6 alone, and every slice that renames a node or moves
# a materialization moves them by construction. A ctest gate over such a number
# is either re-baselined every commit (a number that always agrees) or it
# becomes a target to tune (and the writ control above shows tuning it can move
# it the wrong way). So the VALUES are reported, not gated.
#
# What IS gated — inside this script, exit 2 — are the three properties that do
# NOT move with a slice:
#   G1  every `materializ` occurrence in the trace lies on a `[plan]` line
#       (the text reading and the structured reading see the same population);
#   G2  every `[plan]` HEAD is classified by the table below — a slice that adds
#       a node kind MUST come here and say which side it is on, instead of
#       silently landing in "not a materialization";
#   G3  no probe was lost: one rc file per fixture, count asserted.
#   G4  every ACC credit is WITNESSED: the owning head fired at least once in
#       THIS sweep's trace. See the block below — this one was added because a
#       control proved the credit was previously unfalsifiable.
#
# ⚠⚠ G4, AND WHY `accounted` COULD NOT BE READ AS A TREE MEASUREMENT BEFORE IT
# (R-B, 2026-08-15 — found by the control, not by inspection). `ACC` used to map
# a binding-name class to a PROSE STRING, and `acc_n` summed the classes whose
# name appeared as a key. Nothing anywhere connected that credit to the trace:
# the artifact channel asked "is this name in my table", never "did the plan node
# I am naming actually get emitted". So the number moved when the TABLE moved,
# on a tree where nothing had changed.
#
# That is not a hypothesis. R-B's control tree — the pre-R-B emitter, rebuilt,
# swept with the post-R-B table — printed `accounted=2569 (70.73%)`, IDENTICAL to
# the R-B tree, with `__out` credited 650 for `query output` while all four
# query-output heads had a fire count of ZERO on that same sweep. The instrument
# printed a 650-binding ownership claim and the evidence against it in the same
# report, three lines apart, and summed the claim anyway.
#
# The delta the R-B stage first recorded for itself (`accounted` 1919 -> 2569)
# was therefore a comparison of two SEPARATELY-MEASURED columns — old tree with
# old table against new tree with new table — attributed entirely to the tree.
# The tree-vs-tree delta was ZERO. The fix is to make the credit depend on
# something the ledger's author does not control: `ACC` now names the OWNING
# HEAD, that head must be in `MAT`, and a class whose owners all have a zero fire
# count is REFUSED — reported under CLAIMED BUT UNWITNESSED and counted in the
# worklist, where an unowned collection belongs. With that in place the same two
# trees read 1919 (52.84%) and 2569 (70.73%), and the difference is the EMITTER.
#
# ⚠ A tree older than the table now exits 2 rather than printing a flattering
# number, and that is the intended direction: the ledger is dated, and a credit
# whose node is not in the tree being measured is a claim about a different tree.
# The numbers still print in full before the exit, so a control remains readable.
#
# EXIT 0 measured (values on stdout) · 2 the instrument could not measure, or
# G1/G2/G3/G4 failed.
set -uo pipefail
OUT="${1:?outdir}"
REPO=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
LOGOSC="${2:-$REPO/build/bin/logosc}"
TESTS="${3:-$REPO/tests/logos/pass}"
export LOGOS_LIB_DIR=${LOGOS_LIB_DIR:-$REPO/build/lib/logos}
export LC_ALL=C

[ -x "$LOGOSC" ] || { echo "FAIL(2): no logosc at $LOGOSC"; exit 2; }
rm -rf "$OUT"; mkdir -p "$OUT/_st"
shopt -s nullglob
FIXTURES=("$TESTS"/wql_*.logos "$TESTS"/deem_*.logos)
if [ "${#FIXTURES[@]}" -lt 20 ]; then echo "FAIL(2): ${#FIXTURES[@]} fixtures — population lost"; exit 2; fi

one() {
    local f="$1" OUT="$2" LOGOSC="$3"
    local b; b=$(basename "$f" .logos)
    local d; d=$(mktemp -d)
    LOGOS_TRACE_PLAN=1 "$LOGOSC" "$f" --gen-dir "$d/gen" -o "$d/o.o" \
        > "$d/out" 2> "$OUT/$b.err"
    echo "$?" > "$OUT/_st/$b"
    shopt -s nullglob
    local U=() x
    for x in "$d"/gen/*.gen.logos; do
        case "$(basename "$x")" in logos.gen.*) ;; *) U+=("$x");; esac
    done
    [ "${#U[@]}" -ge 1 ] && cat "${U[@]}" > "$OUT/$b.user"
    rm -rf "$d"
}
export -f one
printf '%s\0' "${FIXTURES[@]}" | xargs -0 -P "$(nproc)" -I{} bash -c 'one "$@"' _ {} "$OUT" "$LOGOSC"

ST=("$OUT"/_st/*)
if [ "${#ST[@]}" -ne "${#FIXTURES[@]}" ]; then
    echo "FAIL(2) G3: ${#ST[@]} rc files for ${#FIXTURES[@]} probes — probes were lost."; exit 2
fi
UD=("$OUT"/*.user)
RED=0; for s in "${ST[@]}"; do [ "$(cat "$s")" = 0 ] || RED=$((RED+1)); done
echo "population: ${#FIXTURES[@]} fixtures, ${#UD[@]} with user dumps, $RED non-compiling"

python3 - "$OUT" <<'PY'
import re, sys, glob, collections, os
OUT = sys.argv[1]

# ── THE CLASSIFIER, TRACE CHANNEL ──────────────────────────────────────────
# MAT: heads that name a COMPILER-INSERTED COLLECTION.
MAT = {
    'materialize':        'the producer returns a container, not an iterator',
    'arrange':            'the join builds a keyed index over the build side',
    'key vector':         "the sort's `__ks` + the index vectors it permutes",
    'group frame':        'one row per distinct group key',
    'accumulator':        'one cell per group per aggregate',
    'group count':        "avg's denominator, bumped per folded row",
    'representative row': 'the group carries a base-row ordinal to re-bind',
    'drain':              'a stream landed because the plan reads it twice',
    'sort':               'the order is imposed rather than carried',
    # ── ADR 0025 R-B2 — THE OUTPUT SEAM, FIVE-VALUED ─────────────────────
    # The query output landing. It was the largest UNOWNED class in the
    # artifact channel below (650 `__out` bindings) and had no node at all;
    # S5 declined to give it one because with `direct` blocked the answer
    # would have been one constant word on every site. It is not one word:
    # the five heads below are the five distinguishable seams, and R-B0
    # measured every one of them by fire count BEFORE the node existed
    # (477 / 16 / 5 / 107 / 45, summing to the 650). A head here is a
    # POSITIVE claim about a collection that exists — not an absence
    # counted as coverage, which is the defect `T` carries.
    'query output':
        'the landing a query returns; buffered while `direct` is refused',
    'query output bounded by limit':
        'the landing is READ — `__out.len()` is the limit guard operand',
    'query output distinct carrier':
        'the landing IS the dedup structure; `distinct` rescans per push',
    'incremental snapshot output':
        "the incremental tier's read surface; no `_stream` door of its own",
    'rel result':
        'an internal seam consumed by the enclosing query, never returned',
}
# NOMAT: heads that decide something else. `no materialization` is an ABSENCE
# and belongs HERE — counting it as coverage is the historical defect.
NOMAT = {
    'no materialization', 'scan', 'size', 'size unknown', 'sizes deferred to run',
    'hash join', 'loop join', 'drive fixed', 'drive either side', 'drive',
    'order admitted', 'order admitted but not carried', 'order refused',
    'order search', 'stream', 'prepared plan', 'EMITTED', 'PURE', 'REP',
    'declined', 'steps_from', 'rung_from', 'btree_from', 'btree_at', 'btree_upto',
    'hashmap_at', 'incremental',
}
def head_of(line):
    m = re.match(r'^\[plan\] (.+?) -> (.*?)(?:   \(|$)', line)
    if not m: return None
    h = m.group(2)
    h = re.sub(r' on .*$', '', h)              # ` on <subject>`
    h = re.sub(r' \[.*$', '', h)               # ` [a range]` / ` [every row]`
    h = re.sub(r' when driven from .*$', '', h)
    h = re.sub(r' in `.*$', '', h)
    h = re.sub(r'\s*\d+ of \d+$', '', h)
    h = h.strip()
    if h.startswith('__ctr_b') or h.startswith('__ctr_leafbatch'): h = 'ctr access path'
    return h

plan_lines, mat_txt_lines, stray_txt = [], [], 0
for p in glob.glob(os.path.join(OUT, '*.err')):
    for ln in open(p, errors='replace'):
        ln = ln.rstrip('\n')
        if ln.startswith('[plan]'):
            plan_lines.append(ln)
            if 'materializ' in ln: mat_txt_lines.append(ln)
        elif 'materializ' in ln:
            stray_txt += 1

heads = collections.Counter()
unknown = collections.Counter()
N1 = 0
for ln in plan_lines:
    h = head_of(ln)
    if h is None: unknown['<unparsed>'] += 1; continue
    heads[h] += 1
    if h in MAT: N1 += 1
    elif h in NOMAT or h == 'ctr access path': pass
    else: unknown[h] += 1

D1 = len(plan_lines)
T  = len(mat_txt_lines)
tcomp = collections.Counter(head_of(l) or '<unparsed>' for l in mat_txt_lines)

print()
print("── TRACE CHANNEL ──────────────────────────────────────────────────────")
print(f"  D1  all [plan] ground sentences ............................. {D1}")
print(f"  N1  named MATERIALIZATION nodes ............................. {N1}   ({100.0*N1/D1:.2f}% of D1)")
for k in sorted(MAT, key=lambda k: -heads.get(k, 0)):
    print(f"        {heads.get(k,0):6d}  {k}")
print(f"  T   historical text match ('materializ' anywhere on the line)  {T}")
print("      ⚠ its composition — the reason T is NOT a count of materializations:")
for k, v in tcomp.most_common():
    tag = 'MATERIALIZATION' if k in MAT else ('ABSENCE' if k == 'no materialization' else 'incidental mention')
    print(f"        {v:6d}  {k:<22s} {tag}")

if stray_txt:
    print(f"FAIL(2) G1: {stray_txt} 'materializ' occurrence(s) OFF the [plan] channel —")
    print( "            the text reading and the structured reading no longer see one population.")
    sys.exit(2)
if unknown:
    print("FAIL(2) G2: unclassified [plan] head(s) — a node kind landed without saying")
    print("            which side of criterion 1 it is on. Add it to MAT or NOMAT here:")
    for k, v in unknown.most_common(20): print(f"            {v:6d}  {k}")
    sys.exit(2)

# ── ARTIFACT CHANNEL ───────────────────────────────────────────────────────
# ACC: binding-name class -> (the OWNING PLAN HEADS, the prose). Anything else
# is the criterion-1 worklist.
#
# ⚠ THE FIRST FIELD IS THE POINT AND IT IS NOT DECORATION. It is the head this
# class claims as its owner, and G4 below refuses the credit unless that head
# actually FIRED in this sweep's trace. Before R-B this column did not exist and
# the credit was a prose string — see the G4 block in the header for the control
# that measured what that was worth (650 bindings credited to a node with a fire
# count of zero, on a tree where the node did not exist). Every head named here
# must also be a key of `MAT`; G4 asserts that too, so a typo here refuses the
# class rather than silently crediting it.
ACC = {
    '__hm':     (('arrange',),              'arrange (the keyed index)'),
    '__ks':     (('key vector',),           'key vector (the sort keys)'),
    '__ix':     (('key vector',),           'key vector (the permuted index vectors)'),
    '__g_key':  (('group frame',),          'group frame (the group key column)'),
    '__ga':     (('accumulator',),          'accumulator (per-group accumulator columns)'),
    '__g_cnt':  (('group count',),          "group count (avg's denominator)"),
    '__g_row':  (('representative row',),   'representative row (the group\'s base-row ordinal)'),
    '__sv':     (('materialize',),          'materialize (a producer that returns a container)'),
    # ADR 0025 R-B — the output seam. TWO keys, not one, and the split is the
    # whole point of R-B1: before it, the rel one-shot's landing was also
    # spelled `__out`, so one row held 605 query outputs and 45 rel results and
    # any owner claimed for it would have over-credited the 45 — the same
    # name-only-key defect the `__rel_*` note below records. R-B1 renamed the
    # rel landing to `__rout` (the query-side `__out` survives only as a BORROW
    # alias, which this regex does not match), and R-B2 gave each its node.
    # ⚠ Unlike `__rel_*`, these two keys are CLEAN: every `__out` binding is a
    # query output and every `__rout` is a rel result, and the identity is
    # gated per fixture and in total in `plan_ground_census_gate.sh` FACT J.
    '__out':    (('query output', 'query output bounded by limit',
                  'query output distinct carrier', 'incremental snapshot output'),
                 'query output (the four query-side output-seam heads)'),
    '__rout':   (('rel result',), 'rel result (the one-shot rel helper landing)'),
}
# ⚠ ACC IS KEYED ON THE NAME ALONE, AND THE KEY CANNOT EXPRESS TWO OWNERS.
# `drain` and `sort` land into `__rel_<x>` bindings — but so do UNOWNED Vec
# landings under the same names (217 `__rel_*` bindings today; `__rel_g` 35,
# `__rel_r` 17, `__rel_w` 17, `__rel_path` 15, …). The owned ones are the
# BUFFER-typed 12 (`__rel_s` 7, `__rel_m` 3, `__rel_t` 1, `__rel_d` 1 =
# drain 7 + sort 5, the identity `plan_ground_census_gate.sh`
# FACT B pins). A name-only key would have to take all 217 or none, so it takes
# none and the 12 are counted as worklist. The printed ACCOUNTED is therefore a
# FLOOR: the site-level reading is 12 higher. Closing this needs the per-node
# attribution recorded as OPEN in the criteria doc §4d, not a wider name key.
BIND = re.compile(r'\blet\s+(?:mut\s+)?(__?[A-Za-z_0-9]+?)\d*\s*:\s*(Vec|Buffer|HashMap|BTreeMap)\s*<')
cls = collections.Counter()
D2 = 0
for p in glob.glob(os.path.join(OUT, '*.user')):
    for ln in open(p, errors='replace'):
        m = BIND.search(ln)
        if not m: continue
        D2 += 1
        n = m.group(1)
        key = next((a for a in ACC if n == a or n.startswith(a + '_')), None)
        cls[key or n] += 1

# ── G4: A CREDIT IS ONLY A CREDIT IF ITS OWNER FIRED ───────────────────────
# `witnessed` is decided by THIS sweep's trace channel (`heads`), which the
# ledger's author does not control — that independence is the whole repair.
# A class whose owning heads all read zero is REFUSED: it is reported apart and
# it falls into the worklist, which is where an unowned collection belongs.
badhead = sorted({h for _, (hs, _) in ACC.items() for h in hs if h not in MAT})
unwitnessed = {k: hs for k, (hs, _) in ACC.items()
               if not any(heads.get(h, 0) for h in hs)}
OWNED = {k for k in ACC if k not in unwitnessed}

acc_n = sum(v for k, v in cls.items() if k in OWNED)
refused_n = sum(v for k, v in cls.items() if k in unwitnessed)
print()
print("── ARTIFACT CHANNEL ───────────────────────────────────────────────────")
print(f"  D2  emitted collection bindings (family defs excluded) ...... {D2}")
print(f"      ACCOUNTED (a named plan node owns the class, and it FIRED)  {acc_n}   ({100.0*acc_n/D2:.2f}%)")
for k in sorted(OWNED, key=lambda k: -cls.get(k, 0)):
    hs = ACC[k][0]
    print(f"        {cls.get(k,0):6d}  {k:<10s} {ACC[k][1]}  [{sum(heads.get(h,0) for h in hs)} plan line(s)]")
if unwitnessed:
    print(f"      ⚠ CLAIMED BUT UNWITNESSED — counted as worklist ......... {refused_n}")
    print( "        the ledger names an owner this sweep's trace never emitted; the")
    print( "        credit is REFUSED (G4) rather than summed. See the header.")
    for k in sorted(unwitnessed, key=lambda k: -cls.get(k, 0)):
        print(f"        {cls.get(k,0):6d}  {k:<10s} owner head(s) {', '.join(unwitnessed[k])} — fire count 0")
print(f"      UNACCOUNTED — THE CRITERION-1 WORKLIST .................. {D2-acc_n}")
for k, v in sorted(((k, v) for k, v in cls.items() if k not in OWNED), key=lambda kv: -kv[1])[:20]:
    print(f"        {v:6d}  {k}")
print()
print(f"SUMMARY  N1/D1={N1}/{D1}={100.0*N1/D1:.2f}%  T={T}  D2={D2} accounted={acc_n} "
      f"({100.0*acc_n/D2:.2f}%)")

if badhead:
    print()
    print("FAIL(2) G4: ACC names owning head(s) that are not in MAT — a credit")
    print("            cannot rest on a head this instrument does not classify as")
    print("            a materialization:")
    for h in badhead: print(f"            {h}")
    sys.exit(2)
if unwitnessed:
    print()
    print(f"FAIL(2) G4: {len(unwitnessed)} ACC class(es) claim an owner with a ZERO fire")
    print( "            count on this tree — the ledger is describing a different tree.")
    print( "            The values above are measured and complete; only the CREDIT is")
    print( "            refused. Either the stage that emits the node is not in this")
    print( "            tree, or the entry was never true.")
    for k in sorted(unwitnessed):
        print(f"            {k:<10s} {cls.get(k,0):5d} binding(s), owner(s) {', '.join(unwitnessed[k])}")
    sys.exit(2)
PY
rc=$?
exit "$rc"  # lint:exit-ok — `rc` is python3's wait status, a real byte (0 measured, 2 G1/G2 red or cannot measure)
