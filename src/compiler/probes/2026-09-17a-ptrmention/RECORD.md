# 2026-09-17a-ptrmention — "WHICH REGIONS DOES THIS PARAMETER MENTION?" IS ASKED BY A WALKER WITH NO `K::Ptr` ARM, SO A BINDER UNDER A RAW POINTER IS CALLED *FREE* AND PINNED TO `'static`: TWO ARMS CLOSE 1 AND 2 TIER-3 ROWS AT COST 0 IN EVERY COLUMN, AND THE THIRD PREDICTED ROW IS REFUTED BY ITS OWN RUN — IT COMPILES AND COMPUTES THE WRONG ANSWER

site: src/compiler/sema_impl.hpp::collect_param_regions_
site: src/compiler/sema_impl.hpp::build_call_lt_subst_
site: src/compiler/sema_impl.hpp::type_mentions_lt_
site: src/compiler/sema_impl.hpp::collect_input_regions_
site: tools/dlog/region_walker_arms.dl
build: 8f5483d04b7c49ac 43 (base, read) · build-ptrmention2 armed (probes ptrmention / ptrmentionall), clang-20 RelWithDebInfo
measured: 2026-09-17
fires: LOGOS_CENSUS on the BASE binary, per target program — subst.call.unmapped 2 (mutptr), 2 (refptr), 2 (fnptr_call_result):
  the FREE -> 'static branch is REACHED at each, so the pricing is a measurement and not an unreached site. The control that did
  not move fires a different branch — static_arg_pins: subst.call.mapped 4 + meet.call.binder 4, unmapped 0.
  ⚠ No corpus-wide fire census was taken; these are per-program counts.
verdict: PRICED — recommend landing `ptrmentionall`; it closes 2 rows run-verified at cost 0 in every column.

## STEP 1, READ FROM THE TREE (with the corrections this prompt earns)

    HEAD c774ba25d, clean · soundness_queue `# TOTAL` 233 = 233 rows by direct listing
    (tier1 35 · tier2 66 · **tier3 121** · tier4 11) · bc_admits 60 · blocked 8
    probe-log-lint 394 records, rc 0, "every site symbol resolves"
    build_hash 8f5483d04b7c49ac 43 · queue gate rc 0, 233 hold (LOGOS_LIB_DIR supplied)
    logos_00_probe_log_lint + logos_00_soundness_queue: both PASS (ctest rc 0)
    dlog selftest rc 0 — 28fc7c75 19 walkers / 24 findings / try_path 1-5 / domain 42-5, duty 1 -> 0

⚠ **TWO PROMPT FACTS ARE STALE.** It says "232 rows" and "123 of them tier 3";
measured today the ledger is **233** and tier 3 is **121**. Rule 17 — a handed-down
list is a hypothesis, including this prompt.

⚠ `probe-log-lint` is GREEN (394 records). The prompt's note that 33 `site:` lines
carry no `::` is **stale in the same direction**: measured today it is **88** of 528
`site:` lines with no `::` at all, so those records are still never symbol-checked.

## THE CENSUS — TIER 3 BY WHERE THE REFUSAL COMES FROM

All 121 programs compiled with the queue gate's own reader (`LOGOS_VERIFY_LAYOUT=1`),
bucketed by the first diagnostic. **Derived, not inherited** — the three previous
rounds each report different buckets:

| bucket | n |
|---|---|
| sema diagnostic | 90 |
| MLIR verifier error as the verdict | 15 |
| parse / syntax error | 5 |
| `mlir_gen:` internal as the verdict | 5 |
| compiler crash (2× rc 134 abort, 2× rc 139 segv) | 4 |
| compiles clean (`run` row) | 1 |
| other | 1 |

## THE GROUPING, AND HOW IT WAS TESTED

The largest *symptom* cluster is 15 rows sharing one sentence — `variance mismatch —
… — lifetime structure incompatible` — emitted from ONE site (`sema_impl.hpp:7542`).
**A shared emitter is not a shared door**, and the census proved it: arming
`LOGOS_CENSUS` split those rows by which branch of `build_call_lt_subst_` decides them.

| branch | rows | door |
|---|---|---|
| `subst.call.unmapped` (FREE → `'static`) | mutptr, refptr, fnptr | **this round's door** |
| `subst.call.mapped` + `meet.call.binder` | static_arg_pins | first-wins at a MAPPED binder |
| type binder, not a region binder | generic_type_param_two_regions | 14d reason 1 |

