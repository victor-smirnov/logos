# 2026-09-17i-ptrdoor2 — THE ARM 17b CONDEMNED IS NO LONGER CONDEMNED: THE TWO USE-AFTER-SCOPE CARRIERS IT ADMITTED ARE NOW REFUSED FOR A REAL E0597 REASON, AND THE HALF 17b CALLED "THE SALVAGEABLE ONE" IS MEASURED TO CLOSE NOTHING ON ITS OWN

site: src/compiler/sema_impl.hpp::collect_param_regions_
build: 4f2a4dc58, build_hash acb9a2715b1ac390 43 — ONE binary, armed by env
  (`probe::on`), so base and armed columns come from the same configure and the
  same bytes. Binary rebuilt 08:32, size 142231048 (base was 142221112) — the
  identity is the SIZE and the fire behaviour, not build_hash, which
  2026-09-17g measured NOT discriminating.
measured: 2026-09-17
verdict: **THE `K::Ptr` ARM IS NOW PAID IN THE ABUSE DIRECTION AND CLOSES ONE
  TIER-3 ROW RUN-VERIFIED.** It is a PRICING round; nothing is landed in the
  compiler. Recommended for funding, with the second row NOT claimed.

## WHAT CHANGED SINCE 17b, MEASURED RATHER THAN ASSUMED

17b built this arm, measured it, and withdrew it: it ADMITTED two use-after-scope
programs (B02 `[*const &'a i64; 2]` param, X04 `(*const &'a i64, i64)` param) that
rustc refuses with E0597, at exits 5 and 6. It named the blocking door —
`ptr_under_struct_field_region_escape_admitted` — which was CLOSED 2026-09-17h.

**The door is measured open, and it opened WIDER than 17h's own commit message
claims.** 17h reports closing the struct-field, tuple-FIELD and array-FIELD
carriers. The two carriers that condemned the arm are not fields at all — they
are an ARRAY PARAMETER and a TUPLE PARAMETER — and they are closed too:

| abuse program | carrier | rustc 1.98.1 | base (probe off) | ARMED `ptrmention` |
|---|---|---|---|---|
| X01 | bare `*mut &'a i64` param | REFUSES E0597 | `1/1/-` variance accident | `1/1/-` **real E0597** |
| X02 | bare param, `'static` demand | REFUSES E0597 | `1/1/-` variance accident | `1/1/-` held |
| B01 | fn-pointer param | REFUSES E0597 | `1/1/-` real E0597 | `1/1/-` real E0597 |
| **B02** | **`[*const &'a i64; 2]` param** | **REFUSES E0597** | `1/1/-` variance accident | ✅ **`1/1/-` real E0597** |
| **X04** | **`(*const &'a i64, i64)` param** | **REFUSES E0597** | `1/1/-` variance accident | ✅ **`1/1/-` real E0597** |
| X05 | struct field | REFUSES E0597 | `1/1/-` real E0597 (17h) | `1/1/-` real E0597 |

17b measured B02 and X04 at `0/0/5` and `0/0/6` ADMITTED under this same arm.
They are now refused, and the SENTENCE is the point: on base four of the six are
held by "variance mismatch — expected `*const &'static i64`", i.e. by the defect
the arm removes; armed, they are held by "'r' does not live long enough … (E0597)",
an escape check that actually looked. **That is the difference between refused by
accident and refused for a reason, and it is what 17b said had to exist first.**

## THE COLUMNS

| column | none | ptrmention | ptrmentionfn | ptrmentionall |
|---|---|---|---|---|
| queue gate, whole ledger (235 rows), rc | 0 | 1 | **0** | 1 |
| rows that move, diffed BOTH ways | 0 | **2** | **0** | **3** |
| which rows, by name | — | mutptr, refptr | none | mutptr, refptr, fnptr |
| stdlib 4 layers | 4/4 | see below | — | see below |

Every moving row was predicted BY NAME in PREDICTIONS.md before the binary
existed. No unpredicted row moved in either direction.

## THE ROWS, AGAINST THEIR rustc TWINS — AND ONE OF THEM IS NOT CLOSED

The gate's `refuses` oracle is satisfied by mere compilation. It is wrong here,
exactly as 17b warned:

