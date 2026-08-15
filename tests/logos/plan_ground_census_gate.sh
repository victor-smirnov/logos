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
# `MG_UNDECIDED` / `MG_GPATH` / `MG_UNPROVEN` — the grounds the single-pass walk
# (`plan_insert_drains`, `plan_mark_single_pass` before the S2j inversion) carries
# that no fixture had driven through a node, and the S2 rule was that the proof
# could not be retired until the set was empty. It was retired at S2j, by moving
# the grounds onto the nodes rather than by dropping them — which is why this
# gate's subject did not change when the function's name did: the census has
# always read the TRACE, and the trace still names all eight grounds.
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
#   the single-pass walk that read it could not execute. CONTROL, both
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
# out of a source its query never names, because the walk answers only for the
# sources the ENTRY QUERY NAMES while the prelude materializes every source the
# natspec REGISTERED. (S2j moved that fallback INTO `plan_insert_drains`, where
# the sentence is now authored beside the ground instead of being recovered from
# the mode pass one phase later; the trace text is unchanged.) `MG_UNPROVEN` is the plan's honest account of
# that drain, and it is pinned here, not fixed in passing: removing the drain is
# an emitter change with its own artifact delta.
#
# ── ADR 0025 S3b — THIS GATE DID NOT MOVE, AND THAT IS THE ASSERTION ─────────
#
# S3b changed the Drain node's LANDING TYPE (`Vec<R>` -> `Buffer<R>`, the
# accumulator spelling: two string literals in `plan_walker::emit_prelude_oneshot`'s
# drain arm). It changed no ground, no node, no plan decision and no count here,
# and every number in this header is the same number afterwards. That is not a
# stage that "didn't need the census" — it is the census being the thing that says
# S3b touched only the landing: had the change reached the DECISION layer, the
# `MG_*` tallies or the Arrange/hash-join counts would have moved, and had it
# reached the slice arm, `logos_09_slice_scan_codegen`'s byte-pin would have.
# Corpus-wide the whole delta was 10 sites in 6 fixtures, +60 bytes.
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
#   FACT C  THE ARRANGE DEFICIT IS CLOSED — 598 == 598, per fixture and in
#           total (594 == 594 at S2d; S2h and then S3f each added one fixture
#           that builds two indexes, and the EQUALITY is what carried both
#           additions unremarked — the number moves, the assertion does not).
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
#   FACT G  THE PERMUTATION VECTORS ARE COUNTED — #(`let mut __ix<k>`) == 311,
#           corpus-wide, plus the per-fixture direction `perm >= ks` (a key
#           vector with nothing to permute is red). NEW IN S3e.
#
#           ⚠ IT IS A COUNT AND NOT AN EQUALITY AGAINST THE NODE LAYER, and the
#           number that refuses the equality is 85. S3e was asked to make the 311
#           permutation bindings "node-owned" by the Sort node. Measured on this
#           corpus first: 39 of the 89 fixtures that emit `__ix<k>` emit NO `__ks`
#           whatsoever, accounting for 85 of the 311 bindings. Those are not sort
#           permutations. The aggregate emitter declares `let mut __ix0` in its
#           group-row OUTPUT phase UNCONDITIONALLY (`rexpr_walk.logos`, the
#           `output` block: `collect` starts as a bare `__ix0.push(__r)` and only
#           the `mods.has_sort` arm adds `__ks`/`sort_perm_frag`), so `__ix0`
#           there is the group-row enumeration order and exists with no `order by`
#           anywhere in the query. A Sort node owning all 311 would assert a
#           materialization for an ordering nobody requested, 85 times.
#
#           ⚠ AND THE REMAINING 226 ARE ALREADY NODE-OWNED — under FACT E, by the
#           name S2d chose ON A MEASUREMENT. The scaffolding sort already reports
#           through `join_sel::sort_key_vector` (which fires at exactly the three
#           `order by` sites, already resolves the blind class through
#           `join_sel::node_subject`, and is pinned per fixture and in total).
#           What S3e would add is a RENAME of its verdict word to `sort` — and
#           that is the one edit this gate can prove wrong without running the
#           emitter: FACT B is `drain + sort == __it_` PER FIXTURE over the
#           PRELUDE plane, so a scaffolding line reading `sort` reds 89 fixtures
#           and, worse, would be counted as a prelude materialization by every
#           other reader of the trace. S2d wrote that down; this is the sensor
#           that keeps the two planes apart now that the count is pinned.
#
#           SO THE BLIND SPOT S3e ACTUALLY CLOSED is neither of those: nothing
#           held the 311 at all. `tot["ix"]` is the INDEX builds (`__hm`/`__hs`/
#           `__bt`, FACT C); the permutation vectors were never read off the
#           artifact, so a sort permutation appearing or vanishing moved no
#           number in this census. It does now.
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
nit=0; nix=0; nks=0; npm=0; ngk=0; ngc=0; ngr=0; nga=0
if [ "${#UD[@]}" -ge 1 ]; then
    grep -Eh 'let mut __it_[a-z_0-9]+:' "${UD[@]}" > "$d/it" 2>/dev/null
    grep -Eh 'let mut __(hm|hs|bt)[0-9]+:' "${UD[@]}" > "$d/ix" 2>/dev/null
    grep -Eh 'let mut __ks:' "${UD[@]}" > "$d/ks" 2>/dev/null
    # S3e — THE PERMUTATION VECTORS. `__ix<k>` (digit-suffixed) and NOT the bare
    # `__ix`, which is `jc_order_pick`'s join-order discriminant — a different
    # binding that this grep must not collect. Measured: 311 suffixed against 24
    # bare, corpus-wide.
    grep -Eh 'let mut __ix[0-9]+:' "${UD[@]}" > "$d/pm" 2>/dev/null
    # ADR 0025 §8 — THE GROUP FRAME'S EMITTED COLUMNS, one grep per family
    # because the four are pinned apart: they have different consumers
    # (`__g_cnt` exists for `avg`, `__g_row` for the representative class) and a
    # single pattern would let one family absorb another's disappearance.
    grep -Eh 'let mut __g_key:' "${UD[@]}" > "$d/gk" 2>/dev/null
    grep -Eh 'let mut __g_cnt:' "${UD[@]}" > "$d/gc" 2>/dev/null
    grep -Eh 'let mut __g_row:' "${UD[@]}" > "$d/gr" 2>/dev/null
    grep -Eh 'let mut __ga_[a-z_0-9]*:' "${UD[@]}" > "$d/ga" 2>/dev/null
    nit=$(wc -l < "$d/it")
    nix=$(wc -l < "$d/ix")
    nks=$(wc -l < "$d/ks")
    npm=$(wc -l < "$d/pm")
    ngk=$(wc -l < "$d/gk")
    ngc=$(wc -l < "$d/gc")
    ngr=$(wc -l < "$d/gr")
    nga=$(wc -l < "$d/ga")