That is the grouping I tested — one change, does it move both — and the whole-queue
diff confirmed the set exactly, both ways.

## THE FACT

`collect_param_regions_` answers "which regions does this parameter type mention?".
`build_call_lt_subst_` splits on it three ways: MAPPED (an argument gave it a region),
MENTIONED (appears in a parameter type, region unnameable → elided, permissive), FREE
(appears in NO parameter type → **`'static`**).

Its arm set is Ref/MutRef, Struct/ZonedStruct/Enum, Tuple, Slice/Array. It has **no
`K::Ptr`, no `K::FnPtr`, no `K::TraitObject`/`DstRef`**. So a binder mentioned only
under one of those is called FREE and pinned to `'static` — and the *pairing* walk in
the very same function already has a `K::Ptr` arm, as do `type_mentions_lt_`,
`type_region_opaque_` and `region_loss_noncov_`. This walk is the narrow one.

⚠ `arm_inst()` is `inline bool arm_inst() { return true; }` (probe.hpp:119, "NO LONGER
A PROBE"), so the three-way policy is unconditionally live — not probe-gated. Proven
live by measurement, not by reading: `subst.call.unmapped 2` on each target row.

## THE ARMS, AND THE COLUMNS — every set diffed BOTH ways

`ptrmention` = `K::Ptr` only. `ptrmentionall` = + FnPtr/Closure/FnItem, TraitObject/DstRef,
and the Slice lifetime slot. Two names because the halves are separable (rule 9).

| column | `ptrmention` | `ptrmentionall` |
|---|---|---|
| **queue gate, whole 233 rows, armed vs unarmed** | **2 rows move** | **3 rows move** |
| `fail_text_oracle.py` (1887 fixtures) | 1887 common, **0 changed** | 1887 common, **0 changed** |
| spec fail tier by name (`logos_25_spec_fail*`, 494 tests) | — | base rc 0 / armed rc **0**, 494 of 494 |
| stdlib, 4 layers | **4 of 4 compile** | **4 of 4 compile** |
| `run_oracle.py` (7461 fixtures) | — | 7461 common, 1 changed = `cast-region-to-uint` (subtracted) → **0**; set diff 0/0 |
| valgrind | n/a — no drop glue moves | n/a |

⚠ `ceiling-probe.sh` is not a column here: it cannot see a queue row.

## THE ROWS — PREDICTED BY NAME BEFORE THE BUILD, THEN MEASURED

| row | predicted | measured `ptrmention` | measured `ptrmentionall` | verdict |
|---|---|---|---|---|
| `mutptr_region_param_elided_let_arg_refused` | closes | `0/0/0` | `0/0/0` | **CLOSES** — rustc twin runs 0 |
| `fnptr_call_result_region_param_reads_static_refused` | ptrmention no, all yes | unmoved | `0/0/18` | **CLOSES under `ptrmentionall`** — rustc twin runs 18 |
| `refptr_inner_region_elision_demands_static_refused` | closes | `0/0/1` | `0/0/1` | ⛔ **NOT CLOSED — WRONG ANSWER** (rustc twin runs 0) |
| `static_arg_pins_shared_callee_region_refused` | no move | unmoved | unmoved | control held |
| `generic_type_param_two_regions_first_wins_refused` | no move | unmoved | unmoved | control held |

Predicted set == measured set, across all 233 rows, in both directions. No row opened.

## ⚠ THE QUEUE GATE CANNOT TELL A FIX FROM A MISCOMPILE

The gate reports `refptr` as **"NO LONGER REPRODUCES — the program now compiles clean
(cc=0 diag=0 run=1)"** and demands the row be deleted. It must not be: rustc compiles
the same program and runs **0**, logosc armed runs **1**. A `refuses` row's oracle is
satisfied by mere compilation, so **the gate would have certified a miscompile as a
closure.** Only the rustc twin separated them. This is the fourth round to record that
the exit code is blind at the moment a refusal is relaxed.

## THE SECOND DEFECT, FOUND BY THE ARM (doors in series)

`&*mut T` **as a parameter** compares the OUTER reference, not the raw pointer:
`same<'a>(x: &*mut &'a i64, y: &*mut &'a i64) -> bool { x == y }` over two copies of one
pointer returns **false**. Controls that localize it:

* direct `p == q` on two `*mut &i64` — base compiles, runs **0** (correct);
* through the `&*mut` parameter — armed runs **1**; rustc twin runs **0**;
* the `'static` spelling of the same shape is still REFUSED on base, so the wrong
  answer is unreachable until this round's region door opens.

⚠ My first control for this was itself illegal Rust — the elided `same(x: &*mut &i64, …)`
is refused by rustc ("lifetime may not live long enough"). The explicit-`'a` form is the
legal one and is what the row uses.

## NEIGHBOURS — measured, not assumed (standing rule 2026-09-12)

| neighbour | verdict | reason / number |
|---|---|---|
| `K::FnPtr` at the same walker | **closed by the same change** (`ptrmentionall`) | +1 row, run-verified 18 |
| `K::TraitObject`/`DstRef`/Slice slot at the same walker | co-landed in `ptrmentionall` | 0 rows of its own; cost 0 in every column |
| `refptr` row | **ROWED, reason 2 — doors in series** | the region door opens, a second defect (`&*mut` equality) then gives the wrong answer |
| `collect_input_regions_` (same missing `Ptr` arm, feeds return elision) | **ROWED, reason 1 — no carrier** | N01/N02 compile and run 0 **on base**; the arm moves neither |
| `fill_elided_regions_` (no Ptr, and no Slice/Array — coverage 6/18) | ROWED, reason 3 | its own cost unmeasured; a strictly larger hole, not this door |
| `mint_type_lts_`, `static_item_regions_`, `type_mentions_impl_anon_`, `region_invariantly_pinned_` | ROWED, reason 3 | dlog + per-site read agree they lack `Ptr`; each is a different decision, unpriced here |

## dlog — `region_walker_arms.dl` (NEW RULE; selftest rc 0 first)

The question the previous round said was next for dlog: **which ARMS does a walk have.**
An absence has no spelling, so grep cannot answer it. The rule reuses `lir_dispatch.dl`
by pointing its `target_enum` at `LogosType::Kind`, derives the walker set from behaviour
(`arm_call(F,K,F)` — an arm that calls the walker itself), and derives the structural
kinds rather than listing them.

Answer: **35 region walkers**; `Ptr` absent from 15 of them, `FnPtr` from 20;
`dead_arm` empty. Coverage: `region_loss_noncov_` 16/18, `collect_param_regions_` 14/18
(armed), `type_region_opaque_` 12/18, `type_mentions_lt_` 11/18,
`collect_input_regions_` 8/18, `fill_elided_regions_` 6/18.

⚠ **CROSS-CHECK, AND IT DISAGREED — the extractor was right and my framing was wrong.**
dlog reported `collect_param_regions_` as NOT missing `Ptr`, against my per-site read.
Cause: `ask.sh` extracted the tree **as it stood on disk**, with my probe already
installed — it was describing the ARMED source. The delta is itself the control: it
confirms the edit added exactly Ptr/FnPtr/Closure/FnItem/TraitObject/DstRef and nothing
else. The BASE-tree run of this rule was **not** performed (the revert came after the
armed build, and re-extraction was not re-run); the base arm set in this record rests on
the per-site read, and the rule's header carries that known-answer control for the next
round to check.

## MISTAKES OF MY OWN

* `LOGOS_CENSUS` is a FILE PATH, not a flag; `LOGOS_CENSUS=1` wrote a file named `1`
  into the repo root and printed nothing. Removed; the census was re-taken correctly.
* Three background jobs were launched as `( … ) &` *inside* a backgrounded tool call, so
  the outer shell exited immediately and the detached child was killed — two columns and
  one build silently produced nothing while reporting exit 0. Relaunched without the
  inner `&`.
* The first armed configure passed only `CMAKE_BUILD_TYPE` and defaulted to gcc; the
  generated `logos_parser.cpp` then failed to compile (`'na_fail_0' was not declared`).
  Not a tree defect — the two generated files are byte-identical and `build/` compiles
  that exact file under clang-20. Reconfigured with the mirrored cache.
* The first `run_oracle` diff compared base's rc against base's own sha (wrong `join`
  field offsets) and reported 7459 of 7461 changed. The real answer is 1.
* A wait loop broke on the FIRST `NO LONGER REPRODUCES` match and reported an incomplete
  gate log; re-waited on the completion marker.

## WHAT DESERVES FUNDING

**Land `ptrmentionall`** — 2 rows closed run-verified against their rustc twins, cost 0
in every column the harness owns, both illegal twins still refused, stdlib intact.
The `refptr` row stays open with reason 2 named, and the `&*mut`-parameter equality
defect it exposed is a new row this round did not have the budget to price.
