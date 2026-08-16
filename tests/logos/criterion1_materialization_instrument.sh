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
#                      Buffer | HashMap | HashSet | BTreeMap)<…>` in the user dumps.
#
# ⚠⚠ D2's BIND regex ALSO carries an UNDOCUMENTED-until-R-G NAME filter: the
# binding must start with `_`. That is the D4/HashSet shape a THIRD time, on
# the NAME axis instead of the TYPE axis: 2 compiler-emitted collections are
# dropped by it TODAY — `let mut out: Vec<WritEdgeRow>` in `__gs_edges_Db` /
# `__gs_edges_Cfg`, emitted by derive_graph_source.logos inside its quote and
# filled row by row. They are in no D2, no ACC class, no worklist, no census
# FACT (R-G audit finding #1, confirmed complete by the verifier: exactly 2
# corpus-wide). The widening lands ALONE on its own commit per the R-C1 rule
# above; until then every printed percentage is a share of a population that
# excludes these 2, and §7 of the criteria doc counts them in its "79 unnamed"
# rider.
#
# ⚠⚠ `HashSet` WAS MISSING FROM THAT LIST UNTIL R-C1, AND THE MISS WAS 319
# BINDINGS — A WHOLE CONTAINER KIND, counted by no instrument in this tree: not
# D2, not any ACC class, not any census FACT. They are the fixpoint plane's
# NOVELTY structures (`__rs_<m>` the shadow set, `__os_<m>` the over-deletion
# distinct set, bare `__rs`/`__hs` in `_run`) — one hash-set entry per derived
# row, which is a compiler-inserted materialization by any reading of criterion
# 1. The old filter's total was exactly Vec 3028 + HashMap 592 + Buffer 12 =
# 3632, so the miss was clean rather than partial: the type filter dropped the
# class silently and the printed 70.73% was a share of a population that
# EXCLUDED it.
#
# This is §6.4's D4 shape recurring — "a right number with a wrong reason: the
# TYPE filter dropped it" — now at 319×, the second time in this arc. The rule
# it costs: a denominator defined by an ENUMERATION of kinds must be re-derived
# against the artifact's own type histogram, not maintained by hand.
#
# ⚠ R-C1 LANDED ALONE, BEFORE ANY NAMING, and it made every printed number
# WORSE: D2 3632 -> 3951, accounted 2569 UNCHANGED (65.02% from 70.73%),
# worklist 1063 -> 1382. A criterion cannot be closed on the same commit that
# fixes the population it is measured over, so the population moves first and
# the naming stages that follow are measured against the corrected denominator.
#
# ⚠ WHAT THIS FILE READS ON THE TWO TREES OF R-C'S ONE-VARIABLE CONTROL, so the
# next reader can check the script against a number instead of a claim. Control
# = this tree with ONLY `stdlib/mem/wql/rexpr_walk.logos` reverted to `9395c3d1`
# (HEAD = post-R-B — the one-variable control for R-C's uncommitted diff; NOT
# `93295e0c`, whose emitter reads D1 4042 / N1 1336 / accounted 1929 and would
# credit R-C with R-B's 650 `__out` bindings),
# rebuilt, swept with THIS version of this file, restored and re-measured to the
# R-C column afterwards:
#
#            D1     N1              T     D2     accounted        worklist  exit
#   control  4692   1986 (42.33%)   617   3951   2579 (65.27%)    1372      2
#   R-C      5407   2701 (49.95%)   621   3951   3294 (83.37%)     657      0
#
# `D2` at a ZERO delta is the load-bearing row: R-C2 writes only to the trace
# channel (170/170 user dumps byte-identical) and R-C3 is a rename, so the naming
# round built nothing. The control's exit 2 is G4 doing its job — all seven R-C
# classes read a zero fire count there, including `__rds` at 0 BINDINGS, which is
# the rename's own signature; and `__rs` holds 263 on the control (218 fixpoint +
# 45 rel) against 218 here, which is the two-owner ambiguity R-C3 removed.
#
# ⚠ NOT collection bindings, deliberately, and each excluded kind is named so
# the next reader inherits an argument instead of a list: `SliceStream` (1002
# after R-F stage 2 — was 149 pre-R-F, the round's own +853 falsified the
# recorded count while the ARGUMENT held: a view allocates nothing per row, D2
# did not move) and `HashMapKeys` (39) are BORROWS/VIEWS over storage someone
# else owns — no landing, nothing allocated per row; `Option` (26) is a scalar
# cell. ⚠ These parenthetical counts are hand-maintained against a population
# that moves — re-derive them against the artifact histogram whenever a route
# lands (the R-F verifier's F3: stale by 853 on the round that shipped it).
# Adding a
# kind here is a claim that the emitter ALLOCATES per row under that type.
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
    # ── ADR 0025 R-G — THE TWO `Vec` PRELUDE LANDINGS THAT WERE NARRATED AS
    # ABSENCES. R-F left 205 `__rel_*` `Vec` landings unowned and measured their
    # decomposition: 84 fixpoint accumulators, 45 one-shot rel results, 76 true
    # container producers. The first two are collections THIS COMPILER'S CODE
    # builds — `Vec::<R>::new()` grown by the semi-naïve driver, and the rel
    # helper's returned result — and the plan said `no materialization on
    # already a buffer` about both, which is not a missing name but a FALSE one.
    # Fire counts measured at G2 on the emitter-only tree, before either head
    # was classified here: 84 and 45.
    'fixpoint accumulator':
        "`__rfa_<r>` — a recursive SCC's total, grown until the fixpoint closes",
    'rel result landing':
        "`__rls_<r>` — the rel helper's returned container, bound in the prelude",
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
    # ── ADR 0025 R-C2 — THE FIXPOINT PLANE, SIX SEAMS ────────────────────
    # 670 bindings emitted by the `_scc`/`_od`/`_odp` region, none of which
    # had a node before R-C2. S6-B declared the incremental tier out of the
    # BATCH plane; that is a statement about what CONSUMES these rows, and
    # criterion 1 names what EXISTS regardless of which plane consumes it.
    # Fire counts were measured at the emitter before the nodes existed:
    # 222 / 218 / 176 / 46 / 4 / 4.
    'fixpoint frontier':
        "`__dl_<m>` — the epoch's driving delta, rescanned per in-SCC occurrence",
    'fixpoint derived frontier':
        '`__nd_<m>` — rows derived this epoch, appended while `__dl` is scanned',
    'fixpoint novelty set':
        '`__rs_<m>` — SET novelty; the `__rsp.insert(row)` gate decides new',
    'fixpoint novelty lattice':
        '`__best_<m>` — LATTICE novelty; improvement, not membership',
    'fixpoint lattice key roster':
        '`__keys_<m>` — the lattice map has no ordered enumeration surface',
    'over-deletion set':
        "`__os_<m>` — DRed's |OD|, a set because one row has many derivations",
    # ADR 0025 R-C3 — the one-shot rel's dedup set, `__rds`, 1:1 with `rel
    # result`. A SECOND collection beside the landing, not the landing rescanned
    # — which is what tells it apart from `query output distinct carrier`.
    'rel dedup set':
        "`__rds` — the `_run` rel helper's novelty set, beside `__rout`",
    # ── ADR 0025 R-E — THE RETRACTION'S TRANSACTIONAL SNAPSHOT, 3 SEAMS ───
    # `<q>_dred`'s shadow handle, 145 `__cp<i>` bindings — the largest unowned
    # non-`__rel_*` class in the worklist once R-C2 took its former row-mates.
    # Fire counts measured at the emitter BEFORE these heads were classified
    # here (this file, run at G2 on the emitter-only tree): 66 / 57 / 22,
    # summing to the 145 the artifact channel reads. Three heads and not one
    # because the three answer different questions about the SAME `Vec` copy —
    # what it costs and why it exists; see the block at `cp_node` in
    # `stdlib/mem/wql/rexpr_walk.logos`.
    'retraction snapshot relation':
        '`__edb` / `__tot_<m>` — the ROW stores, O(|EDB| + Σ|total|)',
    'retraction snapshot group frame':
        '`__gk` / `__gc` / `__ga_<a>` — the group-frame columns the INSERT path writes in place',
    'retraction snapshot latch':
        "`__lat` — the |OD|/|S|/|RD| triples; O(members), no rows, one per shadow handle",
    # ── ADR 0025 R-E — THE INCREMENTAL TIER'S PER-ROUND WORKING SET ───────
    # 294 bindings — after `__cp` the WHOLE of the criterion-1 worklist outside
    # `__rel_*`. Fire counts measured at G2 on the emitter-only tree, before any
    # of the eight was classified here: 70 / 44 / 44 / 35 / 35 / 22 / 22 / 22.
    # Argument for eight heads (and for not splitting the three shared roles by
    # driver) at `iw_node` in `stdlib/mem/wql/rexpr_walk.logos`.
    'member total working set':
        "`__tt<i>` — the member's working total; drivers APPEND, so the round's rows are its suffix",
    'work counter':
        "`__wcd` — NOT DATA: one i64 in a Vec because the ABI has no scalar out-param; sunk in its own block",
    'derived suffix':
        "`__nw` — the rows this round derived, read off `__tt<fi>`'s suffix; the fold's `+1` half",
    'over-deletion candidates':
        "`__odv<i>` — DRed phase 1's OD_<m>, the ROWS (the `__os_<m>` set beside it holds the COUNT)",
    'removed rows':
        "`__rmv<i>` — what `_cpt` actually took out; the fold's `-1` half, and NOT OD_<m>",
    'epoch input working set':
        "`__ecp` — the epoch's copy of `__h.__edb`, and also the inbound presence gate's dedup structure",
    'preserved input':
        "`__pres` — `__h.__edb` minus the retracted set; what the commit shrinks the input to",
    'latch out-param':
        "`__lt` — NOT DATA: three i64 per member, `_cpt`'s out-param, committed as `__h.__lat`",
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
    # ADR 0025 R-D — `build-side batch pull`. A PULL SHAPE, not a node: it says
    # the join's build phase reads its (already streamed) source a BATCH at a
    # time instead of a row at a time. It builds nothing the row-at-a-time
    # spelling did not build — the same hash index, the same payload — so it is
    # on the NOMAT side. What it replaces is a REFUSAL: before it, a streamed
    # batch source on a build side did not compile at all.
    'build-side batch pull',
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
    # ⚠ ADR 0025 R-H (b′) — `__sv` IS RETIRED WITH ITS SUBJECT, NOT RE-HEADED.
    # The key credited 502 bindings to the head `materialize`, ground "a
    # producer that returns a container". It never described them: `__sv` was
    # the `_stream` FACADE's own temp
    # (`let __sv: Vec<E> = <bare>(…)?;` then `Buffer::from_vec(__sv)`), and
    # `Buffer::from_vec` is a struct literal over a by-value `Vec`
    # (`stdlib/mem/stream/buffer.logos`) — a MOVE. Nothing was materialized at
    # any of the 502 sites, so the credit was the R-F F1 class (the plan
    # asserting a materialization the artifact does not perform) at 12.9% of
    # the accounted numerator. (b′) deletes the temp at the emitter
    # (`emit_stream_surface`), so the key is removed rather than re-grounded:
    # there is no binding left to head. THE PRINTED SHARE FALLS AS A RESULT —
    # an ACCOUNTED class left the denominator AND the numerator — and that is
    # REPORTED, NOT NETTED: D2 and `accounted` both drop by 502 and the
    # WORKLIST (the unaccounted remainder, the number this instrument exists
    # for) does not move at all.
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
    # ADR 0025 R-C1 — THE ARRANGE NODE'S SET FORM, VISIBLE ONLY ONCE `HashSet`
    # ENTERED THE POPULATION. `hash_build_frag`'s `set_form` arm emits
    # `__hs<k>: HashSet<K>` where the payload arm emits `__hm<k>: HashMap<K,
    # Vec<P>>` — one site, one node, two container types. `plan_ground_census_
    # gate.sh` FACT C has always grepped `__(hm|hs|bt)` and read 598 == 598;
    # THIS instrument read 588 against an `arrange` fire count of 598 and the
    # 10-line gap sat in the worklist looking like an unowned class. It was
    # never unowned: it was untyped. No emitter change — R-C1's population fix
    # IS the whole repair, and the identity `__hm + __hs == arrange` now closes
    # here too.
    '__hs':     (('arrange',),              'arrange (the join build side, set form)'),
    # ── ADR 0025 R-C2 — THE FIXPOINT PLANE ───────────────────────────────
    # Five of the six R-C2 heads take a credit here. Every key is measured
    # CLEAN: the exact bare name is emitted zero times and every binding is
    # `<key>_<relname>`, so unlike `__rel_*` below the key cannot be holding
    # two identities. The counts are pinned per fixture and per head by FACT K
    # in `plan_ground_census_gate.sh`, together with the two identities.
    '__dl':     (('fixpoint frontier',),          "fixpoint frontier (the epoch's driving delta)"),
    '__nd':     (('fixpoint derived frontier',),  'fixpoint derived frontier (rows derived this epoch)'),
    '__best':   (('fixpoint novelty lattice',),   'fixpoint novelty lattice (best-per-key improvement map)'),
    '__keys':   (('fixpoint lattice key roster',),'fixpoint lattice key roster (the map\'s enumeration)'),
    '__os':     (('over-deletion set',),          "over-deletion set (DRed's |OD|)"),
    # ── ADR 0025 R-C3 — THE SIXTH FIXPOINT KEY, AND THE DEBT R-C2 RECORDED ─
    # R-C2 landed five of its six heads here and REFUSED the sixth in writing:
    # a key `__rs` would also have matched the 45 BARE `__rs` bindings, which
    # are a different role in a different emitter region (the one-shot rel
    # helper's dedup set in `_run`, 1:1 with `__rout`). Taking the credit would
    # have over-credited 45 rel-dedup sets as fixpoint novelty sets — the
    # `__rel_*` two-owner defect, repeated knowingly.
    #
    # R-C3 paid it the only way that works, which is R-B1's way: RENAME AT THE
    # EMITTER, then name. The `_run` set is `__rds` now, the two classes are
    # separable in the artifact, and both keys below are measured CLEAN (the
    # bare name `__rs` is emitted zero times on this tree). Nothing was created
    # or destroyed: 263 bindings that were one ambiguous row are two owned rows.
    '__rs':     (('fixpoint novelty set',), 'fixpoint novelty set (the member shadow set)'),
    '__rds':    (('rel dedup set',),        'rel dedup set (the one-shot rel helper novelty set)'),
    # ── ADR 0025 R-E — `<q>_dred`'s TRANSACTIONAL SHADOW SNAPSHOT ─────────
    # ⚠ ONE KEY, THREE OWNERS — and unlike `__rel_*` that is SOUND here, which
    # is the distinction this table has to keep making. `__rel_*` is refused
    # because its 217 bindings have owners for 12 of them and NO owner for the
    # other 205: a name-only key would have to credit all 217. `__cp<i>` is the
    # opposite case — EVERY one of the 145 is owned, by exactly one of the
    # three heads below, and the split is by which handle field the index `i`
    # happens to sit at. So the key credits a fully-owned class; it just cannot
    # say WHICH of the three owns any given binding. That per-binding
    # attribution is what `plan_ground_census_gate.sh` FACT L pins (per fixture,
    # plan side against artifact side, with the `latch == 1 per _dred` clause),
    # and it is why the identity lives there rather than being asserted here.
    '__cp':     (('retraction snapshot relation', 'retraction snapshot group frame',
                  'retraction snapshot latch'),
                 "retraction snapshot (`<q>_dred`'s shadow handle — three seam heads)"),
    # ── ADR 0025 R-E — THE PER-ROUND WORKING SET, EIGHT KEYS, ONE HEAD EACH ─
    # ⚠ EVERY KEY MEASURED CLEAN, and the measurement is the class count itself:
    # each of the eight worklist classes stood at EXACTLY the fire count of its
    # head (70/44/44/35/35/22/22/22) before this table named it, so no key is
    # holding a second identity the way `__rel_*` does. That is the check the
    # `__rel_*` note below demands, done per key rather than assumed.
    '__tt':     (('member total working set',),   "member total working set (`__tt<i>`)"),
    '__wcd':    (('work counter',),               'work counter (`__wcd`, one i64, sunk)'),
    '__nw':     (('derived suffix',),             'derived suffix (`__nw`, the fold\'s +1 half)'),
    '__odv':    (('over-deletion candidates',),   'over-deletion candidates (`__odv<i>`, the rows)'),
    '__rmv':    (('removed rows',),               "removed rows (`__rmv<i>`, the fold's -1 half)"),
    '__ecp':    (('epoch input working set',),    'epoch input working set (`__ecp`)'),
    '__pres':   (('preserved input',),            'preserved input (`__pres`)'),
    '__lt':     (('latch out-param',),            'latch out-param (`__lt`, three i64 per member)'),
    # ── ADR 0025 R-F — THE PRELUDE LANDING, SPLIT BY NODE. TWO KEYS, ONE
    # HEAD EACH, AND THE SPLIT IS WHAT MAKES THE CREDIT POSSIBLE AT ALL.
    #
    # R-E recorded the debt in this very table: "the accounted figure is still a
    # FLOOR, by 12" — 12 Buffer-typed landings owned by `drain` (7) and `sort`
    # (5) spelled `__rel_<r>`, a name 205 UNOWNED `Vec` landings also answered
    # to. A name-only key must take all 217 or none, so it took none.
    #
    # R-F paid it the way R-B1 (`__out` -> `__rout`) and R-C3 (`__rs` -> `__rds`)
    # did: RENAME AT THE EMITTER, then name. `push_land_name`
    # (`stdlib/mem/wql/access_plan.logos`) spells the landing from the NODE —
    # `MAT_DRAIN` -> `__rdb_<r>`, `MAT_SORT` -> `__rsb_<r>` — and the `Vec` arms
    # keep `__rel_<r>`. Nothing was created or destroyed: D2 3954 before and
    # after, N1/D1/T unchanged, 161 of 171 user dumps byte-identical and the
    # other 10 differing in exactly 36 lines, every one of which becomes
    # byte-identical when the new prefixes are substituted back.
    #
    # ⚠ TWO KEYS AND NOT ONE KEY WITH TWO OWNERS. `__cp` below is the case for
    # one-key-many-owners: its per-binding attribution is undecidable from the
    # name. Here it IS decidable — the emitter branches on the node when it
    # writes the name — so the stronger form is available and taken: each key
    # carries ONE head, and `plan_ground_census_gate.sh` FACT N pins plan
    # against artifact PER HEAD, per fixture and in total (7 == 7, 5 == 5, and
    # no fixture mixes the two).
    #
    # ⚠ BOTH KEYS MEASURED CLEAN: the exact bare names `__rdb`/`__rsb` are
    # emitted zero times, every binding is `<key>_<relname>`, and the residual
    # count of Buffer-typed `__rel_*` landings is ZERO — so neither key is
    # holding a second identity the way `__rel_*` did.
    '__rdb':    (('drain',), 'drain (the read-twice stream landed in a Buffer)'),
    '__rsb':    (('sort',),  'sort (the imposed order landed in a Buffer, then permuted)'),
    # ── ADR 0025 R-G — THE FIXPOINT ACCUMULATOR, THE FIRST OF THE 205 `Vec`
    # LANDINGS R-F LEFT UNOWNED, AND THE ONE WHOSE GROUND WAS FALSE.
    #
    # R-F's note below records the remaining 205 as "UNOWNED, a weaker and
    # different claim" than the ambiguity it had just removed. The R-F verifier
    # then measured what they are, and 84 of them are not merely unowned: the
    # plan narrated a `let mut …: Vec<R> = Vec::<R>::new()` that the semi-naïve
    # driver fills as `no materialization on already a buffer — the producer
    # hands back a container, which IS a buffer, nothing is built`. There is no
    # producer, nothing was handed over, and the plan is what declares the Vec.
    #
    # ⚠ THE HEAD HAD TO BE DECIDED SOMEWHERE ELSE THAN THE GROUND WAS. The false
    # ground is written by `access_plan_decide_mode`'s `!offers` arm, which runs
    # three passes before `compute_rel_scc` — so at that site "recursive" does
    # not exist and the 84 cannot be told from the 45 one-shot rel results.
    # `plan_mark_rel_landings` re-heads them after the condensation, which is
    # the first point where the kind is known; `push_land_name` spells the
    # binding from the node, so this key exists at all.
    #
    # ⚠ KEY MEASURED CLEAN: the bare name `__rfa` is emitted zero times, every
    # binding is `__rfa_<relname>`, and `plan_ground_census_gate.sh` FACT O pins
    # `#(fixpoint accumulator lines) == #(__rfa_* landings)` per fixture and in
    # total — with a RHS-SHAPE clause beside it, because head and name are both
    # derived from `rel_node` and a mis-route would otherwise move them
    # together (the R-C2 P2 probe's shape, re-run for this stage).
    '__rfa':    (('fixpoint accumulator',),
                 "fixpoint accumulator (a recursive SCC's grown total)"),
    # ── ADR 0025 R-G ARM B — THE ONE-SHOT REL RESULT, AND THE ROW THAT IS
    # COUNTED TWICE ON PURPOSE.
    #
    # 45 landings, 1:1 with `__rout` — the same rows, bound inside the rel
    # helper (`rel result`, credited above) and again outside it where the
    # prelude takes the helper's return value. D2 counts BINDINGS, so both are
    # in it, and this table now owns both. That is a MOVE of one container
    # across a call and not two allocations, and it is recorded here rather than
    # netted out of D2: adjusting the denominator to make a seam disappear is
    # how a population stops being re-derivable (the `HashSet` miss in the
    # header, from the other direction).
    #
    # ⚠ THE INHERITED SENTENCE WAS THE NEARLY-TRUE ONE, which is why this arm
    # was the harder of the two to see: "the producer hands back a container"
    # describes the emitted line correctly and names the wrong producer. The
    # producer is a fn this same compilation emitted from the rel block, so the
    # buffer is the plan's, one frame down — the opposite answer to the question
    # a reader asks the ground (is this the plan's cost or the source's?).
    #
    # ⚠ KEY MEASURED CLEAN: bare `__rls` is emitted zero times, every binding is
    # `__rls_<relname>`, and FACT O pins `#(rel result landing) == #(__rls_*
    # shaped landings)` per fixture, with the cross-frame identity
    # `rel result landing == rel result` (45 == 45) beside it.
    '__rls':    (('rel result landing',),
                 "rel result landing (the helper's returned container, bound outside it)"),
    # ⚠ WHAT REMAINS AFTER R-G, AND IT IS A NAMED ADMISSION RATHER THAN A GAP.
    # R-F left 205 `__rel_*` `Vec` landings unowned and said naming them was the
    # next stage's. R-G measured what they were and split them three ways: 84
    # fixpoint accumulators and 45 one-shot rel results, both of which are
    # collections THIS COMPILER BUILDS and both of which carried a FALSE ground
    # (`already a buffer`) — they have heads and keys above; and 76 that are
    # what the ground always said, a NATIVE source whose producer returns a
    # container the query compiler did not build.
    #
    # THOSE 76 KEEP `__rel_<r>` AND STAY IN THE WORKLIST, DELIBERATELY. A head
    # here would be a claim that the plan performed a materialization, and it
    # did not: the prelude binds a value the source handed over. The honest
    # reading of criterion 1 over this tree is therefore "MET WITH NAMED
    # ADMISSIONS" — 3877 of 3954 owned (98.05%), the remainder being these 76
    # plus the one `__cv` refused below, each with a ground that is TRUE.
    #
    # ⚠ THE ADMISSION IS FALSIFIABLE, which is what separates it from the
    # unowned rows it replaces: it rests on every remaining `__rel_<r>` being a
    # native container producer's binding. `plan_ground_census_gate.sh` FACT O
    # pins that from the other side — a `Vec` landing spelled `__rel_<r>` whose
    # RHS is `Vec::<…>::new()` or a `__wql_…_rel_…` call is RED (0 and 0), so a
    # plan arm that quietly rejoined this class cannot hide inside the 76.
}
# ── ADR 0025 R-E — `__cv`, 1 BINDING, REFUSED WITH ITS GROUND ───────────────
# After the R-E stages the worklist is 218: `__rel_*` 217 (refused, see the note
# below) and `__cv` 1. `__cv` is REFUSED TOO, on a different and stronger
# ground, and it is written here rather than silently owned because a refusal
# that is not stated reads exactly like a class nobody looked at.
#
# `__cv` is emitted by `emit_comp` (`stdlib/mem/wql/codegen.logos`), the emitter
# for the `SComp` SYNTAX NODE — a LIST COMPREHENSION the user wrote,
# `[HEAD for V in SRC if GUARD]`. Its emitted form is
# `({ let mut __cv: Vec<T> = …; while … { __cv.push(HEAD) } __cv })`: the
# landing IS the value the expression denotes. Criterion 1 asks about
# COMPILER-INSERTED materializations — collections the planner put between a
# producer and a consumer that the program did not ask for. A comprehension's
# result is not one of those; naming it with a plan node would announce as a
# planner decision something the user typed.
#
# ⚠ THE REFUSAL IS FALSIFIABLE, which is what separates it from a shrug: it
# rests on `__cv` being emitted ONLY from `emit_comp`, i.e. only under a source
# `SComp`. Grep the emitter — one declaration site, in that function, reached
# from nowhere else. The day a planner path emits a `__cv`, or the day a
# comprehension is fused into its consumer so that the `Vec` becomes optional,
# this sentence is false and the class comes back here as an owner.
#
# ⚠ ACC IS KEYED ON THE NAME ALONE, AND THE KEY CANNOT EXPRESS TWO OWNERS —
# WHICH IS WHY R-F MOVED THE EMITTER RATHER THAN WIDENING THE KEY. Through R-E
# `drain` and `sort` landed into `__rel_<x>` bindings and so did UNOWNED Vec
# landings under the same names (217 `__rel_*`, of which `__rel_s` was 7 Buffer
# + 1 Vec and `__rel_m` 3 Buffer + 6 Vec — the collision was at the IDENTICAL
# name, not merely a shared prefix). A name-only key had to take all 217 or
# none, so it took none and the printed ACCOUNTED was a FLOOR by 12.
#
# R-F removed the ambiguity at its source (`push_land_name`, keyed on the plan
# node), so the two Buffer classes are now `__rdb_*` / `__rsb_*` and take their
# credits above. The 205 `Vec` landings that remain are not ambiguous any more —
# they are UNOWNED, a weaker and different claim, and they stay in the worklist
# until a head exists for them. The site-level reading and the printed figure
# now agree; there is no residual floor to subtract.
BIND = re.compile(r'\blet\s+(?:mut\s+)?(__?[A-Za-z_0-9]+?)\d*\s*:\s*(Vec|Buffer|HashMap|HashSet|BTreeMap)\s*<')
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
