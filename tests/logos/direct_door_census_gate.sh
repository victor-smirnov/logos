#!/usr/bin/env bash
# direct_door_census_gate.sh LOGOSC PASS_DIR FACTS_ROOT
#
# THE DIRECT-DOOR CENSUS OVER THE **WHOLE** `tests/logos/pass` CORPUS —
# ADR 0025 §12 `direct`.
#
# ── WHY THIS GATE EXISTS: A POPULATION NOBODY SWEPT ─────────────────────────
# `logos_09_pull_shape` and `logos_09_plan_ground_census` both sweep
# `pass/wql_*.logos` + `pass/deem_*.logos` and nothing else. That glob is 191 of
# the 2180 pass fixtures. The §12 direct door — the four emitted items per
# eligible query, cross-pinned 1:1:1:1 in `pull_shape_gate.sh` — is emitted by
# `rexpr_walk::emit_stream_direct` for ANY query in ANY fixture, so the other
# 1989 fixtures could hold half-emitted doors and no gate anywhere would move.
# MEASURED: they hold 26 doors, on 11 fixtures, versus the 10 the two existing
# gates pin. A door that lost its facade in `memoria_ctr_plan_pushdown` reddened
# NOTHING before this file existed.
#
# ── WHY A SECOND GATE AND NOT A WIDER POPULATION ON THE TWO EXISTING ONES ───
# The alternative considered (and rejected) was widening `pull_shape_gate.sh`'s
# `FIXTURES=(...)` from 191 to 2180 fixtures, which would have kept one
# instrument. Four reasons it is the wrong trade, in decreasing weight:
#
#   1. IT WOULD CHANGE WHAT THE CRITERIA NUMBERS MEAN. `pull_shape`'s pins are
#      ADR 0025's criterion-2 accounting: 1031 batch pulls against 3301 indexed
#      walks, bucketed by four positive rules over the WALK SUBJECT. Those
#      buckets were derived over the query corpus; the other 1989 fixtures emit
#      ~100 `__container_item` units, `trama`, `schema_catalog`, and ~90 kinds
#      of one-off metaprog hook, none of which is a query plane. Folding them in
#      does not make criterion 2 broader — it makes its denominator a different
#      quantity wearing the same pin. `walks_unclaimed` (pinned 0) would take
#      the whole non-query tree and every new bucket rule invented to empty it
#      would be a rule about something ADR 0025 does not measure.
#   2. THE PINS ARE NOT SEPARABLE FROM THE SWEEP. Widening the population
#      re-derives all 40 pins in `pull_shape` and all of `plan_ground_census`'s
#      (which additionally partitions a refusal VOCABULARY) — every one with its
#      own accounting, for a change whose subject is 6 regexes.
#   3. COST. The 191-fixture sweep is ~20 s; the 2180-fixture sweep measured
#      3 m 58 s wall / 107 min CPU at -P32. Paying that inside `pull_shape` buys
#      nothing for the 40 pins that are already answered by 191.
#   4. THE WIDE POPULATION NEEDS ARCHIVES. 50 pass fixtures do not compile
#      standalone (they consume fixture archives via `-l`); teaching the two
#      existing gates the archive map adds a failure surface to instruments that
#      currently need none.
#      ⚠ REASON 4 IS RETIRED BY task #85 AND KEPT AS HISTORY. Nothing here
#      knows an archive map any more: the facts come off the compile each
#      fixture's own ctest test already runs, with the flags CMake gave it.
#      Reasons 1-3 are what still keeps this gate separate — 3 in particular,
#      now that the cost is a FOLD rather than 2254 recompiles.
#
# So: a THIN gate, carrying ONLY the door facts, over the WHOLE corpus. The
# populations and their sum are asserted here (CLAUSE 1) so the two instruments
# cannot both believe the other covers a fixture.
#
# ── THE PARTITION, STATED ───────────────────────────────────────────────────
#   CORPUS    every `tests/logos/pass/*.logos`                          2180
#   GLOB      basename matches `wql_*` or `deem_*` — `pull_shape`'s and
#             `plan_ground_census`'s population                          191
#   NONGLOB   every other pass fixture — pinned by NOTHING before this   1989
#   GLOB + NONGLOB == CORPUS, and the two rules are asserted DISJOINT
#   (no basename may match both), so no fixture is counted twice or dropped.
# THIS GATE SWEEPS BOTH HALVES. It re-measures the GLOB half too, at 10 doors,
# which is `pull_shape`'s `dx_struct`/`dx_inherent`/`dx_forward`/`dx_facade` pin
# read by an independent sweep — and it can see two GLOB fixtures `pull_shape`
# CANNOT (`wql_mapping_cross_module_e2e`, `wql_wref_field_pkg` need `-l`
# archives and fail to compile in that gate's sweep, silently contributing no
# dumps). Both measure 0 doors, which is why `pull_shape`'s 10 is not
# understated — a fact nothing recorded until this gate measured it.
#
# ── WHAT A DOOR IS, POSITIVELY, AND WHY PROVENANCE IS PART OF THE RULE ──────
# A §12 door is FIVE spellings of one decision (`emit_simple`'s `dx_on`):
#   dx_struct    `#[borrow_carrying]` + `pub struct <Q>Dx… {`
#   dx_inherent  `pub fn next_batch(self: &mut <Q>Dx…) -> Option<&[`
#   dx_forward   `fn next(&mut self) -> Option<RowsBatch<`   (trait method)
#   dx_facade    `-> Result<<Q>Dx…, ElError>`                (the opener)
#   dx_impl      `impl BatchStream<RowsBatch<…>> for <Q>Dx`
# plus the forwarding BODY `return self.next_batch();`, counted apart from its
# header for the reason `pull_shape` records (a copied column is not a
# cross-pin). All six are pinned EQUAL: pinning one would let four go missing.
#
# ⚠ THE SHAPE ALONE IS NOT THE RULE, AND THE CORPUS PROVES IT. The wide sweep
# found `pass/bc_d8_quote_field_split_admit` emitting `pub struct QuoteDx` under
# `#[borrow_carrying]` with a matching `next_batch` — 2 of the 5 spellings, from
# `gen_quote`, a HAND-WRITTEN metaprog hook that deliberately mimics the door's
# shape to exercise borrow-check through the quote channel (task #74/#75). Under
# a shape-only rule that fixture reads as a HALF-EMITTED DOOR and this gate
# would have opened permanently red on an artifact that is not its subject.
# The rule is therefore SCOPED BY PROVENANCE: every `--gen-dir` unit carries a
# `// emitted by: <fn>` header, and a door may only be counted in a unit whose
# emitter is `deem`. The mimic lands in the NON-DEEM residual below, which is
# pinned per spelling (not as one number) so it cannot absorb a real door.
#
# ── THE PLAN↔ARTIFACT IDENTITY, PER FIXTURE ─────────────────────────────────
# `LOGOS_TRACE_PLAN=1` makes the plan state the decision in words:
# "`_stream` DOOR is now the §12 DIRECT form". That is the PLAN's door count and
# it is compared to the ARTIFACT's, PER FIXTURE — not as two totals, because two
# totals agree while a door moves from one fixture to another.
#
# ── COMPILE COVERAGE: THE SWEEP MAY NOT BE PARTIALLY BLIND ──────────────────
# 50 pass fixtures need fixture archives. The map below MIRRORS
# `logos_pass_extra_args` in `tests/logos/CMakeLists.txt` and is deliberately
# NOT tolerant: a fixture that fails to compile reds CLAUSE 2 (`unswept` is
# pinned 0). A hand-kept mirror drifts — this is the mechanism that makes the
# drift red instead of silently shrinking the population, which is the only
# defensible way to keep a copy.
#
# EXIT 0 all pins hold · 1 a pin moved · 2 the gate could not measure.
set -uo pipefail

LOGOSC="${1:?logosc}"
PASS="${2:?pass dir}"
# The facts tree written by the per-fixture ctest tests (task #85).
#
# ⚠ THE ARCHIVE MAP IS GONE, AND THAT IS THE BIGGEST SINGLE THING THIS CHANGE
# BUYS HERE. This script used to carry a `case "$b" in …` mirror of
# `logos_pass_extra_args` in `tests/logos/CMakeLists.txt` — twenty-odd `-l`
# lines plus the `memoria_` prefix rule plus the `--test` rule — because its
# own sweep had to reproduce, by hand, the flags each fixture's real test is
# given. The comment above admitted the copy drifts and offered the `unswept`
# pin as the mechanism that makes the drift red. There is now no copy: the
# facts come off the compile the fixture's OWN ctest test runs, with the flags
# CMake gave it, and a rule added there reaches this census by construction.
#
# The other half of what went: a `one.sh` worker, `xargs -0 -P "$SWEEP_P"` and
# a `SWEEP_P` picked from `LOGOS_GATE_SWEEP_P` / `CTEST_INTERACTIVE_DEBUG_MODE`
# / `nproc` — a second scheduler inside a test ctest was already scheduling.
# This gate was the measured EXCEPTION to "serial under ctest" (2254 compiles,
# ~7360 CPU-seconds, 4.1x its own ceiling if run serially), and the exception
# existed only because the compiles were re-done here. They are not re-done.
#
# ⚠ AND IT IS THE BITE-PROOF HOOK, replacing the `PRESWEPT` fourth argument and
# the `LOGOS_DOOR_SWEEP_OUT` copy-out that went with it. That pair had a hole
# its siblings' had too: the PRESWEPT branch skipped the probe-completeness
# check, so a perturbation that DELETED a fixture left the gate measuring what
# remained. There is one path now — copy this tree, perturb the copy, pass the
# copy — and the completeness and staleness refusals run on it like on any
# other, which is exactly what CLAUSE 1's "never probed" leg is about.
FACTS="${3:?facts root}"

export LC_ALL=C
[ -x "$LOGOSC" ] || { echo "FAIL(2): no logosc at $LOGOSC"; exit 2; }
[ -d "$PASS" ]   || { echo "FAIL(2): no pass dir $PASS"; exit 2; }
# shellcheck source=facts_fold.sh
. "$(dirname "$0")/facts_fold.sh"

TMPD=$(mktemp -d)
trap 'rm -rf "$TMPD"' EXIT
OUT="$TMPD/o"
mkdir -p "$OUT/_st" "$OUT/_plan"

