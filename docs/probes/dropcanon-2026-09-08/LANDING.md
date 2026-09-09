# THE RUST-CANONICAL DROP LANDING — 2026-09-08, THREE COMMITS IN SERIES

Owner decision (Victor, 2026-09-08): *"Делаем Rust-канонично."*
`Drop::drop` takes `&mut self`; moving a sub-value out of a value that
implements `Drop` is E0509 and is REFUSED; drop glue is "run `Drop::drop`, then
drop the fields".

## STEP 1 CENSUS, RUN, NOT COPIED

    git log --oneline -3   5ac67c841 / ca9ec814f / cccfc7e89     status: clean
    # TOTAL   soundness_queue 81 · bc_admits 91 · bc_admits_blocked 15
    awk rows                81 by direct listing (tier1=28 tier2=7 tier3=41 tier4=5)
    probe-log-lint          249 records, every site symbol resolves
    build_hash.py           3bd2c3af1c114227 43  (READ)
    QUEUE GATE              rc 0 — all 81 rows reproduce

CORRECTIONS TO THE PROMPT, re-verified against the prompt text in front of me:
  · the STEP-1 gate command CARRIES `LOGOS_LIB_DIR`. Landed. Stop reporting it.
  · "close, or row by name: the unrowed double free `if self.armed`" — ALREADY
    ROWED as `drop_body_conditional_move_double_drops_both_paths`. Nothing to row.
  · the handed-down report's "custom_dst_smartptr_owning_drop … its recorded
    use-after-free is NOT reproduced by either arm" is CONTRADICTED: it
    reproduces under valgrind on the landed binary (two invalid reads of size 8,
    exit 42 both ways). It was not reproduced because it was never run under a
    memory checker. Recorded in unrowed_backlog.ledger.

## THE CLASS, ENUMERATED BY THE PROPERTY

The property: *a move whose place is a sub-value of a value whose type
implements `Drop`.* Enumerated by writing one hand program per DOOR that can
express it, not by grepping a spelling. TEN doors:

    dotted path            `let q = s.f;`
    destructuring let      `let S { f: x, g: y } = s;`
    tuple-struct pattern   `let S(x, y) = s;`
    functional update      `let s1 = T { a: 2, ..s0 };`
    field-init from a base `let s1 = T { a: 2, b: s0.b };`
    drop body, uncond      `fn drop(self: P) { let q = self.inner; }`
    drop body, conditional `if self.armed { let q = self.f; }`
    generic drop body      `impl<T> Drop for W<T>`, instantiated
    generic plain fn       `fn take_out<T>(g: W<T>) -> T`
    nested path            `let q = a.b.inner;` (B impls Drop)
    MATCH struct pattern   `match s { S { f: a } => .. }`      <- SEPARATE SITE
    MATCH enum payload     `match e { E::V(x) => .. }`         <- SEPARATE SITE

The first ten land at ONE site in `borrow_check.cpp` (sema rewrites the `let`
forms into a dotted read). The two `match` doors reach a DIFFERENT site and
produce ZERO fires at the first — a structural hole, not a predicate answering
wrong. So the class is TWO sites, and a landing that closes only the first
leaves `borrowck-move-error-with-note--b` and `drop-trait-enum-b154` open. Both
are closed here.

Legal controls, all silent (over-refusal check, six shapes): a field moved out
of a Drop-LESS struct · the WHOLE Drop value moved · a Copy field read · `&s.f`
· a field moved out of a `#[no_auto_drop]` wrapper · a drop body that only READS.

## THE E0509 LANDING, MEASURED — 12 DOORS AT 2 SITES

Site 1, `borrow_check.cpp` FieldRead/TupleIndex `moving` branch: dotted path ·
destructuring `let` · tuple-struct pattern · FRU · field-init from a base ·
drop-body move (unconditional, conditional, generic) · generic plain fn ·
nested path. All fire, with the type named.

Site 2, `visit_stmt`'s `Code::Match` arm: the enum-payload door and the
struct-pattern door.

