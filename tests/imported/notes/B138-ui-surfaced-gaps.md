# B138 — UI-surfaced gaps

Batch B138 imported 25 DISTINCT rustc UI run-pass tests (pinned SHA
`4b0c9d76ae7d387229caea55cfa73c280b08b8a7`) across: generics (3), traits (2),
closures (2), enum (2), pattern (2), numbers-arithmetic (2), structs (1),
slice (1), coercion (1), associated-types (1), typeck (1), casts (1),
iterators (1), regions (1), recursion (1), dyn (1), impl-trait (1).
Do NOT modify the compiler/stdlib. All 25 compile + link + exit 0.

Suffix `-b138` on every file (global ctest-name uniqueness).

## NEW gaps surfaced

NONE. All 25 tests compiled + linked + exited 0 after standard
known-conventions adaptations. Every divergence below maps to an
already-catalogued known-open, a MEMORY-blessed divergence, or a faithful
mechanical port rule (no compiler change attempted; per instructions the
compiler/stdlib were not modified).

## Re-confirmed known-open / blessed-divergence (NOT re-reported)

- **Self-referential enum payload requires explicit boxing**
  (`enum/enum-recursive-list-b138`). A by-value recursive payload
  `Cons { head: i32, tail: List }` errors at sema: *"infinite-size enum
  'List' (variant payload contains itself by value); box the payload with
  '*const List'"*. Rust uses `Box<List>`; Logos's idiomatic equivalent is a
  raw `*const List` tail (deref under `unsafe`). This is the documented Logos
  model (the error message itself prescribes the fix), not a new gap.

- **`iter_over_slice` is an unsafe free fn** (`iterators/iter-map-filter-sum-b138`).
  Pipeline construction must be wrapped in `unsafe { .. }` (matches existing
  B29/B49-era iter imports). Predicate to `iter_filter` is `fn(T) -> bool`
  (takes T BY VALUE, not `&T`) — a `&T` predicate segfaults at runtime; this
  is the known fn-ptr-predicate calling convention, not a new bug.

- **closure→fn-ptr not auto-coerced at trait-default-method args**
  (known-open since B29) — iterator pipeline used named fn-ptr predicates +
  free-fn adapters (`iter_filter`/`iter_fold`), not closures.

- **`impl Fn` in return position not on the Logos surface**
  (`impl-trait/impl-trait-return-closure-b138`) — the test returns a concrete
  `fn(i32) -> i32` function pointer selected by tag instead of `impl Fn`.
  Tractable as a representation/feature add (impl-Trait-return over closures),
  classed deep (needs an existential return ABI). Preserved the
  select-and-invoke intent.

- **at-binding over inclusive range segfaults logosc** (B107 known-open) —
  `pattern/pat-range-guard-b138` uses match-arm `if` GUARDS (`x if x < 10`)
  instead of `e @ 0..=9` range-at-bindings, which are known to crash.

- **nested/loop-driven FnMut closures abort at runtime** (B107 known-open) —
  `closures/closure-capture-mut-b138` uses a single flat FnMut capturing a
  `mut` local across straight-line calls (no loop / no nesting).

- **bodyless unit struct `struct Foo;`** (B107/B137 known-open) — not used
  here; struct-like enum variants and FRU exercised instead.

## Mechanical port rules applied (per batch conventions, not gaps)

- `pub fn main()` → `fn main() -> i32 { …; return 0i32; }`; `assert!` /
  `assert_eq!` / `panic!` → distinct nonzero return codes.
- `isize`/`usize` → `i64`/`u64`; all integer literals suffixed; negatives as
  `0 - n`; `i32::MAX` written as the literal `2147483647i32`.
- `&self` → `self: &Self`; `match self` → `match *self`.
- `#[repr]`/`#[derive]`/`Box`/`println!`/`Rc`/`RefCell`/`transmute` facets
  dropped where incidental to the test's point.
- `Box<dyn Trait>` → `&dyn Trait` (stack) for the dynamic-dispatch test.
- `Option<i32>` results matched with `match` (no `Eq` on `Option<i32>` yet)
  for the checked-arithmetic test.
