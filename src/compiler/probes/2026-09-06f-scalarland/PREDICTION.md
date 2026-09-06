# ROUND 2026-09-06f — PREDICTION, WRITTEN BEFORE THE FIRST BUILD OF THE FIX

Base binary 9c95cb54f5729e61, HEAD 2f289d34c, queue TOTAL 45, gate rc 0.
Base hand table: tables/base_9c95cb54f5729e61.txt (30 programs, measured first).

## THE CLASS, STATED
"An arm whose pattern asks a question about the scrutinee's SCALAR CORE, under a
scrutinee typed as a `&`/`&mut` chain over that core."  RFC 2005: a non-reference
pattern matches the POINTEE.  `pat_scrut_one_layer` (2026-09-05b) collapses the
chain to ONE layer, which is what an AGGREGATE door wants — a `&Agg` IS the base
pointer.  A scalar door compares a VALUE and needs ZERO.

## THE CLASS, ENUMERATED BY PROPERTY (never by spelling)
(A) SEMA — every door that refuses a non-scalar scrutinee TYPE.  Enumerated by
    the property "emits `… pattern requires … scrutinee, got '{}'`":
    grep gives 8 such sites; 6 are scalar (PAT_INT/PAT_NEG_INT at sema_stmt.cpp
    integer door, PAT_CHAR, PAT_CHAR_RANGE, PAT_BOOL, PAT_RANGE) and 2 are
    aggregate (tuple, slice) plus byte-string and the reference-pattern door,
    which are NOT members.  All 6 are reached through the ONE entry of
    `build_pattern_impl`.  → one structural change: `pat_scrut_scalar_core`.
(B) CODEGEN — every function holding its OWN match-scrutinee lowering that feeds
    that value to a scalar comparison.  Enumerated by the property "calls
    `emit_range_test` or `arith::CmpIOp` against a match scrutinee value":
      1. `MLIRGenImpl::gen_match`                 (mlir_gen_stmt.cpp) — 6 sites
      2. `MLIRGenImpl::gen_stmt_kind(SLetElseView)` (mlir_gen_stmt.cpp) — 3 sites
      3. `MLIRGenImpl::gen_expr_kind(EMatchExprView)` (mlir_gen_expr.cpp) — 5 sites
    plus the BIND side of the same question, `&n` over a scalar, in each of the
    two payload extractors (gen_match's `extract_payload`, the match
    expression's `extract_arm_payload`).  → one structural change:
    `MLIRGenImpl::scalar_core_scrut`, called once per door, read ONLY at the
    scalar sites.
    `if let` / `while let` are NOT a fourth function in this enumeration — no
    such symbol exists in the tree (grep: no IfLet/WhileLet in mlir_gen_*).
    PREDICTION: they lower through one of the three above and close with them.
    ⚠ The queue row `iflet_literal_under_ref_scrutinee` asserts they are a
    separate door.  If g05/g10 stay broken, that row's claim is right and this
    enumeration is wrong — that is the falsifiable half.

## WHY PER-ARM AND NOT PER-MATCH (rule 5, paid last round)
2026-09-06e's priced arm MUTATED `scrut` under a per-MATCH exemption
(`whole_scrut_binder`) and so refused h33/h34/h35 — a literal arm and a binder
arm in the SAME match, legal Rust.  This fix never mutates `scrut`: the core is a
SECOND value (`sc_scrut`) read only where a scalar is compared.  The exemption
disappears, so the per-arm shapes g07/g08/g09 are the falsifier.

## PREDICTED CLOSED — 6 ROWS, BY NAME
  range_pattern_under_ref_scrutinee
  literal_patterns_under_ref_scrutinee
  refpat_scalar_under_ref_mlir_abort
  match_expr_literal_under_ref_scrutinee
  iflet_literal_under_ref_scrutinee
  at_binding_range_under_ref_scrutinee

## PREDICTED STILL OPEN — 1 ROW, BY NAME AND REASON
  refbind_scalar_under_ref_segv — a DIFFERENT root: gen_match's RefBind case
  alloca-WRAPS the scrutinee value, so `ref r` binds the alloca, not the
  reference.  Nothing here touches RefBind.  Base run 139, predicted 139 after.

## PREDICTED HAND VERDICTS (base in tables/base_*.txt: 20 of 20 g refused)
  g01…g20  →  cc=0 diag=0 run=0, all twenty.
  CONTROLS, predicted UNCHANGED: c01 c02 c03 c05 c07 c08 c09 c10 → run 0.
                                 c04 → run 139 (the RefBind row, not ours).
  CONTROL PREDICTED TO CHANGE: c06 (`match &&v { &&n => }`) is run=1 at base —
  it COMPILES CLEAN AND COMPUTES GARBAGE, a defect the handoff did not record
  and no row pins.  The RefPat-over-scalar half of this fix should take it to 0.
  If it does not, it is a NEW QUEUE ROW.

## COST, PREDICTED
  bc ledger: 0 both ledgers (this is a pattern door, not a borrow).
  pass corpus / stdlib / runtime: 0.  The census 2026-09-06e took over 6490 pass
  fixtures found 442 scalar-door arrivals and NOT ONE at ref-depth >= 1, so no
  fixture can reach the new branch.  ⚠ THAT CENSUS IS A HANDED-DOWN NUMBER
  (rule 17) and its own record says the same file read twice gave 253 then 442.
  A zero here is a prediction, NOT a safety claim (rule 5): the oracles decide.
