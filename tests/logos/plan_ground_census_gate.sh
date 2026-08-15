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
nit=0; nix=0; nks=0; npm=0; ngk=0; ngc=0; ngr=0; nga=0; nqo=0; nqs=0; nro=0
nfdl=0; nfnd=0; nfrs=0; nfos=0; nfbe=0; nfky=0; nfrd=0
ncpr=0; ncpf=0; ncpl=0; ncpt=0
nitt=0; niod=0; nirm=0; niwc=0; ninw=0; niec=0; nipr=0; nilt=0
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
    # ADR 0025 R-B — THE OUTPUT SEAM'S TWO LANDINGS, GREPPED APART. `__out` is
    # the query output and `__rout` the rel one-shot's result; before R-B1 both
    # were spelled `__out` and no census could tell them apart. ⚠ `let mut`, so
    # the 336 `let __out: &mut Vec<…>` fixpoint ALIASES (and R-B1's own alias)
    # are excluded — an alias is a borrow onto someone else's landing, not a
    # materialization, and counting it here would make FACT J assert that the
    # plan names a collection that was never allocated.
    # ⚠ `Vec<`, AND THE TYPE IS LOAD-BEARING — MEASURED, NOT ASSUMED. This
    # grep was written `let mut __out:` and FACT J reddened on its first run,
    # corpus-wide 606 against a plan side of 605, all of it one fixture
    # (`wql_trama_combined_e2e`). The 606th is `let mut __out: String` — the
    # TRAMA TEMPLATE renderer's output buffer (`trama_render.logos:732`), which
    # reuses the name and is not a query output at all: it is a rendered string,
    # it has no plan, and `criterion1_materialization_instrument.sh`'s D2 never
    # counted it either (its type filter is Vec|Buffer|HashMap|BTreeMap). So the
    # collection form is what FACT J is about — and the String landing is
    # counted BESIDE it rather than dropped, because narrowing a population
    # without pinning what left it is how a class goes quiet.
    grep -Eh 'let mut __out: Vec<' "${UD[@]}" > "$d/qo" 2>/dev/null
    grep -Eh 'let mut __out: String' "${UD[@]}" > "$d/qs" 2>/dev/null
    grep -Eh 'let mut __rout:' "${UD[@]}" > "$d/ro" 2>/dev/null
    # ── ADR 0025 R-C2 — THE FIXPOINT PLANE'S SIX LANDINGS (FACT K) ───────
    # ⚠ EVERY PATTERN REQUIRES THE `_<relname>` SUFFIX, and that is what keeps
    # these greps honest rather than convenient. The role name alone is
    # AMBIGUOUS in this tree: bare `__rs` is the one-shot rel helper's dedup
    # set in `_run` — a different role in a different emitter region, 45 of
    # them, 1:1 with `__rout` — while `__rs_<m>` is the fixpoint member's
    # novelty shadow set. A pattern written `let mut __rs` would silently take
    # both and FACT K would assert an identity over a population it had
    # mis-drawn. The relation name can itself begin with `__` (`__reach_w`
    # gives `__rs___reach_w`), which `[a-z_0-9]+` covers.
    grep -Eh 'let mut __dl_[a-z_0-9]+:'   "${UD[@]}" > "$d/fdl" 2>/dev/null
    grep -Eh 'let mut __nd_[a-z_0-9]+:'   "${UD[@]}" > "$d/fnd" 2>/dev/null
    grep -Eh 'let mut __rs_[a-z_0-9]+:'   "${UD[@]}" > "$d/frs" 2>/dev/null
    grep -Eh 'let mut __os_[a-z_0-9]+:'   "${UD[@]}" > "$d/fos" 2>/dev/null
    grep -Eh 'let mut __best_[a-z_0-9]+:' "${UD[@]}" > "$d/fbe" 2>/dev/null
    grep -Eh 'let mut __keys_[a-z_0-9]+:' "${UD[@]}" > "$d/fky" 2>/dev/null
    # R-C3 — the one-shot rel helper's dedup set. Its own name since R-C3
    # renamed it off the `__rs` prefix; before that this grep could not have
    # been written at all without also taking the 218 fixpoint shadow sets.
    grep -Eh 'let mut __rds:' "${UD[@]}" > "$d/frd" 2>/dev/null
    # ── ADR 0025 R-E — THE RETRACTION SNAPSHOT'S THREE SEAMS (FACT L) ────
    # ⚠ THE BINDING NAME CANNOT ANSWER THIS ONE, and that is why the anchor is
    # the COPY LOOP and not the declaration. `__cp<i>` is indexed by POSITION
    # in the shadow handle's field list, so the name says nothing about which
    # field family the binding snapshots — a grep on `let mut __cp` can only
    # produce the 145-wide total, which is exactly the pin a re-routing between
    # the three heads survives. The loop's guard names the SOURCE FIELD
    # (`while (__cx<i> < __hh.<f>.len())`), so the artifact states its own
    # partition and the plan side is checked against it rather than against a
    # restatement of itself. `__cx` is emitted by `push_clone_field` and by
    # nothing else in this tree (grepped), so the anchor is exclusive.
    grep -Eh 'while \(__cx[0-9]+ < __hh\.(__edb|__tot_[a-z_0-9]+)\.len\(\)\)' "${UD[@]}" > "$d/cpr" 2>/dev/null
    grep -Eh 'while \(__cx[0-9]+ < __hh\.(__gk|__gc|__ga_[a-z_0-9]+)\.len\(\)\)' "${UD[@]}" > "$d/cpf" 2>/dev/null
    grep -Eh 'while \(__cx[0-9]+ < __hh\.__lat\.len\(\)\)' "${UD[@]}" > "$d/cpl" 2>/dev/null
    # The TOTAL, read off the declarations — the third number that makes the
    # three above a PARTITION rather than three independent counts. A handle
    # field family added to the snapshot without a head lands in none of the
    # three greps and `cpr + cpf + cpl == cpt` is what refuses it.
    grep -Eh 'let mut __cp[0-9]+: Vec<' "${UD[@]}" > "$d/cpt" 2>/dev/null
    ncpr=$(wc -l < "$d/cpr"); ncpf=$(wc -l < "$d/cpf")
    ncpl=$(wc -l < "$d/cpl"); ncpt=$(wc -l < "$d/cpt")
    # ── ADR 0025 R-E — THE PER-ROUND WORKING SET'S EIGHT SEAMS (FACT M) ──
    # One grep per family, for the FACT H reason: they have different consumers
    # and a single pattern would let one family absorb another's
    # disappearance. `__tt`/`__odv`/`__rmv` carry a member index and the other
    # five do not, which is itself the per-member/per-round distinction the
    # identities below rest on — so the digit is REQUIRED where it is emitted
    # and REFUSED where it is not, rather than `[0-9]*` everywhere.
    grep -Eh 'let mut __tt[0-9]+: Vec<'   "${UD[@]}" > "$d/itt" 2>/dev/null
    grep -Eh 'let mut __odv[0-9]+: Vec<'  "${UD[@]}" > "$d/iod" 2>/dev/null
    grep -Eh 'let mut __rmv[0-9]+: Vec<'  "${UD[@]}" > "$d/irm" 2>/dev/null
    grep -Eh 'let mut __wcd: Vec<'        "${UD[@]}" > "$d/iwc" 2>/dev/null
    grep -Eh 'let mut __nw: Vec<'         "${UD[@]}" > "$d/inw" 2>/dev/null
    grep -Eh 'let mut __ecp: Vec<'        "${UD[@]}" > "$d/iec" 2>/dev/null
    grep -Eh 'let mut __pres: Vec<'       "${UD[@]}" > "$d/ipr" 2>/dev/null
    grep -Eh 'let mut __lt: Vec<'         "${UD[@]}" > "$d/ilt" 2>/dev/null
    nitt=$(wc -l < "$d/itt"); niod=$(wc -l < "$d/iod"); nirm=$(wc -l < "$d/irm")
    niwc=$(wc -l < "$d/iwc"); ninw=$(wc -l < "$d/inw"); niec=$(wc -l < "$d/iec")
    nipr=$(wc -l < "$d/ipr"); nilt=$(wc -l < "$d/ilt")
    nfdl=$(wc -l < "$d/fdl"); nfnd=$(wc -l < "$d/fnd"); nfrs=$(wc -l < "$d/frs")
    nfos=$(wc -l < "$d/fos"); nfbe=$(wc -l < "$d/fbe"); nfky=$(wc -l < "$d/fky")
    nfrd=$(wc -l < "$d/frd")
    nit=$(wc -l < "$d/it")
    nix=$(wc -l < "$d/ix")
    nks=$(wc -l < "$d/ks")
    npm=$(wc -l < "$d/pm")
    ngk=$(wc -l < "$d/gk")
    ngc=$(wc -l < "$d/gc")
    ngr=$(wc -l < "$d/gr")
    nga=$(wc -l < "$d/ga")
    nqo=$(wc -l < "$d/qo")
    nqs=$(wc -l < "$d/qs")
    nro=$(wc -l < "$d/ro")