shopt -s nullglob
FIXTURES=("$PASS"/*.logos)
NFIX=${#FIXTURES[@]}
# THE BLINDNESS FLOOR (the `pull_shape` reason): a fold that finds three
# fixtures and all its pins at zero reads exactly like a healthy one.
if [ "$NFIX" -lt 1500 ]; then
    echo "FAIL(2): only $NFIX pass fixtures matched — the fold is blind."
    exit 2
fi
# EVERY member of the population, or nothing at all — the refusal names each
# missing one. This is the clause that keeps "not selected under -R" from
# reading as "no doors here". See facts_fold.sh.
#
# ⚠ IT COVERS THE FIVE FIXTURES NO SUITE REGISTERS. `pass/*.logos` holds 2254
# files and five of them have no `.expected`, so the loop that registers corpus
# tests never sees them (they are `unregistered.ledger`'s, held in both
# directions by `logos_00_corpus_registration`). Their facts are produced by
# `logos_09_facts_<base>` tests registered from the same glob in
# `tests/logos/CMakeLists.txt` — derived there, not listed — precisely so that
# this population stays 2254 rather than quietly becoming 2249.
facts_require "$FACTS" "$LOGOSC" "direct-door" "${FIXTURES[@]}"

# ── the fold: stage what the census reads, from the facts ────────────────────
# The layout is the deleted worker's, so the python census below is unchanged.
for f in "${FIXTURES[@]}"; do
    b=$(basename "$f" .logos)
    G="$FACTS/$b"
    cp "$G/rc" "$OUT/_st/$b"
    # The plan trace is the compile's whole stderr. Only the door sentence is
    # kept — the rest is megabytes of ground text this gate has no claim on.
    grep -c '`_stream` DOOR is now the §12 DIRECT form' "$G/plan.err" > "$OUT/_plan/$b"
    # ⚠ EVERY unit, INCLUDING `logos.gen.*` — and that is a DELIBERATE
    # DIVERGENCE from `pull_shape_gate.sh`, which drops them. That gate's
    # subject is the PULL, and a `next_batch()` in a `logos.gen.*` unit is the
    # stdlib's own `BatchStream` impl for a container family, not a query
    # pulling anything — dropping them is right THERE. It is wrong HERE:
    # MEASURED, `memoria_showcase_deem` emits 4 direct doors and 2 of them land
    # in `logos.gen.borrow_carrying.Hs*` units, `container_item_from_module` and
    # `memoria_ctr_vec_deem` one each — a door for a container family declared
    # in an imported package is emitted into that package's gen unit.
    # Inheriting the user-module rule would have hidden 4 of the corpus's 36
    # doors from the very gate written to stop doors hiding. What scopes this
    # gate is PROVENANCE (`// emitted by: deem`), not package name.
    # ⚠ THIS IS WHY `facts_emit.sh` KEEPS THE UNITS SEPARATE instead of
    # concatenating them once: three gates, three scoping rules, one artifact.
    U=("$G"/gen/*.gen.logos)
    [ "${#U[@]}" -ge 1 ] && cat "${U[@]}" > "$OUT/$b.user"
done
ST=("$OUT"/_st/*)
if [ "${#ST[@]}" -ne "$NFIX" ]; then
    echo "FAIL(2): ${#ST[@]} rc files for $NFIX fixtures — the staging lost some."
    exit 2
fi

python3 - "$OUT" "$PASS" <<'PY'
import os, re, sys, glob

OUT, PASS = sys.argv[1], sys.argv[2]
fail = []

# ── THE PIN BLOCK ───────────────────────────────────────────────────────────
# Measured 2026-08-19 on the tree carrying #75, by the sweep in this file:
# 2180 pass fixtures compiled with the CMake archive map, `--gen-dir` dumps
# split into units by their `// emitted by:` header, doors counted in the `deem`
# units only.
PIN = {
    # ── CLAUSE 1, the partition ────────────────────────────────────────────
    # ⚠ These three move whenever the pass corpus does, BY DESIGN: a dropped
    # fixture is exactly the failure this gate cannot otherwise see. Re-derive
    # them with the stage that added or removed the fixture, and say which half
    # it joined — that sentence is the accounting.
    # #71/#72 round (2026-08-19): 2180 -> 2184. The round added 8 fixtures, 4 of
    # them pass — bc_flowsum_rawtrip_outparam_admit, bc_fatret_nested_call_admit,
    # bc_fatret_methodarg_admit, bc_fatret_struct_field_admit — and THIS gate
    # sweeps the WHOLE pass corpus, not the wql_*/deem_* glob, so `bc_*` names
    # stay out of the two OLDER gates' populations and land squarely in this
    # one. That is the gate doing its job on its first outside contact: it was
    # written because doors outside the glob were pinned by nothing, and the
    # first thing it caught was its own population drifting. Doors did NOT move
    # (36 = 10 + 26) — the new fixtures hold none, which the `doors`/`glob`/
    # `nonglob_doors` pins below assert independently of this count.
    # ⚠ RE-DERIVED at the #77/#78/#79 escape-channel stage (three borrow-check
    # channels landed one at a time): +3 / +0 / +3. The three are PASS fixtures
    # and none matches the `wql_*` / `deem_*` glob, so the whole delta lands in
    # `nonglob` and `glob` is unmoved:
    #   pass/bc_esc_return_summary_admit   (#77, return escape through a call)
    #   pass/bc_esc_outparam_scope_admit   (#78, out-param scope escape)
    #   pass/bc_esc_fnptr_admit            (#79, fn-pointer call)
    # 2184 + 3 = 2187 = 191 + 1996, and 1993 + 3 = 1996. The DOOR counts are
    # unmoved (36 = 10 + 26) — none of the three declares a container family,
    # which is what the sweep measured: `doors 36 = glob 10 + nonglob 26` on the
    # very run that reported these two pins moved and nothing else.
    # ⚠ RE-DERIVED at the #80 deferred-init-slot stage (a codegen fix, not a
    # borrow-check one): +1 / +0 / +1. The one fixture is
    #   pass/bc_fatval_deferred_init_len  (#80, `let v: T;` fat-slot class)
    # — a PASS fixture outside the `wql_*` / `deem_*` glob, so the whole delta
    # lands in `nonglob`: 2187 + 1 = 2188 = 191 + 1997, and 1996 + 1 = 1997.
    # DOOR counts unmoved (36 = 10 + 26): the fixture declares no container
    # family — it is `str` / slice / dyn / closure / array / tuple locals only.
    # ⚠ RE-DERIVED at the #77 round 2 stage (the seed/flag repair, the
    # MethodCall door, the unresolvable-fn-pointer route and the return-temp
    # diagnostic, landed one at a time): +3 / +0 / +3. The three are PASS
    # fixtures and none matches the `wql_*` / `deem_*` glob:
    #   tests/logos/pass/bc_esc_summary_seed_field_admit.logos
    #   tests/logos/pass/bc_esc_method_retain_admit.logos
    #   tests/logos/pass/bc_esc_fnptr_param_admit.logos
    # DERIVED BY DIRECT FILE LISTING, not by adding 3 to the previous pin:
    #   ls tests/logos/pass/*.logos | wc -l                       -> 2191
    #   ls tests/logos/pass/{wql_*,deem_*}.logos | wc -l          ->  191
    # so nonglob is 2000 by the same listing minus the glob listing, and the
    # partition closes: 2191 = 191 + 2000. The four FAIL fixtures this stage
    # added are outside this gate's population by construction (it sweeps the
    # pass corpus only). DOOR counts unmoved (36 = 10 + 26): none of the three
    # declares a container family — they are `&i64` / `str` / fn-pointer
    # borrow-check shapes only.
    # ⚠ RE-DERIVED at the #86 VERIFY stage (MISS 1 the mutation after the let,
    # MISS 2 the residency exemption checked in the abuse direction, MISS 3
    # container holders — landed one at a time): +8 / +0 / +8. The eight are
    # PASS fixtures and none matches the `wql_*` / `deem_*` glob:
    #   tests/logos/pass/bc_esc_holder_assign_field_admit.logos
    #   tests/logos/pass/bc_esc_holder_assign_whole_admit.logos
    #   tests/logos/pass/bc_esc_holder_assign_option_admit.logos
    #   tests/logos/pass/bc_esc_holder_assign_tuple_admit.logos
    #   tests/logos/pass/bc_esc_holder_residency_backed_admit.logos
    #   tests/logos/pass/bc_esc_holder_container_vec_str_admit.logos
    #   tests/logos/pass/bc_esc_holder_container_vec_struct_admit.logos
    #   tests/logos/pass/bc_esc_holder_container_outparam_admit.logos
    # DERIVED BY DIRECT FILE LISTING, not by adding 8 to the previous pin:
    #   ls tests/logos/pass/*.logos | wc -l                       -> 2211
    #   ls tests/logos/pass/{wql_*,deem_*}.logos | wc -l          ->  191
    # so nonglob is 2020 by the same listing minus the glob listing, and the
    # partition closes: 2211 = 191 + 2020. The eight FAIL fixtures this stage
    # added are outside this gate's population by construction (it sweeps the
    # pass corpus only). DOOR counts unmoved (36 = 10 + 26), measured by the
    # sweep itself: none of the seven declares a container family — they are
    # `str` / `Option<str>` / tuple / `Rc` / `Vec<…>` borrow-check shapes, and
    # the `Vec` ones are stdlib containers, not `direct` doors.
    # ⚠ RE-DERIVED at the #86 VERIFY ROUND 2 stage (MISS-A/B/C/D: the escape
    # fact is deposited on the PLACE ROOT, not on the name written through;
    # the index-assign door; the residency exemption checked per SHARE): +4 /
    # +0 / +4. The four are PASS fixtures and none matches the `wql_*` /
    # `deem_*` glob:
    #   tests/logos/pass/bc_esc_holder_reborrow_field_admit.logos
    #   tests/logos/pass/bc_esc_holder_reborrow_container_admit.logos
    #   tests/logos/pass/bc_esc_holder_index_assign_admit.logos
    #   tests/logos/pass/bc_esc_holder_residency_pershare_admit.logos
    # DERIVED BY DIRECT FILE LISTING, not by adding 4 to the previous pin:
    #   ls tests/logos/pass/*.logos | wc -l                       -> 2215
    #   ls tests/logos/pass/{wql_*,deem_*}.logos | wc -l          ->  191
    # so nonglob is 2024 by the same listing minus the glob listing, and the
    # partition closes: 2215 = 191 + 2024. The four FAIL fixtures this stage
    # added are outside this gate's population by construction (it sweeps the
    # pass corpus only). DOOR counts unmoved (36 = 10 + 26), measured by the
    # sweep itself: none of the four declares a container family — they are
    # `str` / `Vec<str>` / `Rc<Writ>` borrow-check shapes, and the `Vec` ones
    # are stdlib containers, not `direct` doors.
    # 2026-08-20, #58/#59/#60 (the bare-struct-name IDENTITY class): +16 / +0 /
    # +16. The sixteen are PASS fixtures and none matches the `wql_*` /
    # `deem_*` glob — eight HOMONYM programs (a user struct sharing a name with
    # an imported one, values asserted at RUNTIME because a name-only check
    # cannot see a stride bug) and their eight collision-free `_ctl` oracles:
    #   tests/logos/pass/mlirgen_odr_vec_stride.logos
    #   tests/logos/pass/mlirgen_odr_vec_stride_ctl.logos
    #   tests/logos/pass/mlirgen_odr_vec_header.logos
    #   tests/logos/pass/mlirgen_odr_vec_datumcol.logos
    #   tests/logos/pass/mlirgen_odr_tuple_field.logos
    #   tests/logos/pass/mlirgen_odr_tuple_field_ctl.logos
    #   tests/logos/pass/mlirgen_odr_match_stmt.logos
    #   tests/logos/pass/mlirgen_odr_match_stmt_ctl.logos
    #   tests/logos/pass/mlirgen_odr_match_expr.logos
    #   tests/logos/pass/mlirgen_odr_match_expr_ctl.logos
    #   tests/logos/pass/mlirgen_odr_pat_nested.logos
    #   tests/logos/pass/mlirgen_odr_pat_nested_ctl.logos
    #   tests/logos/pass/mlirgen_odr_pat_refutable.logos
    #   tests/logos/pass/mlirgen_odr_pat_refutable_ctl.logos
    #   tests/logos/pass/mlirgen_odr_mangle_channels.logos
    #   tests/logos/pass/mlirgen_odr_mangle_channels_ctl.logos
    # (`vec_header` / `vec_datumcol` share `vec_stride`'s SHAPE and therefore
    # its `_ctl` oracle — the two extra names are there because both SIGSEGV'd,
    # i.e. the class is not confined to wrong answers or to one name.)
    # DERIVED BY DIRECT FILE LISTING, not by adding 16 to the previous pin:
    #   ls tests/logos/pass/*.logos | wc -l                       -> 2231
    #   ls tests/logos/pass/{wql_*,deem_*}.logos | wc -l          ->  191
    # so nonglob is 2040 by the same listing minus the glob listing, and the
    # partition closes: 2231 = 191 + 2040. This stage added NO fail fixtures.
    #
    # #59 (the FREE-FN generic-instance channel of the same class), +2/0/+2:
    # the two `_ctl` oracles that `vec_header` / `vec_datumcol` were MISSING.
    # The claim above — that they "share vec_stride's SHAPE and therefore its
    # `_ctl` oracle" — did not survive measurement: with the element widened
    # past the stdlib homonym's size (the only shape in which those two names
    # bite at all) their numbers differ from `vec_stride`'s, so each needs its
    # own twin. RE-DERIVED BY DIRECT FILE LISTING:
    #   ls tests/logos/pass/*.logos | wc -l                       -> 2233
    #   ls tests/logos/pass/{wql_*,deem_*}.logos | wc -l          ->  191
    # partition closes: 2233 = 191 + 2042. No fail fixtures, DOORS unmoved
    # (36 = 10 + 26) — both twins are plain structs with one stdlib `Vec<T>`.
    # DOOR counts unmoved (36 = 10 + 26), measured by the sweep itself: none of
    # the sixteen declares a container family — they are plain structs, one
    # `Vec<T>` stdlib container each in the vec/mangle fixtures, and no `direct`.
    # 2026-08-20 (the METHOD-RESOLUTION channel of the same class), +2/0/+2.
    # RE-DERIVED BY DIRECT FILE LISTING, not by adding 2 to the previous pin:
    #   ls tests/logos/pass/*.logos | wc -l                       -> 2235
    #   ls tests/logos/pass/{wql_*,deem_*}.logos | wc -l          ->  191
    # partition closes: 2235 = 191 + 2044. The two are
    # mlirgen_odr_drop_glue_homonym and its `_ctl`; the round's other two
    # fixtures are FAIL fixtures and this population is the PASS corpus, so
    # they move nothing here. DOOR counts unmoved (36 = 10 + 26): neither
    # declares a container family — one stdlib `Vec<T>` each, no `direct`.
    # 2026-08-21 (#61 D6 — the typeof-container projection in a struct FIELD,
    # an enum PAYLOAD and a TUPLE element), +1/0/+1.
    # RE-DERIVED BY DIRECT FILE LISTING, not by adding 1 to the previous pin:
    #   ls tests/logos/pass/*.logos | wc -l                       -> 2236
    #   ls tests/logos/pass/{wql_*,deem_*}.logos | wc -l          ->  191
    # partition closes: 2236 = 191 + 2045. The one is
    # typeof_container_field_admit; the round's other fixture
    # (typeof_container_tuple_field_no_family_fail) is a FAIL fixture and this
    # population is the PASS corpus, so it moves nothing here. DOOR counts
    # unmoved (36 = 10 + 26), measured by the sweep itself: the new fixture
    # DECLARES a container (`container Ked`) and creates it, but declares no
    # `direct` output form, which is what a door is counted on.
    # 2026-08-21 (#68 — the `&dyn` TUPLE ELEMENT with no explicit cast), +1/0/+1.
    # RE-DERIVED BY DIRECT FILE LISTING, not by adding 1 to the previous pin:
    #   ls tests/logos/pass/*.logos | wc -l                       -> 2237
    #   ls tests/logos/pass/{wql_*,deem_*}.logos | wc -l          ->  191
    # partition closes: 2237 = 191 + 2046. The one is
    # tuple_dyn_element_implicit; the round added no FAIL fixture at all (a
    # codegen repair has no refusal to pair with — see the census ledger). DOOR
    # counts unmoved (36 = 10 + 26), measured by the sweep itself: the new
    # fixture declares no container and no `direct` output form.
    # 2026-08-21 (#69 class A — the `-> !` tail of a loop body), +1/0/+1.
    # RE-DERIVED BY DIRECT FILE LISTING, not by adding 1 to the previous pin:
    #   ls tests/logos/pass/*.logos | wc -l                       -> 2238
    #   ls tests/logos/pass/{wql_*,deem_*}.logos | wc -l          ->  191
    # partition closes: 2238 = 191 + 2047. The one is
    # bc_loop_bot_divergent_call_admit; the round's other two fixtures are FAIL
    # fixtures and this population is the PASS corpus, so they move nothing
    # here. DOOR counts unmoved (36 = 10 + 26): no container, no `direct`.
    # 2026-08-21 (#68 CLASS — the aggregate-literal slot type, closed at the one
    # coercion judgment instead of the let-annotation), +1/0/+1.
    # RE-DERIVED BY DIRECT FILE LISTING, not by adding 1 to the previous pin:
    #   ls tests/logos/pass/*.logos | wc -l                       -> 2239
    #   ls tests/logos/pass/{wql_*,deem_*}.logos | wc -l          ->  191
    # partition closes: 2239 = 191 + 2048. The one is
    # array_dyn_element_implicit (the ARRAY half of the class); the round's
    # other pass fixture, tuple_dyn_element_implicit, was ALREADY in the 2238
    # baseline and is extended in place, and its FAIL fixture
    # (tuple_dyn_element_no_impl) is outside this population by construction.
    # DOOR counts unmoved (36 = 10 + 26): no container, no `direct`.
    # 2026-08-21 (#95 — an aggregate SLOT is not a coercion site; the ROOT of
    # the #68 class closes in the REFUSING direction), +1/0/+1.
    # RE-DERIVED BY DIRECT FILE LISTING, not by adding 1 to the previous pin:
    #   ls tests/logos/pass/*.logos | wc -l                       -> 2240
    #   ls tests/logos/pass/{wql_*,deem_*}.logos | wc -l          ->  191
    # partition closes: 2240 = 191 + 2049. The one is
    # aggregate_unsize_literal_and_cast_admit; the round's other SIX fixtures
    # are FAIL fixtures (five refusals plus one pinned OVER-refusal, TASK #96)
    # and this population is the PASS corpus, so they move nothing here. DOOR
    # counts unmoved (36 = 10 + 26): no container, no `direct` output form.
    # 2026-08-21 (#95 M1/M2/M3 — the three live crashes the #95 round's OWN
    # VERIFY found inside the #95 landing: the depth cap admitted on exhaustion,
    # the owning `Box<dyn>` slot was exempted, and the generic-instance arm never
    # walked an ENUM instance), +3/0/+3.
    # RE-DERIVED BY DIRECT FILE LISTING, not by adding 3 to the previous pin:
    #   ls tests/logos/pass/*.logos | wc -l                       -> 2243
    #   ls tests/logos/pass/{wql_*,deem_*}.logos | wc -l          ->  191
    # partition closes: 2243 = 191 + 2052. The three are
    # aggregate_unsize_deep_nesting_admit, aggregate_unsize_boxdyn_literal_admit
    # and aggregate_unsize_enum_literal_admit — one ADMIT twin per defect. The
    # round's other TEN fixtures are FAIL fixtures and this population is the
    # PASS corpus, so they move nothing here. DOOR counts unmoved (36 = 10 + 26):
    # no container, no `direct` output form in any of the thirteen.
    # 2026-08-21 (CLASS SWEEP A, sites b1+b2 of "a lookup KEY is not an
    # IDENTITY"): +7/0/+7. RE-DERIVED BY DIRECT FILE LISTING, not by adding 7:
    #   ls tests/logos/pass/*.logos | wc -l                       -> 2250
    #   ls tests/logos/pass/{wql_*,deem_*}.logos | wc -l          ->  191
    # partition closes: 2250 = 191 + 2059. The seven are the five
    # intrinsic_bare_name_* fixtures (homonym + _ctl for each of the two
    # intercept families, plus the arm-fires/abuse-direction pin) and the
    # copy_verdict_homonym_drop_glue pair. The round's eighth fixture is a FAIL
    # fixture (copy_verdict_homonym_use_after_move_fail) and this population is
    # the PASS corpus, so it moves nothing here. DOOR counts unmoved
    # (36 = 10 + 26): no container and no `direct` output form in any of them.
    # 2026-08-21 (CLASS SWEEP A, site b5 / task #88 — the impls_ TARGET half):
    # +4/0/+4, the impl_target_homonym_drop and impl_target_homonym_copy_verdict
    # pairs. RE-DERIVED BY DIRECT FILE LISTING:
    #   ls tests/logos/pass/*.logos | wc -l                       -> 2254
    #   ls tests/logos/pass/{wql_*,deem_*}.logos | wc -l          ->  191
    # partition closes: 2254 = 191 + 2063. DOOR counts unmoved (36 = 10 + 26).
    # 2026-08-22 (task #99 — the nine bare-name type predicates; `is_anyval`
    # decided a REPRESENTATION on a bare struct name, so a user
    # `struct AnyVal { raw: i64 }` was lowered as an i32 and read garbage,
    # silently): +2/0/+2, the anyval_homonym_repr homonym/control PAIR.
    # RE-DERIVED BY DIRECT FILE LISTING, not by adding 2 to the previous pin:
    #   ls tests/logos/pass/*.logos | wc -l                       -> 2256
    #   ls tests/logos/pass/{wql_*,deem_*}.logos | wc -l          ->  191
    # partition closes: 2256 = 191 + 2065. The round added NO fail fixture at
    # all — the defect is a wrong ANSWER, not a missing refusal — so the fail
    # corpus is unmoved too. DOOR counts unmoved (36 = 10 + 26): neither file
    # declares a container family or a `direct` output form.
    # 2026-08-22 (task #102 — a compiler-SYNTHESISED type could be handed the
    # USER's package by `resolve_struct_pkg_`'s bare-name lookup, whose first
    # tier is the module under compilation; a user `struct WritStatic` was
    # admitted and the binary exited 2): +4/+2/+4 — two homonym/control PAIRS in
    # pass (`synth_pkg_type_homonym`, `synth_pkg_identspan_quote`) and one in
    # fail (`synth_pkg_writstatic_homonym`), the fail pair being the refusal the
    # round's headline repro now gets instead of a silent wrong answer.
    # 2026-08-22 (#100 + its residual): the trait-homonym round added eight pass
    # fixtures (`trait_homonym_*`) and its own verify's witness added two more
    # (`trait_homonym_gat_arity{,_ctl}` — the GAT-arity site that still consulted
    # `traits_` bare). None declares a container family or a `direct` output
    # form, so the DOOR counts are unmoved; only the population moves.
    # RE-DERIVED BY DIRECT FILE LISTING, never by adding to the previous pin:
    #   ls tests/logos/pass/*.logos | wc -l                       -> 2273
    #   ls tests/logos/pass/{wql_*,deem_*}.logos | wc -l          ->  191
    # 2026-08-22 (#94): +3 more, the array-in-arm miscompile pair and its
    # tuple control. Same argument — none declares a family or a `direct`
    # form. partition closes: 2273 = 191 + 2082. DOORS unmoved (36 = 10 + 26).
    # 2026-08-22 (#103 — the `mlir_gen:` channel becomes fatal): +2, the
    # drop-glue homonym/control PAIR (`drop_glue_struct_homonym_field_list`,
    # `…_control`). `gen_drop_value` asked `all_struct_defs_` BARE-FIRST, so a
    # user `struct Item` inside a stdlib generic instance took
    # `logos.std.compiler.metaprog.Item`'s field list and every field destructor
    # was skipped — 0 drops under the homonym against the control's 2. Neither
    # file declares a container family or a `direct` output form, so the DOOR
    # counts are unmoved; only the population moves.
    # RE-DERIVED BY DIRECT FILE LISTING, never by adding to the previous pin:
    #   ls tests/logos/pass/*.logos | wc -l                       -> 2275
    #   ls tests/logos/pass/{wql_*,deem_*}.logos | wc -l          ->  191
    # partition closes: 2275 = 191 + 2084. DOORS unmoved (36 = 10 + 26).
    # 2026-08-22 (#110 — one value, N destructor calls): +11 pass fixtures, the
    # counting-oracle set for five distinct drop/move roots and their control
    # twins (drop_enum_field_struct_move_once{,_control},
    # copy_enum_field_payload_copy, drop_option_into_iter_terminals_once,
    # drop_for_loop_item_once{,_control}, drop_tuple_element_returned_once
    # {,_control}, match_array_index_copy_elem, drop_fnptr_arg_consumed_once
    # {,_control}); the round's two FAIL fixtures do not live in this
    # population. None declares a container family or a `direct` output form,
    # so the DOOR counts are unmoved; only the population moves.
    # RE-DERIVED BY DIRECT FILE LISTING, never by adding to the previous pin:
    #   ls tests/logos/pass/*.logos | wc -l                       -> 2286
    #   ls tests/logos/pass/{wql_*,deem_*}.logos | wc -l          ->  191
    # partition closes: 2286 = 191 + 2095. DOORS unmoved (36 = 10 + 26).
    # ⚠ RE-DERIVED at the #112 raw-duplicate stage (the `*((&x) as *const T)`
    # duplicate-owner class). 23 PASS fixtures added — `rawdup_*_drop_once` (11),
    # their `rawdup_*_copy_ctl` twins (11), and
    # `rawdup_uninit_assume_init_read_ground` — and none matches the `wql_*` /
    # `deem_*` glob, so the whole delta lands in `nonglob`:
    # 2288 + 23 = 2311 = 191 + 2120, and 2097 + 23 = 2120. Both halves were
    # RE-DERIVED BY DIRECT FILE LISTING (`ls tests/logos/pass/*.logos` = 2311,
    # `ls tests/logos/pass/{wql_*,deem_*}.logos` = 191), not by adding 23 to the
    # previous line. The DOOR counts are unmoved (36 = 10 + 26): none of the 23
    # declares a container family — they are stdlib destructor-count fixtures over
    # Vec / Option / iterator adapters.
    # ⚠ RE-DERIVED at the #118 conditional-move-drop-flag stage. 12 PASS
    # fixtures added — six `cond_move_*` cells of the path lattice and their six
    # `_ctl` twins — and none matches the `wql_*` / `deem_*` glob, so the whole
    # delta lands in `nonglob`: 2319 + 12 = 2331 = 191 + 2140, and 2128 + 12 =
    # 2140. Both halves RE-DERIVED BY DIRECT FILE LISTING
    # (`ls tests/logos/pass/*.logos` = 2331,
    #  `ls tests/logos/pass/{wql_*,deem_*}.logos` = 191), never by adding 12 to
    # the previous line. The round added NO fail fixture — it landed a FIX, not a
    # refusal — so the fail corpus is unmoved. DOOR counts unmoved (36 = 10 + 26):
    # none of the 12 declares a container family or a `direct` output form; they
    # are destructor-count fixtures over a heap-owning `struct Pay`.
    # ⚠ RE-DERIVED at the #119 OnceCell stage. 11 PASS fixtures added — five
    # `dupown_oncecell_*`/`dupown_lazycell_*_drop_once` destructor-count cells,
    # their five `_copy_ctl` twins, and `ptr_drop_in_place_recurses` (the admit
    # half of the corrected `ptr::drop_in_place` promise) — and none matches the
    # `wql_*` / `deem_*` glob, so the whole delta lands in `nonglob`:
    # 2331 + 11 = 2342 = 191 + 2151, and 2140 + 11 = 2151. Both halves
    # RE-DERIVED BY DIRECT FILE LISTING (`ls tests/logos/pass/*.logos` = 2342,
    # `ls tests/logos/pass/{wql_*,deem_*}.logos` = 191), never by adding 11 to
    # the previous line. The round also added ONE fail fixture
    # (`ptr_drop_in_place_needs_unsafe_fail`, the refuse half of that promise),
    # which this gate does not count. DOOR counts unmoved (36 = 10 + 26): none
    # of the 11 declares a container family or a `direct` output form; they are
    # destructor-count fixtures over `OnceCell`/`LazyCell` with a heap-owning
    # `struct Inner { n, Vec<i64> }`.
    # ⚠ RE-DERIVED at the #121/#122/#123 conditional-move + suppression round:
    # +8 / +0 / +8. Eight PASS fixtures, none matching the `wql_*` / `deem_*`
    # glob, all destructor-count oracles over a heap-owning payload:
    #   tests/logos/pass/cond_move_field_source{,_ctl}.logos
    #   tests/logos/pass/divergent_arm_unwind{,_ctl}.logos
    #   tests/logos/pass/no_auto_drop_sibling{,_ctl}.logos
    #   tests/logos/pass/cond_move_lazy_and_guard{,_ctl}.logos
    # BOTH halves RE-DERIVED BY DIRECT FILE LISTING (`ls tests/logos/pass/*.logos`
    # = 2351, `ls tests/logos/pass/{wql_*,deem_*}.logos` = 191), never by adding
    # 8 to the previous line. DOOR counts unmoved (36 = 10 + 26): none of the
    # eight declares a container family or a `direct` output form.
    # ⚠ RE-DERIVED AGAIN at the #121-A ancestor/descendant round: +2 / +0 / +2.
    # Two PASS fixtures, neither matching the `wql_*` / `deem_*` glob:
    #   tests/logos/pass/cond_move_field_overlap.logos    (overlapping pairs;
    #     malloc/free payload so valgrind can see release, not just the call)
    #   tests/logos/pass/cond_move_glue_name_admit.logos  (the ADMIT half of the
    #     borrow-check provenance pair)
    # The two new FAIL fixtures do not enter this gate's corpus (it reads
    # `tests/logos/pass` only). Re-derived BY DIRECT FILE LISTING:
    # `ls tests/logos/pass/*.logos` = 2353,
    # `ls tests/logos/pass/{wql_*,deem_*}.logos` = 191, difference 2162 —
    # never by adding 2 to the previous line. DOOR counts unmoved (36 = 10 + 26):
    # neither fixture declares a container family or a `direct` output form.
    # ⚠ RE-DERIVED AGAIN at the #123 `#[no_auto_drop]` storage-site round:
    # +2 / +0 / +2. Two PASS fixtures, neither matching the `wql_*` / `deem_*`
    # glob, a refuse/admit PAIR over the same twenty-nine values:
    #   tests/logos/pass/no_auto_drop_container.logos      (suppression side:
    #     twenty values reclaimed by hand, nine destroyed by the compiler)
    #   tests/logos/pass/no_auto_drop_container_ctl.logos  (admit side: the
    #     attribute removed, all twenty-nine destroyed by the compiler)
    # Both carry a malloc/free payload so the valgrind gate can see RELEASE and
    # not only the destructor call. Re-derived BY DIRECT FILE LISTING:
    # `ls tests/logos/pass/*.logos` = 2355,
    # `ls tests/logos/pass/{wql_*,deem_*}.logos` = 191, difference 2164 —
    # never by adding 2 to the previous line. This round added NO new gate
    # SCRIPT (both new ctest tests re-use `cond_move_field_valgrind_gate.sh`
    # with a different fixture argument), so `ls tests/logos/*.sh` stays 66.
    # DOOR counts unmoved (36 = 10 + 26): neither fixture declares a container
    # family or a `direct` output form.
    # ── CLASS D / D-c (2026-08-24) ─────────────────────────────────────────
    # ONE pass fixture — `pass/bc_slice_return_param_admit` — the admit twin of
    # the two `fail/bc_slice_return_*` refuse fixtures the `prov_of`
    # SliceLit/SlicePtr arms close. `bc_*` is not `wql_*`/`deem_*`, so the whole
    # delta lands in `nonglob` and `glob` is unmoved. Re-derived BY DIRECT FILE
    # LISTING, never by adding 1 to the previous line:
    #   ls tests/logos/pass/*.logos              -> 2359
    #   ls tests/logos/pass/{wql_*,deem_*}.logos ->  191
    # difference 2168. DOOR counts unmoved (36 = 10 + 26): the fixture declares
    # no container family and no `direct` output form. No new gate SCRIPT, so
    # `ls tests/logos/*.sh` stays 66.
    # ── CLASS D / D-b (2026-08-24) ─────────────────────────────────────────
    # ONE more pass fixture — `pass/bc_block_tail_borrow_outer_admit` — the
    # admit twin of `fail/bc_block_tail_borrow_local_fail` and
    # `fail/bc_if_tail_borrow_local_fail`. Again `bc_*`, so `glob` is unmoved.
    # Re-derived BY DIRECT FILE LISTING:
    #   ls tests/logos/pass/*.logos              -> 2360
    #   ls tests/logos/pass/{wql_*,deem_*}.logos ->  191
    # difference 2169. DOORS unmoved (36 = 10 + 26); no new gate SCRIPT, so
    # `ls tests/logos/*.sh` stays 66.
    # ── CLASS D / D-d.2 (2026-08-24) ───────────────────────────────────────
    # ONE more pass fixture — `pass/bc_recv_reservation_disjoint_admit`, the
    # admit twin of `fail/bc_recv_reservation_conflict_fail`. `bc_*` again, so
    # `glob` is unmoved. Re-derived BY DIRECT FILE LISTING:
    #   ls tests/logos/pass/*.logos              -> 2361
    #   ls tests/logos/pass/{wql_*,deem_*}.logos ->  191
    # difference 2170. DOORS unmoved (36 = 10 + 26); `ls tests/logos/*.sh` 66.
    # ── CLASS D / the temporary-PROJECTION neighbours (2026-08-24) ─────────
    # ONE more pass fixture — `pass/bc_return_borrow_through_ref_call_admit`,
    # the admit twin of `fail/bc_return_borrow_of_temp_projection_fail`.
    # `bc_*` again, so `glob` is unmoved. Re-derived BY DIRECT FILE LISTING:
    #   ls tests/logos/pass/*.logos              -> 2362
    #   ls tests/logos/pass/{wql_*,deem_*}.logos ->  191
    # difference 2171. DOORS unmoved (36 = 10 + 26); `ls tests/logos/*.sh` 66.
    # ── CLASS D / D-a (2026-08-24) ─────────────────────────────────────────
    # ONE more pass fixture — `pass/bc_let_borrow_temp_extended_admit`, the
    # admit twin of `fail/bc_let_borrow_temp_through_call_fail`. `bc_*` again,
    # so `glob` is unmoved. Re-derived BY DIRECT FILE LISTING:
    #   ls tests/logos/pass/*.logos              -> 2363
    #   ls tests/logos/pass/{wql_*,deem_*}.logos ->  191
    # difference 2172. DOORS unmoved (36 = 10 + 26); `ls tests/logos/*.sh` 66.
    # ── CLASS D VERIFY / THE FIVE MISSES (2026-08-24) ──────────────────────
    # THREE pass fixtures, all `bc_*`, so the whole delta lands in `nonglob`
    # and `glob` is unmoved:
    #   pass/bc_const_promotion_admit        — #92 const promotion, the admit
    #        half, runtime-checked through a frame-stomping call
    #   pass/bc_slice_borrow_param_admit     — admit twin of the MISS 1 / MISS 2
    #        refuse fixtures (`&a[0u64]` / `&a[0..2]` over a PARAM)
    #   pass/bc_block_tail_shadowed_name_admit — admit twin of MISS 3, both
    #        channels (D-b's tail check and pop_scope's `dangling_` deposit)
    # Re-derived BY DIRECT FILE LISTING, never by adding 3 to the line above:
    #   ls tests/logos/pass/*.logos              -> 2366
    #   ls tests/logos/pass/{wql_*,deem_*}.logos ->  191
    # difference 2175. DOOR counts unmoved (36 = 10 + 26): none of the three
    # declares a container family or a `direct` output form. No new gate
    # SCRIPT, so `ls tests/logos/*.sh` stays 66. The TWO new fail fixtures
    # (fail/bc_slice_index_return_local_fail, fail/bc_slice_range_return_local_fail)
    # do not enter any of these three counts — they are the pass corpus only.
    # ⚠ RE-DERIVED at the `&<const item>` cell (Phase 1): +1 / +0 / +1. ONE new
    # pass fixture — pass/bc_static_item_return_ref_admit, the ADMIT TWIN of
    # fail/bc_const_item_return_ref_fail — and its name matches neither `wql_*`
    # nor `deem_*`, so the whole delta lands in `nonglob`. Re-derived BY DIRECT
    # LISTING, not by addition:
    #   ls tests/logos/pass/*.logos                          -> 2369
    #   ls tests/logos/pass/{wql_*,deem_*}.logos             ->  191
    #   the same listing minus the glob half                 -> 2178
    # The fail fixture enters none of these three counts (pass corpus only).
    # DOOR counts unmoved (36 = 10 + 26): the fixture declares no container
    # family and no `direct` output form. No new gate SCRIPT.
    # ⚠ RE-DERIVED at the rustc borrow-check import (B167): +0 / +0 / +0, and
    # that zero is the point rather than a formality. The batch landed 412 FAIL
    # fixtures under `tests/imported/fail/` and 463 admit programs under
    # `tests/imported/admit/` — 875 new `.logos` files, the largest single
    # addition this pin has seen — and THIS POPULATION IS `tests/logos/pass`
    # ONLY, so none of them enters it. Re-derived BY DIRECT LISTING on the
    # landed tree, not by asserting the delta is zero:
    #   ls tests/logos/pass/*.logos                          -> 2369
    #   ls tests/logos/pass/{wql_*,deem_*}.logos             ->  191
    #   the same listing minus the glob half                 -> 2178
    # so the partition still closes: 2369 = 191 + 2178. DOOR counts unmoved
    # (36 = 10 + 26) — no imported fixture declares a container family or a
    # `direct` output form. ONE new gate SCRIPT: bc_admits_ledger_gate.sh.
    # ⚠ RE-DERIVED at the D1/D2 NLL-release round (2026-08-25): +6 / +0 / +6.
    # Six new PASS fixtures, none matching the `wql_*` / `deem_*` glob, so the
    # whole delta lands in `nonglob` and `glob` is unmoved. BY DIRECT LISTING on
    # the landed tree, not by asserting the delta:
    #   ls tests/logos/pass/*.logos                          -> 2375
    #   ls tests/logos/pass/{wql_*,deem_*}.logos             ->  191
    #   the same listing minus the glob half                 -> 2184
    # 2375 = 191 + 2184. The six are bc_nll_d1_{loop_ref,for_ref,loop_closure}_
    # admit and bc_nll_d2_field_{holder,holder_mut,sibling}_admit — borrow-check
    # programs with no container family and no `direct` output form, so the DOOR
    # counts are unmoved (36 = 10 + 26), asserted independently below.
    # ⚠ RE-DERIVED at the D6/D3 round (2026-08-25): +5 / +0 / +5. Five new PASS
    # fixtures, none matching the `wql_*` / `deem_*` glob, so the whole delta
    # lands in `nonglob`. BY DIRECT LISTING on the landed tree, not by asserting
    # the delta:
    #   ls tests/logos/pass/*.logos                          -> 2380
    #   ls tests/logos/pass/{wql_*,deem_*}.logos             ->  191
    #   the same listing minus the glob half                 -> 2189
    # 2380 = 191 + 2189. The five are bc_d6_mut_field_capture_{disjoint,nll,
    # through_mutref}_admit and bc_d3_{nested_block,loop_bare_block}_release_
    # admit — borrow-check programs with no container family and no `direct`
    # output form, so the DOOR counts are unmoved (36 = 10 + 26), asserted
    # independently below.
    # ⚠ RE-DERIVED at the over-refusal round (2026-08-26): +17 / +0 / +17, and
    # FIVE OF THE SEVENTEEN WERE ALREADY UNACCOUNTED. This gate was ALREADY RED
    # at dc4fdda52, before that round touched anything: the tree listed 2386
    # pass fixtures against a pin of 2381, because five fixtures landed after
    # 3c3899703 without re-deriving here — bc_derefwrite_shared_dead_admit,
    # bc_place_kind_admit_half, bc_range_view_nll_admit,
    # let_else_loop_no_break_admit, loop_break_targets_inner_admit. It is a
    # tier_full gate, so L2 never ran it and the commits that added them
    # reported themselves green over it. Named here rather than absorbed.
    # BY DIRECT LISTING on the landed tree, never by adding a delta:
    #   ls tests/logos/pass/*.logos                          -> 2398
    #   ls tests/logos/pass/{wql_*,deem_*}.logos             ->  191
    #   the same listing minus the glob half                 -> 2207
    # 2398 = 191 + 2207. The round's own twelve are the bc_reborrow_through_*,
    # bc_reassign_holder_releases_loan_admit, bc_ifexpr_/bc_matchexpr_arms_
    # alternatives_admit, the three bc_derefwrite_field_* admits and the three
    # let_else_labeled_*/labeled_loop_tail_* admits — borrow-check and
    # divergence programs with no container family and no `direct` output form,
    # so the DOOR counts are unmoved (36 = 10 + 26), asserted independently
    # below. The round's other twelve fixtures are FAIL fixtures and this
    # population is the PASS corpus.
    # 2026-08-26 (D1 by-value hop — the OUTER gate was the narrow predicate):
    # +3 pass fixtures, the re-slice refusal's three legal twins —
    #   pass/bc_d1hop_reslice_nll_admit       (write after `r`'s last use)
    #   pass/bc_d1hop_reslice_disjoint_admit  (two shared re-slices, no write)
    #   pass/bc_d1hop_reslice_onehop_admit    (the direct spelling, legal form)
    # — plus one FAIL fixture (bc_d1hop_reslice_assign_conflict), which is not
    # this gate's population. None matches the `wql_*` / `deem_*` glob, so the
    # whole delta lands in `nonglob` and `glob` is unmoved; none declares a
    # container family or a `direct` output form, so the DOOR counts are
    # unmoved (36 = 10 + 26).
    # RE-DERIVED BY DIRECT FILE LISTING, never by adding to the previous pin:
    #   ls tests/logos/pass/*.logos | wc -l                       -> 2401
    #   ls tests/logos/pass/{wql_*,deem_*}.logos | wc -l          ->  191
    # partition closes: 2401 = 191 + 2210.
    # ⚠ THIS RE-DERIVATION WAS DEMANDED BY A GATE, not by memory:
    # `logos_00_population_pin_lint` (tier_commit, landed 2a3bc0594) reds on the
    # commit that moves the corpus. The 23-commit blind window that preceded it
    # is what that gate exists for.
    # 2026-08-26 (class C / C-beta — the closure-as-argument arm): +3 pass
    # fixtures, the legal twins of the new refusal —
    #   pass/bc_clsC_b1_closure_arg_shared_admit    (shared capture, read after)
    #   pass/bc_clsC_b1_closure_arg_disjoint_admit  (mutating capture, other local)
    #   pass/bc_clsC_b1_closure_arg_move_admit      (`move` closure owns it)
    # — plus one FAIL fixture (bc_clsC_b1_closure_arg_conflict), not this gate's
    # population. None matches the `wql_*` / `deem_*` glob, none declares a
    # container family or a `direct` output form: `glob` and the DOOR counts are
    # unmoved (36 = 10 + 26).
    # 2026-08-27 (class B step B — the through-reference exemption rule): +3
    # pass fixtures, the legal halves of three one-token pairs —
    #   pass/bc_thru_ref_var_mut_reborrow_admit      (`&mut *rx`, rx: &mut i64)
    #   pass/bc_thru_ref_field_mut_reborrow_admit    (`&mut *h.r`, h.r: &mut i64)
    #   pass/bc_thru_ref_param_double_deref_admit    (`&mut **t0`, t0: &mut &mut)
    # — plus their three FAIL twins, which are not this gate's population. None
    # matches `wql_*` / `deem_*`, none declares a container family or a `direct`
    # output form: `glob` and the DOOR counts are unmoved (36 = 10 + 26).
    # RE-DERIVED BY DIRECT FILE LISTING, never by adding to the previous pin:
    #   ls tests/logos/pass/*.logos | wc -l                       -> 2411
    #   ls tests/logos/pass/{wql_*,deem_*}.logos | wc -l          ->  191
    # partition closes: 2416 = 191 + 2225. (+1 2026-08-27:
    # pass/bc_deref_move_exempt_admit, the single admit twin of the three
    # E0507-at-the-deref refusals — it carries every exemption of that rule
    # (Copy deref, place base, `&*r`, raw-pointer move-out, the three legal
    # `static` shapes, `ref` bindings). No `wql_*`/`deem_*` match, no container
    # family, no `direct` output form, so `glob` and the DOOR counts are
    # unmoved.) (+1 2026-08-27:
    # pass/bc_dropck_reverse_order_nodrop_admit, the non-Drop pin for the
    # same-frame reverse-drop-order rule — no `wql_*`/`deem_*` match, no
    # container family, no `direct` output form.) (+1 2026-08-27:
    # pass/bc_write_thru_shared_raw_ptr_admit, the raw-pointer exemption pin for
    # the write-through-shared-`&` rule — no `wql_*`/`deem_*` match, no
    # container family, no `direct` output form, so `glob` and the DOOR counts
    # are unmoved.) (+4 2026-08-27: the
    # bc_patloan_* admit twins for the written-`ref` pattern-binding loan.
    # +2 2026-08-27: pass/bc_dropck_loan_nodrop_admit and
    # pass/bc_dropck_loan_raw_field_admit, the two exemption pins for dropck
    # liveness in the loan channel — neither matches `wql_*`/`deem_*`, neither
    # declares a container family or a `direct` output form, so `glob` and the
    # DOOR counts are unmoved.) (+3 2026-08-27: the three admit twins of the
    # CLOSURE BODY WALK — pass/bc_capbody_intra_body_disjoint_admit (RFC-2229
    # disjointness survives the walk), pass/bc_capbody_closure_return_admit (a
    # `return` inside a walked body does NOT answer to the enclosing fn's
    # return contract — the program that caught the round's one over-refusal
    # and sent three rows back to the shelf), and
    # pass/bc_capbody_move_body_not_walked_admit (a `move` body is not walked
    # at all). RE-DERIVED BY DIRECT LISTING: ls tests/logos/pass/*.logos -> 2419,
    # glob unchanged at 191, so 2419 = 191 + 2228. None matches `wql_*`/`deem_*`,
    # none declares a container family or a `direct` output form, so `glob` and
    # the DOOR counts are unmoved.)
    # (+4 2026-08-27: the four legal twins of the GENERIC-AUTOREF RECEIVER TIE
    # — pass/bc_genrecv_two_mut_sequential_admit, .../generic_then_plain_admit,
    # .../field_write_after_admit, .../shared_then_mut_admit. RE-DERIVED BY
    # DIRECT LISTING: ls tests/logos/pass/*.logos -> 2424, glob unchanged at
    # 191, so 2424 = 191 + 2233. The fifth is
    # pass/bc_genrecv_constructed_legals_admit, the eleven hand-written legal
    # shapes that measured the same tie in the over-refusal direction. None matches `wql_*`/`deem_*`, none declares a
    # container family or a `direct` output form, so `glob` and the DOOR counts
    # are unmoved.)
    # (+6 2026-08-28: the six legal twins of the MATCH-GUARD SCRUTINEE BORROW
    # (E0510) — pass/bc_guard_{no_test,sibling_field,sibling_tuple_elem,
    # refmut_arm,indexed_scrut,loan_released}_admit. Every one is a program the
    # CRUDE form of the rule refused, so they are the round's cost measurement,
    # not decoration; bc_guard_loan_released_admit caught a live leak of the
    # guard's own loan into the arm body. RE-DERIVED BY DIRECT LISTING:
    # ls tests/logos/pass/*.logos -> 2430, glob unchanged at 191, so
    # 2430 = 191 + 2239. None matches `wql_*`/`deem_*`, none declares a
    # container family or a `direct` output form, so `glob` and the DOOR counts
    # are unmoved.)
    # (+2 2026-08-28: the two legal twins of the RVALUE-MATCH PATTERN LOAN —
    # pass/bc_mexprpat_refmut_loan_admit (the loan is NLL-released before the
    # conflicting write) and pass/bc_mexprpat_binding_discarded_admit (the arm
    # DECLARES a `ref mut` binding and hands out something else, plus the
    # shared-loan, sibling-field, loop and temporary-scrutinee shapes). The
    # second is the round's cost measurement: it is the program the ungated
    # rule refused, and no program in the corpus had its shape.
    # RE-DERIVED BY DIRECT LISTING: ls tests/logos/pass/*.logos -> 2432, glob
    # unchanged at 191, so 2432 = 191 + 2241. Neither matches `wql_*`/`deem_*`,
    # neither declares a container family or a `direct` output form, so `glob`
    # and the DOOR counts are unmoved.
    # 2026-08-28: +1 → 2433 = 191 + 2242. bc_recvmutbind_pattern_mut_binding,
    # the hand-written counter-example that DECLINED the `recvmutbind`
    # mechanism after it priced CEILING 1 / COST 0 on the whole corpus. Same
    # reasoning as above: no `wql_*`/`deem_*` match, no container family, no
    # `direct` output form, so `glob` and the DOOR counts are unmoved.
    # 2026-08-28: +2 → 2435 = 191 + 2244. bc_idxbase_read_in_index and
    # bc_idxbase_disjoint_field — the legal half of the E0510 index-base loan
    # (the reads that must stay legal, and the field granularity that keeps the
    # rule cheap). Same reasoning again on all three counts.
    # 2026-08-28: +1 → 2436 = 191 + 2245. bc_opeq_init_before_use — the seven
    # ways a binding IS initialised before `v op= e` reads it, which must keep
    # compiling now that lower_compound_assign asks the tracker. Same reasoning
    # again on all three counts.
    # 2026-08-28: +1 → 2437 = 191 + 2246. bc_recvresv_two_phase_legal — the
    # two-phase shapes a bare-place receiver's reservation must keep admitting.
    # Same reasoning again on all three counts.)
    'corpus'            : 2437,
    'glob'              : 191,   # `wql_*` + `deem_*` — pull_shape's population
    'nonglob'           : 2246,  # pinned by NOTHING before this gate; +16 with
                                 # `corpus` above, the sixteen mlirgen_odr_*
                                 # pass fixtures of the #58/#59/#60 identity arc
    'overlap'           : 0,     # ⚠ VACUOUS BY SET ARITHMETIC, kept as a
                                 # readable statement of intent, not a check:
                                 # nonglob_set = corpus_set - glob_set, so the
                                 # intersection is empty however the filesystem
                                 # looks. The partition's REAL content is the
                                 # swept-vs-listed both-directions leg below.

    # ⚠ RE-DERIVED at the #83 generic-receiver stage (the summary plane learns
    # the mono key, and the pre-mono pass gets summaries): +3 / +0 / +3. The
    # three are PASS fixtures and none matches the `wql_*` / `deem_*` glob:
    #   tests/logos/pass/bc_esc_generic_recv_admit.logos
    #   tests/logos/pass/bc_esc_generic_uninst_admit.logos
    #   tests/logos/pass/bc_esc_generic_monokey_admit.logos
    # DERIVED BY DIRECT FILE LISTING, not by adding 3 to the previous pin:
    #   ls tests/logos/pass/*.logos | wc -l                       -> 2194
    #   ls tests/logos/pass/{wql_*,deem_*}.logos | wc -l          ->  191
    # so nonglob is 2003 by the same listing minus the glob listing, and the
    # partition closes: 2194 = 191 + 2003. The four FAIL fixtures this stage
    # added are outside this gate's population by construction. DOOR counts
    # unmoved (36 = 10 + 26): none of the three declares a container family —
    # they are trait / generic-receiver borrow-check shapes only.

    # ⚠ RE-DERIVED at the #86 holder-escape stage (the return gate stops asking
    # "is this a REFERENCE" and asks "does this VALUE hold a borrow", plus the
    # LET that builds the holder and the borrow a method RECEIVER carries):
    # +9 / +0 / +9. All nine are PASS fixtures and none matches the `wql_*` /
    # `deem_*` glob:
    #   tests/logos/pass/bc_esc_holder_return_field_admit.logos
    #   tests/logos/pass/bc_esc_holder_return_struct_admit.logos
    #   tests/logos/pass/bc_esc_holder_return_tuple_admit.logos
    #   tests/logos/pass/bc_esc_holder_return_option_admit.logos
    #   tests/logos/pass/bc_esc_holder_return_chained_admit.logos
    #   tests/logos/pass/bc_esc_holder_return_method_admit.logos
    #   tests/logos/pass/bc_esc_holder_return_dyn_admit.logos
    #   tests/logos/pass/bc_esc_holder_return_generic_admit.logos
    #   tests/logos/pass/bc_esc_holder_outparam_param_borrow_admit.logos
    # DERIVED BY DIRECT FILE LISTING, not by adding 9 to the previous pin:
    #   ls tests/logos/pass/*.logos | wc -l                       -> 2203
    #   ls tests/logos/pass/{wql_*,deem_*}.logos | wc -l          ->  191
    # so nonglob is 2012 by the same listing minus the glob listing, and the
    # partition closes: 2203 = 191 + 2012. The eight FAIL fixtures this stage
    # added are outside this gate's population by construction. DOOR counts
    # unmoved (36 = 10 + 26): none of the nine declares a container family —
    # they are struct/tuple/enum holder and method-extraction borrow-check
    # shapes only.

    # ── CLAUSE 2, compile coverage ─────────────────────────────────────────
    # ZERO. Not "few": an uncompiled fixture is an unmeasured fixture, and the
    # archive map above exists so this stays at zero rather than so the sweep
    # can be tolerant. First measurement without the map was 50.
    'unswept'           : 0,

    # ── CLAUSE 3, the doors, per HALF ──────────────────────────────────────
    # NONGLOB, 26 doors on 11 fixtures (the per-fixture table is `DOORS` below).
    #
    # ⚠ THE BRIEFED FLOOR FOR THIS STAGE WAS 22, DERIVED BY THE S5-direct verify
    # over the 69 non-glob pass fixtures MENTIONING `deem`, counted by the
    # `^pub struct …Dx… {` shape. It is 26. The four it could not see, each with
    # its mechanism — and note that all four are misses of the METHOD, not of
    # the arithmetic:
    #   +2 `memoria_showcase_deem` — 4 doors, not 2. Two of them are emitted
    #      into `logos.gen.borrow_carrying.Hs*` units (the container family is
    #      declared in an imported package), which the inherited user-module
    #      dump rule drops. This gate reads every unit and scopes by emitter.
    #   +1 `memoria_ctr_vec_deem` — one door, same `logos.gen.*` mechanism.
    #   +1 `container_item_from_module` — one door; it does not compile
    #      standalone at all (needs `-l libctr_mod.a`, exit 4), so the earlier
    #      sweep produced no dump for it and it contributed a silent zero.
    # 22 + 2 + 1 + 1 = 26.
    #
    # ⚠ AND THE FLOOR'S OWN 22 CONTAINED A NON-DOOR IT COULD NOT HAVE SEPARATED:
    # the shape count also matches `bc_d8_quote_field_split_admit`'s `QuoteDx`
    # (see the provenance note at the top). It is outside the "mentioning deem"
    # sub-population that produced the 22, so it did not enter that total — but
    # any widening of a SHAPE-only rule to the whole corpus would have taken it
    # in, and the gate would have opened red. The provenance rule is not a
    # refinement of the floor's method; it is the reason the wide count is
    # possible at all.
    'nonglob_doors'     : 26,
    # GLOB, 10 — the same number `pull_shape` pins as `dx_struct`, re-measured
    # here by an independent sweep that (a) compiles the two GLOB fixtures that
    # gate cannot and (b) reads the `logos.gen.*` units it drops. Both extras
    # measure 0 today, so `pull_shape`'s 10 is CORRECT — but it is correct by
    # luck on (b): the day a `wql_*` fixture puts a door in an imported
    # package's unit, that gate would read it as a door that vanished. This
    # gate is where that is caught.
    'glob_doors'        : 10,
    'corpus_doors'      : 36,   # asserted == nonglob + glob, not typed twice

    # ── CLAUSE 4, the six spellings, corpus-wide, cross-pinned ─────────────
    'dx_struct'         : 36,
    'dx_inherent'       : 36,
    'dx_forward'        : 36,
    'dx_facade'         : 36,
    'dx_impl'           : 36,
    'nb_forward'        : 36,

    # ── the NON-DEEM residual, pinned PER SPELLING (no clause of its own —
    #    it is read by CLAUSE 6's pin sweep; the comment used to call it
    #    "CLAUSE 5", which is the plan↔artifact identity's number in the code) ─
    # `pass/bc_d8_quote_field_split_admit`, emitter `gen_quote`: a hand-written
    # mimic of the door shape, 2 spellings of 5. Pinned per spelling and not as
    # a total, so a second non-deem `Dx` cannot cancel against this one — and
    # so that the day this fixture's mimic gains a `BatchStream` impl (which
    # would make it indistinguishable from a door by shape) the gate says which
    # spelling appeared.
    #
    # ⚠ `nb_forward` IS DELIBERATELY ABSENT FROM THIS RESIDUAL, and the reason
    # is a measurement: `return self.next_batch();` occurs 74 times in NON-deem
    # units (the `__container_item` family plane's own `BatchStream` forwarding
    # bodies). It is the one door spelling that is not `Dx`-anchored, so outside
    # a deem unit it is not evidence of a door at all — pinning it here would
    # bolt a container-family count onto a door gate and make this file red on
    # every container fixture. INSIDE a deem unit it IS door-exclusive, which is
    # where CLAUSE 3 counts it. The other five are `Dx`-anchored and measure 0
    # outside deem units, which is why they are pinned and it is not.
    'nd_struct'         : 1,
    'nd_inherent'       : 1,
    'nd_forward'        : 0,
    'nd_facade'         : 0,
    'nd_impl'           : 0,
    # The fixtures contributing ANY non-deem door spelling, by name. A count
    # alone would let the mimic move to another fixture unnoticed.
    'nd_fixtures'       : ['bc_d8_quote_field_split_admit'],

    # ── the plan↔artifact identity — CLAUSE 5 in the code below ────────────
    'plan_doors'        : 36,
}

