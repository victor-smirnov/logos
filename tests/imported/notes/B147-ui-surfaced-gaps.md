# B147 — UI-surfaced gaps

Batch B147 imported 28 DISTINCT rustc UI run-pass tests (pinned SHA
`4b0c9d76ae7d387229caea55cfa73c280b08b8a7`) mined from genuinely less-tapped run-pass
areas: regions (5), binding (5), traits (3), recursion (2), drop (2), coercion (1),
block-result (1), builtin-superkinds (1), enum (1), fn (1), for-loop-while (1),
generics (1), match (1), nll (1), structs (1), type-alias (1).
Do NOT modify the compiler/stdlib. All 28 compile + link + exit 0.

Suffix `-b147` on every file (global ctest-name uniqueness). De-duplicated against
`RUSTC-PROVENANCE.md`, `pass/<area>/`, and per-file `Original path:` headers
(B6/B100+/B107/B133–B146/Bnever). The heavily-mined binding/match/expr/moves dirs were
re-checked file-by-file and skipped where already imported.

## NEW gaps surfaced (distinct from B136–B146 / Bnever known-open)

### G147-1 — a 1-tuple pattern `(x,)` (trailing comma) is a parse error (TRACTABLE)

A one-element tuple pattern with the mandatory trailing comma — `(x,)` — is a syntax
error (`syntax error near ','`), in BOTH a `let` destructure and a `match` arm:

```
let x: (char,) = ('d',);
let (y,) = x;          // syntax error near ','
match ('c',) { (x,) => .. }   // syntax error near ','
```

The 1-tuple TYPE annotation `(char,)` and the 1-tuple LITERAL `('d',)` both parse; only
the PATTERN form `(y,)` / `(x,)` fails. Tractability: TRACTABLE — grammar/parser
missing-case in the tuple-pattern production: accept a single sub-pattern followed by a
trailing comma `( <pat> , )` as a 1-tuple pattern (it currently rejects the comma after
one element). `tuple/one-tuple.rs` DROPPED (its whole point is 1-tuples).

### G147-2 — same-named method on two `impl Trait<T>` blocks for one type collides (TRACTABLE)

Implementing the same trait at two different type-arguments for one receiver type —
`impl MyTrait<u64> for MyType { fn get(..) }` + `impl MyTrait<u8> for MyType { fn get(..) }`
— errors `duplicate function 'MyType__get'`. Rust treats the two `get` methods as
distinct (selected by the bound's type-arg). The mangling keys only on
`<receiver>__<method>` (and, for collisions, `<receiver>__<trait>__<method>` per the
trait-aware-mangling work) but NOT on the trait's TYPE-ARGUMENT, so two instances of the
SAME trait at different type-args collapse to one symbol. Tractability: TRACTABLE —
extend the collision key to include the trait's type-args (or refuse-then-mangle on a
detected `Trait<A>` vs `Trait<B>` collision). Distinct from the B-mv-02 cross-pkg
same-NAME-different-trait collision (that's two different traits; this is one trait at
two type-args). `traits/multidispatch1.rs` DROPPED.

### G147-3 — `where <Type-with-args>: Bound` (non-type-param where-LHS) is a parse error (TRACTABLE)

A where-clause whose left-hand side is a TYPE with arguments rather than a bare
type-parameter — `where Option<K>: Sized`, `where fn(&A): for<'a> Foo<..>`,
`where for<'a> T: Foo<'a>` — is a syntax error (`syntax error near 'fn'` at the fn line).
The bare-type-param form `where T: Bound` and the inline `<T: Bound>` form both work.
Tractability: TRACTABLE — grammar: the where-predicate LHS production currently admits
only an identifier (type-param); it should admit an arbitrary type (`Option<K>`, a
fn-pointer type) and the HRTB `for<'a>` quantifier on the predicate. Moderate scope
(parser + the sema where-clause discharge needs to handle a concrete-type predicate, but
those are typically trivially-satisfied / redundant). `traits/false-ambiguity-where-clause-builtin-bound.rs`
and `traits/static-outlives-a-where-clause.rs` DROPPED on this.

### G147-4 — a closure literal directly in argument position is a parse error (TRACTABLE; narrow facet)