fi
echo "$b $nit $nix $nks $npm $ngk $ngc $ngr $nga $nqo $nqs $nro $nfdl $nfnd $nfrs $nfos $nfbe $nfky $nfrd $ncpr $ncpf $ncpl $ncpt $nitt $niod $nirm $niwc $ninw $niec $nipr $nilt" > "$O/$b.count"
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
import os, re, sys, glob, collections

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
# S6-FIX (2026-08-14, the red this gate CAUGHT after the S6 commit): the
# AuditPrep phase added deem_pipeline_handle_seam (+1 fixture, +1 read-once,
# direct-run verified) and S6-A's emitter changes MOVED read-once grounds
# BETWEEN fixtures — deem_pipeline_chain lost its 2 (it no longer carries any)
# while deem_btreemap_source and deem_order_desc_elision gained; the pinned
# total 22 was walked for neither movement because this gate is tier_full and
# the phase's L2 could not see it. Per-fixture map at the fix (gate's own
# sweep, total 25): batch_scan_drain 4, btreemap_source 4, cross_domain 2,
# ctr_family_streams 2, hashmap_source 2, order_desc_elision 5,
# order_elision 3, pipeline_handle_seam 1, source_size 1, three_domain 2.
# PROCESS NOTE, recorded where it bit: the S6 commit was pushed on a chained
# command that did not check L4's rc — the gate was red AT the push. The fix
# commit follows immediately; the discipline fix is the parent's (never chain
# a commit after a gate read).
# R-A (2026-08-15, ADR 0025 — the slice arm dissolves): 185 -> 186, and the
# three numbers that move are the ONE new fixture's own content, predicted
# before the run and then measured. `pass/deem_slice_param_batch_e2e` is the
# added fixture (the other two R-A registrations —
# `pass/stream_slice_stream_seam`, `fail/slice_stream_mutate_under_scan_fail` —
# are outside this gate's `wql_*`/`deem_*` population and correctly do not move
# it). It declares FOUR `deem`s, TWO of which carry `order by`:
#   `__ks` 127 -> 129, `key vector` 127 -> 129, `__ix<k>` 319 -> 321  = +2 each,
# one per sorting query, which is what a sort emits.
# ⚠ AND NOTHING ELSE MOVED — the control that says R-A changed the SCAN and not
# the plan: arrange 598, index 598, hash-join 495, drain+sort 12, `__it_` 12,
# container 205, and all four group-frame families (152/208/13/7) are unchanged
# across the stage. The collapse replaced an indexed walk with a batch pull
# corpus-wide (-149 walks / +149 pulls, measured on two trees — see
# `docs/deem-interpreter-deletion-census.md`) and the plan did not notice,
# because a slice param has no rel-registry identity and therefore contributes
# no ground on this plane at all. That absence is the R-A residual, not a bug
# in this gate: ROUTE P ("param rels") is what would give these scans a ground
# to census, and it is deliberately not taken here.
# ── R-D (2026-08-15, ADR 0025 — the WritWalk cursor round) ──────────────────
# 186 -> 188, and the split between the two new fixtures is the whole ledger
# entry, PREDICTED before the run and then measured:
#
#   `pass/deem_batch_build_side_join` — a BatchStream producer STREAMED onto a
#     join's BUILD side, the query that did not compile before this round
#     (`build_phase_frag` spelled `Iterator::next` unconditionally). It is ONE
#     ordinary hash-join deem, so it moves exactly the numbers one ordinary
#     hash-join deem moves and nothing else:
#       fixtures 186 -> 188 (see below), outq/aoutq 605 -> 606,
#       arrange 598 -> 599, index 598 -> 599, hashjoin 495 -> 496,
#       readonce 25 -> 26, `query output` head 477 -> 478.
#     The arrange/index IDENTITY (598 == 598 -> 599 == 599) is preserved, which
#     is the S2d clause and the sharpest single check that the new pull shape
#     changed HOW the build side reads and not WHAT it builds.
#
#   `pass/wql_writ_walk_cursor` — the cursor/Vec-producer differential. It
#     matches this gate's `wql_*` glob and so moves the FIXTURE count, and it
#     moves NOTHING ELSE: it declares no `deem` at all (it calls
#     `writ_graph_edges` and `writ_walk` directly and compares rows), so it
#     contributes zero plan lines and zero emitted landings. +1 fixture, +0
#     everywhere else, and that asymmetry is the check that it is a producer
#     differential rather than a query.
#
# ⚠ AND THE WRIT GROUNDS DID NOT MOVE — `container` stays 205. That is the
# ROUND'S REFUSAL made visible on this plane: `impl GraphSource for Writ` still
# declares `rel edge = writ_graph_edges`, so all 41 writ rels still ground
# `MG_CONTAINER`. Declaring the rel over the cursor was MEASURED on a throwaway
# build (+3 materialization nodes same-population — first recorded as +5 by a
# population mix the audit caught — +17,048 corpus bytes across the six writ
# fixtures) and reverted; the numbers and the reason are in
# `stdlib/mem/wql/writ_graph.logos`'s REFUSAL block. A later round closing it
# must move `container` 205 -> 164 and land those 41 on a re-walk ground, NOT on
# `MG_REL_BLOCK`/`MG_SECOND_USE`.
EXPECT_FIXTURES   = 188
# The two fixtures that cannot compile ALONE: each `use`s a companion package the
# suite supplies through a lib path (LOCAL_PUBLIB_USERS / LOCAL_WQLMAP_USERS in
# CMakeLists.txt). Named, so that a THIRD compile failure — or one of these two
# starting to compile, which would mean the pin is stale — is red.
EXPECT_FAILED     = {"wql_mapping_cross_module_e2e", "wql_wref_field_pkg"}
EXPECT_DRAIN_SORT = 12    # drain 7 + sort 5, corpus-wide  (S2h: drain 4 -> 7; S3f: 7 -> 8; S3-desc: sort 3 -> 5; S5-D5: drain 8 -> 7, see RE-DERIVATION above)
EXPECT_IT         = 12    # `let mut __it_…` prelude bindings in the artifacts  (S5-D5: 13 -> 12)
EXPECT_ARRANGE    = 599   # Arrange nodes (R-D: +1) — S2d: == EXPECT_INDEX, exactly
EXPECT_HASHJOIN   = 496   # (R-D: +1) `hash join on` strategy decisions (nest 0 + pre-decided)
EXPECT_INDEX      = 599   # (R-D: +1) emitted `__hm`/`__hs`/`__bt` bindings
EXPECT_KS         = 129   # emitted `__ks` sort-key vectors == `key vector` lines  (S4: +2, `wql_group_single_pass_fold_e2e`; R-A: +2, `deem_slice_param_batch_e2e`'s two `order by` queries)
# S3e — THE PERMUTATION VECTORS, PINNED BUT NOT ATTRIBUTED TO A SORT NODE.
# 311 `let mut __ix<k>` across 89 fixtures. This is a COUNT, not an equality
# against the node layer, and the header says why: 85 of the 311, in 39
# fixtures, are emitted where there is no sort at all.
EXPECT_PERM       = 321   # (R-A: +2, `deem_slice_param_batch_e2e`'s two `order by` queries)
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
EXPECT_NOMAT      = {"container": 205, "readonce": 26, "elided": 8}   # R-D: readonce 25 -> 26 (deem_batch_build_side_join)
# ADR 0025 §8 — THE GROUP FRAME, PINNED PER FAMILY AND NOT AS ONE TOTAL. The
# four have different consumers and different lives (`__g_cnt` exists for `avg`,
# `__g_row` for the representative class), so one number would let a family
# vanish while another grew — which is precisely how `elided` used to hide
# inside `readonce`.
EXPECT_FRAME      = {"gkey": 152, "gacc": 208, "gcnt": 13, "grow": 7}
# ── ADR 0025 R-B — THE OUTPUT SEAM (FACT J) ─────────────────────────────────
# The query output was the criterion-1 worklist's largest class (650 bindings,
# unowned) and had no plan node at all. R-B2 gave it one — FIVE heads, not one,
# because the artifact answers five ways and R-B0 measured every one by fire
# count at the emitter BEFORE the node existed:
#
#   477  query output                    plain landing
#    16  query output bounded by limit   `__out.len()` IS the limit guard operand
#     5  query output distinct carrier   the landing IS the dedup structure
#   107  incremental snapshot output     `_snapshot`; no `_stream` door of its own
#   ---  ------------------------------
#   605  == `let mut __out:` bindings    (the QUERY-OUTPUT class)
#    45  rel result                      == `let mut __rout:` bindings
#
# ⚠ THE SPLIT AT 605/45 IS R-B1 AND IT IS WHY THIS PIN CAN EXIST. Before it the
# rel one-shot's landing was also `__out`, so one name held two identities and
# an owner claimed for it would have over-credited 45 rel results as query
# outputs — the same name-only-key defect §1 records against `__rel_*`. The
# rename made the artifact side separable, and only then was the plan side worth
# stating.
#
# ⚠ WHY BOTH NUMBERS AND THE PER-FIXTURE EQUALITY, and not just the totals: the
# totals move with the corpus (one added query moves 605), while the EQUALITY
# does not — it is the FACT B/C/H pattern, "the plan names exactly what the
# artifact builds". A stage that emits a landing without a node, or a node
# without a landing, is red per fixture even if the two errors cancel in the
# total. The totals are here so that a corpus that quietly SHRANK is also red.
EXPECT_OUTQ       = 606   # (R-D: +1) `let mut __out:` query-output landings
EXPECT_OUTR       = 45    # `let mut __rout:` rel one-shot landings
# THE HOMONYM LANDING, PINNED APART. `trama_render.logos:732` emits `let mut
# __out: String` for a TEMPLATE render — same name, different plane, no query
# and no plan node. It is pinned at its own count so that narrowing FACT J's
# grep to `Vec<` did not make it disappear: a landing removed from a population
# must land in another pin or it has simply gone quiet. ⚠ It is NOT in D2's
# criterion-1 population either (that filter is Vec|Buffer|HashMap|BTreeMap), so
# this pin is the only place in the tree that counts it at all.
EXPECT_OUTS       = 1     # `let mut __out: String` trama template render buffers
EXPECT_OUTHEAD    = {"query output": 478, "query output bounded by limit": 16,
                     "query output distinct carrier": 5,
                     "incremental snapshot output": 107, "rel result": 45}
