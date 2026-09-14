# 2026-09-14e-thruref — PREDICTIONS BY NAME, written after the spec and BEFORE the build (base baa2a38e8b650fb3 43)

Hand battery dirs (scratchpad): ctl.AXJG (b01-b10 r01-r08 l01-l05) · ctlb2.kQ72 (c01-c09 k01-k08) · walk.ek13 (t1-t6) ·
letk.7cSu (m1-m7) · xder.jBUT (x3-x9) · nbr.31vm + nbr2.41T7 (m8-m15) · hb3.sdHf (r09-r22 m16-m20). Base verdicts recorded
before any edit.

## pbdbm (D1 alone: default-mode payload bindings 3/4 record their loan)
  CEILING {issue-51117}. COST: the spec pass list walks type_3 / type_8 IF the population holds them (measured wall in the
  propagate_pat_borrows comment) — predicted non-zero wherever an enum list walk reassigns its cursor.
  hand -> REFUSED: b01 b04 b06 b07 c04 c06 (illegal) · t4 t5 t6 (LEGAL — the recorded wall, reproduced by hand)
  hand -> unchanged ADMIT/RUN: b03 (`match &mut foo`: AddrOf scrutinee, extract_borrow_place has no AddrOf arm — a third
  door) · c09 (move of the OWNER foo; the loan is keyed on bar) · b10 k01-k08 m14 m15 (UNCERTAIN: k05 k08 depend on the
  pattern holder being released at its last use).

## asgref (D2 alone: assigning a reference-typed local skips the field-loan conflict)
  CEILING 0 rows. COST 0 predicted in pass / cfail / stdlib.
  hand -> RUN (legal, refused today): t1 t2 t3 m1 m2 m5.
  hand -> unchanged REFUSED: m8 m9 (the whole-variable counters: reason 1, no carrier) · m13 (place write `h.r = ..`,
  another reader) · m10 m11 m12 x6 x7 x8 · m16 m17 m18 m20 (UNCERTAIN: each passes the assignment now and must be
  refused by the OWNER's loan / the move / the escape; m20 is the likeliest to open).
  hand -> unchanged ADMIT: x3 x4 x5 (the explicit-deref let-borrow hole is a different door).

## pbdbmasg (D1 + D2, the whole — rule 13)
  CEILING {issue-51117}. COST 0 predicted: type_3 / type_8 green again.
  hand: pbdbm's REFUSED illegal set + asgref's RUN legal set, AND t4 t5 t6 RUN, m19 stays REFUSED.

## asgrefany (D1 + the D2 skip for ANY local — the control twin of D2's inner predicate, rule 9)
  CEILING {issue-51117}. COST NON-ZERO predicted in cfail: every fail fixture pinning an assignment to an OWNED local
  under a live field loan; hand m12 -> ADMITTED (illegal). If asgrefany prices identical to pbdbmasg in every column,
  the predicate is unproven by the harness and m12 is the only separator.

## cmpinv (R2: `==`/`!=` on pointer-like operands needs equal regions at invariant positions)
  CEILING {type-check-pointer-comparisons}, with THREE errors (compare_const, compare_mut, compare_fn_ptr) and none for
  compare_hr_fn_ptr / compare_const_fn_ptr. COST 0 predicted.
  hand -> REFUSED: r01 r05 r09 r10 r17 r18 r22 · unchanged RUN: r02 r04 r11 r12 r13 r14 r15 r16 r19 r20 r21.

## cmpinvtop (R2 control twin: top-level `*mut` pointee only)
  CEILING {type-check-pointer-comparisons} by ONE error (compare_mut) — a row closed with 1 of upstream's 3 errors.
  hand -> REFUSED: r01 r05 r17 r18 r22 · unchanged RUN: r09 r10 (the separators) and every cmpinv RUN program.