A closure literal passed DIRECTLY as a call argument — `apply(x, |y| region_identity(y))`
— is a syntax error (`syntax error near '|'`). A closure bound to a `let` first, then
passed, works; and a closure with a BLOCK body directly in argument position
(`pairs(|p| { .. })`, used in B145) also works. So the failing facet is specifically a
closure with an EXPRESSION body (no braces) — `|y| expr` — when it appears directly in
an argument list (the `|` after `,` is mis-lexed). The braced form `|y| { expr }` parses.
NOTE this is narrower than a general "closures in arg position" gap. Tractability:
TRACTABLE — the expr-body closure needs to be accepted in argument position the same way
the block-body closure is. `regions/regions-params.rs` ALSO surfaced a separate
inference gap (see G147-5), so it was DROPPED rather than rewritten with braces.

### G147-5 — closure param type not inferred from the `FnOnce(T)->T` bound's T (fn-family inference)

In `apply<T, F: FnOnce(T)->T>(t: T, f: F)`, a closure passed for `f` whose body forwards
`y` to a fn expecting `&u64` fails: the closure param `y` stays typed as the abstract `T`
and never unifies with the concrete `&u64` call site (`expected &u64, got T`). This is the
same fn-family closure-arg-inference area as B142/B143/B107+ (the bound's type-vars that
appear only inside the `Fn`-family signature aren't propagated to the closure param when
the closure is the argument being inferred). Recorded as a re-confirmation of that area
(NOT a brand-new root). `regions/regions-params.rs` DROPPED.

### G147-6 — a TUPLE-typed parameter to a higher-order `FnMut` closure loses its value (fn-family)

A closure with a TUPLE parameter driven through an `F: FnMut((i64,i64))` bound receives a
garbage/zeroed tuple — `run(|p| { let (a,b) = p; .. })` and `run(|p| { p.0 + p.1 })` both
read wrong values (the call `it((3,4))` does not deliver the tuple). Scalar-arg closures
through the same bound work (verified). This is in the fn-family / closure-ABI area
(B107+); the tuple argument is likely passed by a calling convention the closure thunk
doesn't unpack. Tractability: needs the fn-family closure-call ABI to handle an aggregate
(tuple) argument. `for-loop-while/foreach-put-structured.rs` DROPPED.

## Re-confirmed known-open / blessed-divergence (NOT re-reported as new)

- **`Self { .. }` struct literal / `Self { a, b }` struct pattern** — `unknown struct
  'Self'` in both expression and pattern position (and `Self != scrutinee` in a match).
  `Self`-as-a-type in a constructor/pattern is unresolved (related to the AVOID-list
  `Self` in const-generic/blanket-impl body). `structs/struct-path-self-2.rs` DROPPED
  (its core is exactly `Self { .. }` + `match s { Self { a, b } => Self { .. } }`).
- **a `()`-typed struct FIELD** is rejected at codegen (`mlir_gen: unknown field type`)
  — the B146 unit-field area. `block-result/blocks-without-results-11709.rs` had its
  `S { x: () }` field replaced by an `i64` field initialized by a block whose tail-stmt
  is a call (`S { x: { touch(); 9i64 } }`), preserving the block-as-field-value point.
- **a bare-block field value whose tail is a STATEMENT** (`S { x: { f(); } }`) is typed
  as the statement's value, not `()` (`expected void, got i64`) — same block-tail
  divergence facet noted in Bnever; worked around by ending the block in an explicit
  value.
- **`ref` modifier in a pattern** (`ref _y @ Some(_)`) is a parse error
  (`syntax error near '_y'`), and a plain `_y @ Some(_)` @-binding over an enum-variant
  subpattern is not recognized for exhaustiveness (`match is not exhaustive — missing
  Some`). Same @-binding-over-subpattern surface as B143. `binding/match-with-at-binding-8391.rs`
  DROPPED.
- **reference sub-patterns in a `let` tuple destructure** (`let (&x, &y) = (&3, &'a')`)
  are treated as refutable (`'let <pattern> = expr;' supports struct patterns only`).
  `binding/borrowed-ptr-pattern-infallible.rs` DROPPED. (The `match`-position
  reference-pattern surface is separately AVOID-listed.)