# ── ADR 0025 R-C2 — THE FIXPOINT PLANE'S SIX HEADS (FACT K) ─────────────────
# Every one of these was measured at the emitter BEFORE the node existed (the
# R-B0 discipline, re-run as R-C0 on 9395c3d1), and every one came back at the
# predicted value when the nodes landed. They are pinned per head AND on both
# sides — plan line count and emitted binding count — because the identity is
# what FACT K is about and a pin on one side alone would let both drift
# together.
#
#   222  fixpoint derived frontier    `__nd_<m>`
#   218  fixpoint novelty set         `__rs_<m>`
#   176  fixpoint frontier            `__dl_<m>`
#    46  over-deletion set            `__os_<m>`
#     4  fixpoint novelty lattice     `__best_<m>`   (the four lattice fixtures)
#     4  fixpoint lattice key roster  `__keys_<m>`
#   ---
#   670
#
# ⚠ THE TWO 4s ARE THE ARMS MOST WORTH PINNING, not the least. They fire on
# exactly four fixtures — `wql_rel_sssp_e2e`, `wql_rel_widest_path_e2e`,
# `wql_rel_neg_cycle_abort`, `wql_incr_eligibility_matrix` — and an arm that
# fires four times out of 670 is the arm a refactor silently routes into its
# neighbour. Identity (ii) is what would catch that, and these pins are what
# catch identity (ii) being satisfied by both sides moving at once.
EXPECT_FPHEAD     = {"fixpoint derived frontier": 222,
                     "fixpoint novelty set": 218,
                     "fixpoint frontier": 176,
                     "over-deletion set": 46,
                     "fixpoint novelty lattice": 4,
                     "fixpoint lattice key roster": 4,
                     # R-C3 — 1:1 with `rel result`; see the identity below.
                     "rel dedup set": 45}
