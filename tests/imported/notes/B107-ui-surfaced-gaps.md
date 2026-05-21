# B107 — compiler gaps surfaced by the tests/ui run-pass import (2026-05-21)

Porting another 49 `tests/ui` run-pass tests (batch B107) surfaced the
following real Logos compiler gaps. Tests that hit them were SKIPPED
(per policy: don't force/hack a broken port). Each is a candidate for a
fundamental fix; when fixed, the corresponding upstream test becomes a
regression test. None were fixed in this batch (tests-only).

## Parser / grammar

1. **Parenthesized pattern `(pat)`** — `match x { (pat) => … }` (a binding
   wrapped in grouping parens, not a 1-tuple) is a syntax error.
   Hit by `binding/pat-tuple-7.rs`.

2. **String-literal match arms** — `match s { "foo" => …, _ => … }` is a
   syntax error ("args failed to parse as comma-separated expr list" inside
   `assert_eq!`, and a bare syntax error in plain match). Logos has no
   string-literal patterns. Hit by `issues/issue-22008.rs`,
   `binding/borrowed-ptr-pattern-2.rs`, `pattern/const-pattern-str-match-lifetime.rs`.

3. **`static` item inside a fn body** — `fn main() { static A: &char = &'A'; }`
   is a syntax error near the following token. Hit by
   `borrowck/borrowck-static-item-in-fn.rs`.

4. **`\u{…}` char escape with embedded underscores** — `'\u{10__FFFF}'` is
   rejected by the lexer (the plain `'\u{10FFFF}'` form works). Hit by
   `issues/issue-43692.rs`.

## Sema / type & pattern checking

5. **Struct pattern with a refutable field sub-pattern** — `match v { Foo { f: 0 } => … }`
   reports "struct pattern: refutable field sub-pattern not yet supported".
   Irrefutable binds (`Foo { f: _f }`, `Foo { .. }`) work; a literal/constructor
   sub-pattern in a field does not. Hit by `binding/match-struct-0.rs`,
   `pattern/usefulness/nested-exhaustive-match.rs`.

6. **Shorthand struct field patterns with modifiers** — `Foo { ref mut x, .. }`,
   `Foo { ref x, ref y }`, `Foo { mut x, .. }` are syntax errors; the explicit
   `Foo { x: ref mut x, .. }` form is required. (Worked around in
   `bind-field-short-with-modifiers`.)

7. **Tuple / reference patterns in `let`** — `let (&x, &y) = (&3, &'a');` →
   "'let <pattern> = expr;' currently supports struct patterns only". Hit by
   `binding/borrowed-ptr-pattern-infallible.rs`.

8. **UFCS static trait-method call** — `Foo::<i32>::get(&x)` →
   "call to undefined static method 'Foo::get'". Method-call form `x.get()`
   works. (Worked around in `typeck/ufcs-type-params`.)

9. **Tuple-struct name used as a constructor value** — `let f = A; f(true)`
   (A a tuple struct) → "undefined variable 'A'" / "call to undefined function
   'f'". Hit by `issues/issue-5315.rs`.

10. **Supertrait-item shadowing** — when both a trait and its subtrait declare
    a same-named method and both are blanket-impl'd (`impl<T> A for T` +
    `impl<T> B for T`, `B: A`), a call reports "ambiguous blanket impl … both
    A and B apply" instead of resolving to the subtrait item. Hit by the
    `methods/supertrait-shadowing/*` group.

11. **`T: Add` over primitives** — stdlib's `Add` trait has no primitive impls,
    so `fn foo<T: Add>(a: T, b: T)` cannot be instantiated at `u8`/`u16`.
    Hit by `generics/generic-extern-mangle.rs`.

## Codegen / mlir-gen / LLVM lowering

12. **Field projection off a call result** — `tuple2().1` (project a field
    directly from a function-call return) fails MLIR verification
    ("'llvm.getelementptr' op operand #0 must be LLVM pointer type … got
    struct"). Binding to a local first (`let t = tuple2(); t.1`) works.
    (Worked around in `mir/mir_cast_fn_ret`.)

13. **Tuple relational comparison** — `a < b` / `<=` / `>` / `>=` on tuples
    lowers to `arith.cmpi` on the tuple **pointer** (MLIR verify failure:
    "operand #0 must be signless-integer-like, but got '!llvm.ptr'") instead
    of a lexicographic element compare. `==` / `!=` work. Hit by
    `binop/structured-compare.rs`.

14. **Uninitialized-local conditional assign across a `loop {}`** — a
    `let mut x: i64;` assigned only inside one `if/else` branch (with a
    `loop {}`/match) lowers to an `arith.constant` that fails LLVM
    translation ("missing LLVMTranslationDialectInterface … for arith.constant").
    Hit by `binding/match-join.rs`.

15. **Multi-arm guarded+unguarded `Some` over `Option<i8>`** — a match with
    `Some(x) if g => …`, `Some(_) => …`, `None => …` returning a value fails
    MLIR (func.call operand type mismatch i32 vs ptr in the assert path).
    Same family as the B106 `Option<i64>` deferral. Hit by
    `mir/mir_match_arm_guard.rs`.

## Borrow checker

16. **Match arms each yielding a distinct `&mut` of the same place** —
    `match k { 22 => &mut foo, 44 => foo.twiddle(), … }.emit()` →
    "cannot borrow 'foo': 'foo' is already mutably borrowed". Hit by
    `nll/issue-48070.rs`.

## Compiler crashes (highest priority)

17. **`@`-binding over an inclusive range pattern SEGFAULTS** — `match x { e @ 1..=100 => … }`
    crashes logosc (SIGSEGV). Hit by `issues/issue-35423.rs`.

## Runtime aborts / segfaults (codegen produces bad code)

18. **for-loop over `&[Struct; N]`** — `for elt in &arr { … elt.field … }`
    over a fixed array of structs compiles but segfaults at runtime
    (exit 139). Hit by `for-loop-while/for-destruct.rs`.

19. **Nested / loop-driven FnMut closures capturing mutable locals** —
    closures that capture `&mut`-locals (an index counter, an accumulator)
    and are invoked repeatedly inside a higher-order driver abort at runtime
    (exit 134). Hit by `for-loop-while/foreach-nested.rs`,
    `for-loop-while/foreach-put-structured.rs`.

## Fragile interaction (cascade)

20. **Self-recursive struct via `*mut T` field + trait impl** — a
    `struct T { next: *mut T }` with an `impl Trait for T` triggered an
    unrelated cascading sema error inside `stdlib/lang/iter/iter.logos`
    (FilterIter `pred` field type mismatch). Hit by `typeck/issue-2063.rs`;
    skipped to avoid masking the real (separate) iter-template issue.
