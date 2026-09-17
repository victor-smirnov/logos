# ROUND 2026-09-17e — PREDICTIONS, WRITTEN BEFORE THE ARMED BINARY EXISTS

## The hypothesis
Three tier-3 rows are held by ONE door IN SERIES, and each HALF has been measured
ALONE at zero by a DIFFERENT round, each concluding "reason 1/2":
  * codegen half — `pat_bind` has no `RefPat` case (census 2026-09-16p; the row
    headers record a pat_bind RefPat case "built and measured at zero");
  * sema half — the binder whitelists exclude `RefPat` (2026-09-16p armed it
    alone: a MISCOMPILE, `got=0` vs rustc `got=6`, plus two E0507 admissions).
NOBODY HAS MEASURED THE PAIR. Rules 2 and 13: half a mechanism is not one, and
4 + 0 = 6.

## Sets named BY NAME, to be diffed BOTH WAYS
`refpatpair` (both halves) CLOSES:
  - ref_pattern_nested_in_tuple_binding_undefined_refused   (tuple door, got=6 exit 0)
  - letelse_ref_pattern_binding_undefined_refused           (let-else door, got=5 77 exit 0)
`refpatpair` DOES NOT CLOSE:
  - let_ref_struct_pattern_irrefutable_refused — site A is the `let` door
    whitelist (sema_stmt.cpp:1421), refused BEFORE any binder runs, and a
    PAT_REF `let` has no lowering route (the door lowers struct/tuple-struct/
    slice/single-variant shapes only). Predicted REASON 1/3, not a whitelist entry.
`refpatsema` alone: predicted to REPRODUCE 2026-09-16p — compiles, `got=0`, and
  ADMITS the E0507 twins.
`refpatcg` alone: predicted ZERO — sema never defines the name, so the codegen
  case is unreachable for these shapes (this is the "measured at zero" the row
  headers record).

## Abuse direction, written FIRST (rustc 1.98.1 --edition 2024, MEASURED)
  x_tuple_e0507    E0507 "cannot move out of a shared reference"                  MUST STAY REFUSED
  x_letelse_e0507  E0507 "cannot move out of `r` as enum variant `Some` ..."      MUST STAY REFUSED
⚠ On the BASE binary both are refused ONLY by the "undefined variable" accident
(measured). So a real close must produce a REAL E0507 sentence, read, not an rc.
PREDICTED RISK, NAMED: `bind_pattern_ref`'s RefPat case carries the `byval_`
E0507 walker, so the TUPLE twin should get a real E0507; `lower_let_else`'s
`define_bindings` has NO such check, so the LET-ELSE twin is predicted to be
ADMITTED under the arm — an unpriced admission that would condemn the let-else
half on its own.