⚠ THE STRUCT-PATTERN MATCH DOOR NEEDED ITS OWN WALK, AND FINDING OUT WHY IS THE
ONE THING IN THIS ROUND A READING WOULD NOT HAVE GIVEN. `each_pat_binding`'s
`PC::Struct` case hands every binder a NULL `TypeRef` — there is no type on a
`PatFieldBinding` — and `is_move_type(nullptr)` is false, so the arm saw
`match s { S { f: x } => .. }` as binding nothing that moves. Measured: ZERO
fires even with `String` fields, while the ENUM arm one token away fired
correctly. That null is deliberate and four other consumers read it, so the
field type is resolved AT THE SITE from the pattern's own `struct_name()` via
`ts_.struct_by_name`, not changed under them.

## THE OVER-REFUSAL CONTROLS — five LEGAL programs, all still compile and RUN

    legal1  a Drop owner reborrowed through `&mut self` (`poke()`/`peek()`)
    legal2  the WHOLE Drop value moved through three hops; then out of a
            Drop-LESS struct; then out of a Drop-LESS enum by `match`
    legal3  a generic Drop-less `Cell<T>` drained by `take<T>(c) -> T`,
            sibling still drops (count 2)
    legal4  `#[no_auto_drop]` + `ptr::read` out of a value that DOES impl Drop
            — the documented escape hatch, and the stdlib's own shape
    legal5  a `match` on a Drop enum binding only `_` and a Copy field

## THE PLACE IN THE DIAGNOSTIC IS THE USER'S SPELLING OR NOTHING

Sema rewrites `let S{f:x} = s;` into `let __dst_0 = s; let x = __dst_0.f;`, so
the checker's root at that door is a compiler temporary. The first landing
printed `cannot move out of '__dst_0.f'` — a diagnostic naming a binding in no
source file. It now prints `cannot move out of field 'f' of the destructured
value`. Read, not inferred from an exit code.

## TWO UNPREDICTED CLOSINGS, AND THE LESSON THEY CARRY

The E0509 prediction named 7 rows. NINE closed. The two it did not name are
`nll/enum-drop-access` and `nll/issue-52059-report-when-borrow-and-drop-conflict`,
both of which express their defect as `fn f(x: DropStruct) -> &mut T { return
x.field; }` — a move out of a `Drop` value, which rustc also refuses as E0509
before E0713 is ever consulted. The refusal is correct and the ports are illegal.

⚠ BUT THEIR ROOT IS NOT CLOSED, AND THIS IS THE ROUND'S SHARPEST FINDING:
**a stronger rule landing upstream of an older one RETIRES THE OLDER ONE'S
INSTRUMENT while leaving the older rule's defect exactly where it was.** Those
two rows were the corpus's only observations of the E0713 hole (a `&mut` handed
out past a destructor that then writes through it). They can no longer reach it.
Filed as `e0713_mutref_past_destructor_needs_a_new_carrier`.

It happened THREE more times in the same landing, and where a control was
possible one was written:
  · `nll/move-subpaths-moves-root` and
    `moves/moves-based-on-type-cyclic-types-issue-4821` pinned the E0382
    partial-move sentence on programs whose owners impl `Drop`. Both now report
    E0509 first — again as rustc does — so both are re-pinned to the upstream
    sentence AND `tests/logos/fail/partial_move_root_use_ctl` was added: the same
    program one token apart, `impl Drop for T2` deleted, pinning the partial-move
    sentence so that rule keeps an observation.
  · `tests/logos/pass/cond_move_field_overlap`'s `ov_userdrop` subject moved
    `o.i.p` out of a `Drop` ancestor and pinned the result with the words "the
    pin records what the compiler ACTUALLY does" — a pass fixture asserting the
    divergence the owner retired. The subject is NOT deleted: it is now
    `tests/logos/fail/cond_move_field_overlap_userdrop_e0509`, byte for byte,
    asserting the refusal. The other 19 subjects of that corpus are untouched.

## RUNTIME ORACLE, DIFFED BOTH WAYS (6562 vs 6565 fixtures, compiled+linked+RUN)

No row present on one side only except the three new pass halves. TWO triples
differ: `cast-region-to-uint` (stdout sha; subtracted by name, it prints a stack
address) and `cond_move_field_overlap` (ccrc 0 -> 1, the `ov_userdrop` refusal
above, repaired here). Nothing else in the whole corpus moved at run time.
