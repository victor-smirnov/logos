# 2026-09-17j-ptrland2 — PREDICTIONS, written BEFORE the armed binary exists

site: src/compiler/sema_impl.hpp::collect_param_regions_
base: 8e8d4f0e0, shipped `build/bin/logosc`, size 142221112, built Sep 17 09:09.
  Base verdicts for every program below were captured against THAT binary before
  any rebuild started — a rebuild overwrites it and 17i measured that a compiler
  copied out of its build tree cannot run at all (all 120 rows rc 4).

## WHAT THIS ROUND IS

2026-09-17i priced the `K::Ptr` + `K::FnPtr` arm and recommended funding; it was
a PRICING round and landed NOTHING in the compiler (its own commit message says
so, and the two target rows are still in the ledger at lines 214 and 255). This
round LANDS it, or names the wall.

## THE ARM AS INSTALLED (guarded, so base and armed come from ONE binary)

  `j17ptr` = `K::Ptr` recurses into `pointee()`.
  `j17all` = that, plus `K::FnPtr`/`Closure`/`FnItem` walking `closure_params()`
             and `closure_ret()`.

NOT installed, deliberately: the `K::TraitObject`/`DstRef` arm and the Slice
`lifetime()` slot that 17a's `ptrmentionall` also carried. C01 and C02 compile
and run 0 on BASE (measured this round, and rustc agrees: both legal, run 0), so
those two halves have no carrier and would be unexercised code inside a priced
change — neighbour reason 1.

## THE DECISIVE PREDICTION — THE ABUSE DIRECTION

17b withdrew this arm because it ADMITTED B02 and X04. Both were held on base by
the sentence the arm deletes ("variance mismatch — expected `*const &'static
i64`"), not by an escape check. 17h closed that hole and 17i measured B02/X04
newly refused for a REAL reason. **That is an argument about two programs.** I
wrote five NEW carriers and measured them on base FIRST:

| new abuse program | carrier | rustc 1.98.1 | base sentence |
|---|---|---|---|
| J06 | `*const *const &'a i64` (two-level) | REFUSES E0597 | ⚠ **variance ACCIDENT** |
| J07 | fn ptr in a STRUCT FIELD | REFUSES E0597 | real E0597 |
| J08 | fn ptr in an ARRAY | REFUSES E0597 | real E0597 |
| J09 | tuple of (raw ptr, fn ptr) | REFUSES E0597 | real E0597 |
| J10 | `([*mut &'a i64; 1], i64)` array-in-tuple | REFUSES E0597 | ⚠ **variance ACCIDENT** |

P1. **J06 and J10 MUST STAY REFUSED.** They are held TODAY only by the accident
    this arm removes, and they are carriers no earlier round tested — 17b/17i
    varied the Ptr carrier as bare/array/tuple/struct-field/fn-ptr, never
    two-level or array-nested-in-tuple. **If either is admitted, the arm is
    condemned exactly as it was in 17b and NOTHING LANDS.**
P2. J07/J08/J09 stay refused (already a real E0597 on base).
    ⚠ Note these three ALSO establish that the FnPtr arm's abuse direction had
    been priced by exactly ONE program (B01) in two prior rounds — rule 5, hand
    programs all of one shape.

## THE ROWS

P3. `j17ptr` closes `mutptr_region_param_elided_let_arg_refused` — compiles and
    RUNS 0, which is what its rustc twin does (measured this round: COMPILES,
    run 0).
P4. `j17ptr` alone does NOT close `fnptr_call_result_region_param_reads_static_refused`;
    `j17all` closes it at run 18 (rustc twin measured: COMPILES, run 18).
P5. `refptr_inner_region_elision_demands_static_refused` MOVES in the gate and
    is **NOT CLOSED** — it will compile and answer 1 where rustc answers 0. Its
    second door is the tier-1 row `refptr_param_eq_compares_outer_ref`, measured
    independently here: control D02, which has no inner reference at all, runs
    **1** on base while its rustc twin runs **0**. It must be deleted from the
    claimed set, not counted.
P6. Controls `static_arg_pins_shared_callee_region_refused` and
    `generic_type_param_two_regions_first_wins_refused` UNMOVED.
P7. Whole queue armed, diffed BOTH ways: exactly 2 rows move under `j17ptr`,
    exactly 3 under `j17all`. No unpredicted row moves in either direction.

## THE LEGAL DIRECTION — programs that must go from refused to running

Measured REFUSED on base, all legal (rustc measured COMPILES run 0):
  L01 L02 L03 L05 (17a), G01 G02 (17b), **J01** (two-level `*const`),
  **J05** (array-in-tuple `*mut`) — the last two are this round's own.
  L04 (bare fn-ptr param) is refused on base and needs the FnPtr half.
P8. All of these compile and run their expected value under `j17all`.
P9. Legal J02/J03/J04 already compile and run 0 on BASE — the fn-ptr ARRAY,
    STRUCT-FIELD and TUPLE carriers are already permissive, so L04's bare-param
    shape is the only fn-ptr carrier the arm changes. Predicted UNMOVED.

## WHAT WOULD MAKE ME NOT LAND

Any of: J06 or J10 admitted; an unpredicted queue row moving in either
direction; a legal program that newly runs the WRONG value; a non-zero column in
fail_text_oracle / spec fail tier / stdlib / run_oracle that is not
`cast-region-to-uint`.
