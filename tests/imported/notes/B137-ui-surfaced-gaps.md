# B137 — UI-surfaced gaps

Batch B137 imported ~22 DISTINCT run-pass tests from rustc UI areas
(generics, structs, enum, cast, expr, mir, coercion, binop, traits, self,
operator-overloading, type-alias). rustc commit
`4b0c9d76ae7d387229caea55cfa73c280b08b8a7`. Do NOT modify the compiler/stdlib.

## NEW gaps surfaced

NONE. All 22 tests compiled + linked + exited 0 after standard known-conventions
adaptations. Every divergence below maps to an already-catalogued known-open or
a MEMORY-blessed divergence (no compiler change attempted; per instructions the
compiler/stdlib were not modified).

### CORRECTED DIAGNOSIS (main investigated 2026-05-22) — it is NOT a parse / index gap

The agent's "parse error / no index-auto-deref" guess was wrong on inspection:
- `(*v)[i]` **parses fine** now (no syntax error).
- Indexing **through** a `&[T;N]`/`&[T]` param **works** — `fn get(v:&[i64;4],i:i64){ v[i] }`
  type-checks; `fn get(v:&[i64],i:i64){ v[i] }` compiles+links clean.

The real (single-root) gap is at the **call site**: the unary `&` on an array
*variable* (sema_expr.cpp:1567, the `&array_var` branch) is **context-free** and
**eagerly** lowers `&arr` to a fat-pointer slice `&[T]` (slice_lit), discarding
the precise `&[T;N]` type. So `get(&arr, i)` with a `&[i64;4]` param fails:
"expected &[i64;4], got &[i64]". (Same for the `&[lits]` literal branch ~1601.)

**Why it can't be a one-liner (cascade — deferred per `draw_the_boundary`):**
flipping `&array_var` to produce `Ref<Array>` (`&[T;N]`) by default breaks the
*common* case, because **no `Ref<Array>→Slice` coercion exists at let-init /
return / struct-field** — those sites work ONLY because `&` eagerly slices today
(verified: sema_stmt.cpp let-init at ~1295 calls only `try_coerce_closure_to_fnptr`;
`try_coerce_array_ref_to_slice` is wired at the 3 *call-arg* sites only). So a
correct flip is a 3-4-site cascade.

**Two fix options (deferred precise baghunt):**
(a) *preferred* — thread the expected param type into arg lowering so the
   `&array_var` / `&[lits]` branches emit `Ref<Array>` when the param is `&[T;N]`
   and a slice otherwise (no default change, no cascade — but needs expected-type
   plumbing into `lower_expr` for args).
(b) keep `&` slicing; add a post-hoc arg-site `try_coerce_slice_to_array_ref`
   that rebuilds the slice_lit's base `addr_of` as a `&[T;N]` — blocked on the
   lack of a sub-expr→owned-`LExprPtr` extraction helper (LIR mirror/arena).
(c) flip `&` default + add `Ref<Array>→Slice` coercion at let/return/field/arg —
   the full cascade.
Non-blocking: the coerce-reborrow-mut-vec import was distilled to a `&mut Struct`
field-swap and passes.

## Re-confirmed known-open (NOT re-reported; candidates distilled or dropped)

- `struct Foo;` (bodyless unit struct) — Logos requires a body; written
  `struct Foo {}` + constructed `Foo {}` + matched `Foo {} =>` (unit-like-struct).
- `&array_var` / `&[lits]` eagerly slices to `&[T]` at the call site, so a
  `&[T;N]` param can't be satisfied — SEE the CORRECTED DIAGNOSIS section above
  (the original "parse error / no index-auto-deref" wording was wrong). The
  coerce-reborrow-mut-vec candidate (itself later found already-imported) was
  distilled to a `&mut Struct` field-swap, which passes.
- unit-typed fn parameter (`(): ()` / `_x: ()`) — REJECTED by design ("unit-typed
  parameters carry no information"); unit-pattern-in-fn-arg candidate dropped.
- refutable struct-field sub-pattern in a match arm (`Foo { bar: Some(_), .. }`)
  — "struct pattern: refutable field sub-pattern not yet supported"; the
  nested-exhaustive-match candidate dropped (non-literal refutable struct-field
  family).
- in-language `mod m {..}` + `m::path` — known-open; generic-fn-twice hoisted to
  top level (the mono-dedup, not the module path, is the feature).
- turbofish-in-pattern (`foo::arm::<T>(x)`) — dropped per port conventions.
- string-literal match arms / tuple-with-str-literal-element patterns
  (`match s { "abcd" => .. }`, `("", _)`) — known-open; issue-11940 +
  tuple-usize-pattern-14393 candidates dropped.
- enum discriminant from a shift / another enum's cast (`thing = -5 >> 1`,
  `Y::A = X::A as isize`) — known-open; those candidates dropped.
- `Box<T>` field/receiver → stack `T`; `==`/`!=` on Option/Result AVOIDED
  throughout (match / nonzero-ret guards).

## Source dups caught (checked by exact basename vs RUSTC-PROVENANCE.md)

Initially-picked sources found already-imported in earlier batches and dropped:
u8-to-char-cast (B135), generic-tup (B134), generic-exterior-unique (B135),
binary-minus-without-space (B135), by-value-self-in-mut-slot (B135), move-self
(B134/B135), issue-23304-2 (B134/B135), block.rs/copy.rs (B134 block-expr /
copy-struct-mutref), block-fn (B135), namespaced-enums-xcrate (B107),
cast-does-fallback (B6), binop/operator-overloading (B133), generic-type-synonym
(already on disk), coerce-reborrow-mut-vec-rcvr (already imported).

## Mechanical adaptations applied

`package <name>;` header; `pub fn main()` → `fn main() -> i32 { … return 0i32; }`;
isize/usize → i64/u64; literals suffixed; negatives `0 - n`; `&self` →
`self: &Self`/`self: &T`; `mut self` → `mut self: T`; `match self` → `match *self`
(self:&Enum); `*ref` deref to read through `&` fields; `assert!`/`assert_eq!`/
`panic!`/`println!` → distinct-nonzero return codes; `#[repr(..)]`/`#[derive(..)]`
dropped; cross-crate `aux-build`/`extern crate` dropped (decls inlined); `...`
inclusive-range pattern → `..=`; operator-trait methods take `self: Self` (no
associated `Output` — result type is `Self`).

## Final test set (22)

generics: generic-newtype-struct, generic-fn-twice. structs: unit-like-struct,
module-qualified-destructure. enum: either-generic-match,
enum-payload-wildcard-match. mir: mir-codegen-switchint, mir-const-prop-identity,
mir-void-return-while. numbers-arithmetic: i32-negate, u32-decr,
float-literal-inference. pattern: inc-range-pat. match: tuple-range-arm-order,
char-range-guard. tuple: nested-index. traits: method-generic-default-fwd,
supertrait-bound-chain. type-alias: type-param-fnptr. where-clauses:
where-clause-region-outlives. expr: if-check-recursion, if-panic-arm-as-value.
- dynamic `&[T]` slice-as-value / `Vec` `.reverse()` reborrow — known-open.
- in-language `mod m {..}` — known-open; module-qualified-destructure +
  generic-fn-twice declared at top level (the destructure / mono-dedup is the
  feature, not the module path).
- turbofish-in-pattern (`foo::arm::<T>(_x)`) — dropped per port conventions.
- enum discriminant defined from another enum's cast (`Y::A = X::A as isize`) —
  dropped; explicit-literal discriminant kept.
- nested `fn` item inside a fn body — hoisted to top level (block-fn).