fi
echo "$b $nit $nix $nks $npm $ngk $ngc $ngr $nga" > "$O/$b.count"
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
# empty-sentence fallback in the node author and the three survivor
# citations emitted no text at all. The per-fixture deltas, from that dump:
#   drain    +3   (`w_rel_block`, `w_undecided`, `w_unproven` — one each)
#   __it_    +3   (`let mut __it_s`, one per query — FACT B holds per fixture)
#   arrange  +2   ·  index  +2   ·  hash join  +2   (the two keyed steps)
#   container +1  (`hot`, the fixture's `rel` block — a container, as all are)
#   sort, __ks, key vector, read-once: UNCHANGED (the fixture has no `order by`)
#
# ⚠ S3f MOVED THEM AGAIN, FOR THE SAME KIND OF REASON AND WITH THE SAME ORACLE.
# The corpus gained ONE fixture, `deem_drain_buffer_empty` — the EMPTY drain
# landing (`Buffer::<R>::new()` built and never pushed to), the degenerate case
# §1 has a rule for and the whole corpus was silent about. Its own artifact,
# counted three ways, IS the delta:
#   drain    +1   (`s`, named twice in one chain -> `drained: second use`)
#   __it_    +1   (one query, one prelude producer binding — FACT B per fixture)
#   arrange  +2   ·  index  +2   ·  hash join  +2   (the two keyed steps)
#   materialize +1 (the same rel's `-> materialize` verdict)
#   sort, __ks, key vector, __ix<k>, container, read-once: UNCHANGED (no
#     `order by`, no aggregate, no `rel` block, and the read-once proof is
#     WITHDRAWN here rather than granted — which is why `readonce` does not move)
# The falsifiability is again NOT this gate: the corpus snapshot before and
# after S3f differs by `Only in after: deem_drain_buffer_empty.gen`, every one
# of the 160 pre-existing dumps byte-identical.
# ── RE-DERIVATION AT ADR 0025 S3-desc, WITH THE ATTRIBUTION ───────────────
#
# ⚠ THIS GATE WAS ALREADY RED WHEN S3-desc OPENED, AND THE INHERITED RED WAS
# MEASURED BEFORE ANYTHING WAS RE-PINNED. With S3-desc's two new fixtures held
# OUT of the corpus, the gate reported exactly one failure: `corpus is 178
# fixtures, pin says 177`. S3 added `pass/deem_order_elision` and did not
# re-derive this census — and because this gate is `tier_full`, S3's own L2 gate
# could not see it. Every numeric total was still correct at that point, which
# is what made the attribution below possible: the pins moved for exactly two
# reasons and both are named.
#
# ⚠ AND ONE OF THEM WAS TWO ERRORS CANCELLING — the thing this file exists to
# catch, caught. `EXPECT_IT` was pinned 11; the corpus measured 12 after S3 (the
# `by_val` refuse twin drains and sorts). S3-desc then ELIDED one drain in
# `deem_batch_scan_drain` (`tail_desc` orders by the declared column descending
# and is now a backward walk), taking the measurement back to 11 — the pinned
# number, reached by a stale pin and a real change agreeing by accident. With
# the two new fixtures it is 13. Had the corpus not grown, this clause would
# have read GREEN across a stage that moved it, and the stale pin would have
# been laundered into a vouched-for one.
#
# The deltas from the 177-fixture pin, each attributed:
#   FIXTURES  177 -> 180   +1 `deem_order_elision` (S3, unpinned) +2 S3-desc
#                          (`deem_order_desc_elision`, `deem_order_desc_forward_only`)
#   IT        11  -> 13    +1 S3's `by_val`, -1 `tail_desc` elided by S3-desc,
#                          +2 the two new refuse twins (`desc_val`, `tick_desc`)
#   KS / KV   123 -> 125   the same two refuse twins, one key vector each
#   PERM      311 -> 313   the same two, one permutation each
#   DRAIN+SORT 11 -> 13    drain 8 unchanged, sort 3 -> 5: the same two
#   ELIDED    (new)  8     4 `deem_order_desc_elision` (asc + 3 desc)
#                          + 2 `deem_order_elision` (S3) + 1 `tail_desc`
#                          + 1 `tick_asc`
#
# ⚠ `elided` IS A NEW COUNTER, AND THE ABSENCE LINE IS NOW PARSED RATHER THAN
# PATTERN-MATCHED. Before this stage the "no materialization on <ground>" line
# recognised exactly one ground by its text (`already a buffer`) and dropped
# every other into the groundless `readonce` bucket — so `MG_ORDERED_SOURCE` and
# `MG_ORDERED_SOURCE_REV` read as UNWITNESSED while the corpus reached them
# eight times. The ground is now parsed off the line and matched by exact
# equality against the extracted vocabulary, the same rule the node lines use,
# so the NEXT ground for an absence is witnessed here without editing this file.
# ── RE-DERIVATION AT ADR 0025 §8 (S4-naming), WITH THE ATTRIBUTION ───────────
#
# ⚠ THIS GATE WAS ALREADY RED WHEN THIS STAGE OPENED, AND THE INHERITED RED WAS
# MEASURED AND ATTRIBUTED BEFORE ANYTHING WAS RE-PINNED — the same discipline
# S3-desc used, and the same cause: a stage added a fixture and did not
# re-derive a `tier_full` census its own L2 gate cannot see. Measured on the
# pre-change tree, the gate reported exactly four failures:
#
#   corpus is 181 fixtures, pin says 180
#   127 `__ks` sort-key vectors, pinned 125     ·  127 `key vector` lines, pinned 125
#   319 `__ix<k>` permutation vectors, pinned 313
#
# ALL FOUR ARE ONE FIXTURE — `pass/wql_group_single_pass_fold_e2e`, added by the
# S4 fold, whose own dump holds exactly 2 `__ks` and 6 `__ix<k>` (measured
# directly on the fixture, not inferred from the difference). Nothing else moved:
# drain/sort 13, arrange 598, index 598, hash-join 495, `__it_` 13, container
# 204, read-once 18, elided 8 were all green against the S3-desc pin.
#
# ⚠ AND THE NAMING CHANGE ITSELF MOVED NO NUMBER ABOVE — that is its control,
# and it is checked outside this gate: the corpus snapshot before and after is
# BYTE-IDENTICAL (165 dumps / 7,067,309 bytes, `diff -rq` empty). The group
# frame enters the plan as TRACE, so every artifact-side pin here must be
# unchanged by it, and the arrange/index identity (598 == 598) is the sharpest
# of those controls: a group table spelled `arrange` would have broken it 152
# times.
#
# THE NEW CLASS (FACT H), measured on this tree, plan side and artifact side
# agreeing per fixture and in total:
#   group frame       152   == `let mut __g_key:`  152
#   accumulator       208   == `let mut __ga_*:`   208
#   group count        13   == `let mut __g_cnt:`   13   (S4b: `avg` and nothing else)
#   representative row   7  == `let mut __g_row:`    7   (S4c: the representative class)
#                     ───                           ───
#                     380                           380
# 380 is the WHOLE of the class criterion 1 counted as unnamed on this tree
# (`__gf_*` is 0 — the finalize pass is deleted, and its 488 bindings left the
# count by being deleted rather than by being named).
# ── RE-DERIVATION AT ADR 0025 S5 (the streaming return surface, §12) ────────
#
# ONE clause moved and it is the fixture count: `pass/deem_stream_return_surface`
# (S5) takes the corpus 181 -> 182. MEASURED BEFORE RE-PINNING, the gate reported
# exactly ONE failure:
#
#   corpus is 182 fixtures, pin says 181
#
# EVERY OTHER PINNED NUMBER HELD, and that is this stage's sharpest control, not
# a convenience. S5 adds 486 `*_stream` entries to the ARTIFACT and NOTHING to
# the PLAN: drain/sort 13, arrange 598, index 598, hash-join 495, `__it_` 13,
# `__ks` 127, `__ix<k>` 319, container 204 / read-once 18 / elided 8, and all
# four group-frame families (152/208/13/7) are unchanged. An emitter change that
# had touched a materialization decision — rather than adding a door over the
# result — could not have left all fifteen of those still.
#
# ⚠ AND THE ABSENCE THAT IS DELIBERATE AND NAMED, NOT AN OVERSIGHT. §12 says the
# `buffered` form is "semantically a `Drain` at the output seam, and the plan
# records it as one". NO SUCH NODE IS INSERTED AT S5, on purpose: the node list
# is indexed PER REL (`AccessPlan.prelude_ix(ri)`) and the output seam is not a
# rel, and — the load-bearing half — at S5 the answer is CONSTANT across the
# whole corpus, because `direct` is not landed and every query's stream surface
# is buffered. A node with one value for all 486 entries carries no information
# and would move this census by 486 while saying nothing a reader could act on.
# The output-seam node belongs to the stage where the plan first has TWO answers
# to give, which is `direct`. Written here rather than left implicit, so that the
# next reader finds a decision and not a gap.
# ── RE-DERIVATION AT ADR 0025 S5-D5 (the aggregate pulls batches) ───────────
#
# THREE clauses moved, ALL THREE ARE ONE QUERY, and the query is named:
# `parity_sums` in `pass/deem_batch_scan_drain` — the corpus's only PURE-class
# aggregate (`aggr_group_frame_pure`) over a source whose chosen producer hands
# out BATCHES. S4 proved that fold single-pass at the operator and could not
# claim it, because `emit_aggregate`'s base loop was still `while __i0 <
# (src).len()`; S5-D5 routes that loop through `batch_scan_frag` and the claim
# becomes true of the artifact, so the Drain that stood in for it goes away.
#
#   DRAIN+SORT 13 -> 12    drain 8 -> 7 (this query's `__rel_m` Buffer), sort 5
#                          unchanged — the fixture's OTHER query still sorts.
#   READ-ONCE  18 -> 19    the same node, counted on the other side of the same
#                          decision: `m -> no materialization (read once,
#                          consumed where it stands)` where the trace used to
#                          print `materialize (MG_REGROUP)`.
#   IT         13 -> 12    the `let mut __it_m: …LeafWalk` + `let mut __rel_m:
#                          Buffer<(u64,u64)>` PAIR collapses to one binding —
#                          the walk IS the source now. MEASURED IN THE FIXTURE
#                          (2 -> 1), not inferred from the corpus difference.
#
# ⚠ EVERY OTHER PINNED NUMBER HELD, and that is the control that says this is
# one decision and not a shape change: arrange 598, index 598, hash-join 495,
# `__ks` 127, `__ix<k>` 319, container 204, elided 8, and all four group-frame
# families (152/208/13/7) are unchanged. The FOLD did not move — only what the
# rows arrive in. The corpus snapshot agrees per file: `diff -rq` over 166 dumps
# reports exactly ONE differing file, this fixture, −23 bytes.
#
# ⚠ AND `MG_REGROUP` IS STILL WITNESSED (count 1), which the both-directions
# partition requires. It survives on the two classes that keep their drain: an
# aggregate whose clauses name a representative source row, and — the arm S5-D5
# adds — a PURE aggregate over a producer that hands out ROWS rather than
# batches. Had the ground gone quiet, this gate would red on the debt ledger,
# and the pure-class change would have been a vocabulary deletion in disguise.
# S5-PIPELINE: 182 -> 183, `pass/deem_pipeline_chain` (§12's composition oracle).
# S6-A: 183 -> 184, `pass/deem_rowmajor_batch_source` (§2's row-major layout).
EXPECT_FIXTURES   = 184
# The two fixtures that cannot compile ALONE: each `use`s a companion package the
# suite supplies through a lib path (LOCAL_PUBLIB_USERS / LOCAL_WQLMAP_USERS in
# CMakeLists.txt). Named, so that a THIRD compile failure — or one of these two
# starting to compile, which would mean the pin is stale — is red.
EXPECT_FAILED     = {"wql_mapping_cross_module_e2e", "wql_wref_field_pkg"}
EXPECT_DRAIN_SORT = 12    # drain 7 + sort 5, corpus-wide  (S2h: drain 4 -> 7; S3f: 7 -> 8; S3-desc: sort 3 -> 5; S5-D5: drain 8 -> 7, see RE-DERIVATION above)
EXPECT_IT         = 12    # `let mut __it_…` prelude bindings in the artifacts  (S5-D5: 13 -> 12)
EXPECT_ARRANGE    = 598   # Arrange nodes — S2d: == EXPECT_INDEX, exactly
EXPECT_HASHJOIN   = 495   # `hash join on` strategy decisions (nest 0 + pre-decided)
EXPECT_INDEX      = 598   # emitted `__hm`/`__hs`/`__bt` bindings
EXPECT_KS         = 127   # emitted `__ks` sort-key vectors == `key vector` lines  (S4: +2, `wql_group_single_pass_fold_e2e`)
# S3e — THE PERMUTATION VECTORS, PINNED BUT NOT ATTRIBUTED TO A SORT NODE.
# 311 `let mut __ix<k>` across 89 fixtures. This is a COUNT, not an equality
# against the node layer, and the header says why: 85 of the 311, in 39
# fixtures, are emitted where there is no sort at all.
EXPECT_PERM       = 319
# S5-PIPELINE: readonce 19 -> 21. `deem_pipeline_chain` chains TWO deems onto one
# streamed source (`q2_head` bounded, `q2_all` the unbounded control), and each
# contributes one `read once, consumed where it stands` — the ground that IS the
# pipeline claim at seam 2, so the +2 is the stage's subject and not a side effect.
# ⚠ `container` DOES NOT MOVE, and the prediction that it would was REFUTED by the
# trace rather than absorbed: the fixture's THIRD deem (`q1`, over a `&[Row]`
# parameter) emits NO per-rel materialization ground at all — the `[plan] <rel> ->`
# lines in that file belong to the two streamed rels only. A bare slice parameter
# is scanned where it stands and never reaches `access_plan_decide_mode`'s
# container arm, so 204 is unchanged and predicting 205 was a wrong model of which
# sources own a ground.
# S6-A: container 204 -> 205 AND readonce 21 -> 22, both from the ONE new
# fixture and both PREDICTED — `deem_rowmajor_batch_source` is a differential: the
# same query text over a row-major BATCH source (read once, consumed where it
# stands) and over a `Vec` twin (a container, already a buffer). One ground each
# is the fixture's whole shape, so a move of +1/+1 here is the fixture arriving
# and any other split would have meant it is not the pair it claims to be.
EXPECT_NOMAT      = {"container": 205, "readonce": 22, "elided": 8}   # S5-D5: readonce 18 -> 19
# ADR 0025 §8 — THE GROUP FRAME, PINNED PER FAMILY AND NOT AS ONE TOTAL. The
# four have different consumers and different lives (`__g_cnt` exists for `avg`,
# `__g_row` for the representative class), so one number would let a family
# vanish while another grew — which is precisely how `elided` used to hide
# inside `readonce`.
EXPECT_FRAME      = {"gkey": 152, "gacc": 208, "gcnt": 13, "grow": 7}
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
# ⚠ TWO PREFIXES SINCE THE GROUP FRAME LANDED (ADR 0025 §8). `MG_*` grounds a
# MATERIALIZATION (or the absence of one); `AG_*` grounds a column of the
# aggregate's GROUP FRAME, which is the operator's own state and not an adapter
# over a stream — the reason it is a field rather than a node kind is argued at
# the definitions. Both are TOKENS and both are matched by exact equality on a
# trace line's ground field; the prefixes stay distinct so the census can print
# which vocabulary a ground belongs to and so that neither can be extracted by
# accident when the other's shape changes.
for tok, sent in re.findall(r'pub fn ((?:MG|AG)_\w+)\(\) -> str\s*\{ return "(.*?)"; \}', apl):
    vocab[tok] = sent
