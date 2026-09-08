# ROUND 2026-09-08b — THE CLASS BEHIND rc_coerce_unsized_source_not_moved
# STEP 1 at the start: HEAD 904fd10b3, tree clean, # TOTAL soundness_queue 73 /
# bc_admits 91 / bc_admits_blocked 15, rows by direct listing 73, probe-log-lint
# 248 records all resolving, build_hash 8e5f92705b285d29 43, queue gate rc 0
# (tier1=25 tier2=7 tier3=38 tier4=3).
# CORRECTIONS TO THE PROMPT: none. Its STEP-1 gate command carries
# LOGOS_LIB_DIR, checked against the text given, not against the journal.

## 1. THE CLASS, BY THE PROPERTY

A sema COERCION REWRITE replaces a by-value operand `e` with a SYNTHESIZED node
— `StructLit(FieldRead(e))` for the smart-pointer CoerceUnsized, `Cast(e)` for
the dyn upcast. `mark_moved_expr` self-gates to VarRef / FieldRead / TupleIndex
/ IndexRead, so AFTER the rewrite the operand is invisible to it and the move is
never recorded. The source binding stays live: use-after-move is admitted, and
the source's scope-exit drop runs on storage the coercion result owns.

THE TREE ALREADY HELD ONE MEMBER OF THIS CLASS, FIXED, and its comment names the
failure: `expect_type`, CoercePos::Return, `Box<C> -> Box<dyn Tr>` —
`mark_moved_expr(...); e = builder().cast(...)`, "else codegen gets a mis-keyed
vtable AND an un-consumed Box (a double free)". Mark-before-rebuild is the
tree's own idiom. It had been applied at ONE position.

ENUMERATED REWRITE SITES (by the property, not by grep on `Rc`):
  R1 try_struct_unsize_coerce  sema_expr.cpp:935    no mark  -> FIXED
  R2 coerce_dyn_upcast         sema_expr.cpp:~15444 no mark  -> FIXED
  R3 coerce_arg_to_dyn         sema_expr.cpp:~15466 no mark  -> FIXED
  R4 expect_type Return/Box    sema_expr.cpp:~15042 HAS mark (precedent, untouched)
R1's SIX call sites: sema_expr.cpp:1001, 5294, 5354, 15090, 15455 and
sema_stmt.cpp:4945. ⚠ THE PRIOR ROUND'S REPORT NAMED FIVE AND MISSED
sema_stmt.cpp:4945 (`apply_place_coercions` — a `let`/assignment target). That
is the site c3 and c11 below arrive at.

## 2. THE FIX — one named step, three sites

`SemaChecker::mark_coercion_source_moved(const lir::LExprPtr&)`
(sema_impl.hpp), called immediately before each rebuild at R1, R2, R3. It is a
one-line forward to `mark_moved_expr`, and the SELF-GATING is what keeps it
safe in a by-REFERENCE coercion position: `&Concrete -> &dyn` has a non-move
operand type, so nothing is marked. Diff: 3 lines in sema_expr.cpp, 7 in
sema_impl.hpp.
⚠ WHERE THE FIX DIFFERS FROM ITS PROBE (rule 7). The prior round's `rcunsz`
arm marked the operand at R1 ONLY. R2 and R3 are new here, and R2 is the site
that closes a DOUBLE FREE in a program containing no `Rc` at all.

## 3. COUNTER-EXAMPLES — WRITTEN FIRST, VARIED BY POSITION

/home/logos/sandbox/rcunsz. Eight illegal programs, one per coercion POSITION,
plus seven legal controls. Verdicts, base binary vs the binary of this commit,
compile and (compiled + linked + RUN + valgrind) runtime:

  ILLEGAL                                BASE compile   BASE run          AFTER compile
  c1  explicit `as` in a let             COMPILES       rc 0, 3 vg errs   REFUSED "use of moved variable 'rc'"
  c2  fn-call argument (implicit)        COMPILES       rc 2, 4 vg errs   REFUSED "use of moved variable 'rc'"
  c3  typed `let`, no `as`               COMPILES       rc 0, 3 vg errs   REFUSED "use of moved variable 'rc'"
  c8  struct-literal FIELD value         COMPILES       rc 0, 3 vg errs   REFUSED "use of moved variable 'rc'"
  c9  ENUM PAYLOAD                       COMPILES       rc 2, 4 vg errs   REFUSED "use of moved variable 'rc'"
  c11 ASSIGNMENT rhs                     COMPILES       rc 0, 3 vg errs   REFUSED "use of moved variable 'rc'"
  c12 Box<dyn Ext> -> Box<dyn Base>      COMPILES       rc 134            REFUSED "use of moved variable 'e'"
      UPCAST at a call arg                              "free(): double free
                                                         detected in tcache 2"
  c5  source is a STRUCT FIELD           COMPILES       rc 2, 4 vg errs   STILL COMPILES (1 vg err)

  LEGAL CONTROLS (must keep compiling AND running)     BASE            AFTER
  n1  coercion of a TEMPORARY                          rc 0            rc 0, 0 vg errs
  n2  coercion, source never touched again             rc 0            rc 0, 0 vg errs
  n3  source used BEFORE the coercion                  rc 0            rc 0, 0 vg errs
  n4  the CLONE is coerced, original stays live        rc 0            rc 0, 0 vg errs
  n5  two bindings, one coerced, the other read        rc 0            rc 0, 0 vg errs
  n6  a coercion inside a while loop                   rc 0            rc 0, 0 vg errs
  n7  `return rc;` coerced AT THE RETURN               rc 1, 4 vg errs rc 0, 0 vg errs
  c6  Box<A> -> Box<dyn Sp>, source reused             already REFUSED, unchanged

