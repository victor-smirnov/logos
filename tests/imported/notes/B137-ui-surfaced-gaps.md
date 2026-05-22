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

One re-confirmed PARSE limitation worth a one-line note (already implied by the
known-open "index-on-&ref no-deref" item, NOT a new report):

- `(*v)[i]` — indexing a PARENTHESIZED deref expression is a syntax error
  ("syntax error near ']'"). Combined with no auto-deref when indexing a `&mut
  [T; N]` argument directly (`v[i]` → "expected [i64;4], got i64"), there is no
  spelling for "index through a `&mut`/`&` array reference". Tractable as a
  missing grammar alternative (postfix-index on a parenthesized/primary
  expression) PLUS the index-auto-deref the match/struct families already need.
  Worked around by distilling coerce-reborrow-mut-vec to a `&mut Struct`
  field-swap (field write through `&mut` works).

## Re-confirmed known-open (NOT re-reported; candidates distilled or dropped)

- `struct Foo;` (bodyless unit struct) — Logos requires a body; written
  `struct Foo {}` + constructed `Foo {}` + matched `Foo {} =>` (unit-like-struct).
- index through a `&mut`/`&` reference argument with no auto-deref AND
  `(*v)[i]` (index on a parenthesized deref) is a PARSE error — the
  coerce-reborrow-mut-vec test was distilled to a `&mut Struct` field-swap
  (field write through `&mut` works).
- dynamic `&[T]` slice-as-value / `Vec` `.reverse()` reborrow — known-open.
- in-language `mod m {..}` — known-open; module-qualified-destructure +
  generic-fn-twice declared at top level (the destructure / mono-dedup is the
  feature, not the module path).
- turbofish-in-pattern (`foo::arm::<T>(_x)`) — dropped per port conventions.
- enum discriminant defined from another enum's cast (`Y::A = X::A as isize`) —
  dropped; explicit-literal discriminant kept.
- nested `fn` item inside a fn body — hoisted to top level (block-fn).