# The per-fixture door table — the pin that makes a door MOVING between
# fixtures visible, which no total can. `G`/`N` records which half it is in, so
# a fixture renamed across the glob boundary reds here too.
DOORS = {
    'container_item_e2e'           : ('N', 2),
    'container_item_from_module'   : ('N', 1),
    'memoria_ctr_class_deem'       : ('N', 2),
    'memoria_ctr_gen_vector_deem'  : ('N', 2),
    'memoria_ctr_map_deem'         : ('N', 2),
    'memoria_ctr_plan_pushdown'    : ('N', 5),
    'memoria_ctr_vec_bool'         : ('N', 1),
    'memoria_ctr_vec_deem'         : ('N', 1),
    'memoria_ctr_vec_pos_pushdown' : ('N', 4),
    'memoria_showcase_deem'        : ('N', 4),
    'memoria_showcase_vector'      : ('N', 2),
    'deem_batch_scan_drain'        : ('G', 1),
    'deem_ctr_family_streams'      : ('G', 2),
    'deem_direct_stream_pull'      : ('G', 1),
    'deem_emitted_struct_field_layout': ('G', 1),
    'deem_order_desc_elision'      : ('G', 1),
    'deem_order_elision'           : ('G', 1),
    'deem_pipeline_handle_seam'    : ('G', 1),
    'deem_rowmajor_batch_source'   : ('G', 1),
    'deem_source_size'             : ('G', 1),
}

