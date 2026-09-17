# 2026-09-17m-dynbinder — THE DOOR IS FOUND AND THE CARRIER PROVEN; NO ARMED BINARY WAS BUILT

site: src/compiler/sema_expr.cpp::SemaChecker::try_method_on_dyn (the &dyn dispatch arm)
build: 74fd7681004c1403 43 — ⚠ REBUILT THIS ROUND, see STEP 1
measured: 2026-09-17
status: **PRICING INCOMPLETE.** The door, its carrier and its neighbour set are measured by
        per-site read. NO probe was installed and NO armed binary exists, so there is NO
        column table. Nothing here is a cost or a ceiling. Do not read it as one.

## STEP 1 (measured, with corrections)
- HEAD 6e7389ff1, tree clean at start.
- ⚠ **THE SHIPPED BINARY WAS STALE AND THE FIRST GATE RUN WAS INADMISSIBLE.** build/bin/logosc
  was 11:51; HEAD was committed 12:28 and touched mlir_gen_expr.cpp, mlir_gen_impl.hpp,
  mlir_gen_stmt.cpp. The gate was rebuilt against HEAD before being believed.
- `# TOTAL`: soundness_queue **236** · bc_admits 60 · bc_admits_blocked 8.
- rows by direct listing: 236 (tier1 38 · tier2 66 · **tier3 121** · tier4 11).
- `probe-log-lint.py`: **403 records, every site symbol resolves**, rc 0.
- `build_hash.py`: 74fd7681004c1403 43.
- queue gate with LOGOS_LIB_DIR, on the REBUILT binary: **rc 0** — "soundness queue holds —
  236 open row(s) (tier1=38 tier2=66 tier3=121 tier4=11), '# TOTAL' says 236".
- `tools/dlog/selftest.sh`: **rc 0**, known answer intact (19 walkers / 24 findings /
  try_path 1-5 / domain 42-5; duty discriminates 1 -> 0).

## THE TIER-3 CENSUS — RE-DERIVED BY COMPILING ALL 121
census_tier3_first_error.tsv. ⚠ Keyed on the first **error** line, not the first line: a first
run keyed on the first non-empty line mis-bucketed every row whose output opens with a stdlib
warning (dyn_method's first line is `warning [fn str_split_pat]`, not its error).

| bucket | n |
|---|---|
| sema diagnostic | 108 |
| parse / syntax (rc 4) | 5 |
| compiler crash (2 segv, 2 abort) | 4 |
| compiles clean (the 4 `run` rows) | 4 |

Differs from 17c (90/16/5/5/4/1) because this run buckets on the first ERROR and does not
split backend into verifier vs mlir_gen. Re-derive it; do not inherit either.

## THE GROUPING, AND HOW IT WAS TESTED — "variance mismatch" IS A SENTENCE, NOT A DOOR
12 of the 121 rows print `variance mismatch`. It has **exactly ONE emitter**
(sema_impl.hpp:7557, the `from -> to under variance` checker), so a shared sentence here is
guaranteed and carries no information. Reading the five carriers shows five different upstream
facts: `'_` as a rigid region; dyn dispatch not instantiating; a static item's elided region at
a READ; TYPE-param first-wins with no meet; a tuple-struct ctor with no substitution at all.
17a and 17c recorded the same conclusion independently. **Seventh grouping refutation:** 17c's
finer census branch `subst.call.mentioned only` (4 rows) is ALSO not one door — it contains
the tuple-struct ctor row, which builds no lifetime substitution of any kind.

## THE DOOR, KEYED ON A FACT — "DOES THIS ARGUMENT COMPARISON INSTANTIATE THE CALLEE'S
## LIFETIME BINDERS BEFORE COMPARING?"
All 26 `check_variance` call sites enumerated. The argument-position ones split cleanly:

| instantiates (passes `ipts_`) | does NOT (passes raw `pt`) |
|---|---|
| 4338, 4356 `lower_call` exact | 3846 `lower_call` tuple-struct ctor field |
| 4640, 4658 `lower_call` plain | 3991 `lower_call` closure/fn-ptr |
| 10743, 10779 `lower_method_call` | 5684, 5745 `finish_generic_call` |
|  | 7747 `lower_invoke_expr` closure |
|  | **8538 `try_method_on_dyn`** |
|  | 9205 `lower_method_call` trait-bound receiver |

`inst_call_params_` has exactly 3 call sites (4322, 4621, 10730); `subst_call_ret_lts_` has 4
(4441, 4754, 5587, 17251). **The dyn path is in neither list.**

## THE CARRIER EXISTS — THIS IS NOT A REASON-1 ROW
`SemaTraitMethodInfo` (sema_impl.hpp:5329) declares `param_types`, `ret_type` **and
`lifetime_params`** (5336) and `lifetime_outlives`. The dyn site has every input
`inst_call_params_` takes and does not call it; it substitutes the Self/trait TYPE only
(`self_subst["Self"]`, `subst_type_sema(m.ret_type, trait_subst)`).

## ⚠ TWO DOORS IN SERIES INSIDE ONE SITE — THE ARM MUST BE THE PAIR
`dyn_method_fn_binder_argument_refuses` prints **two** errors, not one:
  1. `method 'pick' arg 1: variance mismatch — expected &'q i64, got &'a i64`
  2. `deref-write '*ptr = …': variance mismatch — expected &'a i64, got &'q i64`
The callee binder `'q` escapes through the RETURN type as well. Arming the argument half alone
cannot close this row — this is the same "ONE MAP, TWO CONSUMERS" contract sema_impl.hpp:797
states in its own words, and 17e/f's lesson (never arm one half of a two-sided door and record
the other half's zero) applies directly.

## ASYMMETRY FOUND IN PASSING
`finish_generic_call` DOES substitute return lifetimes (5587) but compares arguments against
the raw `pt` (5684, 5745). So the two consumers disagree within one function.

## NEIGHBOURS — named, with the test that would decide each (NONE TESTED: no armed binary)
| neighbour | site | status |
|---|---|---|
| `tuplestruct_ctor_declared_binder_name_refused` | 3846 | UNTESTED. Same fact, but no `lifetime_params` list is in scope at a ctor — likely a different change (17c rowed it reason 1). |
| `closure_elided_param_called_with_field_ref_refuses` | 7747 | UNTESTED. Label-confirmed at the closure-call site. |
| `generic_type_param_two_regions_first_wins_refused` | 5684/5745 (by reading: `first<T>` is generic, so it cannot take the `type_params.empty()` exact path) | UNTESTED. I predicted it a CONTROL and the read suggests it is a NEIGHBOUR. 17c measured it unmoved under a DIFFERENT arm, which does not settle this one. |
| `anon_region_let_annotation_refuses_named_region` | let annotation, sema_stmt 2690 | predicted control — different production. |
| `static_ref_nested_borrow_region_not_static_refused` | let 2690 + call | predicted control — a static item's elided region at a READ. |

## WHAT I DID NOT DO
No probe installed, no armed build, no queue-gate diff, no fail_text_oracle, no spec fail
tier, no stdlib, no run_oracle, no valgrind, no rustc twins written for this round. The tree
carries no compiler change (`git status` clean but for this directory), so no L1 was owed
and none is claimed.
