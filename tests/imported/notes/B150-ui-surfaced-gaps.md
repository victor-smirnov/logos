# B150 — UI-surfaced gaps

Batch B150 imported **25 DISTINCT rustc UI run-pass tests** (pinned SHA
`4b0c9d76ae7d387229caea55cfa73c280b08b8a7`), mined for FEATURE COVERAGE across
expr/if-as-value, nested fn items, closures (untyped/mixed-width-arg), fn-ptr
params, generic fns/structs, recursive/explicit enum discriminants, struct-update
syntax, binding-after-`@`, inclusive-range patterns, struct-variant `{..}` rest
patterns, trait inheritance with static supertrait methods, method-generic default
methods, `Copy`-bound implicit copy, mut->imm reborrow coercion, array-repeat
codegen, and uninhabited-enum exhaustive match. Areas: pattern (4), enum (4),
functions-closures (3), generics (3), traits (3), expr (2), binop (1), coercion (1),
iterators (1), mir (1), structs-enums (1), tuple (1). Do NOT modify the
compiler/stdlib. All 25 compile + link + exit 0.

Suffix `-b150` on every file (global ctest-name uniqueness). De-duplicated against
`RUSTC-PROVENANCE.md` and per-file `Original path:` headers (no source-path overlap
with prior batches).

## ⚠️ SILENT MISCOMPILE — highest priority

### G150-2 — ⚠️ SILENT MISCOMPILE: `Option<T> == Option<T>` compiles + runs but returns a WRONG answer (always false)

`Option` has **no `PartialEq` impl** in the Logos stdlib
(`stdlib/lang/option/option.logos` defines no `fn eq` / `impl PartialEq`). Yet
`a == b` over two `Option<i64>` values **does not error at compile time** — it
compiles, links, runs, and silently yields the wrong result.

Minimal repro:
```
package cmp;
fn main() -> i64 {
    let a: Option<i64> = Some(3);
    let b: Option<i64> = Some(3);
    if a == b { return 0; }   // should be true
    return 5;                 // <-- actually taken (exit 5)
}
```
Also `None == None` returns false (exit 5). Matching the same values
(`match (a,b) { (Some(x),Some(y)) => x==y, .. }`) gives the CORRECT answer, so the
payloads are fine — only the `==` operator on the enum is broken. The `==` appears
to fall back to comparing something other than the structural value (discriminant
pointer / handle) WITHOUT requiring or finding a `PartialEq` impl.

- **Tractability: moderate.** Two-part: (a) sema should REQUIRE a `PartialEq`/`Eq`
  impl for `==` on a user/std enum and error if absent (currently silent), and
  (b) provide a (blanket or generated) `PartialEq for Option<T: PartialEq>` in
  stdlib so the natural form works.
- **§B-catch-up** (Rust parity: `Option: PartialEq` is standard; the silent
  fallback is the dangerous part).
- Dropped on this gap: `structs-enums/compare-generic-enums.rs` (whole test is
  `Option == Option`).

## NEW gaps surfaced (no crashes/SIGSEGV in this batch)

### G150-1 — `ref IDENT @ Pattern` is a parse error (binding-mode `ref` combined with binding-after-`@`)

`x @ Pat` works; `ref x @ 1..=10` and `ref x @ A { .. }` are **syntax errors**
(`syntax error near 'x'`). Plain `ref x` (without `@`) and plain `x @ Pat` both
parse; only their combination fails.

Minimal repro:
```
package at;
fn main() -> i64 { match 5 { ref x @ 1..=10 => 0, _ => 1 } }
//                            ^ syntax error near 'x'
```
- **Tractability: trivial-moderate** (grammar: allow `ref`/`ref mut` prefix before
  an `@`-binding in the pattern production).
- **§B-catch-up.**
- Worked around in `pattern/nested-patterns-at-b150` by dropping `ref` (by-value
  bindings preserve the binding-after-`@` essence).

### G150-3 — tuple-struct numeric-field struct-literal `S { 0: .., 1: .. }` is a parse error (construction side)