# ── the population, from the FILESYSTEM ─────────────────────────────────────
# ⚠ THE TWO HALVES ARE DERIVED BY TWO DIFFERENT MECHANISMS ON PURPOSE. Taking
# the glob half as "the ones my prefix test accepts" and the other half as "the
# rest" makes the sum clause below TAUTOLOGICAL — it would hold no matter what
# the filesystem contained, which is the vacuous-green shape this repo keeps
# catching. So the GLOB half is read with the LITERAL SHELL GLOBS the other two
# gates use (`wql_*.logos`, `deem_*.logos`) and the PREFIX rule is computed
# separately; CLAUSE 1 asserts the two agree AS SETS. A fixture the shell glob
# takes but the prefix test does not (or the reverse) means this gate and
# `pull_shape` disagree about who owns it — the one way a fixture really can be
# counted twice or by nobody.
corpus_set = set(os.path.basename(p)[:-6]
                 for p in glob.glob(os.path.join(PASS, '*.logos')))
glob_set   = set(os.path.basename(p)[:-6]
                 for p in (glob.glob(os.path.join(PASS, 'wql_*.logos'))
                           + glob.glob(os.path.join(PASS, 'deem_*.logos'))))
prefix_set = set(b for b in corpus_set
                 if b.startswith('wql_') or b.startswith('deem_'))
