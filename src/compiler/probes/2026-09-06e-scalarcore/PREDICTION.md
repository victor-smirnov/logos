# ROUND 2026-09-06e — PREDICTIONS, WRITTEN BEFORE THE BATCH FINISHED ITS BUILD

Base binary 9c95cb54f5729e61; queue gate rc 0 on 41 rows; base hand table in
tables/base_9c95cb54f5729e61.txt (30 shapes, measured BEFORE any edit).

## THE FOUR ARMS
  scsema     sema's six SCALAR doors (PAT_INT/NEG_INT/CHAR/CHAR_RANGE/BOOL/RANGE)
             collapse the scrutinee's &/&mut chain to the CORE. Nothing else.
  scvalstmt  gen_match loads the scalar core (zero layers) when no arm binds the
             whole scrutinee. Nothing else. RefPat is NOT a whole-scrut binder.
  scall      both of the above (same build, both names answered at both sites).
  scallb     both, but a `&n` RefPat arm IS a whole-scrut binder — the rule-9
             twin, identical at every other column, separating ONLY on
             "does a reference pattern over a scalar keep the address".

## QUEUE CEILING, PREDICTED BY NAME (the gate's own red list is the oracle)
  scsema     0 rows. The two literal/range rows stop being REFUSED and start
             dying in codegen ('arith.cmpi'/'llvm.icmp' operand must be
             integer-like) — the gate still reads them as `refuses`-reproducing
             ONLY if it counts a codegen abort as a refusal; the reader sets
             diag=1 on any `error:` line, and the mlir abort prints `error:`.
             So: PREDICTED 0 rows closed, and the refusal TEXT moves. That is a
             SERIES, not a refutation (rule 2).
  scvalstmt  1 row: refpat_scalar_under_ref_mlir_abort. The other two are still
             refused in sema and never reach the value.
  scall      3 rows: range_pattern_under_ref_scrutinee,
             literal_patterns_under_ref_scrutinee,
             refpat_scalar_under_ref_mlir_abort.
  scallb     2 rows: the range and literal rows; refpat_scalar STAYS OPEN
             (it keeps the address and either aborts or computes garbage).
  ADDITIVITY (rule 13): 0 + 1 != 3. The increment is POSITIVE (+2) and the
  parts are in SERIES; if scall closes fewer than 3 the declared root is wrong.

## bc-LEDGER CEILING (ceiling-probe.sh's own population): predicted 0 for all
  four arms. The bc ledger is about borrows, not pattern doors. A 0 here is NOT
  a reason to decline (prompt, 2026-09-05: that was the wrong criterion).

## HAND PROGRAMS (30 shapes; base verdicts in tables/base_*.txt)
  Under scall, PREDICTED to go cc=0 run=0:
    h01 h02 h03 h04 h05 h06 h07 h08 h09 h10 h12 h13 h15 h21 h22 h27 h28 h29 h30
  PREDICTED still wrong under scall, and why:
    h11  `n @ 1..=5` under `&` — At is a whole-scrut binder, so the value keeps
         the address while sema now accepts: refusal MOVES to a codegen abort.
    h14  match EXPRESSION — a SECOND value door (mlir_gen_expr.cpp
         EMatchExprView) that this arm does not touch: sema now accepts, the
         expression door still cmpi's the pointer.
    h16  let-else — a THIRD value door (gen_let_else, its own Range/Int tests).
  PREDICTED unchanged (controls, must stay green): h17 h18 h19 h20 h23 h24 h26.
  ⚠ h25 is MY OWN BUG, not a defect: `out = n` with `n: &i64` under a reference
    scrutinee is a type error in Rust too. Excluded from every set.