- **`..` rest in a tuple-STRUCT match pattern** (`S(1, 2, ..)`) is a syntax error,
  although `..` rest in a plain TUPLE match pattern now works (a B145 known-open since
  fixed). `binding/pat-tuple-3.rs` kept only the plain-tuple facet (the tuple-struct
  facet dropped).
- **calling a trait method through an ARRAY of trait objects** `[&dyn T; N]` — both
  `for pup in &arr { pup.bark() }` (`receiver is not a struct (got &&dyn Barks)`) and the
  indexed `arr[i].bark()` (SIGSEGV) fail; a single `&dyn T` / `&mut dyn T` dispatch works
  (kept `coercion/trait-object-mut-to-shared-b147`). `traits/dynamic-dispatch-trait-objects-5666.rs`
  DROPPED. (Array/collection-of-trait-objects facet of the dyn-dispatch surface.)
- **`impl Trait for fn(A,B)->C`** (implementing a user trait for a function-POINTER type)
  is a parse error (`syntax error near 'impl'`) — the fn-family impl-target area
  (B142/B143). `traits/fn-type-trait-impl-15444.rs` DROPPED.
- **`.method()` chained directly onto a `match`-EXPRESSION in statement position**
  (`match .. { .. }.emit()`) is a syntax error (`near '}'`). Binding the match result to
  a `let` first works — but `nll/issue-48070.rs` ALSO needs NLL (every match arm borrows
  `foo` mutably; Logos rejects the overlapping `&mut foo` / `foo.twiddle()` borrows:
  `'foo' is already mutably borrowed`), so it was DROPPED on the borrow-check facet.
- **assign-after-`loop {}` to an immutable deferred-init local** — Logos rejects the
  second `v = 2;` (`assignment to immutable variable 'v'`) where Rust allows it because
  the `loop {}` makes the second assignment unreachable. `for-loop-while/liveness-assign-imm-local-after-loop.rs`
  DROPPED (liveness-through-divergence facet; arguably a deliberate stricter rule).
- **a non-local `break 'L` from an inner loop does NOT make that inner loop type `!`** —
  `let _: i32 = loop { break 'outer }` errors `expected i32, got void` (the inner loop's
  only exit is the non-local break, so Rust types it `!` and coerces; Logos types it
  `void`). `for-loop-while/loop-labeled-break-value.rs` kept the labeled-break-from-nested-loop
  structure (the `let _: i32` never-coercion wrapper dropped).
- **range/iterator terminals need `use`** (`(0..3).sum()` → `RangeI64 not in scope
  (missing use std.lang.range)`) — prelude-aware batch convention (no `use`); the
  iterator-sum tests (`iterators/iterator-type-inference-sum-15673.rs`,
  `iter-cloned-type-inference.rs`) DROPPED.
- **`union`** declarations are a parse error (`syntax error near 'union'`) — no union
  support; ALL `tests/ui/union/*` DROPPED.
- **generic associated types `type Y<'a>`** (a lifetime/type param on an associated
  type) is a parse error (`syntax error near '>'`) — GAT unsupported.
  `generic-associated-types/generic-associated-type-bounds.rs` DROPPED.
- **in-language `mod`** → module-scope items (tag-exports, enum-export-inheritance,
  match-path, static-method-type-alias) — B-mod known-open; those candidates DROPPED.
- **`Box<T>` / `Vec` / `String` / `format!` / `Cell` / `mem::*` / `size_of` / threads /
  channels / io** → raw-pointer / stack / fixed-array / i64 rewrites or DROPPED — B111 +
  batch convention.
- A struct named **`Cell`** SHADOWS the prelude `std.cell.Cell` and SIGSEGVs a `&dyn`
  dispatch (a name-collision footgun, not a feature gap) — the coercion test's wrapper
  was renamed `Slot`.

## Mechanical port rules applied (per batch conventions, not gaps)

- `package <name>;` header; `pub fn main()` → `fn main() -> i32 { …; return 0i32; }`;
  `assert!`/`assert_eq!`/`panic!`/`println!`/`unreachable!` → distinct nonzero returns.
- `isize`/`usize` → `i64`/`u64`; all integer literals suffixed; negatives `0 - N`.
- `&self`→`self: &Self`/`self: &<Type>`; `&mut self`→`self: &mut <Type>`; `match self`→
  `match *self`.