nonglob_set = corpus_set - glob_set
bases = sorted(corpus_set)
is_glob = lambda b: b in glob_set
M = {'corpus': len(corpus_set)}
M['glob']    = len(glob_set)
M['nonglob'] = len(nonglob_set)
M['overlap'] = len(glob_set & nonglob_set)

# ── compile coverage ────────────────────────────────────────────────────────
st = {}
for p in glob.glob(os.path.join(OUT, '_st', '*')):
    st[os.path.basename(p)] = open(p).read().strip()
bad = sorted(b for b, r in st.items() if r != '0')
M['unswept'] = len(bad)

# ── the units, split by PROVENANCE ──────────────────────────────────────────
HDR = re.compile(r'^// GENERATED by metaprog', re.M)
EMB = re.compile(r'^// emitted by: (\S+)', re.M)
SPELL = {
 'dx_struct'  : re.compile(r'#\[borrow_carrying\]\n\npub struct [A-Za-z_0-9]+Dx[A-Za-z_0-9]* \{'),
 'dx_inherent': re.compile(r'pub fn next_batch\(self: &mut [A-Za-z_0-9]+Dx[A-Za-z_0-9]*\) -> Option<&\['),
 'dx_forward' : re.compile(r'fn next\(&mut self\) -> Option<RowsBatch<'),
 'dx_facade'  : re.compile(r'-> Result<[A-Za-z_0-9]+Dx[A-Za-z_0-9]*, ElError>'),
 'dx_impl'    : re.compile(r'impl BatchStream<RowsBatch<.*>> for [A-Za-z_0-9]+Dx'),
 'nb_forward' : re.compile(r'return self\.next_batch\(\);'),
}
# `nb_forward` is absent ON PURPOSE — see the pin block: it is the one spelling
# that is not `Dx`-anchored and it occurs 74 times in the container-family
# plane's own units, which is not evidence about a door.
NDKEY = {'dx_struct': 'nd_struct', 'dx_inherent': 'nd_inherent',
         'dx_forward': 'nd_forward', 'dx_facade': 'nd_facade',
         'dx_impl': 'nd_impl'}