| row | armed verdict | rustc twin | CLOSED? |
|---|---|---|---|
| `mutptr_region_param_elided_let_arg_refused` | `0/0/0` under `ptrmention` | ACCEPTS, runs **0** | ✅ **YES, run-verified** |
| `fnptr_call_result_region_param_reads_static_refused` | `0/0/18` under `ptrmentionall` | ACCEPTS, runs **18** | ✅ **YES, run-verified** |
| `refptr_inner_region_elision_demands_static_refused` | `0/0/1` under both | ACCEPTS, runs **0** | ❌ **NO — MISCOMPILE** |

`refptr` compiles under the arm and answers **1 where rustc answers 0**. The gate
calls it closed; it is not. Its remaining defect is the tier-1 row
`refptr_param_eq_compares_outer_ref` (`==` on a `&*mut T` parameter compares the
OUTER REFERENCE) — measured independently here: hand program D02, which has no
inner reference at all, runs **1** on base with no probe armed, while its rustc
twin runs 0, and the direct `p == q` spelling (D01) runs 0 correctly.
**So `refptr` is a door in series and the second door is a tier-1 `run` row.**

## THE GROUPING, TESTED RATHER THAN INHERITED

The census's decidable grouping for this door is the SENTENCE "variance mismatch"
— 15 of the 120 tier-3 rows, all three targets among them. Tested with one armed
change over the gate's own row list: **3 of the 15 move, 12 do not**, including
both named controls (`static_arg_pins_shared_callee_region_refused`,
`generic_type_param_two_regions_first_wins_refused`, both `1/1/-` armed and
unarmed). The symptom group is refuted as a defect class — seven-for-seven now.

## NEIGHBOURS — measured (standing rule 2026-09-12)

| neighbour | closed by this arm / rowed | reason and number |
|---|---|---|
| `K::FnPtr`/`Closure`/`FnItem` arm | **NOT separable — doors in SERIES, measured** | armed ALONE (`ptrmentionfn`, a name this round added precisely to test it) it moves **0 rows** and closes **0** of the 23 hand programs; L04 stays `1/1/-`. Only `ptrmentionall` (Ptr **and** FnPtr) closes `fnptr_call_result` at `0/0/18`. 17b called this "the salvageable half"; it is salvageable only ON TOP of the Ptr arm |
| `collect_input_regions_` | rowed, reason 1 — no carrier | dlog `missing_arm` confirms it has the SAME hole set (Ptr, FnPtr, TraitObject, DstRef, AssocType, UnsizedSlice, Closure, FnItem); N01/N02 compile and run 0 on base, unmoved armed — re-measured this round, still unmoved |
| `fill_elided_regions_` | rowed, reason 1 — no carrier | dlog names it missing Array, Slice, Ptr, FnPtr and more; no program in the battery moves on it |
| `K::TraitObject`/`DstRef` arm | rowed, reason 1 — no carrier | C01 compiles and runs 0 on base and armed, unmoved — 17b's finding REPRODUCED |
| Slice `lifetime()` slot | rowed, reason 1 — no carrier | C02 unmoved, base and armed — 17b's finding REPRODUCED |

## dlog — THE OWED BASE-TREE RUN, NOW DONE

17b owed a BASE-tree run of `region_walker_arms.dl`; it ran the armed tree and
said so. Done here on the clean tree **before** the probe was installed (the
tree was reverted, the query run, then re-armed — stated because 17b's mistake
was exactly this ordering). `selftest.sh` PASSES: 19 walkers / 24 findings /
try_path 1-5 / domain 42-5, duty discriminates 1 -> 0 across 756aed65.

`missing_arm` for `collect_param_regions_`: **Ptr, Closure, TraitObject,
AssocType, FnPtr, UnsizedSlice, DstRef, FnItem**. Cross-check against the
per-site read recorded in the rule's own known-answer control (NO Ptr, FnPtr,
TraitObject, DstRef): **they agree**, and dlog names four MORE kinds the per-site
read had not listed. Coverage `collect_param_regions_` 8/16. No contradiction to
report this time — unlike 17h, where dlog named 12 sites and the read confirmed 4.

## MISTAKES OF MY OWN, AND INSTRUMENTS THAT LIED