if len([k for k in vocab if k.startswith("MG_")]) < 8:
    bad("fewer than 8 MG_* grounds extracted — the materialization vocabulary "
        "was not read")
if len([k for k in vocab if k.startswith("AG_")]) < 4:
    bad("fewer than 4 AG_* grounds extracted — the group-frame vocabulary was "
        "not read, and every clause about it below would be vacuously green")

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
# ── THE GROUP FRAME'S FOUR VERDICTS (ADR 0025 §8) ────────────────────────────
# Read by the same rule as `key vector` above and for the same reason: these are
# FIELDS of the aggregate the plan already carries, so their verdict words are
# their own and none of them is a node kind. In particular the group table is
# NOT spelled `arrange` — the census pins `#(arrange nodes) == #(emitted index
# bindings)`, and an arrange node is the claim that the artifact built an index,
# which a linear `==` scan over `__g_key` did not. That identity is therefore a
# CONTROL on this stage rather than a casualty of it: it must not move.
FRAME = re.compile(r'^\[plan\] ([a-z_0-9]+) -> '
                   r'(group frame|accumulator|group count|representative row)'
                   r' on ([^(]*?)\s+\((.*)$')
FRAMEK = {"group frame": "gkey", "accumulator": "gacc",
          "group count": "gcnt", "representative row": "grow"}