⚠ n7 IS THE RESULT THIS ROUND DID NOT PREDICT AND MUST BE READ AS EVIDENCE, NOT
DECORATION. It was written as an over-refusal CONTROL — a LEGAL program that
must keep compiling. It did keep compiling, and the base binary RAN IT WRONG:
`fn mk(rc: Rc<A>) -> Rc<dyn Sp> { return rc; }` returned the wrong value (rc 1,
4 valgrind errors). Recording the move at R1 makes it rc 0 with 0 errors. A
legal-program control caught a live wrong-answer defect that no illegal program
in the set could see.

⚠ c12 IS THE MEMBER THAT PROVES THE CLASS IS A PROPERTY. It contains no `Rc`,
no smart pointer and no `try_struct_unsize_coerce` — it is a plain `dyn` upcast
through R2 — and on the base binary it ABORTS with a double free. A fix scoped
to `Rc` by name, or to R1 alone, leaves it.

## 4. THE ONE POSITION THAT DID NOT CLOSE, DIFFED BOTH WAYS

c5 (source is a struct FIELD). BOTH HALVES MEASURED SEPARATELY:
  MEMORY HALF, CLOSED — `let d: Rc<dyn Sp> = h.r as Rc<dyn Sp>;` with no
  re-read: 3 valgrind errors before, 0 after. The field IS recorded moved and
  the container's scope-exit glue no longer double-drops it.
  DIAGNOSTIC HALF, OPEN — with the re-read: 4 errors / rc 2 before, 1 error /
  rc 2 after, and still no diagnostic.
TWO CONTROLS on the POST-FIX binary localise it to (FIELD source) x (coercion):
  * no coercion, same field: `sinkrca(h.r)` then `h.r.v()` IS refused,
    "use of moved field 'h.r' (moved on line 13)" — the check exists and fires.
  * VarRef source, same coercion: refused, "use of moved variable 'rc'".
So the recorded move reaches the DROP bookkeeping and not the USE check. That
is a new row, coerce_unsize_field_source_use_admitted (tier 2, `admits`), NOT a
claim that the class is closed. 7 positions of 8 on the diagnostic half, 8 of 8
on the memory half.

## 5. THE TWO ROWS DECLINED, BY NAME, WITH THE NUMBER

replace_site_skips_field_drop_glue — DECLINED. The number that condemns the
available fix: on the prior round's premise oracle, a drop body that MOVES ITS
OWN FIELD OUT owes 1 destructor call and, as a struct FIELD, already gets 1
(correct). Arming the field recursion — every arm that closes any of the row's
shapes — takes it to 2: A DOUBLE DROP. Re-measured on today's base binary:
P1 local 1 / P2 local 2 / P1 as field 0 / P2 as field 1, want 1 each. The
one-line flip trades a leak class for a double-free class, and the corpus holds
no program of the P2-as-a-field shape to say so (cost 1 and runtime 1 of 6553
are cheap only because the corpus cannot see it). The design question — does
the drop BODY own its fields' drops, or does the call site recurse minus what
the body consumed — is the row's actual price, and it is not one this round can
pay without an owner's decision. Two of its consequences are minted as rows
below so the queue carries them instead of prose.

unsized_local_binds_place_dropped_after_free — DECLINED, unchanged from the
prior round and re-verified: closing it reds
tests/logos/pass/custom_dst_smartptr_owning_drop, a pinned-green fixture that
commits the use-after-free. A corpus decision with an owner. Not edited.