# ── ADR 0025 R-E — THE RETRACTION SNAPSHOT'S THREE HEADS (FACT L) ───────────
# Measured at the emitter BEFORE the heads were classified anywhere (the R-B0
# discipline: the criterion-1 instrument was run on the emitter-only tree and
# reported them at G2 as UNCLASSIFIED, with counts). Both sides came back at
# the same three numbers, and the artifact side is derived from a DIFFERENT
# text than the plan side — the copy loop's guard, not the declaration.
#
#   66  retraction snapshot group frame  `__gk` 22 + `__gc` 22 + `__ga_<a>` 22
#   57  retraction snapshot relation     `__edb` 22 + `__tot_<m>` 35
#   22  retraction snapshot latch        `__lat`, one per shadow handle
#   ---
#  145  == the `__cp<i>` class the criterion-1 worklist carried unowned
#
# ⚠ THE 22 IS THE ARM WORTH PINNING HARDEST, for FACT K's reason about its two
# 4s: it is the smallest, it is the one a refactor routes into a neighbour, and
# it is the only one of the three that is a COUNT OF DRIVERS rather than a count
# of fields. `latch != #(shadow handles)` is a wrong answer about |OD|/|S|/|RD|.
EXPECT_CPHEAD     = {"retraction snapshot group frame": 66,
                     "retraction snapshot relation": 57,
                     "retraction snapshot latch": 22}
EXPECT_CPT        = 145   # `let mut __cp<i>: Vec<` shadow-snapshot bindings
# ── ADR 0025 R-E — THE PER-ROUND WORKING SET (FACT M) ───────────────────────
# Measured at G2 in `criterion1_materialization_instrument.sh` on the
# emitter-only tree, before any of the eight was classified anywhere; both sides
# came back at the same eight numbers. Together with FACT L's 145 these 294 are
# the whole of the criterion-1 worklist outside `__rel_*`.
EXPECT_IWHEAD     = {"member total working set": 70,
                     "work counter": 44,
                     "derived suffix": 44,
                     "over-deletion candidates": 35,
                     "removed rows": 35,
                     "epoch input working set": 22,
                     "preserved input": 22,
                     "latch out-param": 22}
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
OUTHEADS = ("query output bounded by limit", "query output distinct carrier",
            "query output", "incremental snapshot output", "rel result")
OUTHEAD = re.compile(r'^\[plan\] (\S+) -> (' + "|".join(OUTHEADS) +
                     r')(?: on .*?)?   \((.*)$')
outhead = collections.Counter()
FRAMEK = {"group frame": "gkey", "accumulator": "gacc",
          "group count": "gcnt", "representative row": "grow"}
# ── ADR 0025 R-C2 — THE FIXPOINT PLANE'S SIX HEADS (FACT K) ──────────────────
# Read by the same rule as the output seam: the head is matched EXACTLY after
# the ` on <row type>` tail is stripped, so a seventh fixpoint form added later
# lands in NEITHER bucket and FACT K reds rather than passing unnamed. The
# order is longest-first for the same reason `OUTHEADS` is: `fixpoint novelty
# lattice` must be tried before `fixpoint novelty set` cannot match it, but
# alternation is first-match and a prefix that is also a whole head would
# otherwise win.
FPHEADS = ("fixpoint derived frontier", "fixpoint novelty lattice",
           "fixpoint lattice key roster", "fixpoint novelty set",
           "fixpoint frontier", "over-deletion set", "rel dedup set")
FPHEAD = re.compile(r'^\[plan\] (\S+) -> (' + "|".join(FPHEADS) +
                    r')(?: on .*?)?   \((.*)$')
FPK = {"fixpoint frontier": "fdl", "fixpoint derived frontier": "fnd",
       "fixpoint novelty set": "frs", "over-deletion set": "fos",
       "fixpoint novelty lattice": "fbe", "fixpoint lattice key roster": "fky",
       "rel dedup set": "frd"}
# THE GROUND SENTENCE PER HEAD, collected so the six can be asserted DISTINCT.
# This is the clause that makes "six heads, not one stamp" checkable instead of
# argued: a blanket stamp is exactly a set of heads sharing one sentence, and an
# emitter that collapsed the six grounds into one would keep every count in this
# gate green while destroying the only property the node was inserted for.
fpground = collections.defaultdict(set)
# ── ADR 0025 R-E — THE RETRACTION SNAPSHOT'S THREE HEADS (FACT L) ────────────
# Same reading rule, same longest-first ordering discipline. Three heads, three
# emitted field families, ONE PARTITION — `cpr + cpf + cpl == cpt` on both
# sides, plus the `latch == 1 per shadow handle` clause, which is what makes the
# 22 a count of transactional retractions rather than a slice of a 145 total.
CPHEADS = ("retraction snapshot group frame", "retraction snapshot relation",
           "retraction snapshot latch")
CPHEAD = re.compile(r'^\[plan\] (\S+) -> (' + "|".join(CPHEADS) +
                    r')(?: on .*?)?   \((.*)$')
CPK = {"retraction snapshot relation": "cpr",
       "retraction snapshot group frame": "cpf",
       "retraction snapshot latch": "cpl"}
# The grounds, collected to be asserted DISTINCT — the FACT K clause that caught
# probe P4 (all grounds collapsed to one sentence, every count green). A stamp
# is arithmetically invisible here too.
cpground = collections.defaultdict(set)
# ── ADR 0025 R-E — THE PER-ROUND WORKING SET'S EIGHT HEADS (FACT M) ─────────
# Longest-first, same discipline: `member total working set` and
# `epoch input working set` share no prefix, but `work counter` must not be able
# to match inside a longer head added later, so the tuple is sorted by length
# and the match is anchored and exact after the ` on <row type>` tail.
IWHEADS = ("over-deletion candidates", "epoch input working set",
           "member total working set", "preserved input", "latch out-param",
           "derived suffix", "removed rows", "work counter")
IWHEAD = re.compile(r'^\[plan\] (\S+) -> (' + "|".join(IWHEADS) +
                    r')(?: on .*?)?   \((.*)$')
IWK = {"member total working set": "itt", "over-deletion candidates": "iod",
       "removed rows": "irm", "work counter": "iwc", "derived suffix": "inw",
       "epoch input working set": "iec", "preserved input": "ipr",
       "latch out-param": "ilt"}
iwground = collections.defaultdict(set)