For a tuple struct `struct S(u8, u16)`, the struct-literal form using positional
numeric field names is a **syntax error** at the opening brace:
```
package nf;
struct S(u8, u16);
fn main() -> i64 { let s = S { 1: 10, 0: 11 }; return 0; }
//                            ^ syntax error near '{' (col 15)
```
Tuple-struct call form `S(11, 10)` works; only the `{ 0:.., 1:.. }` literal form
fails. (The matching pattern form `S { 0: a, 1: b, .. }` was not separately reached
because construction fails first.)
- **Tractability: moderate** (grammar/sema: accept integer field-name keys in a
  struct-literal targeting a tuple struct).
- **§B-catch-up.**
- Dropped on this gap: `structs-enums/numeric-fields.rs`.

### G150-4 — `Iterator::sum()` not usable on ranges / `.iter()` chains ("receiver is not a struct (got <error>)")

`(0..3).map(|i| i).sum()`, `(0..3).sum::<i64>()`, and `arr.iter().cloned().sum()`
all fail with `method call: receiver is not a struct (got <error>)`. The `Sum`
trait and `Iterator::sum<S: Sum<Item>>` exist in `stdlib/lang/iter/iter.logos`, but
the terminal `.sum()` does not resolve on the range/slice-iterator receivers
(consistent with the documented iterator-terminal limitations). `.iter()` on a
fixed array also misbehaves in a `for` loop (emits "unreachable code after
terminator" and fails).
- **Tractability: moderate-deep** (relates to the existing iterator-terminal /
  method-generic-on-iterator dispatch baghunts).
- **§B-catch-up.**
- Dropped on this gap: `iterators/iter-cloned-type-inference.rs`,
  `iterators/iterator-type-inference-sum-15673.rs`. (`iterators/iter-range` — a
  manual `FnMut`-driven loop, no `.sum()` — was imported successfully.)

## Pre-existing / known-divergence drops (NOT new gaps, recorded for the trail)

- `pattern/unit-pattern-matching-in-function-argument-7519.rs` — `fn foo((): ())`
  unit-pattern parameter is a parse error AND `fn foo(x: ())` is explicitly
  rejected by sema ("unit-typed parameters carry no information"). Logos design
  choice; **§A-blessed-divergence**. Dropped.
- `traits/false-ambiguity-where-clause-builtin-bound.rs` — `where Option<K>: Sized`
  (a constructed-type bound, not a bare type-param, in a where clause) is a parse
  error. Likely the same family as other where-clause-shape gaps; not separately
  pursued. Dropped.
- `generics/generic-recursive-tag.rs` — recursive enum `enum List<T>{Cons(T,List<T>),Nil}`
  errors "infinite-size enum; box the payload with `*const List`". Logos enums are
  not auto-indirected on self-recursive value payloads (the Box/DST family in
  DIVERGENCES). Dropped (Box is a conventionally-dropped feature, and routing
  through `*const` would gut the test).
- `traits/inheritance/num1.rs` — `NumExt: NumCast + PartialOrd` requires
  `i64: PartialOrd`, which the stdlib does not provide. Imported with the
  `+ PartialOrd` supertrait dropped (NumCast static-supertrait-method essence kept).

## Successfully imported tests (25)

binop/binary-minus-without-space-b150 · coercion/coerce-reborrow-imm-ptr-arg-b150 ·
enum/enum-u8-variant-b150 · enum/enum-with-generic-parameter-5997-b150 ·
enum/issue-23304-2-b150 · enum/zero-variant-enum-b150 · expr/block-fn-b150 ·
expr/if-bot-b150 · functions-closures/closure-immediate-b150 ·
functions-closures/closure-inference2-b150 · functions-closures/fn-lval-b150 ·
generics/generic-exterior-unique-b150 · generics/generic-fn-twice-b150 ·
generics/generic-newtype-struct-b150 · iterators/iter-range-b150 ·
mir/mir-codegen-array-b150 · pattern/inc-range-pat-b150 · pattern/issue-8351-1-b150 ·
pattern/nested-exhaustive-match-b150 · pattern/nested-patterns-at-b150 ·
structs-enums/functional-struct-upd-b150 · traits/bug-7295-b150 ·
traits/copy-trait-implicit-copy-b150 · traits/inheritance-num1-b150 ·
tuple/nested-index-b150