for k in SPELL:
    M[k] = 0
for k in NDKEY.values():
    M[k] = 0
per_fixture = {}
nd_fixtures = set()
dumps = sorted(glob.glob(os.path.join(OUT, '*.user')))
if not dumps:
    print("FAIL(2): no user dumps in %s — nothing was swept." % OUT)
    sys.exit(2)
for p in dumps:
    b = os.path.basename(p)[:-5]
    blob = open(p, errors='replace').read()
    # Unit boundaries are the dump headers; each unit names its emitter.
    cuts = [m.start() for m in HDR.finditer(blob)] or [0]
    cuts.append(len(blob))
    for i in range(len(cuts) - 1):
        unit = blob[cuts[i]:cuts[i + 1]]
        m = EMB.search(unit)
        deem = bool(m) and m.group(1) == 'deem'
        for k, rx in SPELL.items():
            c = len(rx.findall(unit))
            if not c:
                continue
            if deem:
                M[k] += c
                if k == 'dx_struct':
                    per_fixture[b] = per_fixture.get(b, 0) + c
            elif k in NDKEY:
                M[NDKEY[k]] += c
                nd_fixtures.add(b)
M['nd_fixtures'] = sorted(nd_fixtures)
M['glob_doors']    = sum(v for b, v in per_fixture.items() if is_glob(b))
M['nonglob_doors'] = sum(v for b, v in per_fixture.items() if not is_glob(b))
M['corpus_doors']  = M['glob_doors'] + M['nonglob_doors']