tot = dict(drain=0, sort=0, arrange=0, it=0, ix=0, ks=0, kv=0, hj=0, pm=0,
           container=0, readonce=0, elided=0, materialize=0, stream=0,
           gkey=0, gcnt=0, grow=0, gacc=0,
           agkey=0, agcnt=0, agrow=0, agacc=0,
           outq=0, outr=0, aoutq=0, aouts=0, aoutr=0,
           fdl=0, fnd=0, frs=0, fos=0, fbe=0, fky=0, frd=0,
           afdl=0, afnd=0, afrs=0, afos=0, afbe=0, afky=0, afrd=0,
           cpr=0, cpf=0, cpl=0, cpt=0,
           acpr=0, acpf=0, acpl=0, acpt=0,
           itt=0, iod=0, irm=0, iwc=0, inw=0, iec=0, ipr=0, ilt=0,
           aitt=0, aiod=0, airm=0, aiwc=0, ainw=0, aiec=0, aipr=0, ailt=0)
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
    nd = dict(outq=0, outr=0, drain=0, sort=0, arrange=0, hj=0, kv=0,
              gkey=0, gcnt=0, grow=0, gacc=0,
              fdl=0, fnd=0, frs=0, fos=0, fbe=0, fky=0, frd=0,
              cpr=0, cpf=0, cpl=0, cpt=0,
              itt=0, iod=0, irm=0, iwc=0, inw=0, iec=0, ipr=0, ilt=0)
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
        # ADR 0025 R-B2 — the output seam. Matched on the head EXACTLY (the
        # ` on <elem type>` tail is stripped first), so a sixth head added by a
        # later stage lands in NEITHER bucket and FACT J reds — which is the
        # behaviour asked for: a new output form must come here and say which
        # landing it names, exactly as G2 forces in the instrument.
        om = OUTHEAD.match(line)
        if om:
            h = om.group(2)
            outhead[h] += 1
            nd["outr" if h == "rel result" else "outq"] += 1
            if not om.group(3).strip().rstrip(")").strip():
                bad(f"[{b}] an output-seam `{h}` line carries an EMPTY ground — "
                    f"a landing named and not explained")
        # ADR 0025 R-E — the per-round working set's eight seams (FACT M).
        wm = IWHEAD.match(line)
        if wm:
            h = wm.group(2)
            nd[IWK[h]] += 1
            wgnd = wm.group(3).strip().rstrip(")").strip()
            if not wgnd:
                bad(f"[{b}] a working-set `{h}` line carries an EMPTY ground — "
                    f"a landing named and not explained")
            else:
                iwground[h].add(wgnd)
        # ADR 0025 R-E — the retraction snapshot's three seams (FACT L).
        cm = CPHEAD.match(line)
        if cm:
            h = cm.group(2)
            nd[CPK[h]] += 1
            cgnd = cm.group(3).strip().rstrip(")").strip()
            if not cgnd:
                bad(f"[{b}] a retraction-snapshot `{h}` line carries an EMPTY "
                    f"ground — a landing named and not explained")
            else:
                cpground[h].add(cgnd)
        fm = FPHEAD.match(line)
        if fm:
            h = fm.group(2)
            nd[FPK[h]] += 1
            gnd = fm.group(3).strip().rstrip(")").strip()
            if not gnd:
                bad(f"[{b}] a fixpoint `{h}` line carries an EMPTY ground — a "
                    f"landing named and not explained")
            else:
                fpground[h].add(gnd)
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
    nd["cpt"] = nd["cpr"] + nd["cpf"] + nd["cpl"]
    for k in ("outq", "outr", "drain", "sort", "arrange", "hj", "kv",
              "gkey", "gcnt", "grow", "gacc",
              "fdl", "fnd", "frs", "fos", "fbe", "fky", "frd",
              "cpr", "cpf", "cpl", "cpt",
              "itt", "iod", "irm", "iwc", "inw", "iec", "ipr", "ilt"):
        tot[k] += nd[k]

    cf = os.path.join(OD, b + ".count")
    nit = nix = nks = npm = 0
    agkey = agcnt = agrow = agacc = 0
    aoutq = aouts = aoutr = 0
    afp = dict(fdl=0, fnd=0, frs=0, fos=0, fbe=0, fky=0, frd=0)
    acp = dict(cpr=0, cpf=0, cpl=0, cpt=0)
    aiw = dict(itt=0, iod=0, irm=0, iwc=0, inw=0, iec=0, ipr=0, ilt=0)
    if os.path.exists(cf):
        (_, a, c, e, g, gk, gc, gr, ga, qo, qs, ro,
         xdl, xnd, xrs, xos, xbe, xky, xrd,
         xcpr, xcpf, xcpl, xcpt,
         xtt, xod, xrm, xwc, xnw, xec, xpr, xlt) = open(cf).read().split()
        acp = dict(cpr=int(xcpr), cpf=int(xcpf), cpl=int(xcpl), cpt=int(xcpt))
        aiw = dict(itt=int(xtt), iod=int(xod), irm=int(xrm), iwc=int(xwc),
                   inw=int(xnw), iec=int(xec), ipr=int(xpr), ilt=int(xlt))
        nit, nix, nks, npm = int(a), int(c), int(e), int(g)
        agkey, agcnt, agrow, agacc = int(gk), int(gc), int(gr), int(ga)
        aoutq, aouts, aoutr = int(qo), int(qs), int(ro)
        afp = dict(fdl=int(xdl), fnd=int(xnd), frs=int(xrs),
                   fos=int(xos), fbe=int(xbe), fky=int(xky), frd=int(xrd))
    else:
        bad(f"[{b}] no artifact count file — the emitted side was not read")
    for k in afp:
        tot["a" + k] += afp[k]
    for k in acp:
        tot["a" + k] += acp[k]
    for k in aiw:
        tot["a" + k] += aiw[k]
    tot["it"] += nit
    tot["ix"] += nix
    tot["ks"] += nks
    tot["pm"] += npm
    tot["aoutq"] += aoutq
    tot["aouts"] += aouts
    tot["aoutr"] += aoutr
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

    # FACT J, per fixture — THE OUTPUT SEAM'S NODES ARE ITS EMITTED LANDINGS.
    # Four of the five heads (`query output`, `… bounded by limit`,
    # `… distinct carrier`, `incremental snapshot output`) name a `let mut
    # __out:` binding; the fifth (`rel result`) names a `let mut __rout:`. The
    # identity is by construction at the five emitter sites, and stated here for
    # the reason FACT H gives: an emitter that grew a sixth output form, or one
    # that stopped emitting a landing it still announces, is RED rather than
    # merely different. ⚠ Skipped for the two standalone-failing fixtures for
    # FACT B's reason: they write no dump, so the artifact side would be 0 by
    # absence rather than by measurement.
    if b not in EXPECT_FAILED:
        if nd["outq"] != aoutq:
            bad(f"[{b}] {nd['outq']} query-output plan line(s) vs {aoutq} "
                f"`let mut __out:` landing(s) — the output the plan names is "
                f"not the output the artifact builds")
        if nd["outr"] != aoutr:
            bad(f"[{b}] {nd['outr']} `rel result` plan line(s) vs {aoutr} "
                f"`let mut __rout:` landing(s) — the rel seam the plan names is "
                f"not the one the artifact builds")

    # ── FACT K, per fixture — THE FIXPOINT PLANE'S NODES ARE ITS LANDINGS ──
    # (ADR 0025 R-C2.) Six heads, six emitted binding families, one identity
    # each — the FACT H/J pattern applied to the region that had NO node at all
    # until R-C2: 670 of the criterion-1 worklist's 1382 bindings were emitted
    # by `_scc`/`_od`/`_odp` and named by nothing.
    #
    # ⚠ WHY PER FIXTURE AND NOT ONLY IN TOTAL, restated because it is the whole
    # value: the totals move with the corpus, the EQUALITY does not. An emitter
    # that stopped declaring a member's shadow set while still announcing it,
    # or declared one it never announced, is red here even when the two errors
    # cancel corpus-wide — and both of those are wrong ANSWERS (a member with
    # no novelty structure never terminates), not slow ones.
    #
    # ⚠⚠ THE THREE CLAUSE KINDS BELOW ARE NOT REDUNDANT, AND THE PROOF IS A
    # MEASURED CONTROL RATHER THAN AN ARGUMENT (R-C2 probes, each built and
    # restored to green between runs). The identities are invariant under a
    # pure RE-ROUTING between the heads they relate, which is exactly the
    # refactor most likely to happen:
    #
    #   probe P1  all six heads muted        -> identities GREEN (0 == 0 + 0);
    #             caught by the plan-vs-artifact clauses (159 reds) and by the
    #             corpus per-head pins. G4 in the instrument independently
    #             refused all five ACC credits (rc=2, 452 bindings returned to
    #             the worklist) — ownership is not self-certifying.
    #   probe P2  lattice arm routed into the set head (`__best`/`__keys` 4 -> 0,
    #             `__rs` 218 -> 222)         -> IDENTITY (ii) GREEN, because
    #             222 == 222 + 0 still holds. The lattice plane was destroyed
    #             and the arithmetic closed anyway. Caught ONLY by the
    #             plan-vs-artifact clauses and the per-head pins.
    #   probe P3  the OD node dropped alone  -> identity (i) RED on 26 fixtures,
    #             plus the artifact clause. This is the case the identity is
    #             for: a structure that DISAPPEARS rather than moves.
    #   probe P4  all six grounds collapsed to one sentence -> EVERY count and
    #             BOTH identities green; caught only by the distinct-grounds
    #             clause at the bottom of this file. A blanket stamp is
    #             arithmetically invisible.
    #
    # So: the artifact clauses catch a MISROUTED structure, the identities catch
    # a DROPPED one, and the ground clause catches a node that stopped saying
    # anything. Delete any one of the three and a measured defect walks through.
    if b not in EXPECT_FAILED:
        for k, what, why in (
            ("fdl", "`__dl_<m>` driving delta",
             "the frontier the plan names is not the one the artifact builds"),
            ("fnd", "`__nd_<m>` derived frontier",
             "the epoch derives into a landing the plan did not name"),
            ("frs", "`__rs_<m>` novelty shadow set",
             "a member tests novelty with a set the plan never announced"),
            ("fos", "`__os_<m>` over-deletion set",
             "the DRed |OD| set the plan names is not the one phase 1 builds"),
            ("fbe", "`__best_<m>` improvement map",
             "a lattice member's novelty map is not the one the plan names"),
            ("fky", "`__keys_<m>` lattice key roster",
             "the lattice roster the plan names is not the one emitted"),
            ("frd", "`__rds` one-shot rel dedup set",
             "the rel helper dedups with a set the plan never announced"),
        ):
            if nd[k] != afp[k]:
                bad(f"[{b}] {nd[k]} `{what}` plan line(s) vs {afp[k]} emitted "
                    f"binding(s) — {why}")

        # ⚠⚠ IDENTITY (i) — `__nd == __dl + __os`, PER FIXTURE.
        # The next-delta is emitted at three sites; two of them emit a driving
        # delta beside it and DRed phase 1a does not, because phase 1a is a
        # single pass over the total with no round to drive. So the residual
        # `__nd` without a `__dl` is exactly phase 1a's frontier — and phase 1a
        # emits one per member exactly as the over-deletion set does. The
        # identity is therefore a claim a reader could be WRONG about: the OD
        # set and the phase-1a frontier range over the same member set. Break
        # it in either direction and `__odn_<m>` is compared against a
        # `__tot_.len() - __keep_<m>` computed over a different membership,
        # which returns `RetractAbsent` on a legal retraction or accepts an
        # illegal one. Nothing else in this tree sees that.
        if nd["fnd"] != nd["fdl"] + nd["fos"]:
            bad(f"[{b}] fixpoint identity (i) broken: {nd['fnd']} derived "
                f"frontier(s) != {nd['fdl']} driving delta(s) + {nd['fos']} "
                f"over-deletion set(s) — a derived frontier exists that is "
                f"neither driven by a delta nor accounted by an OD set")
        # ⚠⚠ IDENTITY (ii) — `__nd == __rs + __best`, PER FIXTURE.
        # EVERY derived member has EXACTLY ONE novelty structure, and which one
        # is the set/lattice choice. A member with NEITHER derives rows it never
        # tests for novelty — a non-terminating fixpoint. A member with BOTH
        # tests novelty twice under two disagreeing definitions (membership vs
        # improvement) and the answer depends on which gate ran first. Both are
        # wrong answers; neither is visible to any other clause in this gate.
        if nd["fnd"] != nd["frs"] + nd["fbe"]:
            bad(f"[{b}] fixpoint identity (ii) broken: {nd['fnd']} derived "
                f"frontier(s) != {nd['frs']} shadow set(s) + {nd['fbe']} "
                f"improvement map(s) — a member has two novelty structures or "
                f"none")
        # ⚠ IDENTITY (iii) — `__rds == __rout`, PER FIXTURE (R-C3).
        # The one-shot rel helper emits its landing and its dedup set in ONE
        # quote block, so the equality is by construction TODAY — and that is
        # the argument for pinning it, not against. It is the property the
        # R-C3 rename was for: before it, the dedup set shared a name prefix
        # with 218 fixpoint shadow sets and no census could count it at all,
        # so "one dedup set per rel result" was unstatable rather than true.
        # A helper that grew a second landing, or one that started sharing a
        # set between two rels, breaks it — and a shared set is a WRONG ANSWER
        # (rows of rel A suppressed as duplicates of rel B), not a slow one.
        # ── FACT L, per fixture — THE RETRACTION SNAPSHOT (ADR 0025 R-E) ──
        # Three heads, three emitted field families, read from the artifact's
        # own copy-loop guard rather than from the binding name (which is
        # positional and cannot answer). Same three clause kinds as FACT K, for
        # the same three failure modes: the per-head clauses catch a MISROUTED
        # family, the partition catches a DROPPED one (or a fourth family added
        # with no head), and the distinct-grounds clause at the bottom of this
        # file catches a node that stopped saying anything.
        for k, what, why in (
            ("cpr", "`__edb`/`__tot_<m>` row-store snapshot",
             "the retraction copies a row store the plan did not name"),
            ("cpf", "`__gk`/`__gc`/`__ga_<a>` group-frame snapshot",
             "the retraction copies a group-frame column the plan did not name"),
            ("cpl", "`__lat` latch snapshot",
             "the latch vector the plan names is not the one the driver copies"),
        ):
            if nd[k] != acp[k]:
                bad(f"[{b}] {nd[k]} `{what}` plan line(s) vs {acp[k]} emitted "
                    f"binding(s) — {why}")
        # ⚠⚠ IDENTITY (iv) — THE SNAPSHOT IS A PARTITION, PER FIXTURE.
        # `cpr + cpf + cpl == #(let mut __cp<i>: Vec<)` on the ARTIFACT side.
        # This is the clause the three per-head clauses cannot supply: they
        # compare plan against artifact family by family, so a handle field
        # family added to the shadow snapshot AND given no head is invisible to
        # all three — it is copied, it is committed, and no `[plan]` line and no
        # grep ever mentions it. That is precisely the state `__cp` itself was
        # in before this stage. The total is read off the DECLARATIONS and the
        # parts off the COPY LOOPS, so the two sides come from different text.
        if acp["cpr"] + acp["cpf"] + acp["cpl"] != acp["cpt"]:
            bad(f"[{b}] snapshot identity (iv) broken: {acp['cpr']} + "
                f"{acp['cpf']} + {acp['cpl']} != {acp['cpt']} emitted `__cp<i>` "
                f"binding(s) — the shadow handle copies a field family that no "
                f"retraction-snapshot head names")
        # ⚠ IDENTITY (v) — ONE LATCH PER SHADOW HANDLE, PER FIXTURE.
        # `__lat` is emitted exactly once per `_dred` driver, so the latch count
        # IS the number of transactional retraction drivers this fixture
        # compiles — which is what makes the corpus figure 22 a count of
        # something rather than a slice of 145. It also bounds the other two:
        # a fixture with row-store or frame snapshots and NO latch has a shadow
        # handle whose `_cpt` out-param survives the rollback, which is a
        # retraction that reports the previous epoch's |OD|/|S|/|RD| — a wrong
        # answer to `is_converged()`, not a slow one.
        if acp["cpl"] == 0 and acp["cpt"] != 0:
            bad(f"[{b}] snapshot identity (v) broken: {acp['cpt']} `__cp<i>` "
                f"binding(s) and ZERO `__lat` snapshots — a shadow handle "
                f"without its latch")
        # ── FACT M, per fixture — THE PER-ROUND WORKING SET (R-E) ─────
        for k, what, why in (
            ("itt", "`__tt<i>` member working total",
             "a driver appends into a total the plan did not name"),
            ("iod", "`__odv<i>` over-deletion candidates",
             "DRed phase 1 fills a candidate vector the plan never announced"),
            ("irm", "`__rmv<i>` removed rows",
             "the compact removes into a vector the plan did not name"),
            ("iwc", "`__wcd` work counter",
             "a driver call carries a work counter the plan did not name"),
            ("inw", "`__nw` derived suffix",
             "the fold reads a derived-row vector the plan never announced"),
            ("iec", "`__ecp` epoch input working set",
             "the epoch copies its input into a landing the plan did not name"),
            ("ipr", "`__pres` preserved input",
             "the retraction builds its surviving input unannounced"),
            ("ilt", "`__lt` latch out-param",
             "the latch vector the plan names is not the one `_cpt` writes"),
        ):
            if nd[k] != aiw[k]:
                bad(f"[{b}] {nd[k]} `{what}` plan line(s) vs {aiw[k]} emitted "
                    f"binding(s) — {why}")
        # ⚠ IDENTITY (vi) — `__odv == __rmv`, PER FIXTURE.
        # One candidate vector and one removed vector PER MEMBER, declared in the
        # same loop. The claim a reader could be wrong about is that they are the
        # same population: they are not — OD_<m> is what phase 1 REACHED and
        # `__rmv<m>` is what `_cpt` actually TOOK OUT, and a candidate with a
        # surviving derivation stays. The counts match because there is one of
        # each per member; the CONTENTS differ, and the signed fold reads the
        # second. An emitter that started sharing one vector for both folds
        # retractions at `-1` over rows that were never removed.
        if aiw["iod"] != aiw["irm"]:
            bad(f"[{b}] working-set identity (vi) broken: {aiw['iod']} `__odv<i>` "
                f"vs {aiw['irm']} `__rmv<i>` — the compact's input and output "
                f"vectors are no longer one per member")
        # ⚠ IDENTITY (vii) — `__lt == __pres`, PER FIXTURE.
        # Both are emitted exactly once in the `_dred` block, so this counts
        # RETRACTION DRIVERS two ways from two different families. A driver that
        # kept its preserved input and lost its latch commits `__h.__lat` from a
        # stale vector — the next epoch's incrementality assertion then reads the
        # PREVIOUS retraction's |RD|, which is a wrong answer about convergence.
        if aiw["ilt"] != aiw["ipr"]:
            bad(f"[{b}] working-set identity (vii) broken: {aiw['ilt']} `__lt` "
                f"vs {aiw['ipr']} `__pres` — a retraction driver is missing one "
                f"of the two collections every `_dred` block declares")
        # ⚠⚠ IDENTITY (viii) — `__lt == __cp` LATCH SNAPSHOT, PER FIXTURE, AND
        # IT CROSSES FACT L AND FACT M. The shadow handle snapshots `__lat`
        # exactly once (FACT L's latch head) and the driver declares its `__lt`
        # out-param exactly once, in the SAME emitted function. So a `_dred` that
        # has one and not the other either commits a latch it never wrote or
        # writes one the rollback cannot restore — and neither FACT alone can
        # see it, because each is internally consistent on its own side.
        if aiw["ilt"] != acp["cpl"]:
            bad(f"[{b}] identity (viii) broken: {aiw['ilt']} `__lt` out-param(s) "
                f"vs {acp['cpl']} `__lat` snapshot(s) — the retraction driver "
                f"and its shadow handle disagree about the latch")
        if acp["cpl"] != 0 and acp["cpt"] < 5 * acp["cpl"]:
            bad(f"[{b}] snapshot identity (v) broken: {acp['cpl']} latch "
                f"snapshot(s) but only {acp['cpt']} `__cp<i>` binding(s) — "
                f"every shadow handle copies `__gk`, `__gc`, `__edb`, at least "
                f"one `__tot_<m>` and its `__lat`, so the floor is 5 per latch")
        if nd["frd"] != nd["outr"]:
            bad(f"[{b}] identity (iii) broken: {nd['frd']} `rel dedup set` "
                f"line(s) vs {nd['outr']} `rel result` line(s) — a one-shot rel "
                f"landing without its novelty set, or a set without a landing")

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
        ("outq", EXPECT_OUTQ, "query-output plan lines"),
        ("outr", EXPECT_OUTR, "`rel result` plan lines"),
        ("aoutq", EXPECT_OUTQ, "`let mut __out: Vec<` query-output landings"),
        ("aouts", EXPECT_OUTS, "`let mut __out: String` trama render buffers"),
        ("aoutr", EXPECT_OUTR, "`let mut __rout:` rel landings"),
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
# FACT J, per HEAD — the five-valued answer is pinned VALUE BY VALUE, not as
# one total of 650. The whole ground for inserting this node (against S5's
# recorded refusal of a constant one) is that the answer VARIES across the
# corpus; a single total would stay green while the emitter collapsed all five
# arms into one word, which is exactly the node S5 refused. Each arm therefore
# carries its own fire count, and an arm that goes to ZERO is red — the "green
# over a branch that never executed" rule, pinned rather than hoped for.
for h, want in sorted(EXPECT_OUTHEAD.items()):
    if outhead[h] != want:
        bad(f"corpus total: {outhead[h]} `{h}` output-seam line(s), pinned {want}")