tot = dict(drain=0, sort=0, arrange=0, it=0, ix=0, ks=0, kv=0, hj=0, pm=0,
           container=0, readonce=0, elided=0, materialize=0, stream=0,
           gkey=0, gcnt=0, grow=0, gacc=0,
           agkey=0, agcnt=0, agrow=0, agacc=0)
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
        if not tok.startswith(("MG_", "AG_")):
            witness[tok] += text.count(probe)
    nd = dict(drain=0, sort=0, arrange=0, hj=0, kv=0,
              gkey=0, gcnt=0, grow=0, gacc=0)
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
            # Found by writing the `MG_UNPROVEN` witness: the node author pushed
            # the per-rel sentence, which was `""` for a rel the single-pass walk
            # never answered for, so the corpus's first unproven drain printed
            # `-> drain on unproven: no single-read proof ()` — a ground naming a
            # mechanism and no account of it. Since S2j the sentence is authored
            # WITH the node, in `plan_insert_drains`'s fallback loop, so the
            # empty case has no way to arise; this is the sensor that keeps it
            # that way, and it is a NEW assertion rather than a tightened one.
            if not m.group(4).strip().rstrip(")").strip():
                bad(f"[{b}] a `{kind}` node on ground `{gnd}` carries an EMPTY "
                    f"justification — a materialization named and not explained")
            continue
        # ── THE GROUP FRAME'S COLUMNS (ADR 0025 §8) ─────────────────────────
        # Counted by verdict, with the ground matched by EXACT EQUALITY exactly
        # as a node line's is, and the sentence asserted non-empty for the same
        # reason FACT F gives: a ground naming a mechanism with no account of it
        # is the silent materialization this gate exists to catch, one field
        # over. These do NOT enter `named` — a group frame is state the
        # aggregate builds, not the answer to "what did the plan build over this
        # rel", and letting it discharge FACT A would let an unexplained
        # materialization ride out on an aggregate's coat-tails.
        f = FRAME.match(line)
        if f:
            verb, gnd = f.group(2), f.group(3).strip()
            nd[FRAMEK[verb]] += 1
            for tok, probe in vocab.items():
                if tok.startswith("AG_") and probe == gnd:
                    witness[tok] += 1
            if not gnd:
                bad(f"[{b}] a `{verb}` group-frame line carries an EMPTY ground")
            if not f.group(4).strip().rstrip(")").strip():
                bad(f"[{b}] a `{verb}` line on ground `{gnd}` carries an EMPTY "
                    f"justification — group state named and not explained")
            continue
        if line.startswith("[plan] ") and " -> hash join on " in line:
            nd["hj"] += 1
        if KEYV.match(line):
            nd["kv"] += 1
        if " -> materialize " in line or line.endswith(" -> materialize"):
            mat.add(line.split()[1])
        if " -> no materialization" in line:
            # THE ABSENCE LINE IS A GROUND FAMILY, NOT A CONTAINER SPECIAL CASE
            # (ADR 0025 S3/S3-desc). This branch used to name exactly one ground
            # by its text (`already a buffer`) and drop everything else into the
            # generic read-once bucket. That was correct while `MG_CONTAINER` was
            # the only ground for an ABSENCE; S3 added `MG_ORDERED_SOURCE` and
            # S3-desc `MG_ORDERED_SOURCE_REV`, and both were counted as
            # groundless read-once reads — so two published grounds read as
            # UNWITNESSED while the corpus reached them 4 times, which is the
            # census counting the question instead of the answer.
            #
            # Generalized rather than extended by two more `in line` tests: the
            # ground is PARSED off the line and matched by EXACT EQUALITY against
            # the extracted vocabulary, the same rule the node lines above use.
            # A ground added to `access_plan.logos` after this is witnessed here
            # with no edit to this file — which is the property that failed.
            rel = line.split()[1]
            gnd = ""
            if " -> no materialization on " in line:
                gnd = line.split(" -> no materialization on ", 1)[1]
                gnd = gnd.split("   (", 1)[0].strip()
            if gnd:
                named.add(rel)
                for tok, probe in vocab.items():
                    if tok.startswith("MG_") and probe == gnd:
                        witness[tok] += 1
                if gnd == vocab.get("MG_CONTAINER"):
                    tot["container"] += 1
                else:
                    tot["elided"] += 1
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
    for k in ("drain", "sort", "arrange", "hj", "kv",
              "gkey", "gcnt", "grow", "gacc"):
        tot[k] += nd[k]

    cf = os.path.join(OD, b + ".count")
    nit = nix = nks = npm = 0
    agkey = agcnt = agrow = agacc = 0
    if os.path.exists(cf):
        _, a, c, e, g, gk, gc, gr, ga = open(cf).read().split()
        nit, nix, nks, npm = int(a), int(c), int(e), int(g)
        agkey, agcnt, agrow, agacc = int(gk), int(gc), int(gr), int(ga)
    else:
        bad(f"[{b}] no artifact count file — the emitted side was not read")
    tot["it"] += nit
    tot["ix"] += nix
    tot["ks"] += nks
    tot["pm"] += npm
    tot["agkey"] += agkey
    tot["agcnt"] += agcnt
    tot["agrow"] += agrow
    tot["agacc"] += agacc

    # FACT H, per fixture — THE GROUP FRAME'S PLAN LINES ARE ITS EMITTED COLUMNS
    # (ADR 0025 §8). One `group frame` line per `let mut __g_key:`, one
    # `accumulator` per `__ga_<out>`, one `group count` per `__g_cnt` and one
    # `representative row` per `__g_row` — an identity BY CONSTRUCTION, because
    # each call fires inside the branch that declares its column, and stated
    # here so that a naming layer re-derived from the QUERY instead (the shape
    # that drifts) is red rather than merely different. This is FACT B/FACT C's
    # rule applied to the class those two do not cover.
    #
    # ⚠ SKIPPED FOR THE TWO STANDALONE-FAILING FIXTURES, exactly as FACT B is:
    # they emit no dump, so the artifact side is 0 by absence rather than by
    # measurement, and a fixture that compiles ONLY inside the suite would
    # otherwise be asserted against a file that was never written.
    if b not in EXPECT_FAILED:
        for verb, plan_n, art_n, art in (
                ("group frame", nd["gkey"], agkey, "`__g_key`"),
                ("accumulator", nd["gacc"], agacc, "`__ga_*`"),
                ("group count", nd["gcnt"], agcnt, "`__g_cnt`"),
                ("representative row", nd["grow"], agrow, "`__g_row`")):
            if plan_n != art_n:
                bad(f"[{b}] {plan_n} `{verb}` plan line(s) vs {art_n} emitted "
                    f"{art} binding(s) — the group frame the plan names is not "
                    f"the group frame the artifact builds")

    # FACT G, per fixture: EVERY KEY VECTOR HAS SOMETHING TO PERMUTE.
    # A sort scaffolding emits one `__ks` and `nb` index vectors (nb == the bound
    # sources), so `perm >= ks` holds for every fixture BY CONSTRUCTION — and a
    # violation is the one shape the emitter must never produce: keys collected
    # for a permutation that was not declared. This is the direction that is
    # decidable per fixture; the reverse is NOT an equality and must not be
    # written as one — see the partition in the header.
    if npm < nks:
        bad(f"[{b}] {npm} `__ix<k>` permutation vectors vs {nks} `__ks` key "
            f"vectors — a sort collects keys and permutes nothing")

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
        ("pm", EXPECT_PERM, "`__ix<k>` permutation vectors"),
        ("container", EXPECT_NOMAT["container"], "`already a buffer` grounds"),
        ("readonce", EXPECT_NOMAT["readonce"], "`read once, consumed where it stands` grounds"),
        # ADR 0025 S3/S3-desc — the ELIDED absences, pinned APART from the
        # groundless read-once reads they used to hide inside. An elision that
        # stopped happening would otherwise reappear silently as a read-once
        # read, which is the same number moving in two directions at once.
        ("elided", EXPECT_NOMAT["elided"], "`ordered source` elision grounds"),
        # ADR 0025 §8 — the GROUP FRAME, plan side (the artifact side is
        # compared against it below, so a defect that moved both together is
        # still red against these).
        ("gkey", EXPECT_FRAME["gkey"], "`group frame` lines"),
        ("gacc", EXPECT_FRAME["gacc"], "`accumulator` lines"),
        ("gcnt", EXPECT_FRAME["gcnt"], "`group count` lines"),
        ("grow", EXPECT_FRAME["grow"], "`representative row` lines")):
    if want is None:
        continue
    if tot[key] != want:
        bad(f"corpus total: {tot[key]} {what}, pinned {want}")