- `Box<T>` payloads/fields/receivers → `*const T` raw-pointer / stack `T`; `Drop` impl
  bodies with `println!` → no-op bodies; `static`/`static mut` → locals / returned;
  generic struct/enum + turbofish construction kept.

## Source dups dropped (checked by exact `Original path:` vs imported `.logos` headers + ls pass/)

- `cast/cast-region-to-uint.rs` — ALREADY imported (`pass/cast/cast-region-to-uint.logos`,
  the `{:x}` address-print form); a `-b147` rewrite was written then dropped.
- `binop/structured-compare.rs` — ALREADY imported (verified by header).
- the `generics/generic-tag-{corruption,local,match,values}` variants are imported but the
  base `generics/generic-tag.rs` was NOT — kept as `generic-tag-b147`.

## Final test set (28)

regions (5): regions-borrow-at (pass `&u64` to a deref'ing fn), regions-infer-borrow-scope
(return `&p.x` field borrow), regions-bot (a `-> &'static u64` diverging-`loop{}` fn used
as the body of a generic fn; neither called), regions-infer-borrow-scope-view (slice
identity `view(&[T])->&[T]` applied twice over a `[i64;3]`), regions-return-interior-of-option
(`get(&Option<i64>)` returning the payload via a `Some(ref v)` arm across reassignments).
binding (5): match-enum-struct-0 (struct-variant pattern `E::Foo{f:_f}` missing E::Bar →
wildcard), match-enum-struct-1 (`E::Foo{..}` rest + `E::Foo{f:_f}` named-bind), match-struct-0
(struct literal-field pattern miss + `{..}` + `{f:_f}` + wildcard), match-beginning-vert
(LEADING `|` per arm incl. a guarded multi-alt `| B | C if ..`), pat-tuple-3 (trailing `..`
rest in a MATCH tuple pattern w/ overlapping prefix-literal arms).
traits (3): inherent-method-order (same-named inherent by-value + trait by-ref method
coexist; a `&` receiver picks the trait method), recursive-generic-struct-22655 (a
self-recursive generic struct via a `*const T` wrapper, instantiated without infinite
resolution, #22655), indirect-supertrait-chain-15155 (a generic fn bounded by the leaf of a
2-deep supertrait chain Top:Mid:Base dispatches a Base method, #15155).
recursion (2): recursion-tail-cps (mutually-recursive evenk/oddk threading a `fn(bool)->bool`
continuation), recursive-enum-box (a linked-list enum via `*const List` raw-pointer payloads).
drop (2): drop-trait-generic (`impl<T> Drop for S<T>` on a generic struct), enum-drop-impl-15063
(`impl Drop` for a C-like enum, #15063).
coercion (1): trait-object-mut-to-shared (coerce `&mut dyn Foo` → `&dyn Foo`; dispatch both
&self and &mut self trait methods).
block-result (1): blocks-without-results-11709 (empty-block `let _r: () = {};` + an i64 field
initialized by a block whose tail-stmt is a call, #11709).
builtin-superkinds (1): builtin-superkinds-phantom-typaram (a generic type implements a trait
whose supertrait-bounded param appears only in a marker field).
enum (1): struct-variant-destructure-19340 (construct + match-destructure a struct-like enum
variant `Homura::Madoka{name,age}`, #19340).
fn (1): fnonce-field-in-generic-struct-3904 (a generic struct `X<F:FnOnce(i64,i64)>` holding a
fn item, a method moving self + forwarding the field to a higher-order fn, #3904).
for-loop-while (1): loop-labeled-break-value (labeled `break 'L` from a nested loop, singly &
doubly nested).
generics (1): generic-tag (a generic enum w/ turbofish-qualified payload + nullary variants,
reassigning the local).
match (1): tuple-int-enum-static-36401 (a tuple element pattern mixing an int literal w/ an
enum-variant pattern `(1, Event::Resize)`, #36401).
nll (1): used-mut-from-moves-50461 (a `mut foo: Foo` param self-assigned `foo = foo;` then
read, #50461).
structs (1): struct-order-of-eval-2 (struct-literal field initializers given OUT of declaration
order bind to the right fields).
type-alias (1): type-param (a parameterized type alias `type Bar<T> = fn(T)->bool` resolved &
exercised via a bound fn item).