# ── FACT K, corpus totals + the two identities + the SIX DISTINCT GROUNDS ────
# (ADR 0025 R-C2.) Each head at its own count, for the reason FACT J gives per
# head: a single 670 total would stay green while the emitter collapsed six arms
# into one, which is precisely the blanket stamp this node exists NOT to be.
for h, want in sorted(EXPECT_FPHEAD.items()):
    if tot[FPK[h]] != want:
        bad(f"corpus total: {tot[FPK[h]]} `{h}` fixpoint line(s), pinned {want}")
    if tot["a" + FPK[h]] != want:
        bad(f"corpus total: {tot['a'+FPK[h]]} emitted binding(s) for `{h}`, "
            f"pinned {want} — the artifact side of FACT K moved")
# The two identities in TOTAL as well as per fixture: the per-fixture clause
# catches a plan that stopped describing its own artifact, this catches a corpus
# that quietly SHRANK a whole role away.
if tot["fnd"] != tot["fdl"] + tot["fos"]:
    bad(f"corpus: fixpoint identity (i) broken — {tot['fnd']} != {tot['fdl']} "
        f"+ {tot['fos']}")
if tot["fnd"] != tot["frs"] + tot["fbe"]:
    bad(f"corpus: fixpoint identity (ii) broken — {tot['fnd']} != {tot['frs']} "
        f"+ {tot['fbe']}")
