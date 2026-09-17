# 2026-09-17i-ptrdoor2 — PREDICTIONS, written BEFORE the armed binary exists

site: src/compiler/sema_impl.hpp::collect_param_regions_
base: 4f2a4dc58 (build_hash acb9a2715b1ac390 43), shipped binary saved as logosc_base_4f2a4dc58

## THE QUESTION THIS ROUND EXISTS TO ANSWER

17b withdrew the K::Ptr arm because it ADMITTED two use-after-scope programs
(B02 array param, X04 tuple param; rustc E0597 on both). It named the blocking
door: `ptr_under_struct_field_region_escape_admitted`, CLOSED 2026-09-17h.
The handoff says: re-price the arm now.

MEASURED ALREADY, on the shipped post-17h binary, BEFORE arming anything:
  X05 (struct field)  REFUSED, real sentence:  "'r' does not live long enough … (E0597)"
  B01 (fn pointer)    REFUSED, real sentence:  "'v' does not live long enough … (E0597)"
  B02 (array param)   REFUSED, but by the ACCIDENT: "variance mismatch — expected
                      [*const &'static i64; 2]"
  X04 (tuple param)   REFUSED, but by the ACCIDENT: "variance mismatch — expected
                      (*const &'static i64, i64)"
  X01/X02 (bare)      REFUSED, by the ACCIDENT
So 17h opened the door for the STRUCT-FIELD carrier only. The two carriers that
condemned the arm are still held by the sentence the arm deletes.

## PREDICTION (falsifiable, by name)

P1. `ptrmention` (K::Ptr only) still ADMITS B02 and X04 — 17h's holds_any_ref
    Ptr arm is reached through a struct field, and an array/tuple PARAMETER is a
    different carrier. **If B02 or X04 is admitted, the arm is condemned AGAIN
    and nothing lands.**
P2. `ptrmention` closes mutptr_region_param_elided_let_arg_refused and
    refptr_inner_region_elision_demands_static_refused (17b measured both).
    ⚠ refptr must be checked against its rustc twin, not the gate: 17b measured
    the gate calling it closed while the binary answered 1 and rustc answers 0.
P3. `ptrmentionfn` (K::FnPtr only) closes fnptr_call_result_region_param_reads_static_refused
    and its abuse twin B01 HOLDS — B01 is already refused for a REAL reason on
    base, so the fn-ptr half may be separable from the condemned Ptr half.
    This is the arm split 17b could not price: it shipped Ptr and FnPtr in one edit.
P4. Controls static_arg_pins_shared_callee_region_refused and
    generic_type_param_two_regions_first_wins_refused UNMOVED.

## WHAT WOULD MAKE ME RECOMMEND FUNDING

Only P3 surviving with P1 confirmed: i.e. the FnPtr half closes a row with its
abuse direction genuinely paid, while the Ptr half stays condemned. Cost 0 in
the harness columns is NOT a safety claim here (rule 5) — the carrier-varied
abuse battery is.
