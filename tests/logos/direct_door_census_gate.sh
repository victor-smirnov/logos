#!/usr/bin/env bash
# direct_door_census_gate.sh LOGOSC PASS_DIR ARCHIVE_DIR [PRESWEPT]
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
ARCH="${3:?fixture archive dir}"
# Private hook for the bite-proof, same contract as `pull_shape_gate.sh`: a
# directory of already-swept `*.user` / `_plan` / `_st` trees to read INSTEAD of
# sweeping, so a perturbation lands on the gate's INPUT rather than on the
# stdlib emitter (which no test may rebuild).
PRESWEPT="${4:-}"

export LC_ALL=C
[ -x "$LOGOSC" ] || { echo "FAIL(2): no logosc at $LOGOSC"; exit 2; }
[ -d "$PASS" ]   || { echo "FAIL(2): no pass dir $PASS"; exit 2; }

TMPD=$(mktemp -d)
trap 'rm -rf "$TMPD"' EXIT
OUT="$TMPD/o"
mkdir -p "$OUT/_st" "$OUT/_plan"

if [ -n "$PRESWEPT" ]; then
    [ -d "$PRESWEPT" ] || { echo "FAIL(2): no pre-swept dir $PRESWEPT"; exit 2; }
    cp -r "$PRESWEPT"/. "$OUT"/ || { echo "FAIL(2): could not read $PRESWEPT"; exit 2; }
