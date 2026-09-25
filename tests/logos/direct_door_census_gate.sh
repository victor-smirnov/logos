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
    # ⚠ RE-DERIVED at the 2026-09-16h-clausearm stage, BY DIRECT FILE LISTING and
    # never by adding to the previous line:
    #   ls tests/logos/pass/*.logos | wc -l              -> 3810
    #   ls tests/logos/pass/{wql_*,deem_*}.logos | wc -l ->  191
    # partition closes: 3810 = 191 + 3619. SIX pass fixtures joined `nonglob`
    # (none matches the glob) — bc_0916h_hb_n05_pass, bc_0916h_hb_n07_pass,
    # bc_0916h_hb_n08_pass, bc_0916h_hb_n09_pass,
    # bc_0916h_row_outlives_call_instantiation_pass and
    # bc_0916h_row_outlives_method_call_pass. The first four are hand-battery
    # programs whose verdict MOVED base(refused) -> landed(runs); the last two are
    # the programs of the two soundness_queue rows CLOSED in the same commit. The
    # round's one FAIL fixture (bc_0916h_hb_n02_refuse), its spec pass/fail pair
    # and its one new queue row are not in this population. DOOR counts unmoved
    # (36 = 10 + 26): none of the six declares a container family or a `direct`
    # output form — they are `&i64` / struct / Option region shapes only.

    # ⚠ RE-DERIVED at the 2026-09-09g-tlrefbind stage, BY DIRECT FILE LISTING and
    # never by adding to the previous line:
    #   ls tests/logos/pass/*.logos | wc -l              -> 2989
    #   ls tests/logos/pass/{wql_*,deem_*}.logos | wc -l ->  191
    # partition closes: 2989 = 191 + 2798. EIGHT pass fixtures joined `nonglob`
    # (none matches the glob) — refbind_scalar_under_ref,
    # toplevel_refbind_over_ref_scrutinee, exprmatch_refbind_over_ref_scrutinee,
    # refbind_byvalue_struct_field_read, refpat_refbind_depth2,
    # refpat_refbind_struct_field_read, exprmatch_refbind_byvalue_struct_field,
    # refbind_over_ref_scrutinee_shapes; the round's EIGHT fail halves are not in
    # this population, and none of the sixteen declares a container family or a
    # `direct` output form, so `doors`, `glob` and `nonglob_doors` are unmoved.
    # ⚠ RE-DERIVED at the 2026-09-09j-capret stage, BY DIRECT FILE LISTING and
    # never by adding to the previous line:
    # ⚠ RE-DERIVED AGAIN at the 2026-09-09l-bcs stage, BY DIRECT FILE LISTING:
    #   ls tests/logos/pass/*.logos | wc -l              -> 2981
    #   ls tests/logos/pass/{wql_*,deem_*}.logos | wc -l ->  191
    # partition closes: 2981 = 191 + 2790. Three pass fixtures joined `nonglob`
    # — bcs_temp_struct_let_e0716_ok, bcs_temp_enum_let_e0716_ok,
    # bcs_temp_struct_assign_e0716_ok; the round's three FAIL halves and the
    # imported fail fixture are not in this population, and none of the six
    # declares a container family or a `direct` door, so `doors`, `glob` and
    # `nonglob_doors` are unmoved.
    #   ls tests/logos/pass/*.logos | wc -l              -> 2978
    #   ls tests/logos/pass/{wql_*,deem_*}.logos | wc -l ->  191
    # partition closes: 2978 = 191 + 2787. Three pass fixtures joined `nonglob`
    # (none matches the glob) — bc_capret_shared_reborrow_of_capture_pass,
    # bc_capret_shared_reborrow_thru_call_pass, bc_capret_move_capture_value_pass;
    # the round's three FAIL halves are not in this population. DOOR counts
    # unmoved — none of the three declares a container family or a `direct`
    # output form.
    # ⚠ RE-DERIVED at the 2026-09-10d-vecpkg stage, BY DIRECT FILE LISTING and
    # never by adding to the previous line:
    #   ls tests/logos/pass/*.logos | wc -l              -> 3017
    #   ls tests/logos/pass/{wql_*,deem_*}.logos | wc -l ->  191
    # partition closes: 3017 = 191 + 2826.
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
    # ⚠ RE-DERIVED at the 2026-08-31h round (the closure-return contract with a
    # SCOPED capture exemption, and the outlives binder narrowing; ledger
    # 306 -> 297): +4 / +0 / +4. FOUR new PASS fixtures — the admit twins of the
    # two new fail fixtures (`&u` for `&t`, `-> &i64` for `-> &'static i64`), the
    # legal-shapes set that is the outlives narrowing's COST side, and
    # pass/bc_ltunmentbind_renamed_binder_hole, which pins a HOLE and not a
    # correct verdict — an illegal program the rule misses one token away from
    # one it catches. None matches `wql_*` / `deem_*`, so the whole delta lands
    # in `nonglob`. BY DIRECT LISTING on the landed tree:
    #   ls tests/logos/pass/*.logos                          -> 2498
    #   ls tests/logos/pass/{wql_*,deem_*}.logos             ->  191
    #   the same listing minus the glob half                 -> 2307
    # 2498 = 191 + 2307. DOOR counts unmoved (36 = 10 + 26): no container family
    # and no `direct` output form among them. The THREE new fail fixtures and the
    # NINE imported programs that moved admit -> fail enter none of these three
    # counts: this population is `tests/logos/pass` only.
    # ⚠ RE-DERIVED at the 2026-08-31f round (three declaration-site lifetime
    # rules landed; ledger 310 -> 306): +5 / +0 / +5. FIVE new PASS fixtures —
    # the legal twins of the three mechanisms that landed, plus TWO that pin
    # exemptions with a price and no purchase (the `'_`/elided enum payload
    # lifetimes, and the enum PREPASS carve-out without which five legal
    # programs are refused). None matches `wql_*` / `deem_*`, so the whole
    # delta lands in `nonglob`. BY DIRECT LISTING on the landed tree:
    #   ls tests/logos/pass/*.logos                          -> 2494
    #   ls tests/logos/pass/{wql_*,deem_*}.logos             ->  191
    #   the same listing minus the glob half                 -> 2303
    # 2494 = 191 + 2303. The five are pass/bc_enumpldlt_declared_payload_lifetime,
    # pass/bc_enumpldlt_placeholder_payload_lifetime,
    # pass/bc_ltargdecl_lifetime_arg_arity_match,
    # pass/bc_ltargdecl_selfref_enum_prepass and
    # pass/bc_ltbindresv_ordinary_binder_name — declaration-site lifetime
    # programs with no container family and no `direct` output form, so the DOOR
    # counts are unmoved (36 = 10 + 26). The THREE new fail fixtures and the FOUR
    # imported programs that moved admit -> fail enter none of these three
    # counts: this population is `tests/logos/pass` only.
    # ⚠ RE-DERIVED at the 2026-08-30d round (the `&mut`-affine partition
    # re-mechanised + P9's missing struct field site): +4 / +0 / +4. FOUR new
    # PASS fixtures — the legal twins of the three mechanisms that landed, plus
    # the `'_` placeholder field that pins the exemption the strict spelling
    # would have refused. None matches `wql_*` / `deem_*`, so the whole delta
    # lands in `nonglob`. BY DIRECT LISTING on the landed tree:
    #   ls tests/logos/pass/*.logos                          -> 2489
    #   ls tests/logos/pass/{wql_*,deem_*}.logos             ->  191
    #   the same listing minus the glob half                 -> 2298
    # 2489 = 191 + 2298. The four are pass/bc_mutvecfor_shared_vec_looped_twice,
    # pass/bc_byvalmutref_mutref_to_ref_formal,
    # pass/bc_structfldlt_declared_field_lifetime and
    # pass/bc_structfldlt_placeholder_field_lifetime — borrow-check and
    # lifetime-scope programs with no container family and no `direct` output
    # form, so the DOOR counts are unmoved (36 = 10 + 26). The THREE new fail
    # fixtures and the SIX imported programs that moved admit -> fail enter none
    # of these three counts: this population is `tests/logos/pass` only.
    # ⚠ RE-DERIVED at the bck.NEW landing round (2026-08-30): +5 / +0 / +5.
    # FIVE new PASS fixtures, the legal twins of the four mechanisms that
    # landed out of the `bck.NEW` survey; none matches `wql_*` / `deem_*`, so
    # the whole delta lands in `nonglob`. BY DIRECT LISTING on the landed tree:
    #   ls tests/logos/pass/*.logos                          -> 2485
    #   ls tests/logos/pass/{wql_*,deem_*}.logos             ->  191
    #   the same listing minus the glob half                 -> 2294
    # 2485 = 191 + 2294. The five are pass/bc_scinitcond_lhs_init_twin,
    # pass/bc_indexnomut_indexmut_twin, pass/bc_recvselfderef_ref_self_twin,
    # pass/bc_recvselfderef_unresolved_callee_twin and
    # pass/bc_guardmovearm_expr_guard_borrows_twin — borrow-check / definite-
    # init programs with no container family and no `direct` output form, so
    # the DOOR counts are unmoved (36 = 10 + 26). The FOUR new fail fixtures
    # and the EIGHT imported programs that moved admit -> fail enter none of
    # these three counts: this population is `tests/logos/pass` only.
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
    # Same reasoning again on all three counts.
    # 2026-08-28: +2 → 2439 = 191 + 2248.
    # RE-DERIVED BY DIRECT FILE LISTING, not by adding 2 to the previous pin:
    #   ls tests/logos/pass/*.logos | wc -l                       -> 2439
    #   ls tests/logos/pass/{wql_*,deem_*}.logos | wc -l          ->  191
    # partition closes: 2439 = 191 + 2248. The two are
    # bc_patsubmove_legal_shapes (the twelve shapes a by-value pattern binding's
    # SUB-place move must keep admitting, three of them labelled SILENCES) and
    # bc_mfjoin_loop_partial_move_legal (the ten a loop-edge partial-move join
    # must keep admitting). The round's other two fixtures are FAIL fixtures and
    # this population is the PASS corpus, so they move nothing here. `glob` and
    # the DOOR counts unmoved (36 = 10 + 26): no `wql_*`/`deem_*` match, no
    # container family, no `direct` output form in either.
    # ⚠ AND THIS PIN WAS RED FOR ONE COMMIT BEFORE IT WAS READ, by exactly the
    # 15th kind of gate lie this repo already records: the first of the two
    # fixtures landed under an L4 run invoked as `test-levels.sh L4 bc | tail
    # -25`, so the rc that was read was TAIL's and not the gate's. The gate did
    # its job; the reader broke it. Never pipe a gate whose rc you intend to
    # quote.)
    # 2026-08-28 (B68 field-write variance round): +1 → 2443 = 191 + 2252.
    # RE-DERIVED BY DIRECT FILE LISTING:
    #   ls tests/logos/pass/*.logos | wc -l                       -> 2443
    #   ls tests/logos/pass/{wql_*,deem_*}.logos | wc -l          ->  191
    # partition closes: 2443 = 191 + 2252. The one is
    # bc_fieldassign_variance_legal_shapes — the EIGHTEEN legal field writes
    # that the variance check at `lower_place_assign` must keep admitting, kept
    # as one fixture because the measurement that funded the rule was about the
    # SET. The round's other three fixtures are FAIL fixtures and this
    # population is the PASS corpus, so they move nothing here; the two imported
    # ports it closed moved admit -> fail under tests/imported/ and are outside
    # this population entirely. `glob` and the DOOR counts unmoved (36 = 10 +
    # 26): no `wql_*`/`deem_*` match, no container family, no `direct` form.
    #
    # 2026-08-28 (earlier, B87 field-door round): +2 → 2442 = 191 + 2251.
    # RE-DERIVED BY DIRECT FILE LISTING:
    #   ls tests/logos/pass/*.logos | wc -l                       -> 2442
    #   ls tests/logos/pass/{wql_*,deem_*}.logos | wc -l          ->  191
    # partition closes: 2442 = 191 + 2251. The two are the ADMIT half of the
    # per-path dropck record — bc_dropck_field_same_path_rewrite_admit (the
    # SAME path must not merge) and bc_dropck_field_whole_reowns_admit (a
    # whole-value write must CLEAR the per-path record). The round's other two
    # fixtures are FAIL fixtures and this population is the PASS corpus, so they
    # move nothing here; the two imported ports it closed moved admit -> fail
    # under tests/imported/ and are outside this population entirely. `glob` and
    # the DOOR counts unmoved (36 = 10 + 26): no `wql_*`/`deem_*` match, no
    # container family, no `direct` output form.
    #
    # 2026-08-28 (earlier, ltundecl_wide): +1 → 2440 = 191 + 2249.
    # RE-DERIVED BY DIRECT FILE LISTING, not by adding 1 to the previous pin:
    #   ls tests/logos/pass/*.logos | wc -l                       -> 2440
    #   ls tests/logos/pass/{wql_*,deem_*}.logos | wc -l          ->  191
    # partition closes: 2440 = 191 + 2249. The one is
    # bc_ltscope_impl_legal_shapes — the fourteen signatures a lifetime NAME's
    # scope must keep admitting once that scope is "the fn's own <'a> plus the
    # enclosing impl's, 'static and '_". The narrow scope refused SIXTY-FIVE
    # legal programs on the borrow-check corpus, so this half is the whole
    # reason the round is fundable. The round's other four fixtures are FAIL
    # fixtures and this population is the PASS corpus, so they move nothing
    # here. `glob` and the DOOR counts unmoved (36 = 10 + 26): no `wql_*`/
    # `deem_*` match, no container family, no `direct` output form.
    # 2026-08-28 (the AddrOf capture-mutability read + the TupleIndex capture
    # path), +5/0/+5. RE-DERIVED BY DIRECT FILE LISTING, not by adding 5 to the
    # previous pin:
    #   ls tests/logos/pass/*.logos | wc -l                       -> 2448
    #   ls tests/logos/pass/{wql_*,deem_*}.logos | wc -l          ->  191
    # partition closes: 2448 = 191 + 2257. The five are
    # bc_capaddrmut_{call_and_use,two_sequential,fnmut_bound,shared_not_marked}
    # and bc_captuple_elem_disjoint. Three of them assert a VALUE, not a
    # diagnostic, because the defect was a silent wrong answer (x=1 -> x=2,
    # x=1 -> x=3, y=0 -> y=5 on a control binary with the arm reverted). The
    # round's other two fixtures are FAIL fixtures and this population is the
    # PASS corpus, so they move nothing here; the three imported programs that
    # left `admit` for `fail` are not in tests/logos/pass at all. `glob` and the
    # DOOR counts unmoved (36 = 10 + 26): no `wql_*`/`deem_*` match, no
    # container family, no `direct` output form.
    # 2026-08-29 (the AddrOfTemp `&mut self` receiver reservation), +1/0/+1.
    # RE-DERIVED BY DIRECT FILE LISTING, not by adding 1 to the previous pin:
    #   ls tests/logos/pass/*.logos | wc -l                       -> 2449
    #   ls tests/logos/pass/{wql_*,deem_*}.logos | wc -l          ->  191
    # partition closes: 2449 = 191 + 2258. The one is
    # bc_recv_addroftemp_resv_admit — nineteen legal receiver shapes, every one
    # PROVEN to reach the new deposit, and it asserts a VALUE (`exit: 0` gated
    # on nineteen inequalities) rather than a diagnostic, because the claim is
    # that all nineteen still compile AND still compute what they computed. The
    # round's other native fixture is a FAIL fixture and this population is the
    # PASS corpus, so it moves nothing here; the three imported programs that
    # left `admit` for `fail` are not in tests/logos/pass at all. `glob` and the
    # DOOR counts unmoved: no `wql_*`/`deem_*` match, no container family, no
    # `direct` output form.
    # 2026-08-29 (the METHOD-CALL RECEIVER partial-move check), +2/0/+2.
    # RE-DERIVED BY DIRECT FILE LISTING, not by adding 2 to the previous pin:
    #   ls tests/logos/pass/*.logos | wc -l                       -> 2451
    #   ls tests/logos/pass/{wql_*,deem_*}.logos | wc -l          ->  191
    # partition closes: 2451 = 191 + 2260. The two are
    # bc_recvpartial_disjoint_admit (five legal shapes that keep a field of a
    # partially-moved root reachable — disjoint field method call, disjoint
    # field read, a nested disjoint parent, another variable, the call BEFORE
    # the move) and bc_recvpartial_reinit_admit (three where the receiver IS the
    # whole root and nothing is missing from it — re-initialisation, a Copy
    # field that was never a move, a whole move into a fresh binding). Both
    # assert a VALUE (`exit: 0` gated on inequalities), not a diagnostic: the
    # claim is that all eight still compile AND still compute what they
    # computed. The round's other two native fixtures are FAIL fixtures and this
    # population is the PASS corpus, so they move nothing here; the two imported
    # programs that left `admit` for `fail` are not in tests/logos/pass at all.
    # `glob` and the DOOR counts unmoved: no `wql_*`/`deem_*` match, no
    # container family, no `direct` output form.
    # 2026-08-29 (the COMPOUND-ASSIGN writability call), +1/0/+1.
    # RE-DERIVED BY DIRECT FILE LISTING, not by adding 1 to the previous pin:
    #   ls tests/logos/pass/*.logos | wc -l                       -> 2452
    #   ls tests/logos/pass/{wql_*,deem_*}.logos | wc -l          ->  191
    # partition closes: 2452 = 191 + 2261. The one is
    # bc_opeq_place_writable_ok — twelve legal `place op= e` shapes, each PAIRED
    # with a one-token twin that the new call REFUSES, which is how reach is
    # proved for a landed rule that has no fire log. It asserts a VALUE
    # (`exit: 0` gated on twelve inequalities), not a diagnostic: the claim is
    # that all twelve still compile AND still compute what they computed. The
    # round's other two native fixtures are FAIL fixtures and this population is
    # the PASS corpus, so they move nothing here; the two imported programs that
    # left `admit` for `fail` are not in tests/logos/pass at all. `glob` and the
    # DOOR counts unmoved: no `wql_*`/`deem_*` match, no container family, no
    # `direct` output form.
    # 2026-08-29 (the CALL-HOP DEPOSIT, `callidxcallonly`), +3/0/+3.
    # RE-DERIVED BY DIRECT FILE LISTING, not by adding 3 to the previous pin:
    #   ls tests/logos/pass/*.logos | wc -l                       -> 2455
    #   ls tests/logos/pass/{wql_*,deem_*}.logos | wc -l          ->  191
    # partition closes: 2455 = 191 + 2264. The three are
    # bc_field_deref_mut_borrow (`&mut b.f` on a `Box<S>` — the field auto-deref
    # in a mutable-use position), bc_match_deref_mut_refmut_arm (a `ref mut` arm
    # under `match *x`) and bc_call_hop_disjoint_ok (the COST side: a write
    # through the field auto-deref, a shared field borrow across a disjoint
    # read, two disjoint index borrows, a shared user-`Deref` borrow across a
    # disjoint field read). All three assert a VALUE (`exit: 0` gated on the
    # computed sum), not a diagnostic. The round's other three native fixtures
    # are FAIL fixtures — each the ONE-TOKEN TWIN of a pass fixture above, which
    # is how reach is proved for a landed rule with no fire log — and this
    # population is the PASS corpus, so they move nothing here; the twelve
    # imported programs that left `admit` for `fail` are not in tests/logos/pass
    # at all. `glob` and the DOOR counts unmoved: no `wql_*`/`deem_*` match, no
    # container family, no `direct` output form.
    # 2026-08-29 (the CLOSURE ARGUMENT TIE, `capprovnocap`), +2/0/+2.
    # RE-DERIVED BY DIRECT FILE LISTING, not by adding 2 to the previous pin:
    #   ls tests/logos/pass/*.logos | wc -l                       -> 2457
    #   ls tests/logos/pass/{wql_*,deem_*}.logos | wc -l          ->  191
    # partition closes: 2457 = 191 + 2266. The two that joined the PASS corpus
    # are bc_h4e_closure_arg_tie_param (the ADMIT half of the pair — one token
    # from fail/bc_h4e_closure_arg_tie_dangle: the argument is the param `p`,
    # not `&l`, which is how reach is proved for a landed rule with no fire
    # log) and bc_h4e_closure_arg_tie_legal_shapes (the COST side: a result
    # used while its referent is alive, a closure returning a SCALAR, and a
    # generic `Fn(&i64)->R` instantiated at a non-reference R). Both assert a
    # VALUE (`exit: 0` gated on the computed sum), not a diagnostic. The
    # round's other native fixture is a FAIL fixture and this population is the
    # PASS corpus, so it moves nothing here; the six imported programs that
    # left `admit` for `fail` are not in tests/logos/pass at all. `glob` and
    # the DOOR counts unmoved: no `wql_*`/`deem_*` match, no container family,
    # no `direct` output form.
    # 2026-08-29 (F-1, THE CLOSURE PARAMETER AS A BORROW SOURCE), +3/0/+3.
    # RE-DERIVED BY DIRECT FILE LISTING, not by adding 3 to the previous pin:
    #   ls tests/logos/pass/*.logos | wc -l                       -> 2460
    #   ls tests/logos/pass/{wql_*,deem_*}.logos | wc -l          ->  191
    # partition closes: 2460 = 191 + 2269. The three that joined the PASS
    # corpus are bc_f1_closure_param_escape_twin (the ADMIT half of the pair —
    # one token from fail/bc_f1_closure_param_escape: the holder is declared
    # INSIDE the body, which is how reach is proved for a landed rule with no
    # fire log), bc_f1_closure_param_shadow_legal (the ABUSE direction of the
    # narrowing — a body `let` SHADOWING the parameter, a legal program the
    # ceiling probe refused) and bc_f1_closure_param_legal_shapes (the COST
    # side: six shapes that all reach the arm with a parameter in hand). All
    # three assert a VALUE (`exit: 0` gated on the computed sum), not a
    # diagnostic. The round's other two native fixtures are FAIL fixtures —
    # each the one-token twin of a pass fixture above — and this population is
    # the PASS corpus, so they move nothing here; the three imported programs
    # that left `admit` for `fail` are not in tests/logos/pass at all. `glob`
    # and the DOOR counts unmoved: no `wql_*`/`deem_*` match, no container
    # family, no `direct` output form.
    # 2026-08-29 (tmcbdyn, AN ERASED PAYLOAD HIDES A BORROW), +3/0/+3.
    # RE-DERIVED BY DIRECT FILE LISTING, not by adding 3 to the previous pin:
    #   ls tests/logos/pass/*.logos | wc -l                       -> 2463
    #   ls tests/logos/pass/{wql_*,deem_*}.logos | wc -l          ->  191
    # partition closes: 2463 = 191 + 2272. The three that joined the PASS
    # corpus are the ADMIT halves of this round's three one-token pairs, one
    # per closed root: bc_tmcb_erased_closure_param_twin (bck.C — the closure
    # borrows the PARAMETER, not the local), bc_tmcb_erased_method_outlives_twin
    # (lifereg.N1 — the receiver's brace moves BELOW the use of the box) and
    # bc_tmcb_erased_call_param_twin (lifereg.L5 — the erased Holder borrows the
    # parameter). All three assert a VALUE (`exit: 0` gated on the computed
    # difference), not a diagnostic, so each proves its site is REACHED and
    # still admits. The round's other three native fixtures are FAIL fixtures —
    # each the one-token twin of a pass fixture above — and this population is
    # the PASS corpus, so they move nothing here; the three imported programs
    # that left `admit` for `fail` are not in tests/logos/pass at all. `glob`
    # and the DOOR counts unmoved: no `wql_*`/`deem_*` match, no container
    # family, no `direct` output form.
    # 2026-08-29 (THREE ARMS: the ROOT BITS, the FIELD-PLACE RECEIVER, the
    # `TupleIndex` ARM), +3/0/+3.
    # RE-DERIVED BY DIRECT FILE LISTING, not by adding 3 to the previous pin:
    #   ls tests/logos/pass/*.logos | wc -l                       -> 2466
    #   ls tests/logos/pass/{wql_*,deem_*}.logos | wc -l          ->  191
    # partition closes: 2466 = 191 + 2275. The three that joined the PASS
    # corpus are the ADMIT halves of this round's three one-token pairs, one
    # per arm: bc_fldrootbits_sibling_loan_field_move_twin (the loan names the
    # DISJOINT field `a.j`, not the whole root, across the same move of `a.i`),
    # bc_recvfieldpath_sibling_field_call_twin (the `&mut self` call is on the
    # sibling field `t.w`, not on the borrowed `t.v`) and
    # bc_tupidxmove_sibling_element_twin (the second move is `x.1`, not `x.0`).
    # All three assert a VALUE (`exit: 0` gated on a computed sum), not a
    # diagnostic, so each proves its site is REACHED and still admits. The
    # round's other three native fixtures are FAIL fixtures — each the
    # one-token twin of a pass fixture above — and this population is the PASS
    # corpus, so they move nothing here; the three imported programs that left
    # `admit` for `fail` are not in tests/logos/pass at all. `glob` and the DOOR
    # counts unmoved: no `wql_*`/`deem_*` match, no container family, no
    # `direct` output form.
    # ⚠ RE-DERIVED 2026-08-30 (`slicearr`): +4 / +0 / +4, BY DIRECT LISTING and
    # not by adding 4 to the previous pin:
    #   ls tests/logos/pass/*.logos | wc -l                       -> 2480
    #   ls tests/logos/pass/{wql_*,deem_*}.logos | wc -l          ->  191
    # partition closes: 2480 = 191 + 2289. The four that joined the PASS corpus:
    # bc_slicearr_elem_ref_twin and bc_slicearr_suffix_prefix_twin are the ADMIT
    # halves of this round's two one-token pairs (a `ref` element binding moves
    # nothing; index 0 and index N-sc+j are DISJOINT sub-places of one array),
    # and bc_slicearr_owned_destructure_legal / bc_slicearr_ref_slice_scrutinee_
    # legal are a pass/pass pair with no fail partner — each is the legal program
    # that condemns one of the two spellings this round DECLINED (`slicetype`
    # refuses the first, `slicewhole` the second). The round's two other native
    # fixtures are FAIL fixtures and this population is the PASS corpus, so they
    # move nothing here; the two imported programs that left `admit` for `fail`
    # are not in tests/logos/pass at all. `glob` and the DOOR counts unmoved: no
    # `wql_*`/`deem_*` match, no container family, no `direct` output form.
    # ⚠ RE-DERIVED at the 2026-08-31l round (the meet's guard reached cost 0 in
    # every population this harness owns and the engine STILL did not land —
    # PROBES.md 2026-08-31l): +1 / +0 / +1. ONE new PASS fixture,
    # bc_ltmintgen_two_minted_regions_one_typaram, a COST SENSOR: a legal
    # program every engine arm refuses, found by writing the LEGAL TWIN of a
    # fail fixture whose refusal reason had moved. It matches neither `wql_*`
    # nor `deem_*`, so the delta lands wholly in `nonglob`. BY DIRECT LISTING:
    #   ls tests/logos/pass/*.logos                      -> 2499
    #   ls tests/logos/pass/{wql_*,deem_*}.logos         ->  191
    #   the same listing minus the glob half             -> 2308
    # 2499 = 191 + 2308. The ledger is UNTOUCHED at 297 and no fail fixture
    # moved, so nothing else in this gate changes.
    # ⚠ RE-DERIVED at the 2026-08-31m round (THE REGION SLOT — `&'a [T]` /
    # `&'a str` / `&'a dyn` / `&'a Dst` record their region; PROBES.md
    # 2026-08-31m): +1 / +0 / +1. ONE new PASS fixture,
    # bc_ltregslot_dyn_field_coerced_arg, again a COST SENSOR found by the same
    # instrument — the LEGAL TWIN of the one `.expected` loss `ltregslot`
    # produces (regions-trait-variance). It matches neither `wql_*` nor
    # `deem_*`, so the delta lands wholly in `nonglob`. BY DIRECT LISTING:
    #   ls tests/logos/pass/*.logos                      -> 2500
    #   ls tests/logos/pass/{wql_*,deem_*}.logos         ->  191
    #   the same listing minus the glob half             -> 2309
    # 2500 = 191 + 2309. The ledger is UNTOUCHED at 297 and no fail fixture
    # moved, so nothing else in this gate changes.
    # ⚠ RE-DERIVED at the 2026-08-31n round (THE ELISION ENGINE AND THE REGION
    # SLOT LANDED, ledger 297 -> 276; PROBES.md 2026-08-31n): +3 / +0 / +3.
    # FOUR new PASS fixtures and ONE that LEFT the pass corpus:
    #   + bc_ltcallmeet_expected_loss_twins      the twelve legal twins of this
    #                                            round's `.expected` losses
    #   + bc_ltcallmeet_callee_binder_meet       the meet at the call, admit half
    #   + bc_ltmintiv_minted_regions_unify_at_a_call
    #   + bc_ltimplhdr_self_lifetime_args_from_the_header
    #   - bc_ltunmentbind_renamed_binder_hole    MOVED to tests/logos/fail/ as
    #                                            its own header demanded once the
    #                                            substitution engine landed
    # The 21 closed ledger rows are IMPORTED fixtures moving admit -> fail and
    # touch no count here; bc_ltcallmeet_invariant_binder_no_meet is a FAIL
    # fixture. None of the four matches `wql_*` or `deem_*`, so the whole delta
    # lands in `nonglob`. BY DIRECT LISTING:
    #   ls tests/logos/pass/*.logos                      -> 2504
    #   ls tests/logos/pass/{wql_*,deem_*}.logos         ->  191
    #   the same listing minus the glob half             -> 2313
    # 2504 = 191 + 2313. The fifth new pass fixture is
    # variance_arg_mut_inv_free_binder_admit — the ADMIT half of
    # tests/logos/fail/variance_arg_mut_inv, which L1 (not the `-L bc -L fail`
    # oracle, which does not select it) caught as an OVER-REFUSAL: its region at
    # the invariant position is the CALLEE's own binder and Rust instantiates it.
    # ⚠ RE-DERIVED 2026-08-31p (`capmovewalk` + the by-value param gate, two
    # mechanisms landed one at a time): +4 / +0 / +4. FOUR new PASS fixtures,
    # none matching `wql_*` or `deem_*`, so the whole delta lands in `nonglob`:
    #   + bc_mbparamval_mut_param_admit           the one-token twin of
    #                                             fail/bc_mbparamval_byvalue_param_fail
    #   + bc_mbparamval_legal_shapes              the six hand-written abuse
    #                                             shapes of the by-value gate
    #   + bc_capmovewalk_nonmove_body_admit       the one-token twin of
    #                                             fail/bc_capmovewalk_move_body_ret_capture_fail
    #   + bc_capmovewalk_move_body_legal_shapes   the two legal move-body shapes
    # The 9 closed ledger rows are IMPORTED fixtures moving admit -> fail and
    # touch no count here; the two new native FAIL fixtures are outside this
    # gate's population, which sweeps the pass corpus only. BY DIRECT LISTING:
    #   ls tests/logos/pass/*.logos                      -> 2508
    #   ls tests/logos/pass/{wql_*,deem_*}.logos         ->  191
    #   the same listing minus the glob half             -> 2317
    # 2508 = 191 + 2317. DOOR counts unmoved: none of the four declares a
    # container family — they are `i64` / struct / closure borrow-check shapes.
    # ⚠ RE-DERIVED AGAIN in the same round, at the M4 stage (the reborrow
    # exemption re-keyed on the DEREFERENCED reference, then the by-value gate
    # at the field / AddrOfTemp sites): +1 / +0 / +1. ONE new PASS fixture,
    # outside the `wql_*` / `deem_*` glob:
    #   + bc_thrumutref_legal_shapes   the three legal reborrow-through-a-field
    #                                  shapes the exemption must keep admitting
    # Its refuse half (bc_thrumutref_borrow_of_the_field_fail) is a FAIL fixture
    # and the 3 further closed ledger rows are IMPORTED, so neither touches this
    # count. BY DIRECT LISTING:
    #   ls tests/logos/pass/*.logos                      -> 2509
    #   ls tests/logos/pass/{wql_*,deem_*}.logos         ->  191
    #   the same listing minus the glob half             -> 2318
    # 2509 = 191 + 2318. DOOR counts unmoved: no container family is declared.
    # ⚠ RE-DERIVED AGAIN 2026-08-31r (the `for`-iterable loan under a
    # statement-scoped synthetic holder, and one reader for the whole-value
    # moved fact): +2 / +0 / +2. TWO new PASS fixtures, neither matching the
    # `wql_*` / `deem_*` glob:
    #   + bc_foreachit_legal_shapes              the nine legal `for` shapes the
    #                                            new loan must keep admitting
    #   + bc_movedvalue_borrow_not_moved_admit   the admit twin of the
    #                                            moved-value borrow pin
    # Their two refuse halves (bc_foreachit_mutate_while_iterating_fail,
    # bc_movedvalue_borrow_carries_move_line_fail) are FAIL fixtures and the 2
    # closed ledger rows are IMPORTED admit -> fail, so neither touches this
    # count. BY DIRECT LISTING:
    #   ls tests/logos/pass/*.logos                      -> 2511
    #   ls tests/logos/pass/{wql_*,deem_*}.logos         ->  191
    #   the same listing minus the glob half             -> 2320
    # 2511 = 191 + 2320. DOOR counts unmoved: no container family is declared;
    # they are Vec / array / struct borrow-check shapes.
    # 2026-08-31t: +1 native pass fixture, bc_capsharedloan_legal_shapes — the
    # seven legal whole-value-shared-capture shapes the new loan must keep
    # admitting. Its refuse half (bc_capsharedloan_assign_under_capture_fail) is
    # a FAIL fixture and the 4 closed ledger rows are IMPORTED admit -> fail, so
    # neither touches this count. BY DIRECT LISTING:
    #   ls tests/logos/pass/*.logos                      -> 2512
    #   ls tests/logos/pass/{wql_*,deem_*}.logos         ->  191
    #   the same listing minus the glob half             -> 2321
    # 2026-08-31u: +2 native pass fixtures, bc_recvnestshared_legal_shapes (ten
    # legal `&self`-receiver-in-argument shapes) and bc_e716argtemp_legal_shapes
    # (seven legal borrows of a temporary's field). Their three refuse halves are
    # FAIL fixtures and the 2 closed ledger rows are IMPORTED admit -> fail, so
    # neither touches this count. BY DIRECT LISTING:
    #   ls tests/logos/pass/*.logos                      -> 2514
    #   ls tests/logos/pass/{wql_*,deem_*}.logos         ->  191
    #   the same listing minus the glob half             -> 2323
    # 2026-09-04y: +2 native pass fixtures, bc_sigselfsub_param_self_match_pass
    # and bc_sigselfsub_param_self_byvalue_pass — the two legal twins, one token
    # apart, of the two new `bc_sigselfsub_*_fail` fixtures. Both refuse halves
    # are FAIL fixtures and the mechanism closes NO ledger row, so only the two
    # pass halves move this census. BY DIRECT LISTING:
    #   ls tests/logos/pass/*.logos                      -> 2517
    #   ls tests/logos/pass/{wql_*,deem_*}.logos         ->  191
    #   the same listing minus the glob half             -> 2326
    # 2026-09-06a: +4 native pass fixtures, bc_sigdefuniq_default_rightparam_pass,
    # bc_sigdefuniq_default_selfparam_pass, bc_sigdefuniq_default_notprovided_pass
    # and bc_sigdefuniq_overload_sametypearity_pass — the legal twins of the
    # three new `bc_sigdefuniq_*_fail` fixtures, plus the default-registration
    # path the new arm sits on and the legal same-arity trait overload that
    # condemned the two wider arms. The three refuse halves are FAIL fixtures and
    # the mechanism closes NO ledger row. BY DIRECT LISTING:
    #   ls tests/logos/pass/*.logos                      -> 2521
    #   ls tests/logos/pass/{wql_*,deem_*}.logos         ->  191
    #   the same listing minus the glob half             -> 2330
    # 2026-09-02: +1 native pass fixture, bc_ltbndenv_legal_shapes (eight legal
    # caller-env shapes the new `T: 'a` caller-obligation arm must keep
    # admitting: (1)/(2) the rename and the transitive chain that condemn the
    # strict name-compare, and (4)-(7) the IMPLIED bound from a `&'a T`
    # parameter, which the probe this arm grew from refused — four legal
    # programs no population in the harness contains). Its two refuse halves
    # are FAIL fixtures and the six closed ledger rows are IMPORTED admit ->
    # fail, so neither touches this count.
    # 2026-09-01g: +1 native pass fixture, bc_tpmixbnd_mixed_bound_list_pass
    # — the four spellings of a MIXED type-parameter bound list (`T: Tr + 'a`
    # trait-first, lifetime-first, the `where` form, and one beside a plain
    # sibling), which `type_param` had no alternative for and which were a
    # SYNTAX ERROR before the grammar carried a mixed bound-list element. Its
    # refuse twin, bc_tpmixbnd_mixed_bound_undeclared_fail, is a FAIL fixture
    # and does not touch this count. NAMED, and the half it joined: the
    # nonglob half, since `bc_*` matches neither `wql_*` nor `deem_*`.
    # BY DIRECT LISTING:
    #   ls tests/logos/pass/*.logos                      -> 2525
    #   ls tests/logos/pass/{wql_*,deem_*}.logos         ->  191
    #   the same listing minus the glob half             -> 2334
    # 2026-09-01i (`enum_is_move` asks under the mono-composed instance name),
    # +1/0/+1. RE-DERIVED BY DIRECT FILE LISTING, not by adding 1:
    #   ls tests/logos/pass/*.logos                      -> 2526
    #   ls tests/logos/pass/{wql_*,deem_*}.logos         ->  191
    #   the same listing minus the glob half             -> 2335
    # 2026-09-01k (`declare_pat_bindings` declares RefBind/RefPat), +2/0/+2.
    # RE-DERIVED BY DIRECT FILE LISTING, not by adding 2:
    #   ls tests/logos/pass/*.logos                      -> 2528
    #   ls tests/logos/pass/{wql_*,deem_*}.logos         ->  191
    #   the same listing minus the glob half             -> 2337
    # partition closes: 2528 = 191 + 2337. THE TWO, NAMED, and the half each
    # joined (the NONGLOB half — `bc_*` matches neither `wql_*` nor `deem_*`):
    #   bc_patdeclrefbmut_sequenced_reborrows_pass — the ACCEPT twin of
    #     bc_patdeclrefbmut_two_live_mut_reborrows_fail, differing only in
    #     where `*a = 5i64;` sits, which is the whole NLL question.
    #   bc_patdeclrefbmut_legal_shapes_pass — ten legal shapes over `ref`/
    #     `ref mut` bindings, pinning pat_keys::IS_MUT (the closure reborrow,
    #     which a default-false `is_mut_binding` refuses) and BIND_SLOT (the
    #     shadowed outer name, which must stay a different variable).
    # The round's other TWO native fixtures are FAIL fixtures and this
    # population is the PASS corpus, so they move nothing here; the one
    # imported program that left `admit` for `fail` is not in tests/logos/pass
    # at all. `glob` and the DOOR counts unmoved: no `wql_*`/`deem_*` match, no
    # container family, no `direct` output form.
    # ── the previous round's derivation, kept for the trail ──
    # partition closes: 2526 = 191 + 2335. The one is
    # bc_enumdefkey_legal_shapes — ten legal shapes over generic enums, two of
    # them ONE TOKEN from a fail fixture beside it (`G<i64>` vs `G<String>`,
    # `Option<i64>` vs `Option<String>`), asserting a VALUE (`exit: 0` gated on
    # ten inequalities) rather than a diagnostic: the claim is that widening a
    # payload walk that had never run does not start refusing the Copy half.
    # NAMED, and the half it joined: the nonglob half, since `bc_*` matches
    # neither `wql_*` nor `deem_*`. The round's other two native fixtures are
    # FAIL fixtures and this population is the PASS corpus, so they move nothing
    # here; the one imported program that left `admit` for `fail` is not in
    # tests/logos/pass at all. `glob` and the DOOR counts unmoved: no
    # `wql_*`/`deem_*` match, no container family, no `direct` output form.
    # 2026-09-01n: +1 / +0 / +1 — pass/bc_mcallvar_legal_twins, the legal twins
    # of the eight ledger rows the method-argument variance site closed. The
    # round's other native fixture is a FAIL fixture; the eight imported programs
    # moved admit -> fail are not in tests/logos/pass at all.
    # 2026-09-02p: +1 / +0 / +1 — pass/bc_stfacts_legal_twins, the legal twins
    # of the five ledger rows the param-region facts closed. The round's other
    # native fixture is a FAIL fixture; the five imported programs moved
    # admit -> fail are not in tests/logos/pass at all.
    # 2026-09-02r: +11 / +0 / +11 — the eleven pass/bc_dcl* legal twins of the
    # declaration-site lifetime rules (one pair per mechanism, one token apart).
    # The round's other eleven native fixtures are FAIL fixtures; the thirteen
    # imported programs moved admit -> fail are not in tests/logos/pass at all.
    # BY DIRECT LISTING: ls tests/logos/pass/*.logos | wc -l -> 2541; the glob
    # listing -> 191; 2541 = 191 + 2350.
    # 2026-09-02s: +7 / +0 / +7 — the seven pass/bc_st* legal twins of the
    # 'static-slot landing (one pair per fact, one token apart). The seven
    # fail twins and the eighteen imported programs moved admit -> fail are not
    # in tests/logos/pass. BY DIRECT LISTING: ls tests/logos/pass/*.logos | wc -l
    # -> 2548; the glob listing -> 191; 2548 = 191 + 2357.
    # 2026-09-02u: +12 / +0 / +12 — the twelve pass/bc_drop{wf,call}_* legal twins
    # of the Drop block (E0120/E0366/E0367/E0277/E0040; one pair per predicate,
    # one token apart, plus the three legal shapes the priced arms refused).
    # The ten fail twins and the ten imported programs moved admit -> fail are
    # not in tests/logos/pass. BY DIRECT LISTING: ls tests/logos/pass/*.logos |
    # wc -l -> 2560; the glob listing -> 191; 2560 = 191 + 2369.
    # 2026-09-02w: +10 / +0 / +10 — the ten pass/bc_slit{fat,gen,mint}_* legal
    # twins of the struct-literal block (a bare literal's lifetime args read off
    # its values for str / slice / dyn / DST fields and in the generic path; the
    # elided region of a fat-pointer parameter minted). BY DIRECT LISTING:
    # ls tests/logos/pass/*.logos | wc -l -> 2570; the glob listing -> 191.
    # 2026-09-02y: +18 / +0 / +18 — the eighteen pass/bc_objlt_* legal twins of
    # the object-lifetime-bound block (Src: 'r at the unsize coercion; the deferred
    # generic call typed with the caller's parameter). BY DIRECT LISTING:
    # ls tests/logos/pass/*.logos | wc -l -> 2588; the glob listing -> 191.
    # 2026-09-03a: +15 / +0 / +15 — the fifteen pass/bc_wf_* legal twins of the
    # WF-of-a-written-type block (impl-scope bounds, the impl header's and a
    # parameter mention's RFC 2093 implied bounds, enum payload, method turbofish).
    # BY DIRECT LISTING: ls tests/logos/pass/*.logos | wc -l -> 2603; glob -> 191.
    # 2026-09-02c: 2603 -> 2608. Five pass twins of the §B6-store block
    # (bc_b6ptr_reborrow_root_outlives, bc_b6ptr_param_static,
    # bc_b6ptr_param_holder_field, bc_b6clo_param_pointee_copy,
    # bc_b6clo_store_outer_local), all five non-glob.
    # 2026-09-06a: 2608 -> 2614. Six pass twins of the rigid-minted-region block
    # (bc_ltrigid_two_anon_push_named, _local_rebind_value_named,
    # _local_both_rebound_named, _local_vs_local_swap,
    # _local_reassigned_from_second, _callee_binder_from_local), all six
    # non-glob. 2026-09-02land: 2614 -> 2617. Three pass twins of the
    # pattern-site E0507 block (bc_patref_amp_binds_nothing,
    # bc_patref_variant_binds_by_ref, bc_destrpat_owned_binds_by_value), all
    # three non-glob.
    # 2026-09-03b: 2617 -> 2620. Three pass twins of the closure-parameter mint
    # (bc_closmint_two_params_tied, bc_closmint_struct_arg_tied,
    # bc_closmint_ret_from_param), all three non-glob.
    # 2026-09-03e: 2620 -> 2625. Five pass twins of the temporary-place block
    # (bc_tmpfresh_ret_self, bc_tmpfresh_let_cast, bc_tmpassign_never_used,
    # bc_tmpassign_promoted, bc_tmpassign_mut_local), all five non-glob.
    # 2026-09-03g: 2625 -> 2628. Three pass twins of the lifetime-binder
    # receiver-tie (bc_selflt_binder_shared_shared_admit,
    # bc_selflt_generic_dead_first_admit, bc_selflt_binder_other_param_admit),
    # all three non-glob.
    # 2026-09-03i: 2628 -> 2632. Four pass twins of the receiver-route
    # binding-mut question (bc_drfmut_deref_mut_local_admit,
    # bc_drfmut_deref_thru_mut_ref_admit, bc_drfmut_destructure_pat_mut_admit,
    # bc_drfmut_nested_pat_mut_admit), all four non-glob.
    # 2026-09-03k: 2632 -> 2634. Two pass twins of the receiver-CHAIN move
    # predicate (bc_mvchain_nested_field_owned_admit,
    # bc_mvchain_box_field_owned_admit), both non-glob.
    # 2026-09-03n: 2634 -> 2637. Three pass twins of the call-result loan
    # (bc_argretlet_call_arg_dead_admit, bc_argretlet_mask_scratch_admit,
    # bc_argretlet_recv_borrowing_self_admit), all three non-glob.
    # 2026-09-03p: 2637 -> 2639. Two pass twins of the `move`-capture deposit
    # (bc_capmovemut_sharedref_capture_admit — `&T` IS Copy, the token the key
    # turns on; bc_capmovemut_nested_closures_admit — nested `move` closures
    # over one `&mut`, the `!in_closure_body_` condition), both non-glob.
    # BY DIRECT LISTING: ls tests/logos/pass/*.logos | wc -l -> 2639.
    # 2026-09-04d: 2642 -> 2645. Three pass fixtures of the method-call RECEIVER
    # check (bc_recv_self_lt_invariant_ok — the `'a` off the outer `&mut`;
    # bc_recv_trait_bound_lt_ok — the caller writes `&'r T`, upstream E0621's own
    # suggestion; bc_recv_self_lt_generic_admit — the pinned exemption that the
    # comparand is the INSTANTIATED slot), all three non-glob.
    # BY DIRECT LISTING: ls tests/logos/pass/*.logos | wc -l -> 2645.
    # 2026-09-04f: 2645 -> 2647. Two pass twins of the closure-signature-from-
    # the-bound rule (bc_closbnd_ret_own_region_ok — the bound WRITES `'a` at
    # parameter 0 and at the return and the closure returns THAT parameter;
    # bc_closbnd_elided_param_ok — the elided half, `Fn(&T) -> &T`, the simplest
    # legal closure under such a bound in the language and the shape 09-04e's
    # cost-0 survivor refused), both non-glob.
    # BY DIRECT LISTING: ls tests/logos/pass/*.logos | wc -l -> 2647.
    # 2026-09-09: 2647 -> 2650. Three pass twins of the struct-literal region
    # mint (bc_letann_lt_static_ok — the `let Foo<'static>` annotation fed a
    # module STATIC; bc_structlit_field_lt_static_ok — the same at a struct-
    # literal FIELD; bc_letelide_lt_carry_ok — an ELIDED `let` annotation whose
    # initializer already carries the parameter's own region), all three
    # non-glob, each one token from its fail twin.
    # BY DIRECT LISTING: ls tests/logos/pass/*.logos | wc -l -> 2650.
    # 2026-09-04land: 2650 -> 2667. SEVENTEEN pass fixtures pinning three RUNTIME
    # soundness holes, all seventeen non-glob and every one of them asserting a
    # RUN — exit code plus stdout, and where the real assertion is a destructor
    # COUNT the count is what stdout carries (an rc oracle cannot tell one drop
    # from two, nor from none):
    #   destruct_field_{wild,ref}_through_deref, destruct_field_rest_{owned,
    #   through_deref}, destruct_field_nested_{owned,ref},
    #   destruct_field_ref_binds_a_reference  — the binding mode of a
    #     destructuring `let` (7; the last one's twin is the FAIL fixture
    #     destruct_field_ref_is_not_a_value)
    #   deref_call_boxed_closure{,_direct}, deref_call_ref_closure{,_direct},
    #   deref_call_ref_closure_param — `(*b)()` over a closure place, BOTH
    #     directions of the discriminator (5)
    #   drop_ident_{inherent_is_not_a_destructor,trait_is_a_destructor,
    #   other_trait_is_not_a_destructor,other_trait_named_drop,
    #   generic_impl_runs_once} — the destructor's IDENTITY (5)
    # BY DIRECT LISTING: ls tests/logos/pass/*.logos | wc -l -> 2667.
    # 2026-09-04drop: 2667 -> 2677. TEN pass fixtures pinning the destructor
    # identity defect's remaining two doors, all ten non-glob and every one of
    # them a RUN whose stdout carries the destructor COUNT:
    #   bc_dropident_{inherent_first,impl_first,inherent_only} — the plain
    #     `<T>__drop` name in both declaration orders and with the `impl Drop`
    #     removed (3; impl_first was REFUSED outright before this round)
    #   bc_dropident_{nested_field,array_three,move_once,consumed_by_callee,
    #   heap_free,two_structs} — the same type reached through drop GLUE:
    #     a field, three array elements, a move, a call boundary, a malloc'd
    #     block, and two types at once (6)
    #   bc_dropident_generic_struct — mono's pin-by-method-name (1)
    # BY DIRECT LISTING: ls tests/logos/pass/*.logos | wc -l -> 2677.
    # 2026-09-05a: 2677 -> 2689. Twelve pass fixtures of the soundness-queue
    # round (bc_recvaot_* / bc_patmut_*), all non-glob.
    # 2026-09-06b: 2689 -> 2709. Twenty pass fixtures of the soundness-queue
    # round (bc_patmut_*), all non-glob (`ls tests/logos/pass/*.logos | wc -l`).
    # 2026-09-06d: 2709 -> 2769. Sixty pass fixtures of the soundness-queue
    # round (bc_patown_*), all non-glob (`ls tests/logos/pass/*.logos | wc -l`).
    # 2026-09-08a: 2769 -> 2804. Thirty-five pass fixtures of the soundness-queue
    # round (bc_mutplace_*), all non-glob (`ls tests/logos/pass/*.logos | wc -l`).
    # 2026-09-09b: 2804 -> 2855. Fifty-one pass fixtures of the soundness-queue
    # landing (bc_bindmut_*), all non-glob.
    # 2026-09-09d: 2855 -> 2867. Twelve pass fixtures of the soundness-queue
    # landing that gave the `@`-binding a case at every binder walker, all
    # non-glob (`ls tests/logos/pass/*.logos | wc -l`): at_bind_struct_field,
    # at_bind_struct_field_variant_sub, at_bind_struct_field_drop_count,
    # at_bind_tuple_elem, at_bind_tuple_typed_field, at_bind_or_in_tuple_elem,
    # at_bind_slice_elem, at_bind_let_irrefutable, at_bind_let_struct_sub,
    # at_bind_let_drop_count, pat_bind_enum_tuple_elem,
    # pat_bind_enum_tuple_elem_nested_match.
    # 2026-09-05b: 2867 -> 2870. Three pass fixtures of the soundness-queue
    # landing that collapsed the scrutinee's reference chain at every pattern
    # door, all non-glob (`ls tests/logos/pass/*.logos | wc -l`):
    # patpeel_tuple_two_ref_layers, patpeel_struct_two_ref_layers,
    # patpeel_slice_ref_array.
    # 2026-09-06: 2870 -> 2871. ONE pass fixture, non-glob
    # (`ls tests/logos/pass/*.logos | wc -l`): mlirgen_odr_drop_glue_field_ctl,
    # the renamed control half of soundness-queue row homonym_field_drop_glue_segv.
    # 2026-09-06f: 2871 -> 2880. NINE pass fixtures of the scalar-pattern-core
    # landing, all non-glob (`ls tests/logos/pass/patcore_*.logos | wc -l` -> 9):
    # patcore_range_under_ref, patcore_literal_under_ref,
    # patcore_refpat_scalar_under_ref, patcore_match_expr_literal_under_ref,
    # patcore_iflet_literal_under_ref, patcore_at_range_under_ref,
    # patcore_perarm_scalar_and_binder, patcore_refpat_scalar_depth2,
    # patcore_letelse_literal_under_ref. Their six fail twins are not in this
    # population (it is the `pass` corpus).
    # 2026-09-06h: 2880 -> 2881. ONE pass fixture, non-glob
    # (`ls tests/logos/pass/tupref_*.logos | wc -l` -> 1):
    # tupref_element_ref_mut_write, the pass half of soundness-queue row
    # refmut_binding_write_refused_outside_variant. Its one-token fail twin
    # (tupref_element_ref_mut_nomut) is not in this population (it is `pass`).
    # 2026-09-06i: 2881 -> 2891. TEN pass fixtures, all non-glob, of the
    # irrefutable-destructure drop-glue landing (soundness-queue row
    # destructure_param_move_elem_double_free, closed): nine drop-COUNT halves
    # (`ls tests/logos/pass/destructure_*_drop_*.logos | wc -l` -> 9)
    # destructure_fnparam_tuple_drop_once, destructure_fnparam_plain_drop_once,
    # destructure_fnparam_struct_drop_once, destructure_letstruct_drop_once,
    # destructure_closure_tuple_drop_once, destructure_let_tuplestruct_drop_once,
    # destructure_let_tuplestruct_rest_drop_both,
    # destructure_fnparam_tuple_wild_drop_both,
    # destructure_fnparam_tuple_move_out_drop_once — plus the closed row's own
    # program, destructure_param_move_elem_no_double_free, moved here from
    # tests/soundness/open/.
    # 2026-09-06j: 2891 -> 2894. THREE pass fixtures, all non-glob, of the
    # default-binding-mode landing that closed three soundness-queue rows
    # (match_tuple_copy_elem_no_default_mut_ref, struct_copy_field_no_default_mut_ref,
    # slice_copy_elem_no_default_mut_ref): dbm_tuple_default_ref_mut_writes,
    # dbm_struct_default_ref_mut_writes, dbm_slice_default_ref_mut_writes —
    # `ls tests/logos/pass/dbm_*_default_ref_mut_writes.logos | wc -l` -> 3.
    # Their one-token fail twins (dbm_*_default_shared_write_refused) are NOT in
    # this population (it is `pass`), and the three closed rows' own programs did
    # not move here — each pass half is a rewrite that asserts stdout, and the
    # queue programs were deleted with their rows.
    # 2026-09-06k: 2894 -> 2896. TWO pass fixtures, both non-glob, of the
    # block-expression scope-drop repair — blockexpr_scope_drop_valgrind and
    # blockexpr_scope_drop_format_ctl (the moved-out-buffer control). Each is
    # registered TWICE (the logos_02 corpus test and a logos_09 valgrind gate),
    # which is the +4 on REGISTRY-ALL against the +2 here.
    # `ls tests/logos/pass/blockexpr_scope_drop_*.logos | wc -l` -> 2.
    # 2026-09-06n: 2896 -> 2902. SIX pass fixtures, all non-glob, of the
    # place-assign drop_old repair — placedrop_tuple_elem_drop_old,
    # placedrop_index_elem_drop_old, placedrop_deref_field_drop_old and
    # placedrop_tuple_reinit_after_move. Their five one-token fail twins are NOT
    # in this population (it is `pass`), and the four closed rows' own programs
    # did not move here — each pass half is a rewrite that asserts stdout, and the
    # queue programs were deleted with their rows.
    # plus the fifth class member's pair, placedrop_box_deref_field_drop_old and
    # placedrop_rawptr_deref_field_no_drop_old (a raw-pointer root, which must NOT
    # drop — pass/pass because that abuse direction is a wrong drop, not a refusal).
    # `ls tests/logos/pass/placedrop_*.logos | wc -l` -> 6.
    # 2026-09-07s: 2908 -> 2911. THREE pass fixtures, all non-glob, landed with
    # the three soundness-queue rows this round closed — pass/
    # closure_fnonce_capture_drop_never_called,
    # closure_fnonce_narrow_capture_drop_never_called and
    # closure_void_body_param_epilogue. Their three fail halves are not in this
    # population (the `pass` half only), and the two programs that left
    # tests/soundness/open were never in it either.
    # 2026-09-07q: 2902 -> 2908. SIX pass fixtures, all non-glob, of the
    # default-binding-mode carry at the container doors — three door halves
    # (dbmcarry_tuple/struct/slice_nested_payload_drop_once), the closed tier-2
    # row's pass twin (dbmcarry_ergo_tuple_scalar_nomut), the closed tier-1 row's
    # own program (dbmcarry_nested_payload_drop_count_exit) and the `&mut`
    # over-refusal the same carry repairs (dbmcarry_mutref_tuple_nested_payload_write).
    # Their four one-token fail twins are NOT in this population (it is `pass`).
    # `ls tests/logos/pass/dbmcarry_*.logos | wc -l` -> 6.
    # 2026-09-07u: 2911 -> 2918. SEVEN pass fixtures, all non-glob, of the
    # reference-local place-write repair — refslot_index_write_local_refmut and
    # refslot_tuple_write_local_refmut (the two closed rows' programs, rewritten
    # to assert stdout), four more members of the same class with no row of their
    # own (variable index, nested array, reborrow-to-local, struct element field)
    # and refslot_shape_controls_ctl. Their two one-token fail twins are NOT in
    # this population (it is `pass`).
    # `ls tests/logos/pass/refslot_*.logos | wc -l` -> 7.
    # 2026-09-07v: 2918 -> 2923. FIVE pass fixtures, all non-glob, of the
    # parameter-position `mut`-modifier class — the closed row's own program
    # (patparammut_fn_tuple_param), the two unrowed fn-param struct siblings
    # (patparammut_fn_struct_param, patparammut_fn_struct_rename_param), the
    # unrowed closure sibling (patparammut_closure_tuple_param) and the
    # destructor-count member (patparammut_fn_tuple_movetype). Their five
    # one-token fail twins are NOT in this population (it is `pass`), and the
    # queue shelf is net zero (one program left, one arrived).
    # `ls tests/logos/pass/patparammut_*.logos | wc -l` -> 5.
    # 2026-09-09f: 2923 -> 2928. FIVE pass fixtures, all non-glob, the legal halves
    # of the Rust-2024 binding-modifier rule's `ref`/`ref mut` two thirds and of
    # the two `mut` LEAF holes the same site closes —
    # match_ergo_ref_variant_payload_deref_pass, match_ergo_ref_struct_shorthand_byvalue_pass,
    # match_ergo_mut_leaf_byvalue_pass, match_ergo_mut_array_elem_byvalue_pass and
    # match_ergo_ref_at_binding_byvalue_pass.
    # Their five one-token fail twins are NOT in this population (it is `pass`),
    # and the queue shelf is net zero (one program left, one arrived).
    # `ls tests/logos/pass/match_ergo_*.logos | wc -l` -> 5.
    # 2026-09-08: 2928 -> 2931. THREE pass fixtures, all non-glob, the legal
    # halves of the `bck.NEW-SUBSLICE` landing — bc_subslice_anon_rest_twin
    # (the anonymous rest binds nothing), bc_subslice_disjoint_elem_legal (a
    # rest covering index 2, a later move of index 0: the program that condemns
    # the whole-array spelling) and bc_subslice_ref_slice_scrutinee_legal (the
    # `&[P]` container the arm declines). Their fail twins are NOT in this
    # population (it is `pass`).
    # `ls tests/logos/pass/bc_subslice_*.logos | wc -l` -> 3.
    # 2026-09-08b: 2931 -> 2932. ONE pass fixture, non-glob:
    # bc_refparam_addrof_mut — the LEGAL half of the `bck.A-REFPARAM` landing
    # (`fn g(mut b: &mut i64) { h2(&mut b); }`, which compiles AND runs, the
    # write arriving through two levels of reference). Its one-token fail twin
    # bc_refparam_addrof_nonmut is NOT in this population (it is `pass`).
    # 2026-09-08c: 2932 -> 2934. TWO pass fixtures, both non-glob, the legal
    # halves of the coercion-consumes-its-source landing —
    # coerce_unsize_consumes_source_rc (the coercion result is read, not the
    # source) and dyn_upcast_consumes_source_box (a second Box is read, not the
    # upcast one). Their one-token fail twins are NOT in this population (it is
    # `pass`).
    # `ls tests/logos/pass/{coerce_unsize_consumes_source_rc,dyn_upcast_consumes_source_box}.logos | wc -l` -> 2.
    # 2026-09-08d (fat-representation queue block, two rows closed): +2 pass
    # fixtures, both NON-glob, both the pass half of a one-token pair —
    # enum_ctor_arg_array_to_slice_unsize (an enum variant ctor argument takes
    # the `&[T;N] -> &[T]` unsize) and method_arg_wrapper_unsize_dispatch (the
    # method-candidate selector admits the single-field wrapper unsize). Their
    # fail twins are NOT in this population (it is `pass`). No door is declared
    # by either, so `doors`/`glob`/`nonglob_doors` are unmoved.
    # RE-DERIVED BY DIRECT LISTING (2026-09-08g): `ls tests/logos/pass/*.logos | wc -l`
    # -> 2944, `ls tests/logos/pass/{wql_*,deem_*}.logos | wc -l` -> 191,
    # 2944 = 191 + 2753.  (The two extra 09-08g files are FAIL fixtures and are
    # not in this population.) The three 09-08g files are the PASS halves of the three
    # E0509 pairs (drop_body_moves_own_field_e0509_ok,
    # drop_body_conditional_move_e0509_ok,   -- both renamed to `_e0507_ok`
    # on 2026-09-10 by the stdlib-Drop corpus conversion; the count is unmoved,
    # letstruct_destructure_drop_owner_e0509_ok); their FAIL halves live under
    # tests/logos/fail/ and are not in this population.
    # The EIGHT fixtures that moved before them, all nonglob:
    #   drop_guard_instantiated                            (09-08e, stdlib DropGuard)
    #   drop_glue_recurses_after_user_drop{,_ctl}          (09-08f, arm + one-token control)
    #   drop_glue_enum_payload_after_user_drop{,_ctl}      (09-08f, arm + one-token control)
    # The four 09-08f files are the two soundness-queue rows this round closed,
    # landed as pass PAIRS: each arm's control is the same program with the user
    # `impl Drop` deleted, which read the same counts before the fix and after.
    # RE-DERIVED BY DIRECT LISTING (2026-09-09, G156-5b drop-identity class):
    # `ls tests/logos/pass/*.logos | wc -l` -> 2957,
    # `ls tests/logos/pass/{wql_*,deem_*}.logos | wc -l` -> 191, 2957 = 191 + 2766.
    # The SEVEN that moved, all nonglob, all pass halves:
    #   bc_dropident_diffsig_recv{,_samesig}   (the closed row both_drops_destructor_is_inherent
    #                                           and its ONE-TOKEN twin, `&self` vs `&mut self`)
    #   bc_dropident_diffsig_arity              (the closed row drop_inherent_call_order)
    #   bc_dropident_diffsig_{enum,generic,nested_field,moved}
    #                                           (the other four members of the class)
    # The EIGHTH file, bc_dropident_diffsig_arity_rettype, is a FAIL fixture and is
    # not in this population. No door is declared by any of them, so `doors`,
    # `glob` and `nonglob_doors` are unmoved.
    # RE-DERIVED BY DIRECT LISTING (2026-09-09elide, the elision-expansion
    # conformance landing): `ls tests/logos/pass/*.logos | wc -l` -> 2969,
    # `ls tests/logos/pass/{wql_*,deem_*}.logos | wc -l` -> 191, 2969 = 191 + 2778
    # — never by adding 4 to the line below. The FOUR that moved, all nonglob,
    # all pass halves of the elision-expansion class:
    #   bc_sigelide_trait_elides_receiver_pass   (the closed queue row
    #       impl_names_elided_receiver_lifetime_refused, landed as its own program)
    #   bc_sigelide_impl_elides_receiver_pass    (the closed queue row
    #       impl_elides_named_receiver_lifetime_refused, likewise)
    #   bc_sigelide_two_elided_inputs_pass       (a fresh binder PER elided input —
    #       the program the priced arm `sigelide` still refused)
    #   bc_sigelide_separate_input_binders_pass  (the PARAMETER slot of the class)
    # The FOUR new fail fixtures are their one-token twins and are not in this
    # population. No door is declared by any of them, so `doors`, `glob` and
    # `nonglob_doors` are unmoved.
    # PREVIOUSLY (2026-09-09sigland, the return-slot conformance landing):
    # `ls tests/logos/pass/*.logos | wc -l` -> 2965, glob 191, 2965 = 191 + 2774
    # — never by adding 8 to the line below. The EIGHT that moved, all nonglob,
    # all pass halves of the return-type conformance class:
    #   bc_sigretty_return_{bool,signedness,nominal_struct}_agree_pass
    #   bc_sigretty_return_{default_present,dyn_dispatch}_agree_pass
    #       (the five one-token twins of the five new fail fixtures)
    #   bc_sigretty_legal_return_shapes_pass        (alias / `-> Self` / assoc type /
    #       trait type param / `Pair<Self>` / unit — the check's exemptions)
    #   bc_sigretty_generic_impl_return_self_pass   (substituted Self carrying a
    #       type VARIABLE)
    #   bc_sigretty_alpha_rename_receiver_pass      (the localising control for the
    #       two new soundness-queue rows)
    # The FIVE new fail fixtures are not in this population. No door is declared
    # by any of them, so `doors`, `glob` and `nonglob_doors` are unmoved.
    # RE-DERIVED BY DIRECT LISTING (2026-09-10a-selfrecvland, the impl-vs-trait
    # RECEIVER conformance landing; re-derived again 2026-09-10b by direct
    # listing): `ls tests/logos/pass/*.logos | wc -l` -> 3006,
    # `ls tests/logos/pass/{wql_*,deem_*}.logos | wc -l` -> 191, 3006 = 191 + 2815
    # — never by adding 1 to the line below. The ONE that moved, nonglob, a pass
    # half:
    #   impl_recv_conformant_shapes   (SEVEN legal receiver shapes in one program:
    #       shorthand-vs-explicit, Self substituted to a generic target, a
    #       lifetime-annotated declared receiver, an associated fn with no
    #       receiver, an unoverridden default body, by-value declared AND
    #       written, and a LOCAL `trait Drop` declaring by value whose glue still
    #       runs — the exit code is that destructor's count)
    # The FOUR new fail fixtures — impl_recv_{byvalue_vs_mutref,
    # sharedref_vs_mutref,mutref_vs_sharedref} and
    # drop_impl_byvalue_receiver_refused — are its refusal twins and are not in
    # this population. No door is declared by any of them, so `doors`, `glob` and
    # `nonglob_doors` are unmoved.
    'corpus'            : 4199,  # +2 (2026-09-25: TWO pass fixtures JOINED `nonglob` — boxed_escaping_fnonce_capture_double_free (squeue #137 closed), boxed_fnonce_consume_shapes. corpus 4199 = glob 191 + nonglob 4008.) PREVIOUSLY +5 (2026-09-25: FIVE pass fixtures JOINED `nonglob` — closure_owned_dyn_capture, boxed_move_closure_fat_capture_env_overflow, impl_fn_return_stack_env_dangles (squeue #100 #88 #136 closed), closure_owned_dyn_capture_drops_once, closure_slice_capture_shapes. corpus 4197 = glob 191 + nonglob 4006.) PREVIOUSLY +5 (2026-09-25: FIVE pass fixtures JOINED `nonglob` — rawptr_tuple_eq_no_eq_impl_refused, array_temp_elem_field_store_refused (squeue #239 #298 closed), rawptr_comparison_shapes, temp_rooted_place_assign_shapes, temp_rooted_place_assign_mut_ref. corpus 4192 = glob 191 + nonglob 4001.) PREVIOUSLY +2 (2026-09-25: TWO pass fixtures JOINED `nonglob` — tuple_enum_elem_eq_variadic_impl_return_lost_refused (squeue #274 closed), tuple_enum_elem_eq_shapes. corpus 4187 = glob 191 + nonglob 3996.) PREVIOUSLY +2 (2026-09-25: TWO pass fixtures JOINED `nonglob` — ref_typearg_eq_impl_missing_refused (squeue #270 closed), ref_typearg_comparisons_through_refs. corpus 4185 = glob 191 + nonglob 3994.) PREVIOUSLY +1 (2026-09-25: ONE pass fixture JOINED `nonglob` — operator_traits_on_primitives. corpus 4183 = glob 191 + nonglob 3992.) PREVIOUSLY +3 (2026-09-25: THREE pass fixtures JOINED `nonglob` — operator_trait_rhs_type_param_refused (squeue #291 closed), operator_trait_output_shapes, operator_generic_output_projection. corpus 4182 = glob 191 + nonglob 3991.) PREVIOUSLY +1 (2026-09-25: ONE pass fixture JOINED `nonglob` — localvec_for_ref_own_into_iter (squeue #163). corpus 4179 = glob 191 + nonglob 3988.) PREVIOUSLY +2 (2026-09-25: TWO pass fixtures JOINED `nonglob` — i128_literal_promotion_refused (squeue #476 closed), const_promotion_wide_integers. corpus 4178 = glob 191 + nonglob 3987.) PREVIOUSLY +3 (2026-09-25: THREE pass fixtures JOINED `nonglob` — struct_lit_const_promotion_refused (squeue #475 closed), const_promotion_aggregates, deref_dyn_place_legal_spellings (squeue #91). corpus 4176 = glob 191 + nonglob 3985.) PREVIOUSLY +3 (2026-09-25: THREE pass fixtures JOINED `nonglob` — ifexpr_arm_extended_field_borrow_refused (squeue #297 closed), match_arm_extended_temp_borrow, uninit_assigned_in_ifexpr_arm_drop_flag. corpus 4173 = glob 191 + nonglob 3982.) PREVIOUSLY +2 (2026-09-25: TWO pass fixtures JOINED `nonglob` — closure_elided_param_called_with_field_ref_refuses (squeue #187 closed), closure_elided_param_called_with_named_ref. corpus 4170 = glob 191 + nonglob 3979.) PREVIOUSLY +1 (2026-09-25: ONE pass fixture JOINED `nonglob` — intlit_unsuffixed_adopts_overload (squeue #160 closed). corpus 4168 = glob 191 + nonglob 3977.) PREVIOUSLY +4 (2026-09-24: FOUR pass fixtures JOINED `nonglob` — box_vec_new_infer, struct_lit_field_vec_new_infer_refused, generic_user_indexmut_store_refused (squeue #107 #211 #222 closed), expected_type_flows_into_builders. corpus 4099 = glob 191 + nonglob 3976.) PREVIOUSLY +5 (2026-09-24: FIVE pass fixtures JOINED `nonglob` — struct_named_like_type_param_refused, self_tuple_struct_ctor_refused, enum_tuple_ctor_as_fn_value_refused (squeue #207 #185 #208 closed), constructors_as_values, type_param_named_like_struct_annotated. corpus 4095 = glob 191 + nonglob 3904.) PREVIOUSLY +11 (2026-09-24: ELEVEN pass fixtures JOINED `nonglob` — refbind_over_ref_scrutinee_deref_coerced_call, call_arg_deref_coercion_double_ref, boxref_sized_struct_deref_coercion_missing, field_read_through_ref_to_box_refused, index_through_ref_to_vec_let_refuses, vec_index_store_through_mutref_param_refused, method_autoderef_double_ref_option_infer_refused (squeue #301 #90 #279 #183 #209 #283 closed), deref_coercion_sites, index_through_vec_refs, whole_scrutinee_binder_through_refs, option_methods_through_refs. corpus 4090 = glob 191 + nonglob 3899.) PREVIOUSLY +9 (2026-09-24: NINE pass fixtures JOINED `nonglob` — let_array_pattern_ref_mut_binding_is_raw_pointer_refuses, let_tuplestruct_generic_elem_type_unsubstituted, match_tmp_wild_mut_addrof, slice_rest_ref_binder_syntax, variant_tuple_door_struct_shape_sub_unresolved (squeue #458 #126 #109 #145 #300 closed), let_generic_and_ref_array_patterns, nested_struct_variant_in_tuple_payload, match_temp_scrutinee_mut_binders, array_named_rest_binds_subslice. corpus 4079 = glob 191 + nonglob 3888.) PREVIOUSLY +3 (2026-09-24: THREE pass fixtures JOINED `nonglob` — field_base_temp_built_before_sibling_run (squeue #263 closed), temp_field_base_eval_order_ops_calls, temp_field_base_eval_order_literals. corpus 4070 = glob 191 + nonglob 3879.) PREVIOUSLY +1 (2026-09-24: pass/ptr_same_region_compare_ok JOINED `nonglob` (legal twin of squeue #244, closed as a fail fixture). corpus 4067 = glob 191 + nonglob 3876.) PREVIOUSLY +1 (2026-09-24: pass/vec_iter_mut_store_outliving_ok JOINED `nonglob`. corpus 4066 = glob 191 + nonglob 3875.) PREVIOUSLY +1 (2026-09-24: pass/refcell_push_outliving_refs_ok JOINED `nonglob` (the legal twin of squeue #260, closed as a fail fixture). corpus 4065 = glob 191 + nonglob 3874.) PREVIOUSLY +2 (2026-09-24: TWO pass fixtures JOINED `nonglob` — at_binding_ref_mut_payload_refuses, bc_patmovebind_at_ref_binding_twin (squeue #117 #451 closed). corpus 4064 = glob 191 + nonglob 3873.) PREVIOUSLY +1 (2026-09-24: pass/fnitem_array_elem_static_return_ok JOINED `nonglob` (the legal twin of squeue #146, closed as a fail fixture). corpus 4062 = glob 191 + nonglob 3871.) PREVIOUSLY +3 (2026-09-24: THREE pass fixtures JOINED `nonglob` — let_at_binding_type_annot_syntax (squeue #118 closed), tuple_let_drop_timing, let_pattern_temporary_drops. corpus 4061 = glob 191 + nonglob 3870.) PREVIOUSLY +8 (2026-09-24: EIGHT pass fixtures JOINED `nonglob` — fnparam_struct_ref_mut_field_binds_byvalue, closure_param_struct_pattern_syntax (squeue #120 #121 closed), fnparam_patterns_any_shape, fnparam_pattern_skipped_parts_drop, closure_untyped_pattern_params, let_struct_nested_subpatterns, pattern_binders_drop_reverse_source_order, fnparam_array_drop_rest_unbound (moved from fail/). corpus 4058 = glob 191 + nonglob 3867.) PREVIOUSLY +5 (2026-09-24: FIVE pass fixtures JOINED `nonglob` — variant_payload_nested_at_pattern_refused (squeue #306 closed), variant_payload_at_structural_sub, variant_payload_at_expr_and_while_let, match_expr_ref_scrutinee_struct_sub, match_expr_payload_destructure_drops. corpus 4050 = glob 191 + nonglob 3859.) PREVIOUSLY +4 (2026-09-24: FOUR pass fixtures JOINED `nonglob` — for_header_pattern_tuple_only (squeue #122 closed), for_header_irrefutable_patterns, for_header_binding_modes, for_header_ref_tuple_drop. corpus 4045 = glob 191 + nonglob 3854.) PREVIOUSLY +5 (2026-09-24: FIVE pass fixtures JOINED `nonglob` — let_ref_bind_annotated_refused, let_double_ref_str_annotation_refused, ref_to_slice_ref_param_unsized_refused (squeue #264 #268 #277 closed), let_ref_annotated_temp_drop, multi_ref_slice_types. corpus 4041 = glob 191 + nonglob 3850.) PREVIOUSLY +3 (2026-09-24: let_ref_struct_pattern_irrefutable_refused, letelse_ref_pattern_binding_undefined_refused (squeue #280 #282 closed), let_ref_patterns JOINED `nonglob`. corpus 4036 = glob 191 + nonglob 3845.) PREVIOUSLY +4 (2026-09-24: let_at_binding_tuple_sub_unsupported, let_array_pattern_nested_subpattern_refused (squeue #85 #96 closed), let_irrefutable_nested_patterns, let_else_structural_patterns JOINED `nonglob`. corpus 4033 = glob 191 + nonglob 3842.) PREVIOUSLY +1 (2026-09-24: pass/tuple_in_tuple_place_assign JOINED `nonglob`. corpus 4029 = glob 191 + nonglob 3838.) PREVIOUSLY +3 (2026-09-24: paren_var_assign_target, nested_tuple_field_assign_unimplemented (squeue #116 #131 closed), nested_place_and_paren_assign JOINED `nonglob`. corpus 4028 = glob 191 + nonglob 3837.) PREVIOUSLY +2 (2026-09-24: labeled_foreach_label_lost (squeue #114 closed) and labeled_foreach_forms JOINED `nonglob`. corpus 4025 = glob 191 + nonglob 3834.) PREVIOUSLY +5 (2026-09-24: FIVE pass fixtures JOINED `nonglob` — foreach_array_field_gep_verifier_refused, at_binding_array_sub_verifier_error_refused, match_ref_array_nested_struct_sub_refused (squeue #81 #299 #94 closed), foreach_array_value_forms, array_patterns_through_refs_and_at. corpus 4023 = glob 191 + nonglob 3832.) PREVIOUSLY +1 (2026-09-24: pass/match_array_nested_array_subpattern_mlirgen_malfunction, the row promoted. corpus 4018 = glob 191 + nonglob 3827.) PREVIOUSLY +4 (2026-09-24: FOUR pass fixtures JOINED `nonglob` — array_typed_field_binding_shape_lost, match_array_elem_array_binder_shape_lost_run, match_tuple_door_nested_slice_binds_nothing (squeue #84 #80 #123 closed), array_binders_keep_shape. corpus 4017 = glob 191 + nonglob 3826.) PREVIOUSLY +5 (2026-09-24: FIVE pass fixtures JOINED `nonglob` — let_underscore_defers_drop_to_block_end, match_array_elem_struct_binder_aliases_source (squeue #129 #79 closed), let_underscore_binds_nothing, byvalue_aggregate_binders_copy_stmt, byvalue_aggregate_binders_copy_expr. corpus 4013 = glob 191 + nonglob 3822.) PREVIOUSLY +1 (2026-09-24: pass/struct_pat_ref_and_local_shadow_twins JOINED `nonglob`. corpus 4008 = glob 191 + nonglob 3817.) PREVIOUSLY +2 (2026-09-24: pass/tier2_refusal_admit_twins and pass/array_repeat_operand_evaluated_once JOINED `nonglob`. corpus 4007 = glob 191 + nonglob 3816.) PREVIOUSLY +3 (2026-09-24: THREE pass fixtures JOINED `nonglob` — enum_payload_partial_move_leak (squeue row closed), enum_payload_partial_move_forms, enum_payload_partial_move_flagged. corpus 4005 = glob 191 + nonglob 3814.) PREVIOUSLY +1 (2026-09-24: pass/arraylit_cast_target_elem_type JOINED `nonglob`. corpus 4002 = glob 191 + nonglob 3811.) PREVIOUSLY +3 (2026-09-24: THREE pass fixtures JOINED `nonglob` — operator_operand_owner_after_sibling_exit_leak (squeue #262 closed), operand_owned_across_sibling_exit and method_arg_owned_across_sibling_exit. No wql_/deem_ match and no door. corpus 4001 = glob 191 + nonglob 3810.) PREVIOUSLY +1 (2026-09-24: pass/generic_enum_ctor_inferred_in_literals JOINED `nonglob`. No wql_/deem_ match and no door. corpus 3998 = glob 191 + nonglob 3807.) PREVIOUSLY +6 (2026-09-24: SIX pass fixtures JOINED `nonglob` — array_nonprim_elem_eq_compares_addresses_run, generic_struct_impl_ref_eq_compares_addresses_run, ref_pair_nonprim_tuple_eq_compares_addresses_run (squeue rows closed), eq_ref_pairs_through_value, eq_nonprim_sequences_elementwise and eq_nested_arrays_flattened. No wql_/deem_ match and no door. corpus 3997 = glob 191 + nonglob 3806.) PREVIOUSLY +4 (2026-09-24: FOUR pass fixtures JOINED `nonglob` — weak_local_never_dropped (squeue row closed), weak_drop_forms, weak_ambiguous_name_user_trait_sync and weak_ambiguous_name_user_trait_rc. No wql_/deem_ match and no door. corpus 3991 = glob 191 + nonglob 3800.) PREVIOUSLY +4 (2026-09-24: FOUR pass fixtures JOINED `nonglob` — closure_fnonce_cond_call_untaken_leak (squeue #141 closed), closure_fnonce_cond_release_if_forms, closure_fnonce_cond_release_match_and_expr_forms and closure_fnonce_cond_release_shortcircuit_and_guard. No wql_/deem_ match and no door. corpus 3987 = glob 191 + nonglob 3796.) PREVIOUSLY +2 (2026-09-24: TWO pass fixtures JOINED `nonglob` — generic_operator_operands_moved_admit (squeue #284 closed) and generic_operator_copy_operands_reusable. No wql_/deem_ match and no door. RE-DERIVED FROM logos_00_population_pin_lint's DIRECT LISTING: corpus 3983 = glob 191 + nonglob 3792.) PREVIOUSLY +4 (2026-09-24: FOUR pass fixtures JOINED `nonglob` — let_array_pattern_remainder_group_order_admit, let_array_pattern_temp_source_remainder_admit (squeue #92, #93 closed), partially_moved_array_drop_order and let_array_pattern_temp_source_drops. No wql_/deem_ match and no door. RE-DERIVED FROM logos_00_population_pin_lint's DIRECT LISTING: corpus 3981 = glob 191 + nonglob 3790.) PREVIOUSLY +5 (2026-09-24: FIVE pass fixtures JOINED `nonglob` — variant_payload_nested_struct_sub_by_ref_admit, let_tuple_destructure_ref_scrutinee_admit (squeue #135, #115 closed), variant_payload_nested_binding_modes, let_tuple_over_ref_binding_modes and let_struct_over_ref_binding_modes. No wql_/deem_ match and no door. RE-DERIVED FROM logos_00_population_pin_lint's DIRECT LISTING: corpus 3977 = glob 191 + nonglob 3786.) PREVIOUSLY +2 (2026-09-24: TWO pass fixtures JOINED `nonglob` — tuplestruct_door_default_ref_mode_admit (squeue #144 closed) and tuplestruct_default_binding_modes. No wql_/deem_ match and no door. RE-DERIVED FROM logos_00_population_pin_lint's DIRECT LISTING: corpus 3972 = glob 191 + nonglob 3781.) PREVIOUSLY +2 (2026-09-23: TWO pass fixtures JOINED `nonglob` — field_base_temporaries_order_and_moves and if_expr_cond_temporaries_drop_first. No wql_/deem_ match and no door. RE-DERIVED FROM logos_00_population_pin_lint's DIRECT LISTING: corpus 3970 = glob 191 + nonglob 3779.) PREVIOUSLY +2 (2026-09-23: TWO pass fixtures JOINED `nonglob` — param_ref_struct_cast_after_addr_of_admit (squeue row closed) and param_ref_reads_after_addr_of. No wql_/deem_ match and no door. RE-DERIVED FROM logos_00_population_pin_lint's DIRECT LISTING: corpus 3968 = glob 191 + nonglob 3777.) PREVIOUSLY +4 (2026-09-23: FOUR pass fixtures JOINED `nonglob` — tuple_elem_shared_borrow_through_ref_admit (squeue row closed), tuple_elem_borrows_are_addresses, tuple_aggregate_elem_borrow_identity and tuple_elem_through_field_ref_writes. No wql_/deem_ match and no door. RE-DERIVED FROM logos_00_population_pin_lint's DIRECT LISTING: corpus 3966 = glob 191 + nonglob 3775.) PREVIOUSLY +2 (2026-09-23: TWO pass fixtures JOINED `nonglob` — while_body_return_move_dropped_on_fallthrough_admit (squeue #470 closed) and loop_body_move_exit_paths. No wql_/deem_ match and no door. RE-DERIVED FROM logos_00_population_pin_lint's DIRECT LISTING: corpus 3962 = glob 191 + nonglob 3771.) PREVIOUSLY +2 (2026-09-23: TWO pass fixtures JOINED `nonglob` — self_call_in_return_param_dropped_admit (squeue #86 closed) and param_moved_on_one_branch_drops_on_others. No wql_/deem_ match and no door. RE-DERIVED FROM logos_00_population_pin_lint's DIRECT LISTING: corpus 3960 = glob 191 + nonglob 3769.) PREVIOUSLY +2 (2026-09-23: TWO pass fixtures JOINED `nonglob` — static_array_whole_borrow_addresses_global_admit (squeue row closed) and static_array_borrows_address_global. No wql_/deem_ match and no door. RE-DERIVED FROM logos_00_population_pin_lint's DIRECT LISTING: corpus 3958 = glob 191 + nonglob 3767.) PREVIOUSLY +4 (2026-09-23: FOUR pass fixtures JOINED `nonglob` — fru_temporary_base_unmoved_field_dropped_admit, fnptr_field_call_on_temporary_dropped_admit (squeue #295, #294 closed), fru_and_field_call_temporaries and generic_struct_update_turbofish. No wql_/deem_ match and no door. RE-DERIVED FROM logos_00_population_pin_lint's DIRECT LISTING: corpus 3956 = glob 191 + nonglob 3765.) PREVIOUSLY +2 (2026-09-23: TWO pass fixtures JOINED `nonglob` — loop_break_value_array_lit_admit (squeue #285 closed) and loop_break_value_moves_and_copies. No wql_/deem_ match and no door. RE-DERIVED FROM logos_00_population_pin_lint's DIRECT LISTING: corpus 3952 = glob 191 + nonglob 3761.) PREVIOUSLY +3 (2026-09-23: THREE pass fixtures JOINED `nonglob` — foreach_array_elements_owned_by_loop_admit (squeue #293 closed), foreach_array_ownership_paths and foreach_array_labelled_break_drops_tail. No wql_/deem_ match and no door. RE-DERIVED FROM logos_00_population_pin_lint's DIRECT LISTING: corpus 3950 = glob 191 + nonglob 3759.) PREVIOUSLY +2 (2026-09-23: TWO pass fixtures JOINED `nonglob` — index_place_through_refmut_drops_old_admit (squeue row closed) and index_store_through_refmut_drop_old. No wql_/deem_ match and no door. RE-DERIVED FROM logos_00_population_pin_lint's DIRECT LISTING: corpus 3947 = glob 191 + nonglob 3756.) PREVIOUSLY +4 (2026-09-23: FOUR pass fixtures JOINED `nonglob` — match_expr_array_pattern_struct_elems_admit, at_binding_or_subpattern_enum_admit (squeue #290, #302 closed), match_expr_array_pattern_binders and at_binding_or_alternatives. No wql_/deem_ match and no door. RE-DERIVED FROM logos_00_population_pin_lint's DIRECT LISTING: corpus 3945 = glob 191 + nonglob 3754.) PREVIOUSLY +3 (2026-09-23: THREE pass fixtures JOINED `nonglob` — closure_param_shadow_drop_by_slot_admit, match_arm_nested_binder_shadow_drop_by_slot_admit (squeue #265, #266 closed) and shadow_binders_drop_by_slot. No wql_/deem_ match and no door. RE-DERIVED FROM logos_00_population_pin_lint's DIRECT LISTING: corpus 3941 = glob 191 + nonglob 3750.) PREVIOUSLY +3 (2026-09-23: THREE pass fixtures JOINED `nonglob` — refptr_param_eq_compares_pointers_admit, refptr_inner_region_param_eq_admit (squeue #313, #309 closed) and ref_pair_pointer_eq_compares_through. No wql_/deem_ match and no door. RE-DERIVED FROM logos_00_population_pin_lint's DIRECT LISTING: corpus 3938 = glob 191 + nonglob 3747.) PREVIOUSLY +2 (2026-09-23: TWO pass fixtures JOINED `nonglob` — letelse_or_pattern_scalar_alts_admit (squeue row closed) and letelse_or_pattern_scalar_kinds. No wql_/deem_ match and no door. RE-DERIVED FROM logos_00_population_pin_lint's DIRECT LISTING: corpus 3935 = glob 191 + nonglob 3744.) PREVIOUSLY +2 (2026-09-23: TWO pass fixtures JOINED `nonglob` — variant_payload_char_literal_match_admit (squeue #111 closed) and char_payload_patterns_decode_scalar. No wql_/deem_ match and no door. RE-DERIVED FROM logos_00_population_pin_lint's DIRECT LISTING: corpus 3933 = glob 191 + nonglob 3742.) PREVIOUSLY +8 (2026-09-23: EIGHT pass fixtures JOINED `nonglob` — self_assign_after_move_admit, letstruct_partial_destructure_moves_fields_admit, loop_init_before_break_admit, deferred_init_after_inner_shadow_admit, let_at_binding_struct_pattern_moved_admit (squeue #132/#125/#176/#267/#305 closed), deferred_init_shadow_drop_state_per_binding, for_mut_vec_ref_yields_mut_elem and dyn_coerce_scalar_implementor. No wql_/deem_ match and no door. RE-DERIVED FROM logos_00_population_pin_lint's DIRECT LISTING: corpus 3931 = glob 191 + nonglob 3740.) PREVIOUSLY +1 (2026-09-23: ONE pass fixture JOINED `nonglob` — fn_bound_hr_param_outer_ret_admit. No wql_/deem_ match and no door. RE-DERIVED FROM logos_00_population_pin_lint's DIRECT LISTING: corpus 3923 = glob 191 + nonglob 3732.) PREVIOUSLY +1 (2026-09-23: ONE pass fixture JOINED `nonglob` — assoc_bound_eq_self_supertrait_admit. No wql_/deem_ match and no door. RE-DERIVED FROM logos_00_population_pin_lint's DIRECT LISTING: corpus 3922 = glob 191 + nonglob 3731.) PREVIOUSLY +1 (2026-09-23: ONE pass fixture JOINED `nonglob` — struct_bound_lifetime_in_signature_admit. No wql_/deem_ match and no door. RE-DERIVED FROM logos_00_population_pin_lint's DIRECT LISTING: corpus 3921 = glob 191 + nonglob 3730.) PREVIOUSLY +1 (2026-09-23: ONE pass fixture JOINED `nonglob` — trait_default_body_template_admit (orphan trait default bodies checked as templates). No wql_/deem_ match and no door. RE-DERIVED FROM logos_00_population_pin_lint's DIRECT LISTING: corpus 3920 = glob 191 + nonglob 3729.) PREVIOUSLY +1 (2026-09-23: ONE pass fixture JOINED `nonglob` — cell_get_copy_handles_admit (#469). No wql_/deem_ match and no door. RE-DERIVED FROM logos_00_population_pin_lint's DIRECT LISTING: corpus 3919 = glob 191 + nonglob 3728.) PREVIOUSLY +2 (2026-09-23: TWO pass fixtures JOINED `nonglob` — alias_hole_twice_same_region_admit (twin of var-appears-twice) and tuple_copy_clone_bounds_admit (tuple Copy/Clone impls). No wql_/deem_ match and no door. RE-DERIVED FROM logos_00_population_pin_lint's DIRECT LISTING: corpus 3918 = glob 191 + nonglob 3727.) PREVIOUSLY +3 (2026-09-23: THREE pass fixtures JOINED `nonglob` — closure_hr_ret_capture_used_not_returned_admit (twin of regions-nested-fns-2), tuplestruct_ctor_declared_binder_name_admit (squeue #227 closed) and anon_region_let_annotation_named_region_admit (squeue #177 closed). No wql_/deem_ match and no door. RE-DERIVED FROM logos_00_population_pin_lint's DIRECT LISTING: corpus 3916 = glob 191 + nonglob 3725.) PREVIOUSLY +2 (2026-09-23: TWO pass fixtures JOINED `nonglob` — writ_link_cycle_admit and writ_link_foreign_arena_panics, the static and runtime halves of #464's `Writ::link`. No wql_/deem_ match and no door. RE-DERIVED FROM logos_00_population_pin_lint's DIRECT LISTING: corpus 3913 = glob 191 + nonglob 3722.) PREVIOUSLY +1 (2026-09-23: ONE pass fixture JOINED `nonglob` — let_underscore_lifetime_annotation_admit (`'_` in a let annotation is an inference region, #465 neighbour). No wql_/deem_ match and no door. RE-DERIVED FROM logos_00_population_pin_lint's DIRECT LISTING: corpus 3911 = glob 191 + nonglob 3720.) PREVIOUSLY +1 (2026-09-23: ONE pass fixture JOINED `nonglob` — let_mutref_annotation_to_meet_struct_admit, the program of soundness-queue row #225 (a legal `let r: &mut P = &mut p` refused as a variance mismatch), closed when a bare ADT in a body annotation became the elided spelling of every slot (#465). No wql_/deem_ match and no door, so `glob` and the door counts are unmoved. RE-DERIVED FROM logos_00_population_pin_lint's DIRECT LISTING: corpus 3910 = glob 191 + nonglob 3719.) PREVIOUSLY +1 (2026-09-22: ONE pass fixture JOINED `nonglob` — sliceref_read_of_local_array_says_dangling, the program of a soundness-queue row (#166, a legal program refused as "dangling") closed when the unsizing `&[T; N] -> &[T]` began relating borrow to borrow and element to element. No wql_/deem_ match and no door, so `glob` and the door counts are unmoved. RE-DERIVED FROM logos_00_population_pin_lint's DIRECT LISTING: corpus 3909 = glob 191 + nonglob 3718.) PREVIOUSLY +1 (2026-09-22: ONE pass fixture JOINED `nonglob` — bc_idxbase_elem_write_in_index_admit, was fail/bc_idxbase_elem_write_in_index_fail. It pinned a refusal of `a[{ a[0] = 9; 2 }]`, which rustc 1.98.1 ACCEPTS: an array's length is static and indexing it takes no fake borrow. No wql_/deem_ match and no door, so `glob` and the door counts are unmoved. RE-DERIVED FROM logos_00_population_pin_lint's DIRECT LISTING: corpus 3908 = glob 191 + nonglob 3717.) PREVIOUSLY -3 (2026-09-22: THREE pass fixtures LEFT `nonglob` — bc_capbody_closure_return_admit, bc_capretsc_closure_elided_contract and bc_h4e_closure_arg_tie_param. Each asserted a closure returning its PARAMETER's reference with no `Fn*` bound to tie the two; rustc 1.98.1 refuses that (a closure signature is not elided), and CL_RET_TIED now lets logosc see it. Moved to fail/ as *_refuse. No wql_/deem_ match and no door, so `glob` and the door counts are unmoved. RE-DERIVED FROM logos_00_population_pin_lint's DIRECT LISTING: corpus 3907 = glob 191 + nonglob 3716.) PREVIOUSLY -1 (2026-09-22: ONE pass fixture LEFT `nonglob` — bc_closmint_ret_from_param, which pinned a program rustc refuses (E0597: `with<'q>` borrows its local `t` for `'q` through `f: &dyn Fn(&'q i64)`), moved to fail/bc_closmint_ret_from_param_refuse. No wql_/deem_ match and no door, so `glob` and the door counts are unmoved. RE-DERIVED FROM logos_00_population_pin_lint's DIRECT LISTING: corpus 3910 = glob 191 + nonglob 3719.) PREVIOUSLY +16 (2026-09-22 ADR 0028: SEVENTEEN soundness-queue programs joined `nonglob` as pass fixtures and ONE left it. The seventeen — aggregate_param_ref_field_borrow_returned_says_dangling_refused, array_field_store_value_reads_field_loan_refused, at_binding_whole_struct_wild_fields_refused, box_deref_move_refused_outside_let, dangle_join_diverging_arm, double_ref_deref_return_outer_lifetime_refused, loop_holder_realias_alternating_refuses, loop_holder_realias_referent_read_after_refuses, pattern_ref_binding_loan_outlives_last_use_refused, promoted_literal_call_arg_refused, refvar_reborrow_whole_then_assign_counter_refused, setter_mutref_field_store_after_outer_set_rehomed_refused, tuple_elem_reinit_after_move_refused, vec_mutref_elem_push_store_rehomed_refused, vec_push_arg_reads_loan_refused, vec_push_ifarm_after_loan_last_use_refused, where_impl_region_static_bound_unused_refused — are `refuses` rows the new checker closed by ACCEPTING them; they landed under tests/logos/pass/ in an earlier round of this arc WITHOUT an `.expected`, so they were in no suite and this pin went unread past them. This round wrote the seventeen `exit: 0` files, which is what made the drift visible. The one that left is bc_patmovebind_at_ref_binding_twin, now soundness-queue row #451. No wql_/deem_ match and no door, so `glob` and the door counts are unmoved. RE-DERIVED FROM logos_00_population_pin_lint's DIRECT LISTING: corpus 3911 = glob 191 + nonglob 3720.) PREVIOUSLY +0/-1 (2026-09-21 ADR 0028 blocker triage: ONE pass fixture LEFT `nonglob` — bc_d1r9_f0_retarget_overlap_admit, which pinned a rule Rust does not have and is now soundness-queue row #449. No wql_/deem_ match and no door, so `glob` and the door counts are unmoved. RE-DERIVED FROM logos_00_population_pin_lint's DIRECT LISTING: corpus 3895 = glob 191 + nonglob 3704.) PREVIOUSLY +1 (2026-09-20 ADR 0029 S2, the captures in the closure TYPE: ONE pass fixture joined `nonglob`, closure_send_not_from_sibling — a Send literal beside a `*mut`-capturing sibling of the identical signature, REFUSED before the env left the signature-keyed union and entered the type. Its fail twin closure_dyn_send_needs_own_env is a refusal and not in this population. No wql_/deem_ match and no door, so `glob` and the door counts are unmoved. RE-DERIVED FROM logos_00_population_pin_lint's DIRECT LISTING: corpus 3896 = glob 191 + nonglob 3705.) PREVIOUSLY +2 (2026-09-20 #442 and #105, the per-instantiation closure id: TWO pass fixtures joined `nonglob`, generic_closure_id_collides_across_instances and closure_in_generic_two_insts — the programs of two soundness-queue rows closed by this round, moved from tests/soundness/open/. Both are a closure inside a generic fn instantiated at two types, which did not COMPILE before the fix (`redefinition of symbol named '__closure_0'`). No wql_/deem_ match and no door, so `glob` and the door counts are unmoved. RE-DERIVED FROM logos_00_population_pin_lint's DIRECT LISTING: corpus 3895 = glob 191 + nonglob 3704.) PREVIOUSLY +1 (2026-09-20 `where &T: Trait` receiver autoref: ONE pass fixture joined `nonglob`, where_ref_subject_reads_self — the twin of imported where-clause-ref-bound-b158 whose impl bodies READ `self`, rc 139 before the fix. No wql_/deem_ match and no door, so `glob` and the door counts are unmoved. RE-DERIVED FROM logos_00_population_pin_lint's DIRECT LISTING: corpus 3893 = glob 191 + nonglob 3702.) PREVIOUSLY +1 (2026-09-20 #438 step 8 (#15): ONE pass fixture joined `nonglob`, dyn_vtable_homonym_target — two packages of one module, same `$M`, whose vtable is keyed by the trait's identity and the method by the impl's package. No wql_/deem_ match and no door, so `glob` and the door counts are unmoved. RE-DERIVED FROM logos_00_population_pin_lint's DIRECT LISTING: corpus 3892 = glob 191 + nonglob 3701.) PREVIOUSLY +1 (2026-09-19 ADR 0028 slice 2, exact callees + explicit autoref: ONE pass fixture joined `nonglob`, vec_index_store_value_borrows_vec_admitted, the program of soundness-queue row vec_index_store_value_borrows_vec_refused (#218) closed by this round. No wql_/deem_ match and no door. RE-DERIVED FROM logos_00_population_pin_lint's DIRECT LISTING: corpus 3891 = glob 191 + nonglob 3700.) PREVIOUSLY +1 (2026-09-19 ADR 0028 region-erased impl keys: ONE pass fixture joined `nonglob`, trait_impl_for_ref_method_call_receiver, the program of soundness-queue row trait_impl_for_ref_method_call_receiver_refused (#188) closed by this round. No wql_/deem_ match and no door, so `glob` and the door counts are unmoved. RE-DERIVED FROM logos_00_population_pin_lint's DIRECT LISTING: corpus 3890 = glob 191 + nonglob 3699.) PREVIOUSLY +5 (2026-09-19 ADR 0028 DerefMove + declared-signature elision: FIVE pass fixtures joined `nonglob` — box_field_move, struct_binder_result and box_mutref_payload_write_immut_box (three soundness-queue rows closed, moved from tests/soundness/open/), box_field_move_siblings and trait_default_next_item_binder (new). The round's sixth fixture, fail/box_field_move_twice, is not in this population. None matches wql_/deem_ and none declares a door, so `glob` and the door counts are unmoved. Merged onto 2026-09-18-partial-cmp-return (+1): corpus 3889 = glob 191 + nonglob 3698, RE-DERIVED FROM logos_00_population_pin_lint's DIRECT LISTING.) PREVIOUSLY +1 (2026-09-18-partial-cmp-return LANDING: ONE pass fixture joined `nonglob`, partial_cmp_ordering_forms_admitted — the ADMIT twin for #430/#432, pinning that `partial_cmp -> Ordering`, `-> Option<Ordering>` and a direct `lt` impl all keep working now that sema refuses to emit a comparison helper whose lookup it never resolved. ⚠ THE ROUND ADDED THREE FIXTURES: the other two are fail/partial_cmp_non_ordering_return_refused and fail/partial_cmp_option_foreign_payload_refused, and `fail` fixtures are NOT in this population — which is why this pin moves +1 while the census registry moves by more. Neither matches wql_/deem_ so `glob` is unmoved at 191. RE-DERIVED FROM THE GATE'S OWN PRINTED MEASUREMENT: it printed `corpus 3884 = glob 191 + nonglob 3693 · doors 36 = glob 10 + nonglob 26`, and DOOR IMMOBILITY at 36 = 10 + 26 is what makes re-deriving the population legitimate rather than buying a row.) PREVIOUSLY +1 (2026-09-18-struct-operator LANDING: ONE pass fixture joined `nonglob`, struct_relational_impl_admitted — the ADMIT twin of #427's refusal, pinning that a struct WITH the operator method, a struct with a generic impl, and the primitive paths all keep working now that sema refuses an operator whose method the type never implements. ⚠ THE ROUND ADDED TWO FIXTURES: the other is fail/struct_relational_no_impl_refused, and a `fail` fixture is NOT in this population — which is why this pin moves +1 while the census registry moves +2. Neither matches wql_/deem_ so `glob` is unmoved at 191. RE-DERIVED FROM logos_00_population_pin_lint's DIRECT LISTING (corpus 3883 = glob 191 + nonglob 3692) and cross-checked by an independent `ls tests/logos/pass/*.logos`; the door census itself could not be read in the same run — it exited 2 (facts from a different tree) because the compiler was rebuilt, so DOOR IMMOBILITY IS VERIFIED SEPARATELY after the corpus run regenerates the facts.) PREVIOUSLY +1 (2026-09-18-array-order LANDING: ONE pass fixture joined `nonglob`, array_relational_admitted — the ADMIT twin of #377's refusal, pinning the seven comparison shapes that must keep working now that sema refuses `<` on an aggregate the lowering cannot order (scalars, array `==`/`!=`, all-primitive tuples, `&i32`, C-like enums, `str`, a struct with `lt`). ⚠ THE ROUND ADDED TWO FIXTURES, NOT ONE: the other is fail/array_relational_refused, and a `fail` fixture is NOT in this population — which is why this pin moves +1 while the census registry moves +2. Neither matches wql_/deem_ so `glob` is unmoved at 191, and neither declares a door, so `doors` and `nonglob_doors` are unmoved at 36 = 10 + 26 — the fold re-measured both in the same run. RE-DERIVED BY DIRECT FILE LISTING (ls tests/logos/pass/*.logos): 3882 = 191 + 3691, the partition closes, and the listing was run INDEPENDENTLY of the gate, which printed the same 3882/3691.) PREVIOUSLY +2 (2026-09-18-static-storage LANDING: TWO pass fixtures joined `nonglob`, static_aggregate_storage_O0 and static_aggregate_storage_O2 — the two halves of #343, one program compiled at -O0 and at -O2 (the `_O2` suffix dispatches the flag in logos_pass_extra_args). A struct- or tuple-typed `static` was declared `!llvm.ptr` (8 bytes) while `__logos_static_init` memcpy'd `layout_of(type).size` into it; CONTROL REVERT with the compiler fix backed out has the -O0 half exit 2 and the -O2 half exit 1, both 0 with it. Neither matches wql_/deem_ so `glob` is unmoved at 191, and neither declares a door, so `doors` and `nonglob_doors` are unmoved at 36 = 10 + 26 — the fold re-measured both in the same run. RE-DERIVED BY DIRECT FILE LISTING (ls tests/logos/pass/*.logos): 3881 = 191 + 3690, the partition closes, and the listing was run INDEPENDENTLY of the gate, which printed the same 3881/3690.) PREVIOUSLY +2 (2026-09-18-adr0027s3 LANDING: TWO pass fixtures joined `nonglob`, writ_row_view and bc_row_ref_return_admit — the ADR 0027 S3 row views: `RowRef` (borrows its container, `#[borrow_carrying]`), `RowBuf` (owns an assembled document), `RowRef::to_buf` (D11's explicit promotion) and the `Writ::row_ref` door. The round's THIRD new fixture, fail/bc_row_ref_return_dangle, is the refusal twin — a `RowRef` returned over a fn-local `Writ` — and a `fail` fixture is NOT in this population. Neither pass fixture matches wql_/deem_ so `glob` is unmoved at 191, and neither declares a door, so `doors` and `nonglob_doors` are unmoved at 36 = 10 + 26 — the fold re-measured both in the same run. RE-DERIVED BY DIRECT FILE LISTING (ls tests/logos/pass/*.logos): 3879 = 191 + 3688, the partition closes, and the listing was run INDEPENDENTLY of the gate, which printed the same 3879/3688.) PREVIOUSLY +1 (2026-09-17-adr0027s1 LANDING: ONE pass fixture joined `nonglob`, writ_row_format — the ADR 0027 S1 `WRow` format fixture (presence bitmap + popcount access, type code W_ROW=97). It does not match wql_/deem_ so `glob` is unmoved at 191, and it declares NO door, so `doors` and `nonglob_doors` are unmoved at 36 = 10 + 26 — the fold re-measured both in the same run. RE-DERIVED BY DIRECT FILE LISTING (ls tests/logos/pass/*.logos): 3877 = 191 + 3686, the partition closes.) PREVIOUSLY +2 (2026-09-17q-byvalparam LANDING: TWO pass fixtures joined `nonglob`, typeof_container_byvalue_param_admit and typeof_container_byvalue_param_inline_admit — the two carriers (alias and inline) of a BY-VALUE parameter typed by a `typeof(<container>)` projection, each of which made every binary before this commit die with SIGSEGV (rc 139) in the metaprog round. Neither matches wql_/deem_ so `glob` is unmoved at 191. The ONE new fail fixture typeof_container_byvalue_param_no_family_fail is the refusal twin and is NOT in this population; no door is declared by either, so `doors` and `nonglob_doors` are unmoved. RE-DERIVED BY DIRECT FILE LISTING (ls tests/logos/pass/*.logos): 3876 = 191 + 3685, the partition closes.) PREVIOUSLY +5 (2026-09-17p-ordcmpland LANDING: FIVE pass fixtures joined `nonglob`. ONE is the program of the CLOSED soundness_queue row rawptr_ordering_compare_mlir_verifier_refused (tier 3), landed as bc_0917p_ordcmpland_rawptr_ordering_compare — legal Rust (rustc 1.98.1 --edition 2024 compiles and RUNS it at exit 0, valgrind clean) that every binary before this commit REFUSED with "'arith.cmpi' op operand #0 must be signless-integer-like, but got '!llvm.ptr'". FOUR are hand-battery programs bc_0917p_ordcmpland_hb_{y01_admit,y02_admit,y03_admit,y04_admit}: y01/y02/y04 MOVED base(refused) -> landed(compiles and RUNS the rustc twin's stdout), y03 is the CONTROL that already passed on the base binary and pins `&T` ordering comparing POINTEES not addresses — the regression the crude arm caused. None matches wql_/deem_ so `glob` is unmoved at 191. The THREE new fail fixtures bc_0917p_ordcmpland_hb_{z05,z06,z08}_refuse are refusal twins and are NOT in this population; no door is declared by any of the eight, so `doors` and `nonglob_doors` are unmoved. RE-DERIVED BY DIRECT FILE LISTING (ls tests/logos/pass/*.logos): 3874 = 191 + 3683, the partition closes.) PREVIOUSLY +1 (2026-09-17n-dynbinder LANDING: ONE pass fixture joined `nonglob`, bc_0917n_dynbinder_dyn_method_fn_binder_argument — the program of the CLOSED soundness_queue row dyn_method_fn_binder_argument_refuses (tier 3), legal Rust that rustc 1.98.1 --edition 2024 compiles and RUNS at exit 0 and that every binary before this commit REFUSED with `method 'pick' arg 1: variance mismatch`. It does not match wql_/deem_ so `glob` is unmoved at 191. The TEN new fail fixtures bc_0917n_dynbinder_hb_{x1,x2,x3,x4,x6,x7,b0,b1,b2,b3}_refuse are refusal twins and are NOT in this population; no door is declared by any of them, so `doors` and `nonglob_doors` are unmoved. RE-DERIVED BY DIRECT FILE LISTING (ls tests/logos/pass/*.logos): 3869 = 191 + 3678, the partition closes.) PREVIOUSLY +17 (2026-09-17l-matchplace LANDING: SEVENTEEN pass fixtures joined `nonglob`, none matching wql_/deem_ so `glob` is unmoved at 191. ONE is the program of the CLOSED soundness_queue row match_array_field_place_verifier_error_refused, bc_17l_row_match_array_field_place — legal Rust (rustc 1.98.1 --edition 2024 compiles and RUNS it, k=2 n=21, valgrind clean) that every binary before this commit REFUSED with an MLIR verifier error, 'llvm.getelementptr' op operand #0 on an array VALUE. SIXTEEN are hand-battery programs: bc_17l_hb_{b01_lit_arm,b03_rvalue,b04_through_ref,b06_loop,b08_drop_count,c01_matchexpr,c02_matchexpr_refmut,x13_refmut,x15_refmut_lit,a08_match_field_array,a09_tuple_elem_array,a03_arrfield_struct_elem,a07_dynfield_method,a11_localarray_control,x01_arrfield_copy,x04_matchfield_copy}. TWELVE of the seventeen MOVED base(refused or WRONG VALUE) -> landed(compiles and RUNS the right value); c02_matchexpr_refmut is the one that moved from a LIVE MISCOMPILE (compiled, exited 0, printed src=2 where rustc prints 99). The other FIVE are CONTROLS that already passed on the base binary and are landed to pin the neighbours which condemned two earlier arms: a03, a07, x01 (they refuted round 17k's `fatslarr`), a11 (the closed row's own local-array control) and c01 (the by-value twin separating c02's miscompile from a plain refusal). The round's THREE NEW queue rows are not in this population (match_array_elem_struct_binder_aliases_source, match_array_elem_array_binder_shape_lost_run, foreach_array_field_gep_verifier_refused live under tests/soundness/open/), and the closed row's program LEFT that directory in the same commit. DOOR counts unmoved (36 = 10 + 26): none of the seventeen declares a container family or a `direct` output form — they are array/tuple place-scrutinee match shapes only. RE-DERIVED BY DIRECT FILE LISTING, not by adding to the previous line: ls tests/logos/pass/*.logos | wc -l -> 3868; glob -> 191; 3868 - 191 = 3677.)  # +6 (2026-09-17j-ptrland2 LANDING: SIX pass fixtures joined `nonglob`. TWO are the programs of the CLOSED soundness_queue rows mutptr_region_param_elided_let_arg_refused (runs 0) and fnptr_call_result_region_param_reads_static_refused (runs 18) — both LEGAL Rust (rustc 1.98.1 --edition 2024 ACCEPTS and RUNS each at those exit codes) that every binary before this commit REFUSED with "variance mismatch — expected *mut &'static i64". FOUR are over-refusal controls for the new K::Ptr and K::FnPtr arms in collect_param_regions_, one per CARRIER: bc_0917j_ptrland2_hb_j01_pass (two-level *const), _hb_l05_pass (tuple), _hb_g02_pass (array) and _hb_l04_pass (bare fn pointer — the ONLY program in 38 that separates the FnPtr half from the Ptr half, unmoved under Ptr alone). All six verdicts MOVED base(refused) -> landed(compiles and RUNS the right value). The round's TWO fail fixtures — bc_0917j_ptrland2_hb_j{06,10}_refuse, use-after-scope escapes through carriers held on BASE only by the variance accident this commit deletes — are not in this population, the TWO closed rows' programs LEFT tests/soundness/open/, and one NEW row program JOINED it (ptr_array_in_tuple_param_gep_verifier_refused), which is also not in this population. None matches wql_/deem_, so `glob` is unmoved at 191. RE-DERIVED BY DIRECT FILE LISTING, not by adding to the previous line: ls tests/logos/pass/*.logos | wc -l -> 3851; glob -> 191; 3851 - 191 = 3660.)  # +3 (2026-09-17h-ptrland LANDING: THREE pass fixtures joined `nonglob` — bc_0917h_ptrland_hb_l1_pass, bc_0917h_ptrland_hb_l2_pass, bc_0917h_ptrland_hb_l4_pass. All three are LEGAL raw-pointer-to-reference programs (rustc 1.98.1 --edition 2024 ACCEPTS each; built and RUN at exit 0) kept as the OVER-REFUSAL control for the new `Ptr` arm in `type_is_ha`/`bc_holds_any_ref_type`: the arm makes the checker MORE conservative, so the expensive direction is refusing legal code, and these three are what prove it refuses none. Their verdict did NOT move (cc0 run0 on base AND armed). The round's THREE fail fixtures — bc_0917h_ptrland_hb_x{1,2,3}_refuse, the use-after-scope carriers varied by CARRIER (struct field, tuple field, array field) — are not in this population, and the CLOSED row's program LEFT tests/soundness/open/. None matches wql_/deem_, so `glob` is unmoved at 191. RE-DERIVED BY DIRECT FILE LISTING, not by adding to the previous line: ls tests/logos/pass/*.logos | wc -l -> 3845; glob -> 191; 3845 - 191 = 3654.)  # +3 (2026-09-17f-refpatland LANDING: THREE pass fixtures joined `nonglob` — bc_17f_refpat_tuple_row is the program of the CLOSED soundness_queue row ref_pattern_nested_in_tuple_binding_undefined_refused, which now compiles and RUNS exit 0; bc_17f_hb_tuple_admit is its PRINTING twin (got=6) and is the VALUE oracle, because the exit code is blind at this door — the sema half alone printed got=0 while still exiting 0; bc_17f_hb_refbind_admit is a LEGAL `ref` spelling (rustc rc 0) that the BASE binary refused with "undefined variable 'q'", now got=2 1. All three verdicts MOVED base(refused) -> landed(compiles and RUNS the right stdout). The round's FOUR fail fixtures — bc_17f_hb_{structfield,variantpayload,nestedtuple,atunderref}_refuse, the E0507 abuse twins with the CARRIER varied (struct field, enum variant payload, nested tuple element, whole value under the `&`) — are not in this population, and the closed row's program LEFT tests/soundness/open/. None matches wql_/deem_, so `glob` is unmoved at 191. RE-DERIVED BY DIRECT FILE LISTING, not by adding to the previous line: ls tests/logos/pass/*.logos | wc -l -> 3842; glob -> 191; 3842 - 191 = 3651.)  # +4 (2026-09-17d-arrlit LANDING: FOUR pass fixtures joined `nonglob` — all four are hand-battery LEGAL programs kept as the over-refusal control for the new `ArrLit` provenance arm (rustc ACCEPTS each and runs it 0): bc_esc_arrlit_param_elem_admit (array literal over a PARAMETER borrow), bc_esc_arrlit_inscope_admit (local borrow in an array literal, used IN SCOPE and never returned), bc_esc_arrlit_scalar_elem_admit (array literal of scalars — shares no borrow at all), bc_esc_arrlit_static_elem_admit (array literal over a STATIC borrow returned as 'static). Their verdict did NOT move (cc0 run 0 on base AND armed) — they are here to prove the arm refuses nothing legal. The round's FIVE fail fixtures and its TWO new soundness_queue programs are not in this population. None matches wql_/deem_, so `glob` is unmoved at 191. RE-DERIVED BY DIRECT FILE LISTING, not by adding to the previous line: ls tests/logos/pass/*.logos | wc -l -> 3839; glob -> 191; 3839 - 191 = 3648.)  # +4 (2026-09-16p-tupdoor LANDING: FOUR pass fixtures joined `nonglob` — 1 is the program of the CLOSED soundness_queue row (match_tuple_door_nested_struct_binds_nothing, whose `(P { x, y }, z)` now binds and RUNS `got=7`) and 3 are hand-battery programs whose verdict MOVED base(refused "undefined variable 'x'") -> landed(compiles and RUNS with the right stdout): bc_0916p_tupdoor_hb_h04_admit (struct under TWO tuple levels, got=10), bc_0916p_tupdoor_hb_h05_admit (MOVE-typed binder whose oracle is a destructor count, got=8 n=7), bc_0916p_tupdoor_hb_h06_admit (or-alt join, got=3). The round's ONE fail fixture (bc_0916p_tupdoor_hb_x02_refuse, the E0507 abuse twin) and its ONE new soundness_queue program (match_tuple_door_nested_slice_binds_nothing) are not in this population. None matches wql_/deem_, so `glob` is unmoved at 191. RE-DERIVED BY DIRECT FILE LISTING, not by adding to the previous line: ls tests/logos/pass/*.logos | wc -l -> 3835; glob -> 191; 3835 - 191 = 3644.)  # +7 (2026-09-16n-arrstore2 LANDING: SEVEN pass fixtures joined `nonglob` — 2 are the programs of the two CLOSED soundness_queue rows (bc_16n_row_array_store_value_reads_loan_refused, bc_16n_row_array_index_store_index_reads_loan_refused) and 5 are hand-battery programs whose verdict MOVED base(refused, nerr 2) -> landed(compiles and RUNS exit 0): bc_16n_hb_h03_admit (index AND value read the loan), bc_16n_hb_h05_admit (two loans), bc_16n_hb_h06_admit (nested array), bc_16n_hb_h07_admit (if-arm frame), bc_16n_hb_h08_admit (loop body). The round's TWO fail fixtures (bc_16n_hb_x02_refuse, bc_16n_hb_x04_refuse) and its ONE new soundness_queue program are not in this population. None matches wql_/deem_, so `glob` is unmoved at 191. RE-DERIVED BY DIRECT FILE LISTING, not by adding to the previous line: ls tests/logos/pass/*.logos | wc -l -> 3831; glob -> 191; 3831 - 191 = 3640.)  # +5 (2026-09-16l-refptee LANDING: FIVE pass fixtures joined `nonglob` — 2 are the programs of the two CLOSED soundness_queue rows (bc_16l_row_ref_to_rawptr_type_parse_refused, bc_16l_row_double_amp_lifetime_ref_type_parse_refused) and 3 are hand-battery programs whose verdict MOVED base(parse-refused) -> landed(compiles and RUNS exit 0): bc_16l_hb_l04_admit (&*const i64), bc_16l_hb_l05_admit (&mut *mut i64), bc_16l_hb_l11_admit (&&'a D, the AND-LIFETIME half). The round's TWO fail fixtures (bc_16l_hb_l09_refuse, bc_16l_hb_l14_refuse) and its FOUR new soundness_queue programs are not in this population. None matches wql_/deem_, so `glob` is unmoved at 191. RE-DERIVED BY DIRECT FILE LISTING, not by adding to the previous line: ls tests/logos/pass/*.logos | wc -l -> 3824; glob -> 191; 3824 - 191 = 3633.)  # +9 (2026-09-16j-arrpath2 LANDING: NINE pass fixtures joined — 2 are the programs of the two CLOSED soundness_queue rows (bc_16j_row_nested_destructure_admit, bc_16j_row_letfield_unbound_admit) and 7 are hand-battery programs whose verdict MOVED base -> landed (bc_16j_hb_y{01,04,07,09,10,12,13}_admit; y01 and y09 are the plainest spellings of the let door, which leaked EVERY element on base at n=0, and y10 is the deepest path any spelling in this arc has exercised, arr.0.t.0). The round's FIVE new soundness_queue programs are not in this population. None matches wql_/deem_, so `glob` is unmoved at 191. RE-DERIVED BY DIRECT FILE LISTING, not by adding to the previous line: ls tests/logos/pass/*.logos | wc -l -> 3819; glob -> 191; 3819 - 191 = 3628.)  # +2 (2026-09-16f-landboth LANDING: TWO pass fixtures joined — bc_0916f_landboth_hb_u04_admit and bc_0916f_landboth_hb_u05_admit, the two COST-DIRECTION controls of the ROW 2 arm (u04: the loan is of a DIFFERENT vec, read by the index; u05: the VALUE reads the loan through a call while the index is constant) — both rustc-ACCEPT and both must keep compiling. The round's 5 FAIL fixtures are not in this population, and the CLOSED soundness_queue row's program landed as a FAIL fixture, so it does not join either. Neither matches wql_/deem_, so `glob` is unmoved at 191. RE-DERIVED BY DIRECT FILE LISTING, not by adding to the previous line: ls tests/logos/pass/*.logos | wc -l -> 3804; glob -> 191; 3804 - 191 = 3613.)  # +11 (2026-09-16f-atbind LANDING: ELEVEN pass fixtures joined — 2 from CLOSED soundness_queue rows (bc_0916f_atbind_aggregate_admit, bc_0916f_atbind_byvalue_drop_admit) and 9 hand-battery programs that CAUGHT something (bc_0916f_atbind_hb_{a10,b01,b02,b06,b09,b11,c01,c04,c06}_admit; a10 is the control that REFUTED the first arm, c01/c06 are the two sites the handed-down framing never named). The round's 5 NEW soundness-queue programs are not in this population. None matches wql_/deem_, so `glob` is unmoved at 191. RE-DERIVED BY DIRECT FILE LISTING: ls tests/logos/pass/*.logos | wc -l -> 3802; glob -> 191; 3802 - 191 = 3611.)  # +10 (2026-09-16d-landrulings LANDING: FOURTEEN pass fixtures joined — 8 bc_0916d_dbmstruct_hb_q{01,02,03,04,07,08,11,12}_admit (the struct-shape binding-mode battery, q01 being the LEAK control), 3 bc_0916d_arrdecay_hb_a{01,02,04}_admit (the LEGAL array spellings that must keep working), bc_0916d_arrdecay_arrayref_to_arrayptr_admit (the program of the CLOSED soundness_queue row arrayref_to_arrayptr_coercion_refused), and 2 bc_0916d_refrel_hb_r{01,04}_admit; FOUR left — bc_0914a_ptrcoerce_hb_d01_admit, bc_0914b_ptrcoerceland_hb_m10_admit and bc_ptrcoerce_array_decay_admit became FAIL fixtures, bc_0914b_ptrcoerceland_hb_v02i_admit became a soundness_queue row. NET +10, ALL of them non-glob — none matches wql_/deem_, so `glob` is unmoved at 191, and none declares a container family or a `direct` output form, so `doors` and `nonglob_doors` are unmoved. The round's 6 FAIL fixtures and 4 soundness-queue programs are not in this population. RE-DERIVED BY DIRECT FILE LISTING, never by adding to the previous line: ls tests/logos/pass/*.logos | wc -l -> 3791; ls tests/logos/pass/{wql_*,deem_*}.logos | wc -l -> 191; partition closes: 3791 = 191 + 3600).  # +10 (2026-09-16c-armelem LANDING: 9 bc_armelem16c_hb_h*_admit hand-battery pass fixtures (h01 h02 h03 h10 h13 h14 h16 h17 h19) + bc_armelem16c_slice_arm_elems_admit, the program of the CLOSED soundness_queue row slice_pattern_arm_binding_extra_drops; ALL TEN joined `nonglob` — none matches the wql_/deem_ glob, so `glob` is unmoved at 191. The round's 3 NEW rows are soundness-queue programs, not in this population. RE-DERIVED BY DIRECT FILE LISTING: ls tests/logos/pass/*.logos | wc -l -> 3781; ls tests/logos/pass/{wql_*,deem_*}.logos | wc -l -> 191; partition closes: 3781 = 191 + 3590).  # +2 (2026-09-15h-boxdynref LANDING: boxdyn_mut_explicit_deref_arg (the closed soundness_queue row boxdyn_mut_explicit_deref_arg_no_vtable) and boxdyn_boxref_param_takes_amp_box (hand-battery ce07), BOTH joined `nonglob` — neither matches the glob, and neither declares a container family or a `direct` output form, so `glob`, `doors` and `nonglob_doors` are unmoved. The round's 3 FAIL halves are not in this population. RE-DERIVED BY DIRECT FILE LISTING: ls tests/logos/pass/*.logos | wc -l -> 3771; ls tests/logos/pass/{wql_*,deem_*}.logos | wc -l -> 191; partition closes: 3771 = 191 + 3580). +21 (2026-09-15g-aggtemp LANDING: 9 pass fixtures from the 11 closed soundness_queue rows (2 of the 11 became FAIL fixtures) + 12 bc_0915g_aggtemp_hb_g*_admit hand-battery pass fixtures, all joined `nonglob`; RE-DERIVED BY DIRECT FILE LISTING: ls tests/logos/pass/*.logos | wc -l -> 3769; ls tests/logos/pass/{wql_*,deem_*}.logos | wc -l -> 191; partition closes: 3769 = 191 + 3578).  # +3 (2026-09-15g-aggtemp pricing: bc_0915g_aggtemp_hb_b0{1,2,3}_admit, the hand programs that refuted the priced arms, all joined `nonglob`; RE-DERIVED BY DIRECT FILE LISTING: ls tests/logos/pass/*.logos | wc -l -> 3748; ls tests/logos/pass/{wql_*,deem_*}.logos | wc -l -> 191; partition closes: 3748 = 191 + 3557). +4 (2026-09-15f-rustcaudit, the rustc audit of the soundness queue's by-reading rows: bc_0915f_rustcaudit_{vec_index_store_len_hoisted,array_index_store_len_in_index,struct_lit_rawptr_field_static_region,struct_lit_mutref_field_local_region}_admit pass fixtures, all joined `nonglob`; RE-DERIVED BY DIRECT FILE LISTING: ls tests/logos/pass/*.logos | wc -l -> 3745; ls tests/logos/pass/{wql_*,deem_*}.logos | wc -l -> 191; partition closes: 3745 = 191 + 3554). +44 (2026-09-15f-consumeland, the landing that closed soundness_queue row byvalue_operator_operand_not_moved_double_drop: bc_0915f_consumeland_operator_operands_moved_admit + 43 bc_0915f_consumeland_hb_<id>_admit pass fixtures, all joined `nonglob`; RE-DERIVED BY DIRECT FILE LISTING: ls tests/logos/pass/*.logos | wc -l -> 3741; ls tests/logos/pass/{wql_*,deem_*}.logos | wc -l -> 191; partition closes: 3741 = 191 + 3550). +92 (2026-09-15d-argrefland, the landing that closed soundness_queue rows addr_of_struct_ref_local_arg / addr_of_ref_foreach_elem / addr_of_ref_match_binder / addr_of_ref_closure_capture / addr_of_ref_closure_param _passes_referent_run and struct_pattern_over_double_ref_reads_slot_run: 6 bc_argrefland_*_admit + 86 bc_0915d_argrefland_hb_<id>_admit pass fixtures, all joined `nonglob`; RE-DERIVED BY DIRECT FILE LISTING: ls tests/logos/pass/*.logos | wc -l -> 3697; ls tests/logos/pass/{wql_*,deem_*}.logos | wc -l -> 191; partition closes: 3697 = 191 + 3506). +4 (2026-09-15c-argref pricing: bc_0915c_argref_hb_{a09,a24,b02,b03}_admit; `ls tests/logos/pass/*.logos | wc -l` -> 3605, glob -> 191). +57 (2026-09-15b-refeqland, the reference-comparison landing that closed soundness_queue rows nested_ref_eq / ref_struct_eq / ref_pair_aggregate_eq / generic_ref_typevar_eq / ref_pair_ordering: 5 bc_refeqland_*_admit + 25 bc_0915a_refeq_hb_<id>_admit + 27 bc_0915b_refeqland_hb_<id>_admit pass fixtures, all joined `nonglob`; RE-DERIVED BY DIRECT FILE LISTING: ls tests/logos/pass/*.logos | wc -l -> 3601; ls tests/logos/pass/{wql_*,deem_*}.logos | wc -l -> 191; partition closes: 3601 = 191 + 3410). +86 (2026-09-15 shadowslot landing: 8 bc_shadowslot_*_admit + 33 bc_0914p_shadowslot_hb_<id>_admit + 45 bc_0915_shadowland_hb_<id>_admit pass fixtures). +53 (2026-09-14o-autoreffund, the autoref landing that closed soundness_queue's eight autoref rows and bc_admits issue-36082: 8 bc_autoreffund_*_admit + 45 bc_0914o_autoreffund_hb_<id>_admit pass fixtures; the two hb fail fixtures and the moved port are not in this population). +6 (2026-09-14m-storeedge-land, the store-edge landing that closed bc_admits row buffer-reuse-pattern-issue-147694 and soundness_queue rows vec_push_after_outer_push_store_rehomed_{admits,refused}, setter_field_store_after_outer_set_rehomed_{admits,refused}: the THREE pass halves bc_storeedge_{vec_push_after_outer_push,setter_field_after_outer_set,issue147694_twin}_admit and the THREE hand-battery catches bc_0914m_storeedgeland_hb_{s28,s57b,v13}_admit — all six joined `nonglob`, none matches the glob; the round's thirteen native fail fixtures, the port moved tests/imported/admit -> tests/imported/fail and the six new soundness_queue programs do not live in this population. RE-DERIVED BY DIRECT FILE LISTING, not by adding 6: ls tests/logos/pass/*.logos | wc -l -> 3405; ls tests/logos/pass/{wql_*,deem_*}.logos | wc -l -> 191; partition closes: 3405 = 191 + 3214.)  +17 (2026-09-14k-fnptrbinderland, the fn-pointer binder landing that closed bc_admits rows issue-54124 and issue-101280 and soundness_queue rows fnptr_elided_param_{let_from_named,return_from_named,nested_option}_admits, fnptr_item_named_binder_vs_fnptr_type_refused, fnptr_hrtb_sub_binder_to_named_return_refused: the SEVEN pass halves bc_fnptrbind_{let_named_as_elided,return_named_as_elided,item_binder_hrtb,hrtb_sub_to_named,nested_option_return,issue54124_twin,issue101280_twin}_admit and the TEN hand-battery catches bc_0914k_fnptrbinderland_hb_{c01,c02,c08,c12,c14,c15,c30,c36,c37,c39}_admit — all seventeen joined `nonglob`, none matches the glob; the round's ten native fail fixtures, the two closed rows' programs moved tests/imported/admit -> tests/imported/fail, and the one new soundness_queue program do not live in this population. RE-DERIVED BY DIRECT FILE LISTING, not by adding 17: ls tests/logos/pass/*.logos | wc -l -> 3399; ls tests/logos/pass/{wql_*,deem_*}.logos | wc -l -> 191; partition closes: 3399 = 191 + 3208.)  +13 (2026-09-14j-fnptrbinder, the fn-pointer binder pricing: 13 legal hand-battery catches landed as bc_0914j_fnptrbinder_hb_<id>_admit pass fixtures, each asserting its exit code on build c52dcb19dff987ff; the round's 9 new soundness_queue programs do not live in this population. None declares a container family or a `direct` door. RE-DERIVED BY DIRECT FILE LISTING, not by adding 13: ls tests/logos/pass/*.logos | wc -l -> 3382; ls tests/logos/pass/{wql_*,deem_*}.logos | wc -l -> 191; partition closes: 3382 = 191 + 3191.)  +33 (2026-09-14i-harvest2, the second hand-battery harvest — the batteries of rounds 2026-09-14e-thruref and 2026-09-14f-thrurefland, snapshot /home/logos/sandbox/hand-harvest-2026-09-14b, manifest src/compiler/probes/2026-09-14i-harvest2/MANIFEST.tsv: 33 legal catches landed as bc_0914{e_thruref,f_thrurefland}_hb_<id>_admit pass fixtures, each asserting its exit code on build c52dcb19dff987ff; the round's 41 bc_*_hb_*_refuse fail fixtures and its two new soundness_queue programs do not live in this population. None declares a container family or a `direct` door. RE-DERIVED BY DIRECT FILE LISTING, not by adding 33: ls tests/logos/pass/*.logos | wc -l -> 3369; ls tests/logos/pass/{wql_*,deem_*}.logos | wc -l -> 191; partition closes: 3369 = 191 + 3178.)  +184 (2026-09-14h-harvest, the hand-battery harvest: 184 legal catches of 43 earlier rounds landed as bc_<round>_hb_<id>_admit pass fixtures, each asserting its exit code and stdout on build c52dcb19dff987ff; the round's 180 bc_<round>_hb_<id>_refuse fail fixtures and its one new soundness_queue program do not live in this population. None declares a container family or a `direct` door. RE-DERIVED BY DIRECT FILE LISTING, not by adding 184: ls tests/logos/pass/*.logos | wc -l -> 3336; ls tests/logos/pass/{wql_*,deem_*}.logos | wc -l -> 191; partition closes: 3336 = 191 + 3145.)  +13 (2026-09-14f-thrurefland, the overwrite-through-reference + pointer-comparison landing that closed bc_admits row type-check-pointer-comparisons and soundness_queue rows refvar_assign_conflicts_with_pointee_field_loan_refused / place_assign_ref_field_while_pointee_loan_refused: the ELEVEN pass halves bc_ptrcmp_{constptr_mutref,fnptr_param,mutptr_regions,mutual_outlives,ref_peel,self_field}_admit and bc_thruref_{assign_generic_field,assign_mutref_cursor,assign_struct_ref_field,assign_tuple_ref_elem,place_retarget}_admit plus the TWO closed queue programs landed as bc_thruref_assign_ref_local_admit and bc_thruref_place_retarget_row_admit — all thirteen joined `nonglob`, none matches the glob; the round's thirteen native fail halves, the closed row's program moved tests/imported/admit -> tests/imported/fail, and the five new soundness_queue programs do not live in this population. RE-DERIVED BY DIRECT FILE LISTING, not by adding 13: the pass/*.logos set at HEAD 1862a9262 diffed by NAME against the worktree, both ways — 13 joined, 0 left, 0 of the joined match wql_*/deem_*; ls tests/logos/pass/*.logos | wc -l -> 3152; ls tests/logos/pass/{wql_*,deem_*}.logos | wc -l -> 191; partition closes: 3152 = 191 + 2961.)+12 (2026-09-14d-meetoblland, the meet-token landing that closed bc_admits rows regions-creating-enums3 and regions-glb-free-free--glb-free-free: the TWELVE pass halves bc_meet_{structlit_nongeneric,structlit_generic,enumlit,enumlit_aggregate_payload,enumlit_str_payload,enumlit_one_str,call,method_ret,self_field_store,invariant_elided_arg,struct_ptr_field,struct_ptr_invariant}_admit — all twelve joined `nonglob`, none matches the glob; the round's twelve native fail halves, the two closed rows' programs moved tests/imported/admit -> tests/imported/fail, and the four new soundness_queue programs do not live in this population. RE-DERIVED BY DIRECT FILE LISTING, not by adding 12: ls tests/logos/pass/*.logos | wc -l -> 3139; ls tests/logos/pass/{wql_*,deem_*}.logos | wc -l -> 191; partition closes: 3139 = 191 + 2948.)+15 (2026-09-14b-ptrcoerceland, the cross-kind-coercion + Vec-store-receiver landing that closed bc_admits rows type-check-pointer-coercions and borrowck-loan-vec-content: the THIRTEEN pass halves bc_ptrcoerce_{shared_to_const,unique_to_mut,mut_to_const,array_decay,mutref_to_ref,vec_to_slice,vec_to_mut_slice}_admit and bc_vecstore_{elem_loan,addrof_push,closure_beside_loan,iter_live,value_reads_loan,loop_loan}_admit plus the TWO pins bc_ptrcoerce_generic_struct_pointee_admit and bc_ptrcoerce_elided_mutptr_admit — all fifteen joined `nonglob`, none matches the glob; the round's thirteen native fail halves, the two closed rows' programs moved tests/imported/admit -> tests/imported/fail, and the fourteen new soundness_queue programs do not live in this population. RE-DERIVED BY DIRECT FILE LISTING, not by adding 15: ls tests/logos/pass/*.logos | wc -l -> 3127; ls tests/logos/pass/{wql_*,deem_*}.logos | wc -l -> 191; partition closes: 3127 = 191 + 2936.)+8 (2026-09-13f-declarrivalland, the declaration-arrival landing that closed bc_admits rows outlives-with-missing, constructor-lifetime-early-binding-error and trait-associated-constant: the EIGHT pass halves bc_declarrival_{where_undecl_method,where_undecl_impl_header,where_undecl_second_param,ctor_lt_turbofish,struct_lit_lt_turbofish,unit_variant_lt_turbofish,assoc_const_region,method_ret_slice_region}_admit — all eight joined `nonglob`, none matches the glob; the round's eight native fail halves, the three closed rows' programs moved tests/imported/admit -> tests/imported/fail, and the three new soundness_queue programs do not live in this population. RE-DERIVED BY DIRECT FILE LISTING, not by adding 8: ls tests/logos/pass/*.logos | wc -l -> 3112; ls tests/logos/pass/{wql_*,deem_*}.logos | wc -l -> 191; partition closes: 3112 = 191 + 2921.)+14 (2026-09-13d-staticdemand, the 'static-demand class landing that closed bc_admits rows issue-69114-static-mut-ty and regions-static-bound: the FOURTEEN pass halves bc_staticdemand_{where_static_param,transitive_bound,second_arg,method_bound,path_call,generic_fn,dyn_method,bound_tparam_method,deferred_generic,method_nonstatic_bound,static_mut_elided,static_mut_param,static_mut_tuple,static_mut_place}_admit — all fourteen joined `nonglob`, none matches the glob; the round's fourteen native fail halves, the two closed rows' programs moved tests/imported/admit -> tests/imported/fail, and the nine new soundness_queue programs do not live in this population. RE-DERIVED BY DIRECT FILE LISTING, not by adding 14: ls tests/logos/pass/*.logos | wc -l -> 3104; ls tests/logos/pass/{wql_*,deem_*}.logos | wc -l -> 191; partition closes: 3104 = 191 + 2913.)  +12 (2026-09-13b-selfregion-land, the impl-self-region class landing that closed bc_admits rows issue-55394--b and issue-98170: the TWELVE pass halves bc_selfregion_{anon_header_ret,anon_pick,anon_trait_default_pick,generic_selflit,let_self_annot,ref_target_let_self,selflit_fn_binder,self_struct_variant,self_tuple_variant,self_unit_variant,two_anon,typearg_anon_let_self}_admit — all twelve joined `nonglob`, none matches the glob; the round's eleven native fail halves, the two closed rows' programs moved tests/imported/admit/nll -> tests/imported/fail/nll, and the one new soundness_queue program do not live in this population. None declares a container family or a `direct` door. RE-DERIVED BY DIRECT FILE LISTING, not by adding 12: ls tests/logos/pass/*.logos | wc -l -> 3090; ls tests/logos/pass/{wql_*,deem_*}.logos | wc -l -> 191; partition closes: 3090 = 191 + 2899.)  +6 (2026-09-12r-escroot, call-result region instantiation + the positive let door that closed bc_admits rows method-ufcs-inherent-3, method-ufcs-inherent-4 and regions-free-region-ordering-caller1: the SIX pass halves bc_escroot_call_result_static, bc_escroot_ref_local_index, bc_letbind_call_param_named, bc_letbind_temp_anon, bc_static_call_callee_region_instantiated, bc_method_call_fn_binder_instantiated — all six joined `nonglob`, none matches the glob; the round's six native fail halves, the three closed rows' programs moved tests/imported/admit -> tests/imported/fail, and the eight new soundness_queue programs do not live in this population. RE-DERIVED BY DIRECT FILE LISTING, not by adding 6: ls tests/logos/pass/*.logos | wc -l -> 3078; ls tests/logos/pass/{wql_*,deem_*}.logos | wc -l -> 191; partition closes: 3078 = 191 + 2887.)  +3 (2026-09-12n-bcdoor, the operand-position `&<temporary>` minting-site class landing that closed bc_admits row temporary-lifetime-extension-tuple-ctor: the THREE pass halves bc_bcdoor_fnptr_admit, bc_bcdoor_closure_admit and bc_bcdoor_argborrow_admit (the last is the over-refusal guard — a TEMPORARY receiver whose result borrows the ARGUMENT, legal, and it must keep admitting) — all three joined `nonglob`, none matches the glob; the round's three native fail halves (bc_bcdoor_{fnptr,closure,argborrow}_refuse) and the closed row's own program, moved tests/imported/admit/lifetimes -> tests/imported/fail/lifetimes/, do not live in this population. RE-DERIVED BY DIRECT FILE LISTING, not by adding 3: ls tests/logos/pass/*.logos | wc -l -> 3072; ls tests/logos/pass/{wql_*,deem_*}.logos | wc -l -> 191; partition closes: 3072 = 191 + 2881.)  # +6 (2026-09-12k-uninitborrow, the definite-assignment class landing that closed bc_admits row move-of-addr-of-mut: the SIX pass halves bc_uninitborrow_{shared,mut,indexwrite,indexcompound}_admit, bc_uninitborrow_bothbranches_admit and bc_uninitborrow_shadow_admit — all six joined `nonglob`, none matches the glob; the round's five native fail halves (bc_uninitborrow_{shared,mut,indexwrite,indexcompound,onebranch}_refuse), the closed row's own program moved tests/imported/admit/moves -> tests/imported/fail/moves, and the one new soundness_queue program (loop_init_before_break_refuses) do not live in this population. RE-DERIVED BY DIRECT FILE LISTING, not by adding 6: ls tests/logos/pass/*.logos | wc -l -> 3069; ls tests/logos/pass/{wql_*,deem_*}.logos | wc -l -> 191; partition closes: 3069 = 191 + 2878.)  # +3 (2026-09-12i-loanbackedge, the loop back-edge loan-retirement class landing that closed bc_admits row borrowck-lend-flow-loop: the THREE pass halves bc_backedge_reassign_holder_admit (the closed row's program with one line deleted), bc_backedge_field_use_below_raise_admit (the field member's Q>P control) and bc_backedge_two_holders_admit (an over-refusal the same change REPAIRS) — all three joined `nonglob`, none matches the glob; the round's one native fail half (bc_backedge_field_lend_flow_refuse), the closed row's own program moved tests/imported/admit/borrowck -> tests/imported/fail/borrowck, and the two new soundness_queue programs do not live in this population. RE-DERIVED BY DIRECT FILE LISTING, not by adding 3: ls tests/logos/pass/*.logos | wc -l -> 3063; ls tests/logos/pass/{wql_*,deem_*}.logos | wc -l -> 191; partition closes: 3063 = 191 + 2872.)  # +5 (2026-09-12b-rpitbound, the `-> impl Trait` return-bound check that closed bc_admits row borrow-immutable-upvar-mutation-impl-trait: the FIVE pass halves bc_rpitbound_{kind_mut,kind_move,trait,sig_arity}_admit and bc_rpitbound_hidden_typaram_admit (the DEFERRAL guard, a hidden type that is still a TypeVar) — all five joined `nonglob`, none matches the glob; the round's five native fail halves (bc_rpitbound_{kind_mut,kind_move,trait_unimpl,trait_uncalled,sig_arity}_refuse), the closed row's own program moved tests/imported/admit/borrowck -> tests/imported/fail/borrowck, and the two new soundness_queue programs do not live in this population. RE-DERIVED BY DIRECT FILE LISTING, not by adding 5: ls tests/logos/pass/*.logos | wc -l -> 3060; ls tests/logos/pass/{wql_*,deem_*}.logos | wc -l -> 191; partition closes: 3060 = 191 + 2869.)  # +5 (2026-09-12f-argresvassign, the `Code::Assign` reservation arm that closed bc_admits row issue-27868: the FIVE pass halves bc_argresvassign_{scalar,struct,array,shadow,twophase_recv}_admit — all five joined `nonglob`, none matches the glob; the round's three native fail halves (bc_argresvassign_{scalar,struct,array}_refuse) and the closed row's own program, moved tests/imported/admit/nll/issue-27868 -> tests/imported/fail/nll/, do not live in this population. RE-DERIVED BY DIRECT FILE LISTING, not by adding 5: ls tests/logos/pass/*.logos | wc -l -> 3055; ls tests/logos/pass/{wql_*,deem_*}.logos | wc -l -> 191; partition closes: 3055 = 191 + 2864.)  # +2 (2026-09-12b-dwvardoor, the plain-deref-write door of `lifereg.B`: the TWO pass halves bc_lifereg_derefwrite_samelt_admit and bc_lifereg_derefwrite_field_ptr_samelt_admit — both joined `nonglob`, neither matches the glob; their two fail twins (bc_lifereg_derefwrite_param_escape_fail, bc_lifereg_derefwrite_field_ptr_escape_fail) do not live in this population, and the round DELETED soundness_queue row lifereg_deref_store_param_admits while OPENING two others, whose programs are not registered tests. RE-DERIVED BY DIRECT FILE LISTING, not by adding 2: ls tests/logos/pass/*.logos | wc -l -> 3050; ls tests/logos/pass/{wql_*,deem_*}.logos | wc -l -> 191; partition closes: 3050 = 191 + 2859.)  # +2 (2026-09-12a-taggedidentity, the `tagged_enums_` identity landing: ONE new pass half coex_tagged_enum_bare_key, joined `nonglob`, does not match the glob; the round declared NO new fail fixture and closed NO soundness_queue row. ⚠ THE OTHER +1 IS A DRIFT THE PRECEDING COMMIT 7b3ee3277 LEFT BEHIND: it added coex_enum_bare_key and did not move this pin, so this gate and logos_00_census_pin have been RED at HEAD since — exactly the 7eabc226a case one line up, twice in two days. RE-DERIVED BY DIRECT FILE LISTING, not by adding 2: ls tests/logos/pass/*.logos | wc -l -> 3048; ls tests/logos/pass/{wql_*,deem_*}.logos | wc -l -> 191; partition closes: 3048 = 191 + 2857.)  # +3 (2026-09-11-optboxfield, the sema niche-arm pkg-key landing that closed soundness_queue row layout_verify_optbox_struct_field: the THREE pass halves option_ptr_wrapper_niche_struct_field, option_ptr_wrapper_niche_struct_field_ctl and option_box_recursive_struct_field_list (the closed row's own program) — all three joined `nonglob`, none matches the glob; the round declared NO new fail fixture. RE-DERIVED BY DIRECT FILE LISTING, not by adding 3: ls tests/logos/pass/*.logos | wc -l -> 3046; ls tests/logos/pass/{wql_*,deem_*}.logos | wc -l -> 191; partition closes: 3046 = 191 + 2855.)  # +4 (2026-09-11-bufdrop, the owning-buffer drop-glue class landing: the FOUR pass halves bufwriter_{drop_flushes_buffer,close_then_drop_flushes_once} and btreemap_{drop_frees_storage,free_then_drop_frees_once} — all four joined `nonglob`, none matches the glob; the round declared NO new fail fixture and closed NO soundness_queue row. RE-DERIVED BY DIRECT FILE LISTING, not by adding 4: ls tests/logos/pass/*.logos | wc -l -> 3043; ls tests/logos/pass/{wql_*,deem_*}.logos | wc -l -> 191; partition closes: 3043 = 191 + 2852.)  # +1 (2026-09-11b-vgleak, the borrowed-`Box<dyn>` landing: the ONE pass half boxdyn_borrow_arg_keeps_box_drop joined `nonglob`, does not match the glob; its fail twin of the same name and the two new soundness_queue programs (boxdyn_mutborrow_arg_no_vtable, rcdyn_borrow_arg_no_vtable) do not live in this population. RE-DERIVED BY DIRECT FILE LISTING, not by adding 1: ls tests/logos/pass/*.logos | wc -l -> 3039; ls tests/logos/pass/{wql_*,deem_*}.logos | wc -l -> 191; partition closes: 3039 = 191 + 2848. DOOR counts unmoved.)  # +2 (2026-09-11e, the `lifereg.B` per-path landing: the TWO carriers bc_lifereg_path_sibling_admit and bc_lifereg_path_rewrite_admit — both joined `nonglob`, neither matches the glob; the round's one native fail fixture is imported (tests/imported/fail/lifetimes/mut-slice-struct-lifetime-transmute--c17) and its one imported pass half do not live in this population, and the two new soundness_queue programs are not registered tests)  # +5 (2026-09-11c, the closure-literal capture-list landing: the FIVE pass halves bc_capsof_iife_{loan_nll_admit,loop_nll_admit,nested_nll_admit,param_return_admit,scalar_result_admit} — all five joined `nonglob`, none matches the glob; the round's five native fail halves and its one imported fail fixture do not live in this population)  # +6 (2026-09-11b, the E0207 landing: the SIX pass halves bc_e0207_{assoc_names_static_not_binder,binder_unused_is_legal,nested_in_assoc_type_ok,nested_trait_arg_constrains,self_ref_constrains,trait_arg_constrains} — all six joined `nonglob`, none matches the glob; the round's four native fail halves and its three imported fail fixtures do not live in this population)  # +1 (2026-09-11j, the dyn-vtable package-key landing: `coex_dyn_bare_key`, the multi-package carrier ADDED BY THE PRECEDING COMMIT 7eabc226a which did not move this pin; joined `nonglob`, does not match the glob; the round declared NO new fail fixture)  # +2 (2026-09-11i, the alias-lookup-order landing: alias_local_shadows_imported_homonym{,_control}, both joined `nonglob`, neither matches the glob; the round declared NO new fail fixture)
    'glob'              : 191,   # `wql_*` + `deem_*` — pull_shape's population
    'nonglob'           : 4008,  # +2 (2026-09-25: boxed_escaping_fnonce_capture_double_free, boxed_fnonce_consume_shapes, squeue #137.) PREVIOUSLY +5 (2026-09-25: closure_owned_dyn_capture, boxed_move_closure_fat_capture_env_overflow, impl_fn_return_stack_env_dangles, closure_owned_dyn_capture_drops_once, closure_slice_capture_shapes, squeue #100 #88 #136.) PREVIOUSLY +5 (2026-09-25: rawptr_tuple_eq_no_eq_impl_refused, array_temp_elem_field_store_refused, rawptr_comparison_shapes, temp_rooted_place_assign_shapes, temp_rooted_place_assign_mut_ref, squeue #239 #298.) PREVIOUSLY +2 (2026-09-25: tuple_enum_elem_eq_variadic_impl_return_lost_refused, tuple_enum_elem_eq_shapes, squeue #274.) PREVIOUSLY +2 (2026-09-25: ref_typearg_eq_impl_missing_refused, ref_typearg_comparisons_through_refs, squeue #270.) PREVIOUSLY +1 (2026-09-25: operator_traits_on_primitives.) PREVIOUSLY +3 (2026-09-25: operator_trait_rhs_type_param_refused, operator_trait_output_shapes, operator_generic_output_projection, squeue #291.) PREVIOUSLY +1 (2026-09-25: localvec_for_ref_own_into_iter, squeue #163.) PREVIOUSLY +2 (2026-09-25: i128_literal_promotion_refused, const_promotion_wide_integers, squeue #476.) PREVIOUSLY +3 (2026-09-25: struct_lit_const_promotion_refused, const_promotion_aggregates, deref_dyn_place_legal_spellings, squeue #475 #91.) PREVIOUSLY +3 (2026-09-25: ifexpr_arm_extended_field_borrow_refused, match_arm_extended_temp_borrow, uninit_assigned_in_ifexpr_arm_drop_flag, squeue #297.) PREVIOUSLY +2 (2026-09-25: closure_elided_param_called_with_field_ref_refuses, closure_elided_param_called_with_named_ref, squeue #187.) PREVIOUSLY +1 (2026-09-25: intlit_unsuffixed_adopts_overload, squeue #160.) PREVIOUSLY +4 (2026-09-24: box_vec_new_infer, struct_lit_field_vec_new_infer_refused, generic_user_indexmut_store_refused (squeue #107 #211 #222 closed), expected_type_flows_into_builders.) PREVIOUSLY +5 (2026-09-24: struct_named_like_type_param_refused, self_tuple_struct_ctor_refused, enum_tuple_ctor_as_fn_value_refused (squeue #207 #185 #208 closed), constructors_as_values, type_param_named_like_struct_annotated.) PREVIOUSLY +11 (2026-09-24: refbind_over_ref_scrutinee_deref_coerced_call, call_arg_deref_coercion_double_ref, boxref_sized_struct_deref_coercion_missing, field_read_through_ref_to_box_refused, index_through_ref_to_vec_let_refuses, vec_index_store_through_mutref_param_refused, method_autoderef_double_ref_option_infer_refused (squeue #301 #90 #279 #183 #209 #283 closed), deref_coercion_sites, index_through_vec_refs, whole_scrutinee_binder_through_refs, option_methods_through_refs.) PREVIOUSLY +9 (2026-09-24: let_array_pattern_ref_mut_binding_is_raw_pointer_refuses, let_tuplestruct_generic_elem_type_unsubstituted, match_tmp_wild_mut_addrof, slice_rest_ref_binder_syntax, variant_tuple_door_struct_shape_sub_unresolved (squeue #458 #126 #109 #145 #300 closed), let_generic_and_ref_array_patterns, nested_struct_variant_in_tuple_payload, match_temp_scrutinee_mut_binders, array_named_rest_binds_subslice.) PREVIOUSLY +3 (2026-09-24: field_base_temp_built_before_sibling_run (squeue #263 closed), temp_field_base_eval_order_ops_calls, temp_field_base_eval_order_literals.) PREVIOUSLY +1 (2026-09-24: ptr_same_region_compare_ok.) PREVIOUSLY +1 (2026-09-24: vec_iter_mut_store_outliving_ok.) PREVIOUSLY +1 (2026-09-24: refcell_push_outliving_refs_ok.) PREVIOUSLY +2 (2026-09-24: at_binding_ref_mut_payload_refuses, bc_patmovebind_at_ref_binding_twin (squeue #117 #451 closed).) PREVIOUSLY +1 (2026-09-24: fnitem_array_elem_static_return_ok.) PREVIOUSLY +3 (2026-09-24: let_at_binding_type_annot_syntax (squeue #118 closed), tuple_let_drop_timing, let_pattern_temporary_drops.) PREVIOUSLY +8 (2026-09-24: fnparam_struct_ref_mut_field_binds_byvalue, closure_param_struct_pattern_syntax (squeue #120 #121 closed), fnparam_patterns_any_shape, fnparam_pattern_skipped_parts_drop, closure_untyped_pattern_params, let_struct_nested_subpatterns, pattern_binders_drop_reverse_source_order, fnparam_array_drop_rest_unbound (moved from fail/).) PREVIOUSLY +5 (2026-09-24: variant_payload_nested_at_pattern_refused (squeue #306 closed), variant_payload_at_structural_sub, variant_payload_at_expr_and_while_let, match_expr_ref_scrutinee_struct_sub, match_expr_payload_destructure_drops.) PREVIOUSLY +4 (2026-09-24: for_header_pattern_tuple_only (squeue #122 closed), for_header_irrefutable_patterns, for_header_binding_modes, for_header_ref_tuple_drop.) PREVIOUSLY +5 (2026-09-24: let_ref_bind_annotated_refused, let_double_ref_str_annotation_refused, ref_to_slice_ref_param_unsized_refused (squeue #264 #268 #277 closed), let_ref_annotated_temp_drop, multi_ref_slice_types.) PREVIOUSLY +3 (2026-09-24: two ref-pattern rows and let_ref_patterns.) PREVIOUSLY +4 (2026-09-24: two let-pattern rows and two neighbour fixtures.) PREVIOUSLY +1 (2026-09-24: tuple_in_tuple_place_assign.) PREVIOUSLY +3 (2026-09-24: two assignment rows and nested_place_and_paren_assign.) PREVIOUSLY +2 (2026-09-24: labeled_foreach_label_lost, labeled_foreach_forms.) PREVIOUSLY +5 (2026-09-24: three array-pattern rows and two neighbour fixtures.) PREVIOUSLY +1 (2026-09-24: match_array_nested_array_subpattern_mlirgen_malfunction.) PREVIOUSLY +4 (2026-09-24: three array-binder rows and array_binders_keep_shape.) PREVIOUSLY +5 (2026-09-24: #129 #79 and three neighbour fixtures.) PREVIOUSLY +1 (2026-09-24: struct_pat_ref_and_local_shadow_twins.) PREVIOUSLY +2 (2026-09-24: tier2_refusal_admit_twins, array_repeat_operand_evaluated_once.) PREVIOUSLY +3 (2026-09-24: the three enum_payload_partial_move_* fixtures.) PREVIOUSLY +1 (2026-09-24: arraylit_cast_target_elem_type.) PREVIOUSLY +3 (2026-09-24: squeue #262 and two sibling-exit neighbour fixtures.) PREVIOUSLY +1 (2026-09-24: generic_enum_ctor_inferred_in_literals.) PREVIOUSLY +6 (2026-09-24: the three eq-address rows and three eq_* neighbour fixtures.) PREVIOUSLY +4 (2026-09-24: the four weak_* fixtures, squeue weak_local_never_dropped closed.) PREVIOUSLY +4 (2026-09-24: the four closure_fnonce_cond_* fixtures, squeue #141 closed.) PREVIOUSLY +2 (2026-09-24: generic_operator_operands_moved_admit (squeue #284 closed) and generic_operator_copy_operands_reusable.) PREVIOUSLY +4 (2026-09-24: let_array_pattern_remainder_group_order_admit, let_array_pattern_temp_source_remainder_admit (squeue #92, #93 closed), partially_moved_array_drop_order and let_array_pattern_temp_source_drops.) PREVIOUSLY +5 (2026-09-24: variant_payload_nested_struct_sub_by_ref_admit, let_tuple_destructure_ref_scrutinee_admit (squeue #135, #115 closed), variant_payload_nested_binding_modes, let_tuple_over_ref_binding_modes and let_struct_over_ref_binding_modes.) PREVIOUSLY +2 (2026-09-24: tuplestruct_door_default_ref_mode_admit (squeue #144 closed) and tuplestruct_default_binding_modes.) PREVIOUSLY +2 (2026-09-23: field_base_temporaries_order_and_moves and if_expr_cond_temporaries_drop_first.) PREVIOUSLY +2 (2026-09-23: param_ref_struct_cast_after_addr_of_admit (squeue row closed) and param_ref_reads_after_addr_of.) PREVIOUSLY +4 (2026-09-23: tuple_elem_shared_borrow_through_ref_admit, tuple_elem_borrows_are_addresses, tuple_aggregate_elem_borrow_identity and tuple_elem_through_field_ref_writes.) PREVIOUSLY +2 (2026-09-23: while_body_return_move_dropped_on_fallthrough_admit (squeue #470 closed) and loop_body_move_exit_paths.) PREVIOUSLY +2 (2026-09-23: self_call_in_return_param_dropped_admit (squeue #86 closed) and param_moved_on_one_branch_drops_on_others.) PREVIOUSLY +2 (2026-09-23: static_array_whole_borrow_addresses_global_admit (squeue row closed) and static_array_borrows_address_global.) PREVIOUSLY +4 (2026-09-23: fru_temporary_base_unmoved_field_dropped_admit, fnptr_field_call_on_temporary_dropped_admit (squeue #295, #294 closed), fru_and_field_call_temporaries and generic_struct_update_turbofish.) PREVIOUSLY +2 (2026-09-23: loop_break_value_array_lit_admit (squeue #285 closed) and loop_break_value_moves_and_copies.) PREVIOUSLY +3 (2026-09-23: foreach_array_elements_owned_by_loop_admit (squeue #293 closed), foreach_array_ownership_paths and foreach_array_labelled_break_drops_tail.) PREVIOUSLY +2 (2026-09-23: index_place_through_refmut_drops_old_admit (squeue row closed) and index_store_through_refmut_drop_old.) PREVIOUSLY +4 (2026-09-23: match_expr_array_pattern_struct_elems_admit, at_binding_or_subpattern_enum_admit (squeue #290, #302 closed), match_expr_array_pattern_binders and at_binding_or_alternatives.) PREVIOUSLY +3 (2026-09-23: closure_param_shadow_drop_by_slot_admit, match_arm_nested_binder_shadow_drop_by_slot_admit (squeue #265, #266 closed) and shadow_binders_drop_by_slot.) PREVIOUSLY +3 (2026-09-23: refptr_param_eq_compares_pointers_admit, refptr_inner_region_param_eq_admit and ref_pair_pointer_eq_compares_through.) PREVIOUSLY +2 (2026-09-23: letelse_or_pattern_scalar_alts_admit (squeue row closed) and letelse_or_pattern_scalar_kinds.) PREVIOUSLY +2 (2026-09-23: variant_payload_char_literal_match_admit (squeue #111 closed) and char_payload_patterns_decode_scalar.) PREVIOUSLY +8 (2026-09-23: self_assign_after_move_admit, letstruct_partial_destructure_moves_fields_admit, loop_init_before_break_admit, deferred_init_after_inner_shadow_admit, let_at_binding_struct_pattern_moved_admit (squeue #132/#125/#176/#267/#305 closed), deferred_init_shadow_drop_state_per_binding, for_mut_vec_ref_yields_mut_elem and dyn_coerce_scalar_implementor.) PREVIOUSLY +1 (2026-09-23: fn_bound_hr_param_outer_ret_admit)  # +1 (2026-09-23: assoc_bound_eq_self_supertrait_admit)  # +1 (2026-09-23: struct_bound_lifetime_in_signature_admit)  # +1 (2026-09-23: trait_default_body_template_admit)  # +1 (2026-09-23: cell_get_copy_handles_admit)  # +2 (2026-09-23: alias_hole_twice_same_region_admit, tuple_copy_clone_bounds_admit)  # +3 (2026-09-23: closure_hr_ret_capture…, tuplestruct_ctor…_admit, anon_region_let_annotation…_admit)  # +2 (2026-09-23: writ_link_cycle_admit, writ_link_foreign_arena_panics)  # +1 (2026-09-23: let_underscore_lifetime_annotation_admit)  # +1 (2026-09-23: let_mutref_annotation_to_meet_struct_admit, see 'corpus')  # +0/-1 (2026-09-21 ADR 0028 blocker triage: ONE pass fixture LEFT `nonglob` — bc_d1r9_f0_retarget_overlap_admit, which pinned a rule Rust does not have and is now soundness-queue row #449. No wql_/deem_ match and no door, so `glob` and the door counts are unmoved. RE-DERIVED FROM logos_00_population_pin_lint's DIRECT LISTING: corpus 3895 = glob 191 + nonglob 3704.) PREVIOUSLY +1 (2026-09-20 ADR 0029 S2, the captures in the closure TYPE: ONE pass fixture joined `nonglob`, closure_send_not_from_sibling — a Send literal beside a `*mut`-capturing sibling of the identical signature, REFUSED before the env left the signature-keyed union and entered the type. Its fail twin closure_dyn_send_needs_own_env is a refusal and not in this population. No wql_/deem_ match and no door, so `glob` and the door counts are unmoved. RE-DERIVED FROM logos_00_population_pin_lint's DIRECT LISTING: corpus 3896 = glob 191 + nonglob 3705.) PREVIOUSLY +2 (2026-09-20 #442 and #105, the per-instantiation closure id: TWO pass fixtures joined `nonglob`, generic_closure_id_collides_across_instances and closure_in_generic_two_insts — the programs of two soundness-queue rows closed by this round, moved from tests/soundness/open/. Both are a closure inside a generic fn instantiated at two types, which did not COMPILE before the fix (`redefinition of symbol named '__closure_0'`). No wql_/deem_ match and no door, so `glob` and the door counts are unmoved. RE-DERIVED FROM logos_00_population_pin_lint's DIRECT LISTING: corpus 3895 = glob 191 + nonglob 3704.) PREVIOUSLY +1 (2026-09-20 `where &T: Trait` receiver autoref: ONE pass fixture joined `nonglob`, where_ref_subject_reads_self — the twin of imported where-clause-ref-bound-b158 whose impl bodies READ `self`, rc 139 before the fix. No wql_/deem_ match and no door, so `glob` and the door counts are unmoved. RE-DERIVED FROM logos_00_population_pin_lint's DIRECT LISTING: corpus 3893 = glob 191 + nonglob 3702.) PREVIOUSLY +1 (2026-09-20 #438 step 8 (#15): ONE pass fixture joined `nonglob`, dyn_vtable_homonym_target — two packages of one module, same `$M`, whose vtable is keyed by the trait's identity and the method by the impl's package. No wql_/deem_ match and no door, so `glob` and the door counts are unmoved. RE-DERIVED FROM logos_00_population_pin_lint's DIRECT LISTING: corpus 3892 = glob 191 + nonglob 3701.) PREVIOUSLY +1 (2026-09-19 ADR 0028 slice 2, exact callees + explicit autoref: ONE pass fixture joined `nonglob`, vec_index_store_value_borrows_vec_admitted, the program of soundness-queue row vec_index_store_value_borrows_vec_refused (#218) closed by this round. No wql_/deem_ match and no door. RE-DERIVED FROM logos_00_population_pin_lint's DIRECT LISTING: corpus 3891 = glob 191 + nonglob 3700.) PREVIOUSLY +1 (2026-09-19 ADR 0028 region-erased impl keys: ONE pass fixture joined `nonglob`, trait_impl_for_ref_method_call_receiver, the program of soundness-queue row trait_impl_for_ref_method_call_receiver_refused (#188) closed by this round. No wql_/deem_ match and no door, so `glob` and the door counts are unmoved. RE-DERIVED FROM logos_00_population_pin_lint's DIRECT LISTING: corpus 3890 = glob 191 + nonglob 3699.) PREVIOUSLY +5 (2026-09-19 ADR 0028 DerefMove + declared-signature elision: FIVE pass fixtures joined `nonglob` — box_field_move, struct_binder_result and box_mutref_payload_write_immut_box (three soundness-queue rows closed, moved from tests/soundness/open/), box_field_move_siblings and trait_default_next_item_binder (new). The round's sixth fixture, fail/box_field_move_twice, is not in this population. None matches wql_/deem_ and none declares a door, so `glob` and the door counts are unmoved. Merged onto 2026-09-18-partial-cmp-return (+1): corpus 3889 = glob 191 + nonglob 3698, RE-DERIVED FROM logos_00_population_pin_lint's DIRECT LISTING.) PREVIOUSLY +1 (2026-09-18-partial-cmp-return LANDING: ONE pass fixture joined `nonglob`, partial_cmp_ordering_forms_admitted — the ADMIT twin for #430/#432, pinning that `partial_cmp -> Ordering`, `-> Option<Ordering>` and a direct `lt` impl all keep working now that sema refuses to emit a comparison helper whose lookup it never resolved. ⚠ THE ROUND ADDED THREE FIXTURES: the other two are fail/partial_cmp_non_ordering_return_refused and fail/partial_cmp_option_foreign_payload_refused, and `fail` fixtures are NOT in this population — which is why this pin moves +1 while the census registry moves by more. Neither matches wql_/deem_ so `glob` is unmoved at 191. RE-DERIVED FROM THE GATE'S OWN PRINTED MEASUREMENT: it printed `corpus 3884 = glob 191 + nonglob 3693 · doors 36 = glob 10 + nonglob 26`, and DOOR IMMOBILITY at 36 = 10 + 26 is what makes re-deriving the population legitimate rather than buying a row.) PREVIOUSLY +1 (2026-09-18-struct-operator LANDING: ONE pass fixture joined `nonglob`, struct_relational_impl_admitted — the ADMIT twin of #427's refusal, pinning that a struct WITH the operator method, a struct with a generic impl, and the primitive paths all keep working now that sema refuses an operator whose method the type never implements. ⚠ THE ROUND ADDED TWO FIXTURES: the other is fail/struct_relational_no_impl_refused, and a `fail` fixture is NOT in this population — which is why this pin moves +1 while the census registry moves +2. Neither matches wql_/deem_ so `glob` is unmoved at 191. RE-DERIVED FROM logos_00_population_pin_lint's DIRECT LISTING (corpus 3883 = glob 191 + nonglob 3692) and cross-checked by an independent `ls tests/logos/pass/*.logos`; the door census itself could not be read in the same run — it exited 2 (facts from a different tree) because the compiler was rebuilt, so DOOR IMMOBILITY IS VERIFIED SEPARATELY after the corpus run regenerates the facts.) PREVIOUSLY +1 (2026-09-18-array-order LANDING: ONE pass fixture joined `nonglob`, array_relational_admitted — the ADMIT twin of #377's refusal, pinning the seven comparison shapes that must keep working now that sema refuses `<` on an aggregate the lowering cannot order (scalars, array `==`/`!=`, all-primitive tuples, `&i32`, C-like enums, `str`, a struct with `lt`). ⚠ THE ROUND ADDED TWO FIXTURES, NOT ONE: the other is fail/array_relational_refused, and a `fail` fixture is NOT in this population — which is why this pin moves +1 while the census registry moves +2. Neither matches wql_/deem_ so `glob` is unmoved at 191, and neither declares a door, so `doors` and `nonglob_doors` are unmoved at 36 = 10 + 26 — the fold re-measured both in the same run. RE-DERIVED BY DIRECT FILE LISTING (ls tests/logos/pass/*.logos): 3882 = 191 + 3691, the partition closes, and the listing was run INDEPENDENTLY of the gate, which printed the same 3882/3691.) PREVIOUSLY +2 (2026-09-18-static-storage LANDING: TWO pass fixtures joined `nonglob`, static_aggregate_storage_O0 and static_aggregate_storage_O2 — the two halves of #343, one program compiled at -O0 and at -O2 (the `_O2` suffix dispatches the flag in logos_pass_extra_args). A struct- or tuple-typed `static` was declared `!llvm.ptr` (8 bytes) while `__logos_static_init` memcpy'd `layout_of(type).size` into it; CONTROL REVERT with the compiler fix backed out has the -O0 half exit 2 and the -O2 half exit 1, both 0 with it. Neither matches wql_/deem_ so `glob` is unmoved at 191, and neither declares a door, so `doors` and `nonglob_doors` are unmoved at 36 = 10 + 26 — the fold re-measured both in the same run. RE-DERIVED BY DIRECT FILE LISTING (ls tests/logos/pass/*.logos): 3881 = 191 + 3690, the partition closes, and the listing was run INDEPENDENTLY of the gate, which printed the same 3881/3690.) PREVIOUSLY +2 (2026-09-18-adr0027s3 LANDING: TWO pass fixtures joined `nonglob`, writ_row_view and bc_row_ref_return_admit — the ADR 0027 S3 row views: `RowRef` (borrows its container, `#[borrow_carrying]`), `RowBuf` (owns an assembled document), `RowRef::to_buf` (D11's explicit promotion) and the `Writ::row_ref` door. The round's THIRD new fixture, fail/bc_row_ref_return_dangle, is the refusal twin — a `RowRef` returned over a fn-local `Writ` — and a `fail` fixture is NOT in this population. Neither pass fixture matches wql_/deem_ so `glob` is unmoved at 191, and neither declares a door, so `doors` and `nonglob_doors` are unmoved at 36 = 10 + 26 — the fold re-measured both in the same run. RE-DERIVED BY DIRECT FILE LISTING (ls tests/logos/pass/*.logos): 3879 = 191 + 3688, the partition closes, and the listing was run INDEPENDENTLY of the gate, which printed the same 3879/3688.) PREVIOUSLY +1 (2026-09-17-adr0027s1 LANDING: ONE pass fixture joined `nonglob`, writ_row_format — the ADR 0027 S1 `WRow` format fixture (presence bitmap + popcount access, type code W_ROW=97). It does not match wql_/deem_ so `glob` is unmoved at 191, and it declares NO door, so `doors` and `nonglob_doors` are unmoved at 36 = 10 + 26 — the fold re-measured both in the same run. RE-DERIVED BY DIRECT FILE LISTING (ls tests/logos/pass/*.logos): 3877 = 191 + 3686, the partition closes.) PREVIOUSLY +2 (2026-09-17q-byvalparam LANDING: the two by-value `typeof(<container>)` projection-parameter fixtures named on `corpus` above; RE-DERIVED BY DIRECT FILE LISTING, 3876 = 191 + 3685.) PREVIOUSLY +5 (2026-09-17p-ordcmpland LANDING: FIVE pass fixtures joined `nonglob`. ONE is the program of the CLOSED soundness_queue row rawptr_ordering_compare_mlir_verifier_refused (tier 3), landed as bc_0917p_ordcmpland_rawptr_ordering_compare — legal Rust (rustc 1.98.1 --edition 2024 compiles and RUNS it at exit 0, valgrind clean) that every binary before this commit REFUSED with "'arith.cmpi' op operand #0 must be signless-integer-like, but got '!llvm.ptr'". FOUR are hand-battery programs bc_0917p_ordcmpland_hb_{y01_admit,y02_admit,y03_admit,y04_admit}: y01/y02/y04 MOVED base(refused) -> landed(compiles and RUNS the rustc twin's stdout), y03 is the CONTROL that already passed on the base binary and pins `&T` ordering comparing POINTEES not addresses — the regression the crude arm caused. None matches wql_/deem_ so `glob` is unmoved at 191. The THREE new fail fixtures bc_0917p_ordcmpland_hb_{z05,z06,z08}_refuse are refusal twins and are NOT in this population; no door is declared by any of the eight, so `doors` and `nonglob_doors` are unmoved. RE-DERIVED BY DIRECT FILE LISTING (ls tests/logos/pass/*.logos): 3874 = 191 + 3683, the partition closes.) PREVIOUSLY +1 (2026-09-17n-dynbinder LANDING: ONE pass fixture joined `nonglob`, bc_0917n_dynbinder_dyn_method_fn_binder_argument — the program of the CLOSED soundness_queue row dyn_method_fn_binder_argument_refuses (tier 3), legal Rust that rustc 1.98.1 --edition 2024 compiles and RUNS at exit 0 and that every binary before this commit REFUSED with `method 'pick' arg 1: variance mismatch`. It does not match wql_/deem_ so `glob` is unmoved at 191. The TEN new fail fixtures bc_0917n_dynbinder_hb_{x1,x2,x3,x4,x6,x7,b0,b1,b2,b3}_refuse are refusal twins and are NOT in this population; no door is declared by any of them, so `doors` and `nonglob_doors` are unmoved. RE-DERIVED BY DIRECT FILE LISTING (ls tests/logos/pass/*.logos): 3869 = 191 + 3678, the partition closes.) PREVIOUSLY +17 (2026-09-17l-matchplace LANDING: SEVENTEEN pass fixtures joined `nonglob`, none matching wql_/deem_ so `glob` is unmoved at 191. ONE is the program of the CLOSED soundness_queue row match_array_field_place_verifier_error_refused, bc_17l_row_match_array_field_place — legal Rust (rustc 1.98.1 --edition 2024 compiles and RUNS it, k=2 n=21, valgrind clean) that every binary before this commit REFUSED with an MLIR verifier error, 'llvm.getelementptr' op operand #0 on an array VALUE. SIXTEEN are hand-battery programs: bc_17l_hb_{b01_lit_arm,b03_rvalue,b04_through_ref,b06_loop,b08_drop_count,c01_matchexpr,c02_matchexpr_refmut,x13_refmut,x15_refmut_lit,a08_match_field_array,a09_tuple_elem_array,a03_arrfield_struct_elem,a07_dynfield_method,a11_localarray_control,x01_arrfield_copy,x04_matchfield_copy}. TWELVE of the seventeen MOVED base(refused or WRONG VALUE) -> landed(compiles and RUNS the right value); c02_matchexpr_refmut is the one that moved from a LIVE MISCOMPILE (compiled, exited 0, printed src=2 where rustc prints 99). The other FIVE are CONTROLS that already passed on the base binary and are landed to pin the neighbours which condemned two earlier arms: a03, a07, x01 (they refuted round 17k's `fatslarr`), a11 (the closed row's own local-array control) and c01 (the by-value twin separating c02's miscompile from a plain refusal). The round's THREE NEW queue rows are not in this population (match_array_elem_struct_binder_aliases_source, match_array_elem_array_binder_shape_lost_run, foreach_array_field_gep_verifier_refused live under tests/soundness/open/), and the closed row's program LEFT that directory in the same commit. DOOR counts unmoved (36 = 10 + 26): none of the seventeen declares a container family or a `direct` output form — they are array/tuple place-scrutinee match shapes only. RE-DERIVED BY DIRECT FILE LISTING, not by adding to the previous line: ls tests/logos/pass/*.logos | wc -l -> 3868; glob -> 191; 3868 - 191 = 3677.)  # +6 (2026-09-17j-ptrland2 LANDING: SIX pass fixtures joined `nonglob`. TWO are the programs of the CLOSED soundness_queue rows mutptr_region_param_elided_let_arg_refused (runs 0) and fnptr_call_result_region_param_reads_static_refused (runs 18) — both LEGAL Rust (rustc 1.98.1 --edition 2024 ACCEPTS and RUNS each at those exit codes) that every binary before this commit REFUSED with "variance mismatch — expected *mut &'static i64". FOUR are over-refusal controls for the new K::Ptr and K::FnPtr arms in collect_param_regions_, one per CARRIER: bc_0917j_ptrland2_hb_j01_pass (two-level *const), _hb_l05_pass (tuple), _hb_g02_pass (array) and _hb_l04_pass (bare fn pointer — the ONLY program in 38 that separates the FnPtr half from the Ptr half, unmoved under Ptr alone). All six verdicts MOVED base(refused) -> landed(compiles and RUNS the right value). The round's TWO fail fixtures — bc_0917j_ptrland2_hb_j{06,10}_refuse, use-after-scope escapes through carriers held on BASE only by the variance accident this commit deletes — are not in this population, the TWO closed rows' programs LEFT tests/soundness/open/, and one NEW row program JOINED it (ptr_array_in_tuple_param_gep_verifier_refused), which is also not in this population. None matches wql_/deem_, so `glob` is unmoved at 191. RE-DERIVED BY DIRECT FILE LISTING, not by adding to the previous line: ls tests/logos/pass/*.logos | wc -l -> 3851; glob -> 191; 3851 - 191 = 3660.)  # +3 (2026-09-17h-ptrland LANDING: the same three pass fixtures as `corpus` above — the three LEGAL raw-pointer-to-reference over-refusal controls (bc_0917h_ptrland_hb_l1_pass, bc_0917h_ptrland_hb_l2_pass, bc_0917h_ptrland_hb_l4_pass). None matches wql_/deem_, so all three join `nonglob`; partition closes 3845 = 191 + 3654. DOOR counts unmoved: none of the three declares a container family or a `direct` output form — they are `*const &i64` / `*const i64` struct shapes only.)  # +3 (2026-09-17f-refpatland LANDING: the same three pass fixtures as `corpus` above — the CLOSED row's program, its printing value-oracle twin, and the legal `ref` control. RE-DERIVED BY DIRECT LISTING: 3842 - 191 = 3651.)  # +4 (2026-09-16p-tupdoor LANDING: the same four pass fixtures as `corpus` above — the CLOSED row's program plus three hand-battery programs whose verdict MOVED base(refused) -> landed(compiles and RUNS). No door is declared by any of them, so `doors`, `glob` and `nonglob_doors` are unmoved. RE-DERIVED BY DIRECT FILE LISTING: 3835 - 191 = 3644.)  # +7 (2026-09-16n-arrstore2 LANDING: SEVEN pass fixtures joined `nonglob` — 2 are the programs of the two CLOSED soundness_queue rows (bc_16n_row_array_store_value_reads_loan_refused, bc_16n_row_array_index_store_index_reads_loan_refused) and 5 are hand-battery programs whose verdict MOVED base(refused, nerr 2) -> landed(compiles and RUNS exit 0): bc_16n_hb_h03_admit (index AND value read the loan), bc_16n_hb_h05_admit (two loans), bc_16n_hb_h06_admit (nested array), bc_16n_hb_h07_admit (if-arm frame), bc_16n_hb_h08_admit (loop body). The round's TWO fail fixtures (bc_16n_hb_x02_refuse, bc_16n_hb_x04_refuse) and its ONE new soundness_queue program are not in this population. None matches wql_/deem_, so `glob` is unmoved at 191. RE-DERIVED BY DIRECT FILE LISTING, not by adding to the previous line: ls tests/logos/pass/*.logos | wc -l -> 3831; glob -> 191; 3831 - 191 = 3640.)  # +5 (2026-09-16l-refptee LANDING: FIVE pass fixtures joined `nonglob` — 2 are the programs of the two CLOSED soundness_queue rows (bc_16l_row_ref_to_rawptr_type_parse_refused, bc_16l_row_double_amp_lifetime_ref_type_parse_refused) and 3 are hand-battery programs whose verdict MOVED base(parse-refused) -> landed(compiles and RUNS exit 0): bc_16l_hb_l04_admit (&*const i64), bc_16l_hb_l05_admit (&mut *mut i64), bc_16l_hb_l11_admit (&&'a D, the AND-LIFETIME half). The round's TWO fail fixtures (bc_16l_hb_l09_refuse, bc_16l_hb_l14_refuse) and its FOUR new soundness_queue programs are not in this population. None matches wql_/deem_, so `glob` is unmoved at 191. RE-DERIVED BY DIRECT FILE LISTING, not by adding to the previous line: ls tests/logos/pass/*.logos | wc -l -> 3824; glob -> 191; 3824 - 191 = 3633.)  # +9 (2026-09-16j-arrpath2 LANDING: the same nine pass fixtures as `corpus`; none matches the wql_/deem_ glob, so the whole increment lands here. RE-DERIVED BY DIRECT LISTING: 3819 - 191 = 3628. DOOR counts unmoved (26): none of the nine declares a container family or a `direct` output form — they are `[T; N]` array-pattern drop-trace programs only.)  # +2 (2026-09-16f-landboth LANDING: the same two pass fixtures, u04 and u05; neither matches wql_/deem_ so both joined the NONGLOB half. RE-DERIVED BY DIRECT FILE LISTING: 3804 - 191 = 3613.)  # +11 (2026-09-16f-atbind LANDING: ELEVEN pass fixtures joined — 2 from CLOSED soundness_queue rows (bc_0916f_atbind_aggregate_admit, bc_0916f_atbind_byvalue_drop_admit) and 9 hand-battery programs that CAUGHT something (bc_0916f_atbind_hb_{a10,b01,b02,b06,b09,b11,c01,c04,c06}_admit; a10 is the control that REFUTED the first arm, c01/c06 are the two sites the handed-down framing never named). The round's 5 NEW soundness-queue programs are not in this population. None matches wql_/deem_, so `glob` is unmoved at 191. RE-DERIVED BY DIRECT FILE LISTING: ls tests/logos/pass/*.logos | wc -l -> 3802; glob -> 191; 3802 - 191 = 3611.)  # +10 (2026-09-16d-landrulings LANDING: the same net ten pass fixtures, all non-glob; RE-DERIVED BY DIRECT FILE LISTING: 3791 - 191 = 3600).  # +10 (2026-09-16c-armelem LANDING: the same ten pass fixtures, all non-glob; RE-DERIVED BY DIRECT FILE LISTING: 3781 - 191 = 3590).  # +2 (2026-09-15h-boxdynref LANDING: boxdyn_mut_explicit_deref_arg (the closed soundness_queue row boxdyn_mut_explicit_deref_arg_no_vtable) and boxdyn_boxref_param_takes_amp_box (hand-battery ce07), BOTH joined `nonglob` — neither matches the glob, and neither declares a container family or a `direct` output form, so `glob`, `doors` and `nonglob_doors` are unmoved. The round's 3 FAIL halves are not in this population. RE-DERIVED BY DIRECT FILE LISTING: ls tests/logos/pass/*.logos | wc -l -> 3771; ls tests/logos/pass/{wql_*,deem_*}.logos | wc -l -> 191; partition closes: 3771 = 191 + 3580). +21 (2026-09-15g-aggtemp LANDING, see `corpus`). +3 (2026-09-15g-aggtemp pricing, see `corpus`). +4 (2026-09-15f-rustcaudit, the rustc audit of the soundness queue's by-reading rows: bc_0915f_rustcaudit_{vec_index_store_len_hoisted,array_index_store_len_in_index,struct_lit_rawptr_field_static_region,struct_lit_mutref_field_local_region}_admit pass fixtures, all joined `nonglob`; RE-DERIVED BY DIRECT FILE LISTING: ls tests/logos/pass/*.logos | wc -l -> 3745; ls tests/logos/pass/{wql_*,deem_*}.logos | wc -l -> 191; partition closes: 3745 = 191 + 3554). +44 (2026-09-15f-consumeland, the landing that closed soundness_queue row byvalue_operator_operand_not_moved_double_drop: bc_0915f_consumeland_operator_operands_moved_admit + 43 bc_0915f_consumeland_hb_<id>_admit pass fixtures, all joined `nonglob`; RE-DERIVED BY DIRECT FILE LISTING: ls tests/logos/pass/*.logos | wc -l -> 3741; ls tests/logos/pass/{wql_*,deem_*}.logos | wc -l -> 191; partition closes: 3741 = 191 + 3550). +92 (2026-09-15d-argrefland, the landing that closed soundness_queue rows addr_of_struct_ref_local_arg / addr_of_ref_foreach_elem / addr_of_ref_match_binder / addr_of_ref_closure_capture / addr_of_ref_closure_param _passes_referent_run and struct_pattern_over_double_ref_reads_slot_run: 6 bc_argrefland_*_admit + 86 bc_0915d_argrefland_hb_<id>_admit pass fixtures, all joined `nonglob`; RE-DERIVED BY DIRECT FILE LISTING: ls tests/logos/pass/*.logos | wc -l -> 3697; ls tests/logos/pass/{wql_*,deem_*}.logos | wc -l -> 191; partition closes: 3697 = 191 + 3506). +4 (2026-09-15c-argref: the four pass fixtures above, all nonglob; 3605 = 191 + 3414). +57 (2026-09-15b-refeqland: the 57 pass fixtures above). +86 (2026-09-15 shadowslot landing: the eighty-six pass fixtures above, all nonglob) +53 (2026-09-14o-autoreffund: the fifty-three pass fixtures above, all nonglob) +6 (2026-09-14m-storeedge-land: the six pass fixtures above, all nonglob) +17 (2026-09-14k-fnptrbinderland, the seventeen bc_fnptrbind_*_admit / bc_0914k_fnptrbinderland_hb_*_admit pass fixtures — see 'corpus' above).  +13 (2026-09-14j-fnptrbinder, the fn-pointer binder pricing: 13 legal hand-battery catches landed as bc_0914j_fnptrbinder_hb_<id>_admit pass fixtures, each asserting its exit code on build c52dcb19dff987ff; the round's 9 new soundness_queue programs do not live in this population. None declares a container family or a `direct` door. RE-DERIVED BY DIRECT FILE LISTING, not by adding 13: ls tests/logos/pass/*.logos | wc -l -> 3382; ls tests/logos/pass/{wql_*,deem_*}.logos | wc -l -> 191; partition closes: 3382 = 191 + 3191.)  +33 (2026-09-14i-harvest2, the 33 bc_0914{e,f}_*_hb_*_admit pass fixtures — see 'corpus' above).  +184 (2026-09-14h-harvest, the 184 bc_*_hb_*_admit pass fixtures — see 'corpus' above).  +13 (2026-09-14f-thrurefland, the thirteen bc_ptrcmp_*_admit / bc_thruref_*_admit pass halves — see 'corpus' above).+12 (2026-09-14d-meetoblland, the twelve bc_meet_*_admit pass halves — see 'corpus' above).+15 (2026-09-14b-ptrcoerceland, the fifteen bc_ptrcoerce_* / bc_vecstore_* pass halves and pins — see 'corpus' above).+8 (2026-09-13f-declarrivalland, the eight bc_declarrival_*_admit pass halves — see 'corpus' above).+14 (2026-09-13d-staticdemand, the fourteen bc_staticdemand_*_admit pass halves — see 'corpus' above).  +12 (2026-09-13b-selfregion-land, the impl-self-region class landing that closed bc_admits rows issue-55394--b and issue-98170: the TWELVE pass halves bc_selfregion_{anon_header_ret,anon_pick,anon_trait_default_pick,generic_selflit,let_self_annot,ref_target_let_self,selflit_fn_binder,self_struct_variant,self_tuple_variant,self_unit_variant,two_anon,typearg_anon_let_self}_admit — all twelve joined `nonglob`, none matches the glob; the round's eleven native fail halves, the two closed rows' programs moved tests/imported/admit/nll -> tests/imported/fail/nll, and the one new soundness_queue program do not live in this population. None declares a container family or a `direct` door. RE-DERIVED BY DIRECT FILE LISTING, not by adding 12: ls tests/logos/pass/*.logos | wc -l -> 3090; ls tests/logos/pass/{wql_*,deem_*}.logos | wc -l -> 191; partition closes: 3090 = 191 + 2899.)  +6 (2026-09-12r-escroot, call-result region instantiation + the positive let door that closed bc_admits rows method-ufcs-inherent-3, method-ufcs-inherent-4 and regions-free-region-ordering-caller1: the SIX pass halves bc_escroot_call_result_static, bc_escroot_ref_local_index, bc_letbind_call_param_named, bc_letbind_temp_anon, bc_static_call_callee_region_instantiated, bc_method_call_fn_binder_instantiated — all six joined `nonglob`, none matches the glob; the round's six native fail halves, the three closed rows' programs moved tests/imported/admit -> tests/imported/fail, and the eight new soundness_queue programs do not live in this population. RE-DERIVED BY DIRECT FILE LISTING, not by adding 6: ls tests/logos/pass/*.logos | wc -l -> 3078; ls tests/logos/pass/{wql_*,deem_*}.logos | wc -l -> 191; partition closes: 3078 = 191 + 2887.)  +3 (2026-09-12n-bcdoor, the operand-position `&<temporary>` minting-site class landing that closed bc_admits row temporary-lifetime-extension-tuple-ctor: the THREE pass halves bc_bcdoor_fnptr_admit, bc_bcdoor_closure_admit and bc_bcdoor_argborrow_admit (the last is the over-refusal guard — a TEMPORARY receiver whose result borrows the ARGUMENT, legal, and it must keep admitting) — all three joined `nonglob`, none matches the glob; the round's three native fail halves (bc_bcdoor_{fnptr,closure,argborrow}_refuse) and the closed row's own program, moved tests/imported/admit/lifetimes -> tests/imported/fail/lifetimes/, do not live in this population. RE-DERIVED BY DIRECT FILE LISTING, not by adding 3: ls tests/logos/pass/*.logos | wc -l -> 3072; ls tests/logos/pass/{wql_*,deem_*}.logos | wc -l -> 191; partition closes: 3072 = 191 + 2881.)  # +6 (2026-09-12k-uninitborrow, the six bc_uninitborrow_*_admit pass halves — see 'corpus' above).  # +3 (2026-09-12i-loanbackedge, the three bc_backedge_*_admit pass halves — see 'corpus' above).  # +5 (2026-09-12b-rpitbound, the five bc_rpitbound_*_admit pass halves — see 'corpus' above).  # +5 (2026-09-12f-argresvassign, the `Code::Assign` reservation arm that closed bc_admits row issue-27868: the FIVE pass halves bc_argresvassign_{scalar,struct,array,shadow,twophase_recv}_admit — all five joined `nonglob`, none matches the glob; the round's three native fail halves (bc_argresvassign_{scalar,struct,array}_refuse) and the closed row's own program, moved tests/imported/admit/nll/issue-27868 -> tests/imported/fail/nll/, do not live in this population. RE-DERIVED BY DIRECT FILE LISTING, not by adding 5: ls tests/logos/pass/*.logos | wc -l -> 3055; ls tests/logos/pass/{wql_*,deem_*}.logos | wc -l -> 191; partition closes: 3055 = 191 + 2864.)  # +2 (2026-09-12b-dwvardoor, the two bc_lifereg_derefwrite_*_admit pass halves — see 'corpus' above).  # +2 (2026-09-12a-taggedidentity, the `tagged_enums_` identity landing: ONE new pass half coex_tagged_enum_bare_key, joined `nonglob`, does not match the glob; the round declared NO new fail fixture and closed NO soundness_queue row. ⚠ THE OTHER +1 IS A DRIFT THE PRECEDING COMMIT 7b3ee3277 LEFT BEHIND: it added coex_enum_bare_key and did not move this pin, so this gate and logos_00_census_pin have been RED at HEAD since — exactly the 7eabc226a case one line up, twice in two days. RE-DERIVED BY DIRECT FILE LISTING, not by adding 2: ls tests/logos/pass/*.logos | wc -l -> 3048; ls tests/logos/pass/{wql_*,deem_*}.logos | wc -l -> 191; partition closes: 3048 = 191 + 2857.)  # +3 (2026-09-11-optboxfield, the sema niche-arm pkg-key landing that closed soundness_queue row layout_verify_optbox_struct_field: the THREE pass halves option_ptr_wrapper_niche_struct_field, option_ptr_wrapper_niche_struct_field_ctl and option_box_recursive_struct_field_list (the closed row's own program) — all three joined `nonglob`, none matches the glob; the round declared NO new fail fixture. RE-DERIVED BY DIRECT FILE LISTING, not by adding 3: ls tests/logos/pass/*.logos | wc -l -> 3046; ls tests/logos/pass/{wql_*,deem_*}.logos | wc -l -> 191; partition closes: 3046 = 191 + 2855.)  # +4 (2026-09-11-bufdrop, the owning-buffer drop-glue class landing: the FOUR pass halves bufwriter_{drop_flushes_buffer,close_then_drop_flushes_once} and btreemap_{drop_frees_storage,free_then_drop_frees_once} — all four joined `nonglob`, none matches the glob; the round declared NO new fail fixture and closed NO soundness_queue row. RE-DERIVED BY DIRECT FILE LISTING, not by adding 4: ls tests/logos/pass/*.logos | wc -l -> 3043; ls tests/logos/pass/{wql_*,deem_*}.logos | wc -l -> 191; partition closes: 3043 = 191 + 2852.)  # +1 (2026-09-11b-vgleak, the borrowed-`Box<dyn>` landing: the ONE pass half boxdyn_borrow_arg_keeps_box_drop joined `nonglob`, does not match the glob; its fail twin of the same name and the two new soundness_queue programs (boxdyn_mutborrow_arg_no_vtable, rcdyn_borrow_arg_no_vtable) do not live in this population. RE-DERIVED BY DIRECT FILE LISTING, not by adding 1: ls tests/logos/pass/*.logos | wc -l -> 3039; ls tests/logos/pass/{wql_*,deem_*}.logos | wc -l -> 191; partition closes: 3039 = 191 + 2848. DOOR counts unmoved.)  # +2 (2026-09-11e, the two bc_lifereg_path_* carriers — see 'corpus' above).  # +5 (2026-09-11c, the five bc_capsof_iife_* pass halves — see 'corpus' above).  # +6 (2026-09-11b, the six E0207 pass halves — see 'corpus' above).  # +1 (2026-09-11j, coex_dyn_bare_key — see 'corpus' above; the pin drifted at 7eabc226a and is re-derived here by direct listing).  # +2 (2026-09-11i, alias_local_shadows_imported_homonym{,_control}). +5 (2026-09-10e-authneg, the authoritative-negative drop landing: mlirgen_odr_drop_glue_field (the closed soundness-queue program homonym_field_drop_glue_segv, landed as its `_ctl` twin's pass half) plus the two NEW carriers found by varying the carrier and their controls: mlirgen_odr_drop_glue_tuple_elem{,_ctl}, mlirgen_odr_drop_glue_enum_payload{,_ctl} — all five joined `nonglob`, none matches the glob; the round declared NO new fail fixture), +4 (2026-09-10d-vecpkg, the stdlib-`Vec` identity landing: vec_pkg_slice_coerce_stdlib, vec_pkg_for_ref_stdlib, vec_pkg_get_local_own_method, vec_pkg_listcomp_local_homonym — all four joined `nonglob`, none matches the glob; the round's THREE fail halves (vec_pkg_slice_coerce_local, vec_pkg_get_stdlib_noncopy, vec_pkg_listcomp_annot_local) do not live in this population, and the two closed soundness-queue programs were never registered tests), # +1 (2026-09-10h-derefident, the Deref/DerefMut lang-item identity landing: deref_bound_lang_item_autoderef joined `nonglob`; its fail twin deref_bound_local_homonym_no_autoderef does not live in this population),  # +6 (2026-09-10f-copyident, the `Copy` lang-item identity landing: copy_lang_item_user_trait_named_copy_stays_move, copy_lang_item_manual_impl_still_seeds, copy_supertrait_user_trait_named_copy_is_in_the_vtable, copy_supertrait_lang_item_is_not_a_supertrait, copy_e0184_user_trait_named_copy_is_not_the_lang_item, copy_bound_lang_item_handle_kind_exempt — all six joined `nonglob`; the round's FOUR fail halves do not live in this population, and the four closed soundness-queue programs were never registered tests), +7 (2026-09-10d-dropident, the destructor-identity landing: drop_lang_item_user_trait_named_drop_is_ordinary, drop_lang_item_stdlib_drop_runs_at_scope_exit, drop_vtable_inherent_drop_is_not_a_destructor, drop_vtable_lang_item_drop_runs_through_vtable, drop_vtable_other_trait_named_drop_is_not_a_destructor, bc_dropck_inherent_drop_is_not_a_drop_impl_admit, bc_dropck_other_trait_named_drop_admit — all seven joined `nonglob`; the round declared NO new fail fixture, and the two closed soundness-queue programs were never registered tests), +5 (2026-09-10b-methodwhy, the candidate-rejection-reason pass halves: method_arity_too_few_runs, method_recv_shared_needs_mut_runs, method_arg_type_mismatch_runs, method_trait_impl_arity_runs, method_name_truly_absent_present_runs — all five joined `nonglob`; the round's five fail halves do not live in this population), +1 (2026-09-10a-selfrecvland, impl_recv_conformant_shapes, the ADMIT half of the receiver conformance landing; its four fail twins do not live in this population), +4 (2026-09-10dropord, the two drop-order PAIRS: drop_struct_fields_decl_order{,_swapped}, drop_tuple_elems_index_order{,_reversed} — all four joined `nonglob`; the round declared NO new fail fixture), +8 (2026-09-09g-tlrefbind, the top-level ref-binder pass halves; all eight joined `nonglob`, the round's eight fail halves do not live in this population), +3 (2026-09-09l-bcs, the E0716 door pass halves: bcs_temp_struct_let_e0716_ok, bcs_temp_enum_let_e0716_ok, bcs_temp_struct_assign_e0716_ok — all three joined `nonglob`; the round's three fail halves and its imported fail fixture do not live in this population), +3 (2026-09-09j-capret, the §CAPRET closure-capture-return pass halves: bc_capret_shared_reborrow_of_capture_pass, bc_capret_shared_reborrow_thru_call_pass, bc_capret_move_capture_value_pass — all three joined `nonglob`; the round's three fail halves do not live in this population), +6 (2026-09-09c, the parameter-door binder walk: fnparam_array_pattern_binds_nothing, fnparam_tuple_nested_sub_binds_nothing, fnparam_array_rest_positions, fnparam_pattern_walk_depth, fnparam_pattern_walk_drop_count, closure_param_tuple_nested_binds — the round's SEVEN fail halves do not live in this population), +4 (2026-09-09elide, elision-expansion pass halves), +8 (2026-09-09sigland, return-slot conformance pass halves), +6 (2026-09-09h, fatret return-ABI: fatret_closure_impl_fn_pair_survives, fatret_fnptr_thin_return, fatret_customdst_ref_returned, fatret_customdst_selfdescribing_thin, fatret_closure_returning_closure, generic_drop_body_calling_closure_param), +7 (2026-09-09, G156-5b drop-identity pass halves), +3 (2026-09-08g, E0509 pass halves), +4 (2026-09-08f, drop-glue pairs), +1 (2026-09-08e, drop_guard_instantiated), +2 (2026-09-08d), +2 (2026-09-08c), +1 (2026-09-08b), +3 (2026-09-08), +3 (2026-09-06j), +2 (2026-09-06k), +6 (2026-09-06n), +6 (2026-09-07q), +7 (2026-09-07u), +5 (2026-09-07v), +5 (2026-09-09f).  # pinned by NOTHING before this gate; +16 with
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
