# PRICING THE DROP-EMISSION DESIGN DECISION — 2026-09-08

The question, and it is the owner's:

> Does a type's `Drop::drop` BODY own the drops of that value's fields, or does
> the CALL SITE recurse into the fields minus whatever the body consumed?

This round PRICED both answers. Nothing was landed from the decision.

## 0. CENSUS

    queue gate rc 0 — 80 open rows (tier1 27, tier2 7, tier3 41, tier4 5), `# TOTAL` 80
    bc_admits 91 · bc_admits_blocked 15 · probe-log-lint 248 records, every site symbol resolves
    build read BEFORE the edits: eaac8e4b73c1fe24 43
    build read AFTER  the edits: 48200b2a81815b49 43   (build2, arm B': same sources)

The STEP-1 gate command in this round's prompt CARRIES `LOGOS_LIB_DIR`. The
correction four rounds recorded, and two more repeated from the journal after it
was fixed, does NOT apply to the text this round was given. Re-verified against
the prompt, not against PROBES.md.

## 1. THE TREE IMPLEMENTS NEITHER DESIGN — IT IMPLEMENTS BOTH, AT DIFFERENT SITES

Two independent emitters in `src/compiler/mlir_gen_stmt.cpp`:

* `MLIRGenImpl::gen_drop_value` — struct branch and enum branch each call the
  user `Drop::drop` and then `if (!top_level) return;`. The parameter defaults
  to `false` (`mlir_gen_impl.hpp`), and exactly two callers pass `true`, both in
  `mlir_gen_dyn.cpp`. So every ordinary site is design A.
* the scope-exit emitter (`gen_drop_stmt`'s `emit_body`) — step 1 calls
  `drop_fn`, step 2 recurses the fields ITSELF for Struct/Tuple, i.e. design B,
  with the Enum branch guarded `k == K::Enum && drop_fn.empty()`, i.e. design A.

⚠ THE PROMPT'S PREMISE IS HALF RIGHT. The `drop_fn.empty()` enum guard the row
`enum_user_drop_skips_payload_glue` names is NOT in `gen_drop_value`; it is in
the scope-exit emitter. `gen_drop_value`'s enum branch mirrors its struct branch.

## 2. THE SHAPE SWEEP — 23 PROGRAMS, DESTRUCTOR COUNTS, NOT EXIT CODES

Every program adds its value's weight to a counter reached through a `*mut i64`
and RETURNS the total, so a LEAK (too low) and a DOUBLE FREE (too high) are
different numbers. `rc` alone cannot tell them apart and is not used.

    shape                                            want  base   A   B   B'
    s01 local, empty user drop, droppable field         1     1    0   1   1
    s02 local, drop body MOVES its field out            1     2    1   2   2
    s03 assignment over a local                         2     1    0   2   2
    s04 field of a Drop-LESS holder                     1     0    0   1   1
    s05 field of a holder WITH a user drop              1     0    0   1   1
    s06 enum local WITH a user drop                     1     0    0   1   1
    s07 enum local, NO user drop            (control)   1     1    1   1   1
    s08 tuple element                                   1     0    0   1   1
    s09 array element x2                                2     0    0   2   2
    s10 moved into a callee                 (control)   1     1    0   1   1
    s11 two user-Drop levels                            1     0    0   1   1
    s12 `fn drop(self: &mut R)`, local                  1     1    0   1   1
    s13 `fn drop(self: &mut R)`, as a field             1     0    0   1   1
    s14 two droppable fields                            2     2    0   2   2
    s15 assign-over, drop body moves field              2     3    2   4   4
    s16 drop body moves field, value as a FIELD         1     1    1   2   2
    s17 partial move of a field             (control)   2     2    2   2   2
    s18 assignment over a FIELD (`h.p = mk()`)          2     0    0   2   2
    s19 holder in a holder, two levels deep             1     0    0   1   1
    s20 drop body moves field under `if armed`  armed   1     2    1   2   2
    s20 the same guard DISARMED                         1     2    1   2   2
    s21 the same conditional move in a PLAIN fn         1     1    1   1   1
    s22 enum whose drop body destructures `self`     1001  1001 1001 2003 1002

    base wrong on 12 of 23 · A wrong on 15 · B wrong on 5 · B' wrong on 4

PREDICTED BY NAME BEFORE THE RUN (docs/probes/dropdesign-2026-09-08/TARGET_ROWS.txt):
arm B predicted exactly, all 17 shapes then written. Arm A's prediction was
wrong in two cells — s10 (predicted "?", measured 0: a value moved into a callee
also loses its fields) and s15 (predicted 0, measured 2, which is CORRECT and
for the wrong reason, see §5).

## 3. DESIGN A IS INCOHERENT, AND THE MEASUREMENT SAYS SO IN ONE CELL

s01: a struct with an EMPTY `Drop::drop` body and one droppable field, as a
plain local. Under A the count is 0. The field is never dropped by anyone,
because the drop body does not drop `self`'s fields — the compiler emits no
frame drops for a drop function's own `self`. The spec sentence that A rests on

    intrinsic.drop.owner-drops-fields-after-user-drop:
    "A nested (non-top-level) drop calls only the user `impl Drop` and stops,
     because the by-value `self` consumes its own fields at the drop body's
     scope end."

is FALSIFIED by s01 (0, not 1) and by s04/s05/s11/s13/s19, all 0 today. The
clause's first sentence (the owner recurses after the user drop) is design B and
is pinned by a fixture; its second sentence is a claim about a mechanism that
does not exist.

⚠ AND A CANNOT BE RESCUED BY ADDING THAT MECHANISM. s12/s13 spell the destructor
`fn drop(self: &mut R)` — the Rust receiver, which the tree accepts and which the
SPEC's own pinned fixture `tests/spec/pass/intrinsic_1.logos` uses. A `&mut self`
body cannot consume anything; under A those two shapes read 0 and no drop body
could ever make them 1.

## 4. COST, EVERY COLUMN

    arm       fires   ceiling  pass  cfail  stdlib   RUNTIME (of 6557)
    A dropbody 15777      0      4     0     ok         12
    B dropsite 15746      0      2     0     ok          2
    B' dropsite2 (build2: same edits + the enum double-call fixed)
                   —        —      2     —     ok          2

The B' columns were taken in a SECOND build directory (`build2`, same sources +
the one-shot flag, same toolchain). CONTROL: `build2` UNARMED against `build`
UNARMED over all 6557 runtime triples differs in exactly ONE fixture, and it is
`cast-region-to-uint`, which differs between two runs of the same binary.

`cfail` = `scripts/fail_text_oracle.py`, 1457 `-L bc -L fail` fixtures, rc /
stderr-sha / `.expected`-match: 0 changed for either arm. `stdlib` = all four
layers compile under both arms. `ceiling` 0 for both by construction — neither
arm is a borrow-check mechanism, and `bc_admits.ledger` cannot see a destructor.

THE RUNTIME COLUMN IS THE ONLY ONE THAT SEES THIS CLUSTER: it found 12 damaged
fixtures for arm A where the pass column found 4. `cast-region-to-uint` is
subtracted by name in both (it prints a stack address).

ARM A, the 12 (base → A, `ccrc/runrc/stdout-sha`):

    drop-early-return-nested-b167          runrc 0 -> 2
    drop-nested-field-dr                   runrc 0 -> 1
    drop_nested_explicit                   stdout sha af0695fe -> 0eeee5e1
    intrinsic_1                            runrc 0 -> 31        (THE SPEC FIXTURE)
    zoned_storage_pin_acceptance           runrc 42 -> 6
    zz_btree_dyn_probe                     runrc 0 -> 52
    cond_move_field_overlap                stdout sha changed
    no_auto_drop_container_ctl             stdout sha changed
    no_auto_drop_sibling                   stdout sha changed
    no_auto_drop_sibling_ctl               stdout sha changed
    placedrop_box_deref_field_drop_old     stdout sha changed
    ptr_drop_in_place_recurses             stdout sha changed

Three of these are ported rustc tests whose own headers state design B in
words — "user drop body runs, then fields" (`drop_nested_explicit`), "its Drop
runs AND each owned field with its own Drop impl runs too"
(`drop-nested-field-dr`), "Both Drops must run on the early-return path"
(`drop-early-return-nested-b167`) — and the fourth is the SPEC's own fixture for
the clause. Arm A does not merely cost 12 fixtures; it contradicts the language's
written rule at the site that pins it.

ARM B, the 2:

    drop-trait-enum-b154         runrc 0 -> 1
    drop_glue_three_levels       stdout "b3" -> "b3 a7"

## 5. THE SHAPES B MAKES WORSE, AND THE ONE FAMILY NEITHER DESIGN CLOSES

B is wrong on exactly one property: A BY-VALUE `Drop::drop` BODY THAT MOVES ITS
OWN FIELD OUT. s02 (2, want 1), s15 (4, want 2), s16 (2, want 1), s22 (payload
dropped twice), and the corpus instance `drop-trait-enum-b154`, whose drop body
is `match self { Foo::Nested(s, c) => … }` — the destructure binds the payload
by value and consumes it.

The stdlib has a live instance: `stdlib/mem/manually_drop/manually_drop.logos`

    impl<F: FnOnce() -> ()> Drop for DropGuard<F> {
        fn drop(self: DropGuard<F>) { if self.armed { let f = self.f; f(); } }
    }

— a CONDITIONAL move-out of its own field. No fixed answer is right for it: when
armed the body owns `f`, when disarmed the site must drop it. Only a recorded
moved-path (the machinery `skip_paths` / `moved_fields` already implements for
ordinary locals) can be right in both branches.

⚠ AND THAT SHAPE HIDES A THIRD DEFECT, UNROWED, THAT EXISTS UNARMED. s20 is that
guard with a counter: base reads 2 in BOTH the armed and the disarmed spelling.
Disarmed, the body never executes the move, and the field is still dropped twice
— so a `let q = self.f;` inside an `if` in a drop body is dropped
UNCONDITIONALLY at the body's epilogue. The control s21 is the same conditional
move in a PLAIN function and reads 1, correct, on all three binaries. The
discriminator is `self` of a `Drop::drop` body. This is a DOUBLE FREE in the
unmodified compiler with no row.

## 6. THE CRUDE ARM AND THE CORRECT ONE ARE NOT THE SAME PROGRAM (rule 7)

Arm B's scope-exit enum spelling re-enters `gen_drop_value` on the whole value,
which calls the user drop a SECOND time. Measured on s22: 2003 = user drop twice
(2000) + payload three times. Arm B' adds a one-shot `probe_skip_user_drop_`
flag so the site asks for the payload recursion only: s22 reads 1002 — user drop
once, payload twice (the s02 family, unfixed).

B' does not rescue `drop-trait-enum-b154`: it still fails, one check later
(exit 2 instead of exit 1 — the Nested arm, where the payload is double-dropped,
rather than the Simple arm, where the user drop ran twice). BOTH of arm B's costs
are REAL under B': runtime damage 2 of 6557, the same two names.

## 7. WHAT EACH DESIGN DOES TO THE QUEUE

All 80 queue programs were compiled, linked and RUN on one binary under base, A
and B; five rows move.

    row                                          base    A      B / B'
    replace_site_skips_field_drop_glue           rc 11   rc 10  rc 0   CLOSED by B
    enum_user_drop_skips_payload_glue            rc 1    rc 1   rc 0   CLOSED by B
    drop_body_moving_field_double_drops_local    rc 1    rc 0   rc 1   CLOSED by A
    letstruct_destructure_skips_user_drop        rc 100  rc 100 rc 100 unmoved
    enum_payload_partial_move_leak               rc 1    rc 1   rc 1   unmoved
    box_field_move                               rc 134  rc 0   rc 134 MASKED by A
    homonym_field_drop_glue_segv                 rc 139  rc 3   rc 139 MASKED by A

The last two are not closings. Arm A turns a double free (134) and a SIGSEGV
(139) into a clean exit by not emitting the drop at all — the leak replaces the
corruption and the row's own oracle (for `homonym_*`, the `nm -C` symbol) is
untouched. A design that makes a row's program exit 0 while leaving its defect is
a gate red, not a fix.

## 8. THE HANDED-DOWN CLUSTER IS A SYMPTOM, NOT A ROOT (rule 17, rule 13)

The prompt named 14 tier-1 rows. Each row header already names a root, and they
are eleven different sites. ONE candidate change moves at most three of them, and
the eleven below are moved by NEITHER arm, measured over all 80 programs:

    shadowed_binding_never_dropped / shadow_over_param_double_drop
        SemaImpl::declare_var — the frame's drop list is keyed by NAME
    homonym_field_drop_glue_segv          package-blind resolve at a FIELD
    both_drops_destructor_is_inherent     collect_fn's G156-5 filing site
    weak_local_never_dropped              Weak<T>'s Drop does not run
    operator_autoref_temp_never_dropped   push_operand bypasses materialize_recv_ref
    self_call_in_return_kills_param_drops  body_ever_moved_ epilogue skip list
    index_place_through_refmut_never_drops_old  lower_place_assign root predicate
    variant_payload_nested_struct_sub_double_drops  pattern-synth binding mode
    letstruct_destructure_skips_user_drop / enum_payload_partial_move_leak
        the moved-path record — a THIRD mechanism, and the one the DropGuard
        shape in §5 also needs

## 9. FINDINGS ABOUT FIXTURES, NOT LICENCES

* `tests/logos/pass/drop_glue_three_levels` — its own comment says
  "c drops via glue → B::drop(b3) → A::drop(a7)"; `.expected` pins `b3`. Under B
  it prints `b3 a7`, which is the comment, the spec clause and Rust. The pin
  asserts the defect. NOT EDITED.
* `tests/imported/pass/drop/drop-trait-enum-b154` — its header says the enum's
  Drop impl "matches `&Self`"; the code is `fn drop(self: Foo)` and its body
  `match self` MOVES the payload out. The port changed the receiver, and the
  changed receiver is what makes B double-drop. NOT EDITED.

## 10. THE TABLE THE OWNER DECIDES FROM

    ────────────────────────────────────────────────────────────────────────
                                        A  "the BODY owns"   B  "the SITE recurses"
    ────────────────────────────────────────────────────────────────────────
    queue rows CLOSED                   drop_body_moving_    replace_site_skips_
                                        field_double_drops_  field_drop_glue
                                        local (1)            enum_user_drop_skips_
                                                             payload_glue (2)
    queue rows LEFT                     the other 4          drop_body_moving_field_
                                                             double_drops_local,
                                                             letstruct_destructure_
                                                             skips_user_drop,
                                                             enum_payload_partial_
                                                             move_leak
    queue rows MASKED (not closed)      box_field_move,      none
                                        homonym_field_drop_
                                        glue_segv
    hand shapes correct (of 23)         8                    18  (19 for B')
    shapes made WORSE than base         12 (all LEAKS,       3 (s02 s15 s16, all
                                        incl. 4 correct      DOUBLE DROPS; s22 also
                                        today: s01 s10       under crude B only)
                                        s12 s14)
    pass corpus cost                    4                    2
    cfail (1457 fixtures)               0                    0
    stdlib four layers                  ok                   ok
    RUNTIME oracle (6557 fixtures)      12 damaged           2 damaged
    contradicts the SPEC's own          YES — intrinsic_1    no
      pinned fixture                    runrc 0 -> 31
    possible at all under `fn drop(     NO — s12/s13 read 0  yes
      self: &mut T)`                    and no body can
                                        recover them
    ────────────────────────────────────────────────────────────────────────

## 11. VERDICT AND WHAT DESERVES FUNDING

DESIGN A IS NOT A CANDIDATE. It is not "more expensive"; it is incoherent for
the `&mut self` destructor the tree accepts and the spec fixture uses, it
falsifies its own spec sentence at s01, and it turns two queue rows' corruption
into leaks without fixing either. That half of the question is answered by
measurement, not by preference.

DESIGN B IS THE LANGUAGE'S OWN RULE and is worth two queue rows plus fourteen
unrowed shapes, at a measured cost of TWO fixtures out of 6557 at run time. But
IT IS NOT LANDED HERE, and the reason is a decision B does not contain:

    Can a `Drop::drop` body move out of `self`?

Rust's answer is no — the receiver is `&mut self` and E0509 forbids it. Logos
accepts `fn drop(self: T)` and the stdlib's `DropGuard` USES the move-out,
CONDITIONALLY, which no fixed answer serves. So the fundable work, in order:

  1. THE OWNER'S CALL on the `Drop::drop` receiver. If by-value `self` is
     retired (or its move-outs refused), B is a pure win and both cost fixtures
     become fixture repairs — `drop_glue_three_levels` starts printing what its
     own comment says, and `drop-trait-enum-b154` gets the `&Self` receiver its
     own header claims it has.
  2. IF by-value `self` stays: B PLUS a moved-path record from the drop body,
     which is the SAME mechanism `letstruct_destructure_skips_user_drop` and
     `enum_payload_partial_move_leak` are waiting for. That is one mechanism
     buying four rows — the best ratio in this cluster and the only grouping in
     tier 1 that survived being tested.
  3. INDEPENDENT OF THE DECISION, and unrowed: the conditional move-out inside
     a drop body (§5, s20) is dropped unconditionally at the body's epilogue —
     a double free in the unmodified compiler, in both the armed and the
     disarmed spelling, with a plain-function control that is correct.

## 12. HYGIENE

The probe arms were reverted before the round closed; `build2` was deleted and
`build` rebuilt from the restored sources. Probes are recorded here and not left
in any compiled source. `build_hash.py` reads eaac8e4b73c1fe24 43 again — byte for byte the
pre-round value. ⚠ A previous round recorded that this hash MOVES across a
rebuild of an unchanged tree; on this tree, today, it did not. Either way the
evidence for the revert is `git status` plus the rebuild, not the hash.
L1 rc 0 (780/780, gates 156/156). Queue gate rc 0, 80 rows, `# TOTAL` 80.
The 23 shape programs and the runner are kept beside this file so any binary
can be re-measured; they are not registered tests.