# ⚠ THE ANTI-STAMP CLAUSE, AND IT IS THE ONE THAT CANNOT BE SATISFIED BY
# COUNTING. Six heads whose grounds were the SAME sentence would keep every
# count above green and would be exactly the constant-word node S5 refused for
# the output seam. So: each head must carry EXACTLY ONE ground (an emitter that
# started varying a role's explanation per site has stopped stating a property
# of the role), and the six grounds must be SIX DISTINCT sentences. `explain()`
# prints these, and a reader who gets the same sentence for a driving delta and
# an over-deletion set has been told nothing.
seen = {}
for h in sorted(FPHEADS):
    gs = fpground.get(h, set())
    if len(gs) == 0:
        bad(f"head `{h}` carries NO ground anywhere in the corpus — either the "
            f"head stopped firing or its sentence went empty")
    elif len(gs) > 1:
        bad(f"head `{h}` carries {len(gs)} DIFFERENT grounds across the corpus "
            f"— a role's explanation must be a property of the role, not of "
            f"the site")
    else:
        g = next(iter(gs))
        if g in seen:
            bad(f"heads `{seen[g]}` and `{h}` publish the SAME ground sentence "
                f"— six heads sharing one explanation is a blanket stamp, which "
                f"is the node S5 refused for the output seam")
        seen[g] = h
