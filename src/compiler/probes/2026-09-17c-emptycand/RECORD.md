# 2026-09-17c-emptycand — THE ELIDED ARGUMENT IS NOT A CANDIDATE

site: src/compiler/sema_impl.hpp::SemaChecker::build_call_lt_subst_
build: base 20900979284e248d 43 (build/, clang++-20) · armed build-emptycand2-1789626435, same configure flags
L1: rc 0 — 808/808 (L1.1), enumerator smoke 12684 cases, gates tier 353 tests, measured on the REVERTED tree
probe: ltemptycand (the `&`/`&mut` slot alone) · ltemptycandall (+ a Struct/Enum's region args)

## STEP 1 (measured this round)
- HEAD 31b6cbef1, tree clean at start.
- `# TOTAL`: soundness_queue 235 · bc_admits 60 · bc_admits_blocked 8.
- soundness_queue rows by direct listing: 235 (tier1 37 · tier2 66 · **tier3 121** · tier4 11).
- `probe-log-lint.py`: 395 records, every site symbol resolves. `logos_00_probe_log_lint` **Passed** (rc 0).
- `build_hash.py`: 20900979284e248d 43.
- queue gate with `LOGOS_LIB_DIR`: **rc 0**, "soundness queue holds — 235 open row(s)".
  (One `Segmentation fault` line from a row's own program is the measurement, not a gate failure.)
- `tools/dlog/selftest.sh`: **rc 0**, known-answer control reads 19 walkers / 24 findings / try_path 1-5 / domain 42-5.

## THE TIER-3 CENSUS — DERIVED, NOT INHERITED
All 121 tier-3 programs compiled with the gate's own reader (`LOGOS_VERIFY_LAYOUT=1`), bucketed
by the FIRST diagnostic. Buckets differ from every previous round's; they are re-derived here.

| bucket | n |
|---|---|
| sema diagnostic | 90 |
| MLIR verifier error as the verdict | 16 |
| parse / syntax error | 5 |
| `mlir_gen:` internal as the verdict | 5 |
| compiler crash (2x rc 134 abort, 2x rc 139 segv) | 4 |
| compiles clean (a `run` row) | 1 |

## THE GROUPING, AND HOW IT WAS TESTED
The largest SYMPTOM cluster in tier 3 is 15 rows sharing one sentence (`variance mismatch —
… — lifetime structure incompatible`), emitted from ONE site (`sema_impl.hpp::check_variance`).
A shared emitter is not a shared door. I split them by WHICH SUBSTITUTION BUILDER decides
each row, measured with `LOGOS_CENSUS` over all 121 tier-3 programs (⚠ `LOGOS_CENSUS` is a
FILE PATH, not a flag):

| branch | rows | n |
|---|---|---|
| `subst.call.unmapped` (FREE -> `'static`) | mutptr, refptr, fnptr | 3 |
| `subst.call.mapped` + `meet.call.binder`, `meet.call.multi` **0** | static_arg_pins, vec_box | 2 |
| `meet.structlit.multi.inv` / `.applied` | struct_lit_invariant, let_mutref | 2 |
| `meet.enumlit.binder`, no multi | enum_payload_rewrap, struct_variant_field | 2 |
| `subst.call.mentioned` only | anon_region_let, dyn_method, generic_type_param, tuplestruct_ctor | 4 |
| no census bucket at all | static_ref_nested, struct_lit_mutref | 2 |

⚠ `meet.method.*` fires on **0 of 121** rows: the method path (`sema_expr.cpp`, `census_meet_("method", …)`)
CENSUSES its multi-candidate binders and applies no meet at all — a real missing meet with
**no carrier in this queue** (neighbour reason 1).

## THE DOOR I PRICED, AND THE FACT
`build_call_lt_subst_`'s pairing walk records a candidate only when BOTH regions are named
(`if (!p.empty() && !a.empty())`). An argument whose own region is ELIDED contributes
NOTHING, so a callee binder offered one NAMED and one ELIDED caller region is MAPPED rigidly
at the named one and the elided argument is compared against it. The meet cannot repair it:
`meet.call.multi` is 0 precisely because the second candidate never entered `cands`.

Two names (rule 9): `ltemptycand` = the `&`/`&mut` slot alone; `ltemptycandall` = + a
Struct/Enum's region args (the SAME decision at the SAME site — the neighbour test).

## ⛔ THE ARM IS WITHDRAWN. IT ADMITS USE-AFTER-SCOPE PROGRAMS rustc REFUSES.
The abuse battery VARIES THE CARRIER, not the count (17a's lesson, applied): bare ref,
struct field, tuple element, array element, `&mut` out-parameter. **rustc refuses all five**
(E0515 x4, E0597 x1). Base refuses all five too — but BY THE VERY SENTENCE THE ARM REMOVES.

| abuse carrier | rustc | base | `ltemptycand` | `ltemptycandall` |
|---|---|---|---|---|
| bare `&'a i64` | E0515 | refused (variance) | refused — **real** dangling-ref check | same |
| tuple `(&'a i64, i64)` | E0515 | refused (variance) | refused — real check | same |
| `&mut &'a i64` out-param | E0597 | refused (variance) | refused — real E0597-shaped check | same |
| **array `[&'a i64; 1]`** | **E0515** | refused (variance) | ⛔ **COMPILES, RUNS 9** | ⛔ **COMPILES, RUNS 9** |
| **struct field `W<'a>`** | **E0515** | refused (variance) | refused | ⛔ **COMPILES, RUNS 9** |

## THE COUNTER-EXAMPLES ARE A DEFECT OF THE SHIPPED BINARY, NOT OF THE ARM
Asked all-elided (no `'static`, so the variance defect cannot be what refuses), on
`build/bin/logosc` with **NO probe armed** — `counterexamples/esc_*.logos`:

| carrier | rustc | SHIPPED logosc |
|---|---|---|
| bare | E0515 | refused: "cannot return reference to local variable 'n': dangling reference" |
| tuple | E0515 | refused, same sentence |
| **array element** | **E0515** | ⛔ **compiles, runs 9** — reads a dead local |
| **struct field** | **E0515** | ⛔ **compiles, runs 9** — reads a dead local |

So the dangling-reference check has **no carrier through an array element or a struct
field**, and the arm's two abuse failures are that hole showing through once the variance
refusal is relaxed. These are TIER-1 `admits` rows that reproduce with no arm at all.

## THE COLUMNS
| column | `ltemptycand` | `ltemptycandall` |
|---|---|---|
| queue gate, all 235 rows, armed vs unarmed (base: 0 FAIL) | **3 rows move** | **3 rows move** |
| `fail_text_oracle.py` | 1887 common, **0 changed**, 0/0 set diff | not run (arm withdrawn) |
| spec fail tier by name (`logos_25_spec_fail`, 494 tests) | base **494/494**, armed **494/494** | not run |
| stdlib, 4 layers | **4 of 4 compile** | not run |
| hand battery, 10 programs | 1 legal closed, **1 illegal ADMITTED** | 2 legal closed, **2 illegal ADMITTED** |
| `ceiling-probe.sh` | not a column — it cannot see a queue row | — |
| `run_oracle.py` | **NOT MEASURED** — the arm was withdrawn on the abuse direction first | — |

## THE ROWS — PREDICTED BY NAME BEFORE THE BUILD, THEN MEASURED
Predicted set was written to `TARGET_ROWS.txt` before the compiler was touched.
**It was wrong in BOTH directions** (rule 17: a handed-down list, including my own, is a hypothesis).

| row | predicted | measured | verdict |
|---|---|---|---|
| `static_arg_pins_shared_callee_region_refused` | closes | cc0 diag0 **run 0** | closes; rustc twin exit 0 — a true closure |
| `vec_box_invariant_in_t_refused` | closes | **unmoved** | ⛔ prediction REFUTED |
| `mutptr_region_param_elided_let_arg_refused` | control, no move | cc0 diag0 **run 0** | ⛔ moved anyway (17a's door, reached from here too) |
| `refptr_inner_region_elision_demands_static_refused` | control, no move | cc0 diag0 **run 1** | ⛔ moved, and **run 1 where rustc gives 0 — a MISCOMPILE** |
| `generic_type_param_two_regions_first_wins_refused` | control, no move | unmoved | control held |
| `tuplestruct_ctor_declared_binder_name_refused` | control, no move | unmoved | control held |
| `let_mutref_annotation_to_meet_struct_refused` | control, no move | unmoved | control held |
| `struct_lit_invariant_binder_first_wins_covariant_region_refused` | control, no move | unmoved | control held |

⚠ **THE QUEUE GATE AGAIN CALLED A MISCOMPILE A CLOSURE** — `refptr` is reported "NO LONGER
REPRODUCES … compiles clean (cc=0 diag=0 run=1)" while rustc answers 0. Fifth consecutive
round to record this: a `refuses` row's gate oracle is satisfied by mere COMPILATION.

## NEIGHBOURS — measured, not assumed (standing rule 2026-09-12)
| neighbour | closed here / rowed | reason and number |
|---|---|---|
| Struct/Enum region args at the SAME site (`ltemptycandall`) | tested as a strict extension, **both withdrawn** | it closes `hb_legal_struct_defarg` AND admits `ab_struct` (runs 9) |
| `meet.method.*` — the method builder censuses and applies NO meet | **rowed, reason 1 — no carrier** | fires on **0 of 121** tier-3 rows |
| tuple-struct ctor — builds NO lifetime substitution at all | **rowed, reason 1 — no carrier here** | different change; `subst.call.mentioned` only, unmoved by both arms |
| `vec_box_invariant_in_t_refused` | **rowed, reason 3** | its own cost unpriced: the refusal is `compute_variances` over Vec's `*mut T`, a different fact |

## INERTNESS CONTROL
The armed binary with **no** `LOGOS_PROBE` reproduces all 10 base hand verdicts digit for
digit, and the base queue gate reads 0 FAIL. The arm is inert when off.

## MISTAKES AND TOOL FACTS OF MY OWN
* A fresh `cmake -G Ninja -B <dir>` defaults to `/usr/bin/c++` (g++) while `build/` is
  configured with **clang++-20**; under g++ the PEG-generated `logos_parser.cpp` does not
  compile (`'na_fail_0' was not declared in this scope`, 4 errors). Cost: one wasted full
  build. Pass `-DCMAKE_CXX_COMPILER` from `build/CMakeCache.txt`.
* `scripts/fail_text_oracle.py` REQUIRES an output path as `argv[1]`; with none it dies with
  an `IndexError` traceback and rc 1, which reads like a tree failure and is not one.
* ⚠ base and armed fail-text baselines come from TWO configures, so the script's own
  self-invalidation caveat applies; populations were identical (1887/1887) and 0 changed.