# FACT H, in total — the same identity the per-fixture clause asserts, over the
# corpus. Kept BESIDE the pins rather than instead of them: the pins catch a
# class that shrank, this catches a plan that stopped describing the artifact,
# and a change that moved both sides together fails the first while a change
# that moved one fails the second.
for pk, ak, what in (("gkey", "agkey", "`__g_key` group tables"),
                     ("gacc", "agacc", "`__ga_*` accumulator columns"),
                     ("gcnt", "agcnt", "`__g_cnt` count columns"),
                     ("grow", "agrow", "`__g_row` representative rows")):
    if tot[pk] != tot[ak]:
        bad(f"corpus total: {tot[pk]} plan line(s) vs {tot[ak]} emitted {what} "
            f"— the group frame the plan names is not the one the artifact builds")
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
      f"   |  artifact: __it_ {tot['it']}  index {tot['ix']}  __ks {tot['ks']}"
      f"  __ix<k> {tot['pm']}")
print(f"  hash-join decisions {tot['hj']}   materialize {tot['materialize']}"
      f"   stream {tot['stream']}")
print(f"  no materialization: already-a-buffer {tot['container']}, "
      f"read-once {tot['readonce']}   |  silent {len(silent)}")
print("── GROUP FRAME (plan | artifact) ─────────────────────────────────────")
print(f"  group table {tot['gkey']}|{tot['agkey']}  accumulator {tot['gacc']}|{tot['agacc']}"
      f"  group count {tot['gcnt']}|{tot['agcnt']}"
      f"  representative row {tot['grow']}|{tot['agrow']}"
      f"   =  {tot['gkey']+tot['gacc']+tot['gcnt']+tot['grow']} named bindings")
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
