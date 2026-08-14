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
# exemption, because the green then vouches for it.
#
# ⚠⚠ THE MATERIALIZATION HALF OF THE DEBT LEDGER IS EMPTY AS OF S2h, AND ITS FOUR
# ENTRIES LEFT IT BY TWO DIFFERENT ROUTES. It read `MG_REL_BLOCK` /
# `MG_UNDECIDED` / `MG_GPATH` / `MG_UNPROVEN` — the grounds
# `plan_mark_single_pass` carries that no fixture had driven through a node, and
# the S2 rule was that the function could not be deleted until the set was empty.
#
#   THREE WERE DEBT AND ARE NOW WITNESSED — `MG_REL_BLOCK`, `MG_UNDECIDED`,
#   `MG_UNPROVEN`, all three by `pass/deem_mat_ground_witness.logos`, the one
#   fixture this census has ever been given. WHY 175 CORPUS FIXTURES COULD NOT
#   REACH THEM AND ONE FILE COULD: all three require a source whose producer
#   OFFERS AN ITERATOR, because `access_plan_decide_mode` answers `MG_CONTAINER`
#   first for everything else — and every `rel` block, `&Writ` graph and `&[T]`
#   param in the corpus is a container. The fixture pairs an iterator source
#   (`MapSource::rel entry`) with a `rel` block, with a traversal step, and with a
#   query that never names it.
#
#   ONE WAS NOT DEBT — `MG_GPATH` IS DELETED, REFUTED RATHER THAN WITNESSED.
#   `desugar_program_gpaths` (`wql.logos:177`) runs BEFORE `walk_program_params`
#   and clears `has_gpath` on every query kind (Simple's entry node is REPLACED by
#   an `RQJoin`; Join and Aggr assign `false` in place), so the two arms of
#   `plan_mark_single_pass` that read it could not execute. CONTROL, both
#   directions, one at a time, restored to a byte-identical source with a green
#   checkpoint between: `error(…)` in both gpath arms ⇒ the corpus is UNCHANGED
#   (159 dumps / 6,776,657 bytes, `diff -rq` EMPTY, the same two pinned compile
#   failures) across the ~20 graph-path fixtures that exercise the sugar; the SAME
#   edit in the `has_order` arm one line up ⇒ 158 dumps / 3 failures / −111,952
#   bytes, which is what says the zero was measured rather than inherited from a
#   stale build. A ground nothing can reach is not a fixture someone owes — it is
#   a justification for a decision the compiler never makes, and carrying it as
#   debt would have held a census line open forever.
#
# ⚠ THE FIXTURE IS NOT THIS GATE GRADING ITS OWN HOMEWORK, and the guard is
# stated rather than asserted. A fixture written to make a census line green would
# assert the TRACE and nothing else, and would pass over an emitter that
# materialized the wrong thing or nothing at all. Each of the three queries has a
# RUNTIME oracle over the emitted artifact — the row sequence, and the PULL COUNT
# of an iterator that has no length and cannot be read twice. The third one's
# oracle found a real cost rather than confirming one: `w_unproven` pulls six rows
# out of a source its query never names, because `plan_mark_single_pass` answers
# only for the sources the ENTRY QUERY NAMES while the prelude materializes every
# source the natspec REGISTERED. `MG_UNPROVEN` is the plan's honest account of
# that drain, and it is pinned here, not fixed in passing: removing the drain is
# an emitter change with its own artifact delta.
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
#   FACT C  THE ARRANGE DEFICIT IS CLOSED — 596 == 596, per fixture and in
#           total (594 == 594 at S2d; S2h added one fixture that builds two
#           indexes, and the EQUALITY is what carried the addition unremarked).
#           ⚠⚠ THIS CLAUSE CHANGED IN S2d AND BOTH THE NUMBER AND THE
#           SHAPE OF THE ASSERTION MOVED; here is the whole delta.
#
#           BEFORE (S2b): 31 arrange nodes against 594 emitted index bindings,
#           the gap PINNED as accounted debt, and the per-fixture assertion was
#           the inequality `arrange <= hash joins`.
#           AFTER  (S2d): 594 arrange nodes against 594 emitted index bindings,
#           and the per-fixture assertion is the EQUALITY `arrange == emitted
#           index bindings`.
#
#           WHY THE NUMBER MOVED — the debt was two classes and both were one
#           defect. The Arrange node used to be authored by the PLANNER
#           (`AccessPlan::arrange`), keyed by the REL REGISTRY index, one entry
#           per join STEP. So it could not count the carried nests (the emitter
#           writes one build phase per step PER NEST, and `decide_join_orders`
#           does not run until emission — after the plan is finished), and it
#           could not see a source that is not a rel AT ALL: a query over wql!
#           `&[T]` PARAMS registers zero rels, so `si >= 0` gated every node out
#           and the plan said nothing while the artifact built indexes —
#           `wql_key_spelling_e2e`, 0 node lines against 44 emitted indexes. The
#           node is now authored at the build phase (`join_sel::arrange_node`),
#           keyed by the source the registry names when it can and by the
#           source's own name when it cannot. That is the whole 31 → 594.
#
#           WHY THE ASSERTION'S SHAPE MOVED, STATED AS A LOSS AND A GAIN. The
#           old inequality's premise — one strategy decision per built index —
#           IS FALSE, and this gate's own header asserted it: measured,
#           `wql_join_order_multi_e2e` builds 27 indexes under 24 `hash join`
#           lines, because a step decided at emission is traced once and built
#           once per nest. So the inequality could not be kept truthfully. What
#           replaces it is tighter (an equality, per fixture, on the exact count)
#           but it is now TRUE BY CONSTRUCTION: `arrange_node` fires under
#           `build_phase_frag`'s own emitting condition. Said plainly rather than
#           left for a reader to discover — this clause is no longer an
#           independent oracle for "the plan's account matches the artifact". Its
#           independent oracles are the per-fixture LITERALS in
#           `plan_nodes_gate.sh` (`d` arranged 3 times over one source named
#           twice, `h` once) and the total pinned here, which reds if a build
#           phase is emitted from a fourth site that forgets to name its node.
#
#   FACT E  EVERY SORT-KEY VECTOR IS A NAMED KEY COLLECTION — #(`key vector`
#           lines) == #(emitted `let mut __ks`), per fixture and in total (123).
#           NEW IN S2d, and it is the other half of the same blind class: 119 of
#           these vectors sat in 48 fixtures against THREE Sort nodes, because a
#           sort over a slice-param source has no rel to hang a node on. ⚠ IT IS
#           NOT COUNTED AS A NODE, deliberately: `__ks` is the Sort's KEY
#           COLLECTION, not a second materialization — `deem_batch_scan_drain`
#           emits 2 `__ks` against 2 Sort nodes and 3 `__ix0`, the third
#           permutation being the aggregate's group-row one, which has NO key
#           vector. So `__ks` witnesses a sort scaffolding EXACTLY where `__ix0`
#           over-counts, and it is reported as the Sort's field (key type,
#           index-vector arity) under its own verdict word. The prelude Sort
#           (FACT B's 3, part of the 7 `__it_`) is a DIFFERENT thing — the
#           ordered drain of a SOURCE — and conflating the two is what left 119
#           vectors unclassed.
#
#   ⚠ THE OTHER NUMBERS THAT MOVED WITH S2d, AND WHY. `already a buffer`
#     187 → 203 and `read once` 15 → 18. Neither is a behaviour change: those
#     lines are `explain()`'s POSITIVE absence sentences, printed for a rel with
#     no node of its own, and a rel whose ONLY node was the planner-side arrange
#     used to suppress them. With the arrange node authored at emission those
#     rels now say what they build in the prelude — nothing — which is the
#     sentence the absence line exists to say. `drain` 4, `sort` 3, `__it_` 7 and
#     `hash join on` 491 are UNCHANGED: S2d touched no prelude decision.
#
#   ⚠ S2f / S2g — THE SHAPE READINGS DIED AND NOT ONE PINNED NUMBER MOVED. Said
#     here explicitly, because "the gate is unchanged" is exactly what a stage
#     that did nothing also looks like, and the pin block must never be able to
#     mean two things.
#
#     WHAT CHANGED. The three emitter sites that inferred a scan's SHAPE from
#     `rel_stream` now ask the PLAN'S NODE, through one predicate that lives with
#     the field: `MacroParams::rel_src_unmaterialized` (`params.logos`), which is
#     `rel_src_node(src) == AD_NONE()`.
#       • `rexpr_walk::emit_simple`      — the streamed-scan arm (S2f)
#       • `rexpr_walk::analyze_chain`    — `st_stream[i]` (S2g)
#       • `rexpr_walk::chain_nest_frag`  — `base_streams` (S2g)
#     The prelude was already node-driven (S2b). `rel_stream`/`rel_iter` survive
#     at four sites that read a DECISION and not a shape, each now carrying the
#     S1 gate's correction at the line: `join_order::prep_size_reason` and
#     `::run_size_expr_of` (the size axis), `rexpr_walk`'s incremental-fold
#     decline, and the source-vs-scalar readings of `is_slice`.
#
#     WHY NO NUMBER MOVED, AS AN ARGUMENT AND NOT A HOPE. `plan_apply_access`
#     walks `ri < ap.n` with `ap.n == prm.rel_n` (`access_plan_new(prm.rel_n)`,
#     `plan_walker:1890`), runs on EVERY emission path after the last `rel_add`,
#     and writes `rel_node[ri] = AD_NONE()` under exactly the `ap.stream[ri]`
#     that also sets `rel_stream[ri] = true`. So over every rel a plan
#     registered the two predicates are one predicate. They differ only on a
#     name that is NO REL — where the node answers `AD_BUFFER` (a slice param IS
#     a buffer) and the boolean's `false` was an accident of its default. No
#     trace line, no ground and no artifact binding is derived from either, so
#     drain 4 / sort 3 / arrange 594 / hash join 491 / index 594 / `__ks` 123 /
#     container 203 / read-once 18 all stand.
#
#     THE ORACLE THAT MAKES THAT FALSIFIABLE IS NOT THIS GATE. It is the corpus
#     snapshot (`/home/logos/sandbox/wql_corpus_snapshot.sh`): 159 dumps /
#     6,776,657 bytes before and after each of the two stages, `diff -rq` EMPTY,
#     rc 0 from a redirect. Measured en route and worth keeping: adding
#     `use logos.std.wql.access_plan;` to `rexpr_walk.logos` so the call site
#     could spell `AD_NONE()` moved 156 of the 159 dumps (+85,994 bytes) — a
#     generated module inherits the EMITTING module's use-list verbatim. That is
#     why the predicate lives in `params.logos` and why `-1`/`-2` are spelled
#     there rather than called.
#
#     FIRE COUNTS INSIDE THE NEW BRANCHES (temporary `plan_trace`, removed after
#     measuring; a branch that never runs leaves every fixture green):
#       emit_simple streamed arm   11 fires / 6 fixtures
#       analyze_chain st_stream     3 fires / 3 fixtures
#       chain_nest_frag base        4 fires / 4 fixtures
#
#     PROBE PAIRS, each restored to a GREEN BUILD before the next:
#       A  invert `rel_src_unmaterialized`      -> stdlib build RED
#          ("slice has no method 'next'", canon_op_unknown_run)
#       B  a non-rel name answers `AD_NONE`     -> corpus 159 -> 122 dumps,
#          2 -> 39 compile-failures (37 fixtures witness the `AD_BUFFER` answer)
#       C  invert `st_stream[i]`                -> stdlib build RED
#          (canon_split_fast_run, type mismatch `&[u8]` vs `&&[u8]`)
#       D  invert `base_streams`                -> stdlib build RED
#          (canon_split_volume_run, same class, a DIFFERENT function)
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
#
# ⚠ THE SCOPE IS "EVERY DUMP THAT IS NOT `logos.gen.*`", NOT "`test.*`" (S2d).
# It was `test.*` and that is the fixture's PACKAGE name, not a property of the
# user module: two corpus fixtures declare their own package, so their whole
# emitted side — 4 `__ks` vectors between them — was invisible to this census
# while looking exactly like a fixture that builds nothing. Named by shape, so a
# third fixture with its own package name cannot go quiet the same way.
ALLD=("$d"/gen/*.gen.logos)
UD=()
for x in "${ALLD[@]}"; do
    case "$(basename "$x")" in logos.gen.*) ;; *) UD+=("$x");; esac
done
nit=0; nix=0; nks=0
if [ "${#UD[@]}" -ge 1 ]; then
    grep -Eh 'let mut __it_[a-z_0-9]+:' "${UD[@]}" > "$d/it" 2>/dev/null
    grep -Eh 'let mut __(hm|hs|bt)[0-9]+:' "${UD[@]}" > "$d/ix" 2>/dev/null
    grep -Eh 'let mut __ks:' "${UD[@]}" > "$d/ks" 2>/dev/null
    nit=$(wc -l < "$d/it")
    nix=$(wc -l < "$d/ix")
    nks=$(wc -l < "$d/ks")
fi
echo "$b $nit $nix $nks" > "$O/$b.count"
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
# Measured 2026-08-13 on the S2h tree, over the whole wql_/deem_ pass corpus.
#
# ⚠ EVERY NUMBER THAT MOVED IN S2h MOVED FOR ONE REASON — the corpus gained ONE
# fixture, `deem_mat_ground_witness`, and the deltas below are exactly that
# fixture's own artifact, counted three ways. NOTHING ELSE MOVED, and the oracle
# that makes that falsifiable is not this gate: the corpus snapshot
# (`/home/logos/sandbox/wql_corpus_snapshot.sh`) before and after S2h differs by
# the single line `Only in after: deem_mat_ground_witness.gen` — every one of the
# 159 pre-existing dumps is BYTE-IDENTICAL, so the `MG_GPATH` deletion, the
# empty-sentence fallback in `access_plan_plan_nodes` and the three survivor
# citations emitted no text at all. The per-fixture deltas, from that dump:
#   drain    +3   (`w_rel_block`, `w_undecided`, `w_unproven` — one each)
#   __it_    +3   (`let mut __it_s`, one per query — FACT B holds per fixture)
#   arrange  +2   ·  index  +2   ·  hash join  +2   (the two keyed steps)
#   container +1  (`hot`, the fixture's `rel` block — a container, as all are)
#   sort, __ks, key vector, read-once: UNCHANGED (the fixture has no `order by`)
EXPECT_FIXTURES   = 176
# The two fixtures that cannot compile ALONE: each `use`s a companion package the
# suite supplies through a lib path (LOCAL_PUBLIB_USERS / LOCAL_WQLMAP_USERS in
# CMakeLists.txt). Named, so that a THIRD compile failure — or one of these two
# starting to compile, which would mean the pin is stale — is red.
EXPECT_FAILED     = {"wql_mapping_cross_module_e2e", "wql_wref_field_pkg"}
EXPECT_DRAIN_SORT = 10    # drain 7 + sort 3, corpus-wide  (S2h: drain 4 -> 7)
EXPECT_IT         = 10    # `let mut __it_…` prelude bindings in the artifacts
EXPECT_ARRANGE    = 596   # Arrange nodes — S2d: == EXPECT_INDEX, exactly
EXPECT_HASHJOIN   = 493   # `hash join on` strategy decisions (nest 0 + pre-decided)
EXPECT_INDEX      = 596   # emitted `__hm`/`__hs`/`__bt` bindings
EXPECT_KS         = 123   # emitted `__ks` sort-key vectors == `key vector` lines
EXPECT_NOMAT      = {"container": 204, "readonce": 18}
# The DEBT LEDGER: ground tokens the corpus does not reach. Checked in BOTH
# directions — a token here that IS witnessed fails just as loudly.
UNWITNESSED = {
    # the order axis: refusals no corpus query provokes
    "WG_NO_STEP", "WG_UNDECIDED", "WG_ONE_FLOAT", "WG_MAX_FL", "WG_CROSS",
    "WG_NO_SIZE",
    "RJ_PREDBASE", "RJ_PREDPIN", "RJ_SHAPE",
    "SZ_RUN_STREAMS", "SZ_PREP_STREAMS",
    # ⚠ NO `MG_*` ENTRY REMAINS, AND THE ABSENCE IS THE LOAD-BEARING PART (S2h).
    # This block used to hold `MG_REL_BLOCK`, `MG_UNDECIDED`, `MG_GPATH` and
    # `MG_UNPROVEN` as the S2 debt ledger. Three are now witnessed by
    # `deem_mat_ground_witness` and the fourth was refuted and deleted from the
    # vocabulary — the header carries the argument and the control for each.
    # Because the partition is checked in BOTH directions, every one of the eight
    # surviving materialization grounds is now asserted to be REACHED by the
    # corpus: `MG_SECOND_USE`, `MG_ORDER_BY`, `MG_REL_BLOCK`, `MG_RESCAN`,
    # `MG_UNDECIDED`, `MG_REGROUP`, `MG_JOIN_BUILD`, `MG_UNPROVEN` (+ the absence
    # ground `MG_CONTAINER`). A fixture deleted or a query rewritten so that one
    # of them goes quiet is a red here, which is the property the debt ledger was
    # standing in for.
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

# ⚠ THE FOURTH GROUP IS THE SENTENCE, AND IT IS CAPTURED SO IT CAN BE CHECKED
# (FACT F, S2h). The ground and the justification are two fields and only the
# ground was ever asserted; a node with a ground and an EMPTY sentence is the
# silent materialization this whole gate exists to catch, moved one field over.
NODE = re.compile(r'^\[plan\] ([a-z_0-9]+) -> (drain|sort|arrange) on ([^(]*?)\s+\((.*)$')
VERB = re.compile(r'^\[plan\] ([a-z_0-9]+) -> ([a-z ]+?)\s')
# ⚠ `key vector` IS NOT A NODE KIND and must not be spelled `sort` (S2d). The
# fourth whitespace field of a trace line is what every reader of this channel —
# `NODE` above, `plan_nodes_gate`'s awk — takes for the verb, so a line reading
# `-> sort keys on …` would be counted as a Sort node by both. The verdict word
# is therefore its own.
KEYV = re.compile(r'^\[plan\] ([a-z_0-9]+) -> key vector on ')

tot = dict(drain=0, sort=0, arrange=0, it=0, ix=0, ks=0, kv=0, hj=0,
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
    nd = dict(drain=0, sort=0, arrange=0, hj=0, kv=0)
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
            # FACT F — EVERY NODE CARRIES A SENTENCE, NOT ONLY A GROUND (S2h).
            # Found by writing the `MG_UNPROVEN` witness: `access_plan_plan_nodes`
            # pushed `ap.owhy[ri]`, which is `""` for a rel
            # `plan_mark_single_pass` never answered for, so the corpus's first
            # unproven drain printed `-> drain on unproven: no single-read proof
            # ()` — a ground naming a mechanism and no account of it. The fix is
            # the `swhy` fallback at `access_plan.logos`; this is the sensor that
            # keeps it, and it is a NEW assertion rather than a tightened one.
            if not m.group(4).strip().rstrip(")").strip():
                bad(f"[{b}] a `{kind}` node on ground `{gnd}` carries an EMPTY "
                    f"justification — a materialization named and not explained")
            continue
        if line.startswith("[plan] ") and " -> hash join on " in line:
            nd["hj"] += 1
        if KEYV.match(line):
            nd["kv"] += 1
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
    for k in ("drain", "sort", "arrange", "hj", "kv"):
        tot[k] += nd[k]

    cf = os.path.join(OD, b + ".count")
    nit = nix = nks = 0
    if os.path.exists(cf):
        _, a, c, e = open(cf).read().split()
        nit, nix, nks = int(a), int(c), int(e)
    else:
        bad(f"[{b}] no artifact count file — the emitted side was not read")
    tot["it"] += nit
    tot["ix"] += nix
    tot["ks"] += nks

    # FACT B, per fixture
    if b not in EXPECT_FAILED and nd["drain"] + nd["sort"] != nit:
        bad(f"[{b}] {nd['drain']+nd['sort']} drain/sort nodes vs {nit} `__it_` "
            f"prelude bindings in the artifact")
    # FACT C, per fixture: the arrangements ARE the indexes the artifact builds.
    if nd["arrange"] != nix:
        bad(f"[{b}] {nd['arrange']} arrange nodes vs {nix} emitted index "
            f"bindings — the node layer stopped counting what is built")
    # FACT E, per fixture: every sort-key vector is a named Sort key collection.
    if nd["kv"] != nks:
        bad(f"[{b}] {nd['kv']} `key vector` lines vs {nks} emitted `__ks` "
            f"vectors — a sort collected keys and the plan did not say so")

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
        ("ks", EXPECT_KS, "emitted `__ks` sort-key vectors"),
        ("kv", EXPECT_KS, "`key vector` lines"),
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
      f"  key vector {tot['kv']}"
      f"   |  artifact: __it_ {tot['it']}  index {tot['ix']}  __ks {tot['ks']}")
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
