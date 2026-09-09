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
