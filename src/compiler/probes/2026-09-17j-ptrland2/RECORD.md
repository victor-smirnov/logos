# 2026-09-17j-ptrland2 — THE ARM THREE ROUNDS PRICED IS LANDED: TWO TIER-3 ROWS CLOSED RUN-VERIFIED, THE ABUSE DIRECTION PAID ON FIVE NEW CARRIERS, AND THE ROUND'S OWN BATTERY OPENS A ROW AND RE-OBSERVES ANOTHER

site: `src/compiler/sema_impl.hpp::collect_param_regions_`
base: `8e8d4f0e0`, shipped `build/bin/logosc`, size **142221112**, built Sep 17 09:09.
armed (measurement): same configure, size **142231976** — the identity is the SIZE and
  the fire behaviour, not `build_hash.py`, which 2026-09-17g measured NOT discriminating.
measured: 2026-09-17
verdict: **LANDED.**

## WHY THIS ROUND EXISTS — AND A CORRECTION TO THE HANDOFF

The handoff reported 2026-09-17i as finished work ("nothing remains"). It is a PRICING
round: its own commit message says *"nothing landed in the compiler; the probe is reverted"*,
and both target rows were still in the ledger at lines 214 and 255 when this round started.
Derived, not inherited: queue 235 rows, rc 0, tiers 38/66/**120**/11; probe-log-lint 401
records rc 0; `build_hash.py` `acb9a2715b1ac390 43`, **not** used as an armed-vs-base identity.

## THE CHANGE

```
case K::Ptr:                                    // recurse into pointee()
case K::FnPtr: case K::Closure: case K::FnItem: // walk closure_params() + closure_ret()
```

`collect_param_regions_` answers MENTIONED-vs-FREE for a callee's region binders, and FREE
is instantiated at `'static`. A region mentioned only under a raw or fn pointer was
invisible, so the binder was called FREE and the call site read *"variance mismatch —
expected `*mut &'static i64`"*.