## 6. NEW ROWS MINTED (each reproduces on the binary of this commit)

  drop_body_moving_field_double_drops_local     tier 1  run 1
      a user drop body that moves its own field out drops it TWICE as a plain
      top-level local. Counter reads 2, want 1. The OTHER DIRECTION of
      replace_site_skips_field_drop_glue, and the reason that row must not be
      closed by flipping `gen_drop_value`'s `top_level` default.
  enum_user_drop_skips_payload_glue             tier 1  run 1
      an enum with a user Drop and an owning payload leaks it at EVERY site
      including the top-level local (want 1, got 0), because the scope-exit
      enum branch is guarded `k == K::Enum && drop_fn.empty()`. The struct
      spelling of the same shape is CORRECT as a local — that control is what
      separates it from replace_site_skips_field_drop_glue.
  coerce_unsize_field_source_use_admitted       tier 2  admits
      §4 above: the diagnostic half of the FIELD-source position.
  method_with_unsized_wrapper_param_not_found   tier 3  refuses
      an inherent METHOD whose by-value parameter is `Rc<dyn Sp>` is NOT FOUND
      ("'Holder' has no method 'eat'"). Two controls: the same method with
      `r: Rc<A>` IS found; the same signature as a FREE function IS found.
      Found by enumerating the class BY POSITION — the method-argument position
      was the one of eight that could not be measured at all.

## 7. THE STANDING ROW'S SCOPE — the prior round's correction, CARRIED

boxed_move_closure_fat_capture_env_overflow claims three corpus members. Two of
them are not its mechanism (measured by valgrind stack, prior round §6), and
this round SETTLES ONE OF THE TWO BY REPAIR: tests/spec/pass/coerce_4 went from
9 valgrind errors to 0 under the R1 mark alone. Its errors were
rc_coerce_unsized_source_not_moved's, not a boxed closure's. That row is still
NOT re-scoped by this round; this is the second measurement for its owner.

## 8. CORPUS DECISIONS REPORTED, NOTHING EDITED

  * tests/logos/pass/drop_glue_three_levels — `.expected` "b3" contradicts the
    fixture's own comment ("c drops via glue -> B::drop(b3) -> A::drop(a7)"),
    and it is the ENTIRE corpus cost of the declined drop-glue arms.
  * tests/logos/pass/custom_dst_smartptr_owning_drop — pinned green over a
    use-after-free.

## 9. THE ORACLES, FINAL, ON THE COMMIT'S OWN BINARY

  soundness queue gate            rc 0 — 76 rows (tier1=27 tier2=7 tier3=39 tier4=3), `# TOTAL` 76, re-derived BY DIRECT LISTING (76 rows, 76 programs)
  L1 (test-levels.sh, from build/) rc 0 — 779/779, 12 684 generated cases, 156 tier_commit gates
  L4 bc (detached)                 rc 0 after the two pins were re-derived — 4966 tests, 1 pre-existing disabled pair; imported half 1560/1560
  logos_09_direct_door_census      rc 0 — corpus 2934 = glob 191 + nonglob 2743, 36 doors
  full `cmake --build build`       rc 0
  run_oracle.py, BOTH COLUMNS SELF-MEASURED THIS ROUND (base binary swapped in,
  6555 pass fixtures compiled + linked + RUN each side, diffed BOTH WAYS, no
  fixture on only one side):
      logos_02_semantic_core_pass_cast-region-to-uint   sha only — prints a
          stack address, subtracted BY NAME as the tool's note requires
      logos_04_advanced_features_pass_dyn_upcast_consumes_source_box
          rc -6 (SIGABRT, the double free) -> rc 7.  THAT IS THE FIX, in the
          runtime column, on a fixture landed this round.
      ZERO other rows changed in ccrc, run rc or stdout sha.
  fail_text_oracle.py, both columns measured on the two binaries of this round:
      0 of 1457 changed in rc, stderr sha or `.expected` match.
      (The two new fail fixtures are not in that population — it is `-L bc -L
      fail` — and L4 bc ran them green.)
  build_hash.py  8e5f92705b285d29 43  ->  2c5a33ef99e94fc9 43

## 10. CONTROL REVERT — the pre-change binary, on the fixtures this commit lands

  tests/logos/fail/coerce_unsize_consumes_source_rc   BASE rc 1: stderr EMPTY,
      "did not contain: use of moved variable 'rc'". Silent compile.
  tests/logos/fail/dyn_upcast_consumes_source_box     BASE rc 1: stderr EMPTY,
      "did not contain: use of moved variable 'e'". Silent compile.
  tests/logos/pass/coerce_unsize_consumes_source_rc   BASE rc 0 (the legal half
      was already green — it is the ONE-TOKEN twin, not the witness).
  tests/logos/pass/dyn_upcast_consumes_source_box     BASE rc 1:
      "Aborted (core dumped) ... FAIL: exit code 134 (expected 7)".
      ⚠ THE PASS HALF IS ITSELF A RUNTIME WITNESS: the pre-change binary
      MISCOMPILES A LEGAL PROGRAM into a double free. Both halves of that pair
      red on the old binary, for two different reasons.
