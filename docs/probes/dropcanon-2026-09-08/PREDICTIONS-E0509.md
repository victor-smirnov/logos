# PREDICTION FOR THE BORROW-CHECK HALF (E0509, both sites)

Written 2026-09-08, AFTER the source edit and BEFORE the armed binary existed.

## THE ROWS PREDICTED TO CLOSE, BY NAME — 3 QUEUE + 4 bc_admits = 7

soundness_queue (each is an ILLEGAL program; the fix is that it is REFUSED):

    drop_body_moving_field_double_drops_local              run 1  -> refused
    drop_body_conditional_move_double_drops_both_paths     run 1  -> refused
    letstruct_destructure_skips_user_drop                  run 100 -> refused

bc_admits (each is `logos_00_bc_admit_*`, oracle = a silent compile):

    borrowck-move-out-of-struct-with-dtor       dotted path         -> refused
    borrowck-struct-update-with-dtor--b         field-init from base -> refused
    borrowck-struct-update-with-dtor--t17       FRU                  -> refused
    borrowck-move-error-with-note--b            MATCH door           -> refused
                                                (the second site; without it
                                                 this row does NOT close)

## PREDICTED NOT TO CLOSE, WITH THE NUMBER THAT SAYS WHY — 2

    borrowck-move-out-of-tuple-struct-with-dtor--r13    ZERO fires
    borrowck-move-out-of-tuple-struct-with-dtor--t13    ZERO fires

Their moved field is `struct Inner { a: i64 }`, which DIVERGENCES A16 structural
auto-Copy makes `Copy`, so `is_move_type` is false and no move is seen at all.
That is a Copy-inference question and no E0509 rule can reach it. Both stay in
bc_admits.ledger with root `bck.NEW-A16`.

## COST PREDICTED — ZERO, and the two things that would have cost

  · `tests/spec/pass/intrinsic_1.logos` — retired in commit 0e8f1cc97.
  · `stdlib/mem/manually_drop`'s `DropGuard` — repaired in commit 0e8f1cc97,
    and `tests/logos/pass/drop_guard_instantiated` is the fixture that makes
    the repair checkable at all. If that repair were wrong the stdlib would
    not compile, and `stdlib-cost.sh`'s four layers is the oracle.
  · `tests/imported/pass/drop/drop-trait-enum-b154` — its `Nested` arm was
    re-ported to upstream's own `_` in commit 1979d72f4.

## THE OVER-REFUSAL COUNTER-EXAMPLES — five LEGAL programs, shapes the pricing
## phase did not use, each of which MUST still compile and run

    legal1  a `Drop` owner reborrowed through `&mut self`, `poke()`/`peek()`
    legal2  the WHOLE `Drop` value moved through three hops; then destructured
            out of a Drop-LESS struct; then out of a Drop-LESS enum by `match`
    legal3  a GENERIC Drop-less container `Cell<T>` whose T impls Drop, drained
            by a generic `take<T>(c: Cell<T>) -> T` — sibling still drops (2)
    legal4  the documented escape hatch: `#[no_auto_drop]` + `ptr::read` out of
            a value that DOES impl Drop (this is stdlib DropGuard's shape)
    legal5  a `match` on a `Drop` enum binding only `_` and a Copy field

A refusal of ANY of these falsifies the rule, not the count.