# ── the plan side ───────────────────────────────────────────────────────────
plan = {}
for p in glob.glob(os.path.join(OUT, '_plan', '*')):
    try:
        n = int(open(p).read().strip())
    except ValueError:
        n = 0
    if n:
        plan[os.path.basename(p)] = n
M['plan_doors'] = sum(plan.values())

# ── CLAUSE 1: the partition sums, and the halves are disjoint ───────────────
if M['glob'] + M['nonglob'] != M['corpus']:
    fail.append("CLAUSE 1: %d glob + %d nonglob != %d corpus — the partition "
                "lost or double-counted a fixture."
                % (M['glob'], M['nonglob'], M['corpus']))
if glob_set != prefix_set:
    d = sorted(glob_set ^ prefix_set)
    fail.append("CLAUSE 1: the shell glob `wql_*`/`deem_*` and the prefix rule "
                "disagree on %d fixture(s) — this gate and `pull_shape` do not "
                "agree who owns them: %s" % (len(d), ' '.join(d[:12])))
if glob_set - corpus_set:
    fail.append("CLAUSE 1: %d glob fixture(s) are not in the corpus listing."
                % len(glob_set - corpus_set))
# ⚠ THIS IS THE HALF OF CLAUSE 1 THAT CAN ACTUALLY FIRE, and it is the one that
# means "no fixture was dropped". The sum above is forced by arithmetic (the two
# halves are computed from one listing) and the set-equality above compares two
# spellings of one rule; NEITHER can catch the real failure, which is a fixture
# that was LISTED and never PROBED. That is compared here: the staged set
# against the corpus listing, both directions. Since task #85 there is a second,
# EARLIER refusal of the same failure — `facts_require` names every population
# member whose facts are missing or stamped for another tree, before a single
# door is counted — and this clause is the one that still fires if the staging
# itself drops one.
swept = set(st)
if corpus_set - swept:
    d = sorted(corpus_set - swept)
    fail.append("CLAUSE 1: %d corpus fixture(s) were never probed — their doors "
                "are unmeasured and the census is silently short: %s"
                % (len(d), ' '.join(d[:12])))