**NOT landed, deliberately** (17a's `ptrmentionall` also carried them): the
`K::TraitObject`/`DstRef` arm and the Slice `lifetime()` slot. C01 and C02 compile and run
0 on BASE — re-measured here, and rustc agrees both are legal, run 0 — so those halves have
no carrier and would be unexercised code smuggled inside a priced change.

## THE DECISIVE COLUMN: THE ABUSE DIRECTION, ON CARRIERS OF THIS ROUND'S OWN

2026-09-17b withdrew this identical arm because it ADMITTED two use-after-scope programs.
17i re-priced it against the six inherited carriers. **Six programs is an argument about six
programs**, so this round wrote five NEW ones and measured them on base FIRST. Two of them
were held on base by *the very sentence the arm deletes* — the condition that condemned the
arm in 17b:

| abuse program | carrier | rustc 1.98.1 | BASE sentence | LANDED sentence |
|---|---|---|---|---|
| **J06** | `*const *const &'a i64` (two-level) | REFUSES E0597 | ⚠ variance ACCIDENT | ✅ `'p' does not live long enough … (E0597)` |
| **J10** | `([*mut &'a i64; 1], i64)` array-in-tuple | REFUSES E0597 | ⚠ variance ACCIDENT | ✅ `'r' does not live long enough … (E0597)` |
| J07 | fn ptr in a STRUCT FIELD | REFUSES E0597 | real E0597 | real E0597 |
| J08 | fn ptr in an ARRAY | REFUSES E0597 | real E0597 | real E0597 |
| J09 | tuple of (raw ptr, fn ptr) | REFUSES E0597 | real E0597 | real E0597 |
| X01 X02 B01 B02 X04 X05 | inherited six | REFUSES E0597 | 4 accident / 2 real | all REFUSED |

**Eleven abuse programs, eleven still refused, and the two that mattered moved from
refused-by-accident to refused-for-a-reason.** X02 is the one that stays on the variance
sentence — it demands `'static` explicitly, so the accident and the correct answer coincide
there; it is refused either way.

⚠ **J07/J08/J09 also record a gap in the two prior rounds**: the FnPtr arm's abuse direction
had been priced by exactly ONE program (B01, a bare fn-ptr param) across 17b and 17i. Rule 5
— hand programs all of one shape. Three carriers later, it holds.

## THE ROWS, AGAINST THEIR rustc TWINS

The gate's `refuses` oracle is satisfied by mere compilation, so it cannot tell a fix from a
miscompile. Every verdict below is a RUN.

| row | landed verdict | rustc twin | CLOSED? |
|---|---|---|---|
| `mutptr_region_param_elided_let_arg_refused` | `0/0/0` | ACCEPTS, runs **0** | ✅ **YES** |
| `fnptr_call_result_region_param_reads_static_refused` | `0/0/18` | ACCEPTS, runs **18** | ✅ **YES** (needs the FnPtr half) |
| `refptr_inner_region_elision_demands_static_refused` | `0/0/1` | ACCEPTS, runs **0** | ❌ **NO — MISCOMPILE, re-observed** |

## WHOLE-QUEUE DIFF, BOTH WAYS — EXACTLY THE PREDICTED ROWS

| condition | gate rc | rows that move | which |
|---|---|---|---|
| none (control) | **0** | 0 | — 235 hold, tier3=120 |
| `j17ptr` (Ptr only) | 1 | **2** | mutptr, refptr |
| `j17all` (Ptr + FnPtr) | 1 | **3** | + fnptr |

Every mover was named in PREDICTIONS.md before the armed binary existed. No unpredicted row
moved, and none moved in the other direction.

## THE HALVES ARE IN SERIES — RE-MEASURED, NOT INHERITED

The FnPtr half closes `fnptr_call_result` only on top of the Ptr arm: under `j17ptr` that row
reads `1/1/-` (unmoved) and **L04 is the only program in 38 that separates the halves** —
unmoved under `j17ptr`, `0/0/0` under `j17all`. It is landed as a fixture for exactly that
reason. 17b called the FnPtr half "the salvageable one"; it is salvageable only on top.

## WHAT MOVED FROM REFUSED TO RUNNING — THE LEGAL DIRECTION

`0/0/0` on the landed binary, each rustc-measured legal (run 0), each refused on base:
L01 (bare `*mut`), L02 (bare `*const`), L05 (tuple), G01 (two-level `*mut`), G02 (array),
**J01** (two-level `*const`, this round's own), and L04 (fn-ptr) under the FnPtr half.

## TWO THINGS THIS ROUND'S BATTERY FOUND THAT NOBODY PREDICTED

**1. A NEW ROW — `ptr_array_in_tuple_param_gep_verifier_refused` (tier 3).** J05, the
array-nested-in-tuple carrier, is legal (rustc ACCEPTS, runs 0) and is refused on base by the
variance accident. **The arm opens that door and the program lands on a SECOND one**, in the
backend:

```
error: 'llvm.getelementptr' op operand #0 must be LLVM pointer type … but got '!llvm.array<1 x ptr>'
mlir_gen: module verification failed
```

Doors in series — reason 2, not "no carrier": measured, J05 is unmoved between `j17ptr` and
`j17all` while its sibling carriers L05 (tuple) and G02 (array) both close. The user-visible
verdict is wrong AND the sentence is internal, which is the backend bucket this tier collects.

**2. `refptr_inner_region_elision_demands_static_refused` IS RE-OBSERVED, NOT CLOSED.** The
gate calls it closed; the landed binary compiles it and answers **1** where rustc answers 0.
Its `observed` column moves `refuses` -> `run 1`, which is a STRICTER oracle (it now pins the
wrong VALUE, not merely the refusal) and keeps the row open. Its second door is the tier-1 row
`refptr_param_eq_compares_outer_ref`, confirmed independently here: control **D02**, which has
no inner reference at all, runs **1** on the BASE binary with nothing armed while its rustc
twin runs 0. Hand carriers L03 and X03 behave identically and are covered by that tier-1 row
rather than opening duplicates.

## NEIGHBOURS — the standing rule of 2026-09-12

| neighbour | closed in this commit / rowed | reason and the number |
|---|---|---|
| `K::FnPtr`/`Closure`/`FnItem` at this walker | ✅ **CLOSED IN THIS COMMIT** | not separable — armed alone it moves 0 rows and 0 of 38 programs; co-landed, it closes a second row run-verified at 18 |
| `K::TraitObject`/`DstRef` arm | rowed — reason 1, no carrier | C01 compiles and runs 0 on BASE and landed, unmoved; rustc agrees it is legal, run 0 |
| Slice `lifetime()` slot | rowed — reason 1, no carrier | C02 unmoved base and landed; the region lives on the enclosing `Ref`, which the Ref arm already inserts |
| `collect_input_regions_` (same missing-arm set) | rowed — reason 1, no carrier | N01/N02 compile and run 0 on BASE, unmoved by this change |
| `fill_elided_regions_` | rowed — reason 1, no carrier | no program in the 38 moves on it |
| J05's backend door | rowed — **reason 2, doors in series** | opened by this commit onto a second door; NEW ROW `ptr_array_in_tuple_param_gep_verifier_refused` |
| `refptr`'s `==` receiver | rowed — **reason 2, doors in series** | tier-1 `refptr_param_eq_compares_outer_ref`; D02 runs 1 on base unarmed |

## INSTRUMENTS AND MISTAKES OF MY OWN

1. **A `nohup … &` chain launched from a tool call was KILLED mid-run** — the first whole-queue
   diff produced a truncated `ptr.out` and no rc file at all. Re-launched under `setsid` with
   its own rc marker per condition. A missing rc file is the only reason I noticed; had I read
   the truncated output I would have reported a 1-row diff as complete.
2. **My own prediction P8 was REFUTED by J05** — I predicted it would close with the other
   carriers. It does not, and the reason is a second door. Recorded as a row, not repaired.
3. dlog was NOT run during the armed window. 17b's recorded mistake was extracting the ARMED
   tree so its rule described a compiler that exists in no commit; 17i did the owed base-tree
   run and found `missing_arm` for this walker = Ptr, Closure, TraitObject, AssocType, FnPtr,
   UnsizedSlice, DstRef, FnItem. This round's landing removes Ptr, FnPtr, Closure and FnItem
   from that set; the remaining four are the rowed neighbours above, each with a measured
   carrier program rather than a rule's say-so.
4. Built only in `build/`. No new build directory was created and none is left behind.