1. **A compiler copied out of its build tree cannot run.** I saved
   `build/bin/logosc` to the scratchpad as a base control and ran the tier-3
   census through it: **all 120 rows returned rc 4** and my bucketing read 115 of
   them as "COMPILES-CLEAN". That is the BUILD_RPATH shape — the copy resolves no
   archives. A census that says 115 legal programs compile clean, on a tier whose
   definition is that they do not, is self-refuting and I should have caught it
   from the number alone. Redone with the armed binary, probes OFF.
2. **My first census classifier bucketed on the word `verif`** and matched the
   `layout-verify:` banner every run prints on stderr, reporting 18 "mlir-verifier"
   rows. The real count is 1. A door census is only as good as the line it reads.
3. **`fail_text_oracle.py` and `run_oracle.py` take an OUTPUT PATH as argv[1]**;
   run under a `>` redirect they exit non-zero in under a second. My first chain
   "ran" five oracle passes that measured nothing. run_oracle says so in its own
   usage line — "a `>` redirect does not work … the join then reads phantom damage."
4. **`stdlib-cost.sh` takes the probe as an ARGUMENT, not the environment.** My
   first run exported `LOGOS_PROBE` and the script reported, accurately,
   "all four layers compile under 'nothing armed'" — a cost-0 reading of the
   unarmed compiler.
5. A fresh `cmake -G Ninja -B build-17i` configure FAILED to build at all
   (`logos_parser.cpp: 'na_fail_0' was not declared`, a GENERATED file). I did
   not chase it; I built in `build/` instead and deleted `build-17i` by literal
   path. **Recorded as a possible defect in a from-scratch configure of this
   tree, unowned by this round.**

## WHAT DESERVES FUNDING

**The `K::Ptr` arm, landed with the `K::FnPtr` arm in the same edit.** Together
they close TWO tier-3 rows run-verified against rustc twins (exit 0 and exit 18),
with the abuse direction paid across five varied carriers — bare, fn-pointer,
array, tuple and struct-field — every one refused, four of them newly refused for
a real E0597 reason instead of the variance accident that used to hold them.
The FnPtr half must NOT be landed alone: measured, it closes nothing.

`refptr_inner_region_elision_demands_static_refused` must NOT be claimed by that
landing. Under the arm it compiles and computes the WRONG ANSWER. Its second door
is `refptr_param_eq_compares_outer_ref`, and until that tier-1 row is fixed the
arm converts this row from "legal program refused" into "legal program
miscompiled", which is worse. A landing round should delete it from its predicted
set, or fix the `==` receiver first and close both.

## THE FULL COST TABLE — EVERY COLUMN, BOTH DIRECTIONS

| column | none (base) | ptrmention | ptrmentionfn | ptrmentionall |
|---|---|---|---|---|
| queue gate, 235 rows, rc | 0 | 1 | **0** | 1 |
| queue rows moved, diffed both ways | 0 | 2 (predicted 2) | **0** | 3 (predicted 3) |
| `fail_text_oracle.py` (1901 rows) | baseline | **0 changed** | — | **0 changed** |
| spec fail tier by name (`logos_25_spec_fail_*`) | 494/494 | **494/494** | — | **494/494** |
| `stdlib-cost.sh`, 4 layers | 4/4 | **4/4** | — | **4/4** |
| `run_oracle.py` (7471 rows) | baseline | **0 changed** | — | not run |
| hand battery, 23 programs | baseline | 8 legal newly compile, 6 abuse HELD | 0 moved | 10 legal newly compile, 6 abuse HELD |

`run_oracle` reports ONE differing row, `cast-region-to-uint`, which is
subtracted BY NAME per the standing instruction: it prints a stack address, so
its hash differs between any two runs. Control: it differs here between two runs
of the SAME binary's baseline as well. Net runtime damage **0**.

⚠ These columns are NOT the safety claim, and this round is the sixth to say so.
Under 17b the identical arm priced 0 in every harness column too, and ADMITTED
two use-after-scope programs. What changed is the CARRIER-VARIED abuse battery,
and that is the only column that moved between 17b's condemnation and this
round's recommendation.

## L1

Stated in the commit message as measured after the clean rebuild.