if swept - corpus_set:
    d = sorted(swept - corpus_set)
    fail.append("CLAUSE 1: %d probed fixture(s) are not in the corpus — the "
                "sweep is stale: %s" % (len(d), ' '.join(d[:12])))

# ── CLAUSE 2: every fixture compiled ────────────────────────────────────────
if bad:
    fail.append("CLAUSE 2: %d fixture(s) did not compile, so their doors were "
                "never measured — the archive map in this file has drifted "
                "from `logos_pass_extra_args`: %s"
                % (len(bad), ' '.join(bad[:12])))

# ── CLAUSE 3: the six spellings agree, corpus-wide ──────────────────────────
cols = [M[k] for k in ('dx_struct', 'dx_inherent', 'dx_forward',
                       'dx_facade', 'dx_impl', 'nb_forward')]
if len(set(cols)) != 1:
    fail.append("CLAUSE 3: a door is half-emitted — struct %d, inherent %d, "
                "forward %d, facade %d, impl %d, forward-body %d. Five of these "
                "are one decision; they may only move together."
                % tuple(cols))
# ⚠ THE ATTRIBUTION LEG THAT USED TO SIT HERE WAS A TAUTOLOGY AND IS DELETED.
# It compared `corpus_doors` with `dx_struct`, but `per_fixture[b]` is
# incremented with the SAME counter in the SAME branch that increments
# `dx_struct`, so the two are one accumulator read twice and the comparison
# could never be unequal. Presented as a check on attribution, it checked
# nothing (this gate's own verify found it). Attribution IS checked — by
# CLAUSE 4, which names every door-bearing fixture and its count in BOTH
# directions, and by CLAUSE 5's per-fixture plan-vs-artifact identity. A
# tautology beside them does not add a guarantee; it adds the appearance of one.

# ── CLAUSE 4: the per-fixture table, both directions ────────────────────────
for b, (half, n) in sorted(DOORS.items()):
    got = per_fixture.get(b, 0)
    gh = 'G' if is_glob(b) else 'N'
    if got != n:
        fail.append("CLAUSE 4: %-32s pinned %d doors, measured %d" % (b, n, got))
    elif gh != half:
        fail.append("CLAUSE 4: %-32s pinned in half %s, is in half %s"
                    % (b, half, gh))
for b, n in sorted(per_fixture.items()):
    if b not in DOORS:
        fail.append("CLAUSE 4: %-32s emits %d door(s) and is in NO pin — a new "
                    "door-bearing fixture must be named here, not absorbed."
                    % (b, n))

# ── CLAUSE 5: the plan↔artifact identity, PER FIXTURE ───────────────────────
for b in sorted(set(plan) | set(per_fixture)):
    if plan.get(b, 0) != per_fixture.get(b, 0):
        fail.append("CLAUSE 5: %-32s plan says %d direct door(s), artifact has "
                    "%d" % (b, plan.get(b, 0), per_fixture.get(b, 0)))

# ── CLAUSE 6: every pin, exactly ────────────────────────────────────────────
for k in sorted(PIN):
    if M.get(k) != PIN[k]:
        fail.append("PIN %-20s expected %-6s measured %s"
                    % (k, PIN[k], M.get(k)))

print("corpus %d = glob %d + nonglob %d · unswept %d · doors %d = glob %d + "
      "nonglob %d · plan %d · non-deem residual %d/%d"
      % (M['corpus'], M['glob'], M['nonglob'], M['unswept'], M['corpus_doors'],
         M['glob_doors'], M['nonglob_doors'], M['plan_doors'],
         M['nd_struct'], M['nd_inherent']))
if fail:
    print("\n".join(fail))
    print("FAIL: %d clause(s)/pin(s) moved." % len(fail))
    sys.exit(1)
print("OK: %d direct doors pinned across the whole pass corpus." % M['corpus_doors'])
PY
# The python heredoc is the LAST command, so the script's status IS the
# analyser's real process status (0 / 1 / 2) — no arithmetic anywhere near it,
# which is the 8-bit-ceiling class `gate_lint.py` R1 exists to refuse.