else
    [ -d "$ARCH" ] || { echo "FAIL(2): no archive dir $ARCH"; exit 2; }
    shopt -s nullglob
    FIXTURES=("$PASS"/*.logos)
    NFIX=${#FIXTURES[@]}
    # THE BLINDNESS FLOOR (the `pull_shape` reason): a sweep that finds three
    # fixtures and all its pins at zero reads exactly like a healthy one.
    if [ "$NFIX" -lt 1500 ]; then
        echo "FAIL(2): only $NFIX pass fixtures matched — the sweep is blind."
        exit 2
    fi

# ⚠ THE SWEEP'S FAN-OUT IS A BUDGET, NOT `nproc` (task #82). This read
# `-P "$(nproc)"` while `test-levels.sh` runs `ctest -j"$(nproc)"`, so one gate
# asked for 32 workers from inside one of 32 concurrent ctest slots — ~1024-way
# oversubscription on 32 cores. Measured over seven L4 runs on 2026-08-19: every
# run timed out 1-4 tier_full gates, a DIFFERENT subset each time, and every one
# of them passed ALONE in 4-33 s against its ceiling. Wall-clock timeouts under
# that much oversubscription pick victims at random, and a gate that reds at
# random trains the reader to shrug at a red.
# The number below is DECLARED TO CTEST as this test's PROCESSORS in
# tests/logos/CMakeLists.txt — the two must agree, or the declaration is a lie
# and ctest will schedule this gate next to 31 neighbours again.
# ⚠ WHO PARALLELISES, AND WHY IT IS NOT BOTH (Victor, 2026-08-20; task #82).
# UNDER CTEST the parallelism is CTEST'S JOB — it already runs the suite at
# `-j$(nproc)`, so a gate that also fans out `-P$(nproc)` double-dips and the two
# levels MULTIPLY: ~1024 workers on 32 cores. Measured over seven L4 runs
# (2026-08-19): 1-4 tier_full gates timed out on every run, a DIFFERENT subset
# each time, and each passed ALONE in 4-33 s against its ceiling. So under ctest
# this sweep runs SERIALLY and lets ctest schedule the concurrency.
# RUN BY HAND (diagnosis, a bite-proof, a one-off census) there is no outer
# scheduler and the whole box is yours: the default is `nproc`.
# `CTEST_INTERACTIVE_DEBUG_MODE` is set by ctest for every test it runs; it is
# the only marker that needs no cmake-side cooperation, which is what keeps the
# script honest when invoked directly. `LOGOS_GATE_SWEEP_P` overrides both.
if [ -n "${LOGOS_GATE_SWEEP_P:-}" ]; then
    SWEEP_P="$LOGOS_GATE_SWEEP_P"
elif [ -n "${CTEST_INTERACTIVE_DEBUG_MODE:-}" ]; then
    # ⚠ THIS GATE IS THE ONE MEASURED EXCEPTION TO "SERIAL UNDER CTEST", and the
    # exception is arithmetic, not preference. Its sweep is 2203 fixtures and
    # costs ~7 360 CPU-seconds (108 m user + 14 m sys, measured 2026-08-20) —
    # SERIALLY that is 4.1x its own 1800 s ceiling, so the rule as stated turns
    # this test into a deterministic red, which the #83 round's verify measured
    # on an IDLE box (Timeout 1800.12 s, not a load lottery). At 8 it runs in
    # ~415 s with margin. The number is DECLARED to ctest as PROCESSORS 8 at the
    # registration so ctest does not stack eight of these side by side — the two
    # must agree or the declaration is a lie.
    # THE REAL FIX IS TO STOP PAYING IT THREE TIMES: this gate, pull_shape and
    # plan_ground_census each re-compile the SAME corpus. One shared sweep would
    # make the serial rule affordable here too, and it is filed as such.
    SWEEP_P=8
else
    SWEEP_P="$(nproc)"
fi
    cat > "$TMPD/one.sh" <<'WORKER'
#!/usr/bin/env bash
f="$1"; OUT="$2"; LOGOSC="$3"; A="$4"
b=$(basename "$f" .logos)
# ── THE ARCHIVE MAP — mirrors `logos_pass_extra_args` (tests/logos/CMakeLists
# .txt). Same order, same rules: the `memoria_` PREFIX rule first (CMake's own
# comment explains why it is a prefix and not a list), then the name lists.
EX=()
case "$b" in
  memoria_*) EX=(-l "$A/libmemoria-ctr.a" -l "$A/libmemoria-store.a" -l "$A/libmemoria-testkit.a");;
  metacall_item_use_inherit|pub_module_internal_use|pub_module_type_internal|\
  pub_cross_package|pkg_multifile|pub_reexport|pub_enum_trait_cross_pkg|\
  pub_static_cross_module|pub_dyn_cross_module|wql_wref_field_pkg)
      EX=(-l "$A/libpub_lib.a");;
  interior_mut_freeze_canary)   EX=(-l "$A/libub_boundary.a");;
  wql_mapping_cross_module_e2e) EX=(-l "$A/libwql_map_lib.a");;
  cross_pkg_coexistence|cross_pkg_type_coexistence|cross_pkg_type_id_distinct|\
  cross_pkg_const_scoped)       EX=(-l "$A/libcoex.a");;
  coex_from_a|coex_from_b)      EX=(-l "$A/libcoex2a.a" -l "$A/libcoex2b.a");;
  lazy_pkg_basic)               EX=(-l "$A/liblazy_pkg.a");;
  sd_dst_module_methods)        EX=(-l "$A/libsd_dst_mod.a");;
  container_item_from_module)   EX=(-l "$A/libctr_mod.a");;
  metaclass_pmap_from_module)   EX=(-l "$A/libpmap_mod.a");;
  lazy_pkg_chain)               EX=(-l "$A/liblazy_lower.a" -l "$A/liblazy_upper.a");;
  three_layer_chain)            EX=(-l "$A/libhi.a" -l "$A/libmid.a" -l "$A/liblow.a");;
  trait_ident_pkg_chain|trait_ident_bare_alias_bound)
      EX=(-l "$A/libhmid.a" -l "$A/liblhom.a");;
  trait_blanket_bare_alias_bound)     EX=(-l "$A/libbmid.a");;
  trait_blanket_homonym_bound_admits) EX=(-l "$A/libbprobe.a");;
  bc_d1r5_h6_cross_archive_admits)    EX=(-l "$A/libbcxa.a");;
esac
case "$b" in test_harness_*) EX+=(--test);; esac
d=$(mktemp -d)
LOGOS_TRACE_PLAN=1 "$LOGOSC" "$f" "${EX[@]}" --gen-dir "$d/gen" -o "$d/o.o" \
    > "$d/out" 2> "$d/err"
echo "$?" > "$OUT/_st/$b"
# The plan trace goes to stderr. Only the door sentence is kept — the rest is
# megabytes of ground text this gate has no claim on.
grep -c '`_stream` DOOR is now the §12 DIRECT form' "$d/err" > "$OUT/_plan/$b"
shopt -s nullglob
# ⚠ EVERY unit, INCLUDING `logos.gen.*` — and that is a DELIBERATE DIVERGENCE
# from `pull_shape_gate.sh`, which drops them. That gate's subject is the PULL,
# and a `next_batch()` in a `logos.gen.*` unit is the stdlib's own `BatchStream`
# impl for a container family, not a query pulling anything — dropping them is
# right THERE. It is wrong HERE: MEASURED, `memoria_showcase_deem` emits 4
# direct doors and 2 of them land in `logos.gen.borrow_carrying.Hs*` units,
# `container_item_from_module` and `memoria_ctr_vec_deem` one each — a door for
# a container family declared in an imported package is emitted into that
# package's gen unit. Inheriting the user-module rule would have hidden 4 of the
# corpus's 36 doors from the very gate written to stop doors hiding. What
# scopes this gate is PROVENANCE (`// emitted by: deem`), not package name.
U=("$d"/gen/*.gen.logos)
[ "${#U[@]}" -ge 1 ] && cat "${U[@]}" > "$OUT/$b.user"
rm -rf "$d"
WORKER
    chmod +x "$TMPD/one.sh"
    printf '%s\0' "${FIXTURES[@]}" \
      | xargs -0 -P "$SWEEP_P" -I{} "$TMPD/one.sh" {} "$OUT" "$LOGOSC" "$ARCH"
    sweep_rc=$?
    if [ "$sweep_rc" -ne 0 ]; then
        echo "FAIL(2): the corpus sweep itself failed (xargs rc $sweep_rc)."
        exit 2
    fi
    ST=("$OUT"/_st/*)
    if [ "${#ST[@]}" -ne "$NFIX" ]; then
        echo "FAIL(2): ${#ST[@]} rc files for $NFIX probes — probes were lost."
        exit 2
    fi
    # The other half of the bite-proof hook: `LOGOS_DOOR_SWEEP_OUT=<dir>` keeps
    # a copy of the sweep so a perturbation can be applied to it and fed back
    # through `$4`. Unset by the registered test.
    if [ -n "${LOGOS_DOOR_SWEEP_OUT:-}" ]; then
        mkdir -p "$LOGOS_DOOR_SWEEP_OUT" && cp -r "$OUT"/. "$LOGOS_DOOR_SWEEP_OUT"/
    fi
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
    'corpus'            : 2236,
    'glob'              : 191,   # `wql_*` + `deem_*` — pull_shape's population
    'nonglob'           : 2045,  # pinned by NOTHING before this gate; +16 with
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
# that was LISTED and never PROBED. That is compared here: the swept set against
# the corpus listing, both directions. In PRESWEPT mode it is also what refuses
# a stale sweep taken before the corpus changed.
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
