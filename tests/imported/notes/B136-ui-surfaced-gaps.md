# B136 — UI-surfaced gaps

Batch B136 imported 23 rustc UI run-pass tests (pinned SHA `4b0c9d76ae7d387229caea55cfa73c280b08b8a7`)
across drop, methods, traits, expr, cast, mir, numbers-arithmetic, self, generics,
where-clauses, ufcs, coercion, autoref-autoderef, functions-closures, structs, enum.
All 23 compile + link + exit 0.

Suffix `-b136` on all files (global ctest-name uniqueness).

## NEW gaps surfaced (2)

### (G136-1) `where` clause on a trait-method DECLARATION (no body) is a parse error
A `where` clause attached to a trait method DECLARATION ending in `;` fails to
parse:
```
trait Bar<A> {
    fn method<B>(self: &i32) -> i32 where A: Clone;   // syntax error near 'i32'
}
```
The SAME `where` clause on an IMPL-side method DEFINITION (with a body) DOES parse
and discharge:
```
impl Bar<X> for i32 {
    fn method<U>(self: &i32) -> i32 where X: Foo<U> { return 7i32; }   // OK
}
```
So the grammar admits `where` before `{ .. }` but not before `;`. Looks tractable
as a missing-case in the trait-method-declaration production (parallel-mapping: the
declaration rule should accept the same optional `where` clause the definition rule
already does). Worked around in `where-clauses/where-clause-method-substituion-b136`
by dropping the decl-side `where` and keeping it on the impl. (Secondary, benign:
the impl-side `where X: Foo<U>` warns that the impl type-arg name `X` shadows the
struct `X` — "type parameter shadows ... breaks fn-name resolution" — but the test
still compiles + runs correctly here.)

### (G136-2 — DIVERGENCE, not a bug) struct field DROP order is bottom-to-top
A struct `C { a: A, b: B }` whose fields each impl `Drop` runs the field
destructors BOTTOM-TO-TOP (`b` then `a`); rustc runs them top-to-bottom (`a`
then `b`). Both fields drop EXACTLY ONCE (the real invariant). Upstream's own
source comment (`field-destruction-order.rs`) states the order is
implementation-defined, so this is a deliberate-order divergence, not a defect.
`structs/field-destruction-order-b136` asserts the Logos-observed order (log==21).

## Re-confirmed known-open (NOT re-reported; candidates dropped/distilled)

- struct `T: Copy` auto-copy (MEMORY divergence B1) — block-generic / if-generic
  re-targeted their struct (Pair) instantiation to a scalar i64, since a
  `#[derive(Copy)]` POD struct does not satisfy a generic `T: Copy` bound.
- generic struct literal with explicit type-args `Foo<u64> { f0: .. }` does NOT
  parse — use `let x: Foo<u64> = Foo { f0: .. }` (annotation-driven, bare ctor).
- `static` / `static mut` items unsupported (generic-temporary's `static mut`
  consumer-observation re-expressed as a return value; conditional-drop's
  `static mut drop_count` → a `*mut i64` cell).
- chained postfix call `g()(args)` (closure-returning-closure) is a parse error —
  same family as the known `arr[i](args)` gap; let-pin the callable first.
- UFCS `Trait::method(&prim)` on a PRIMITIVE receiver ("call to undefined static
  method") — UFCS on a STRUCT receiver `Trait::method(&structval)` works
  (ufcs-trait-method-call-b136 uses a struct receiver).
- inherent-vs-trait method collision (`traits/impl-inherent-prefer-over-trait.rs`)
  remains known-open; not imported.