# ── FACT M, corpus totals + EIGHT DISTINCT GROUNDS (R-E) ────────────────────
for h, want in sorted(EXPECT_IWHEAD.items()):
    if tot[IWK[h]] != want:
        bad(f"corpus total: {tot[IWK[h]]} `{h}` working-set line(s), pinned {want}")
    if tot["a" + IWK[h]] != want:
        bad(f"corpus total: {tot['a'+IWK[h]]} emitted binding(s) for `{h}`, "
            f"pinned {want} — the artifact side of FACT M moved")
if tot["iod"] != tot["irm"]:
    bad(f"corpus: working-set identity (vi) broken — {tot['iod']} != {tot['irm']}")
if tot["ailt"] != tot["aipr"] or tot["ailt"] != tot["acpl"]:
    bad(f"corpus: identities (vii)/(viii) broken — {tot['ailt']} `__lt`, "
        f"{tot['aipr']} `__pres`, {tot['acpl']} `__lat` snapshot(s)")
for h in sorted(IWHEADS):
    gs = iwground.get(h, set())
    if len(gs) == 0:
        bad(f"head `{h}` carries NO ground anywhere in the corpus — either the "
            f"head stopped firing or its sentence went empty")
    elif len(gs) > 1:
        bad(f"head `{h}` carries {len(gs)} DIFFERENT grounds across the corpus "
            f"— a role's explanation must be a property of the role")
    else:
        g = next(iter(gs))
        if g in seen:
            bad(f"heads `{seen[g]}` and `{h}` publish the SAME ground sentence "
                f"— a blanket stamp over the incremental working set")
        seen[g] = h
# ── FACT L, corpus totals + the partition + THREE DISTINCT GROUNDS (R-E) ────
for h, want in sorted(EXPECT_CPHEAD.items()):
    if tot[CPK[h]] != want:
        bad(f"corpus total: {tot[CPK[h]]} `{h}` snapshot line(s), pinned {want}")
    if tot["a" + CPK[h]] != want:
        bad(f"corpus total: {tot['a'+CPK[h]]} emitted binding(s) for `{h}`, "
            f"pinned {want} — the artifact side of FACT L moved")
if tot["acpt"] != EXPECT_CPT:
    bad(f"corpus total: {tot['acpt']} emitted `__cp<i>` snapshot binding(s), "
        f"pinned {EXPECT_CPT}")
if tot["cpr"] + tot["cpf"] + tot["cpl"] != tot["acpt"]:
    bad(f"corpus: snapshot identity (iv) broken — {tot['cpr']} + {tot['cpf']} "
        f"+ {tot['cpl']} != {tot['acpt']} emitted binding(s)")
# The anti-stamp clause, verbatim in intent from FACT K's: three heads sharing
# one sentence is 145 × "snapshot", which is the constant-word node S5 refused.
for h in sorted(CPHEADS):
    gs = cpground.get(h, set())
    if len(gs) == 0:
        bad(f"head `{h}` carries NO ground anywhere in the corpus — either the "
            f"head stopped firing or its sentence went empty")
    elif len(gs) > 1:
        bad(f"head `{h}` carries {len(gs)} DIFFERENT grounds across the corpus "
            f"— a role's explanation must be a property of the role")
    else:
        g = next(iter(gs))
        if g in seen:
            bad(f"heads `{seen[g]}` and `{h}` publish the SAME ground sentence "
                f"— a blanket stamp over the retraction snapshot")
        seen[g] = h
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
print("── INCREMENTAL WORKING SET (R-E, FACT M) ─────────────────────────────")
for h in ("member total working set", "work counter", "derived suffix",
          "over-deletion candidates", "removed rows", "epoch input working set",
          "preserved input", "latch out-param"):
    k = IWK[h]
    print(f"  {tot[k]:6d}  {h:<28s} plan  |  {tot['a'+k]:6d} emitted")
print("── RETRACTION SNAPSHOT (R-E, FACT L) ─────────────────────────────────")
for h in ("retraction snapshot group frame", "retraction snapshot relation",
          "retraction snapshot latch"):
    k = CPK[h]
    print(f"  {tot[k]:6d}  {h:<28s} plan  |  {tot['a'+k]:6d} emitted")
print(f"  {tot['acpt']:6d}  {'__cp<i> total (partition)':<28s}")
print("── FIXPOINT PLANE (R-C2, FACT K) ─────────────────────────────────────")
for h in ("fixpoint derived frontier", "fixpoint novelty set",
          "fixpoint frontier", "over-deletion set",
          "fixpoint novelty lattice", "fixpoint lattice key roster",
          "rel dedup set"):
    k = FPK[h]
    print(f"  {tot[k]:6d}  {h:<28s} plan  |  {tot['a'+k]:6d} emitted")
print(f"  identities: nd {tot['fnd']} == dl {tot['fdl']} + os {tot['fos']}"
      f"   |   nd {tot['fnd']} == rs {tot['frs']} + best {tot['fbe']}")
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
