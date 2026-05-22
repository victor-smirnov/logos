# B140 — UI-surfaced gaps

Batch B140 imported 13 DISTINCT rustc UI run-pass tests (pinned SHA
`4b0c9d76ae7d387229caea55cfa73c280b08b8a7`) across: autoref-autoderef (2),
const-generics (2), drop (1), match (2), pattern (2), slice (1), traits (2).
Do NOT modify the compiler/stdlib. All 13 compile + link + exit 0.

Suffix `-b140` on every file (global ctest-name uniqueness). The batch was
heavily de-duplicated against existing `pass/` (many initially-picked sources —
operator-overloading, cast, guards, enum-disr-val-pretty, generic-temporary,
rec-tup, expr-if-struct, expr-match-struct, auto-ref, repeated-vector-syntax,
enum-to-numeric-cast, coerce-reborrow-imm-ptr-arg, ignore-all-the-things,
generic-derived-type, mir_codegen_switch, destructure-array-1, vec-matching-fixed
— turned out already imported in earlier batches; see "Source dups dropped").

## NEW gaps surfaced

### G140-1 — a bare statement-expression value `E;` does NOT run Drop (TRACTABLE)

A temporary struct value used as a bare statement-expression — `A { .. };` (no
`let`) inside a block — is NOT dropped at the end of the statement. Isolated
repro: a 1-field Drop type whose `drop` increments a `*mut i64` cell; a block
`{ A { n: 3, c: c }; }` leaves the counter unchanged (the destructor never runs),
whereas `{ let _a = A { .. }; }` correctly drops at end of block (counter +1).

Upstream `drop/destructor-run-for-expression-4734.rs` checks BOTH the `let _a = E;`
and the bare `E;` forms drop. The `let`-binding half was kept; the bare-statement
half was removed and this gap recorded.

Tractability: TRACTABLE — missing-case. The drop-scheduling for a `let`-bound
temporary already fires at scope exit; the statement-expression lowering for a
Drop-typed rvalue isn't enqueuing the temporary into the same end-of-statement
drop set (it likely treats the discarded value as trivially-dead). Parallel to
the existing `let`-temp drop path. NOT a deep (region/representation) gap.

### G140-2 — `match` over a TUPLE of enums mis-dispatches in two shapes (TRACTABLE)

Two related forms of matching a 2-tuple whose elements are enums mis-dispatch
(the wrong arm is selected at runtime, NO compile error):

  (a) a tuple of enum **references** `(e1, e2)` where `e1, e2: &Enum` matched
      against variant subpatterns — `match (e1, e2) { (Enum::Foo(_), Enum::Bar(_)) => .. }`
      returns the catch-all even when the values are Foo/Bar; and
  (b) a tuple of **dereferenced fieldless (C-like)** enum values `(*a, *b)`
      where `a, b: &Color` matched against `(Color::Cyan, Color::Cyan)` etc. —
      `is_eq(&Cyan, &Yellow)` wrongly returns `true` (the first equal-pair arm
      fires for an unequal pair).

NOTE the contrast: a tuple of **dereferenced struct-variant** enum values
`(*e1, *e2)` matched against `(Enum::Foo { .. }, Enum::Bar { .. })` DOES dispatch
correctly (that is `match/match-tuple-of-refs-b140`, which uses the deref form
and passes). So the bug is specific to (a) tuple-of-refs and (b) tuple-of-C-like
deref values; the struct-variant deref tuple is fine.

Workarounds applied: `match/match-tuple-of-refs-b140` uses `(*e1, *e2)` with
struct-variant subpatterns; `traits/typeclasses-eq-static-b140` replaces the
upstream single `match (*a, *b)` over C-like enums with a NESTED per-scrutinee
match (`match *a { Cyan => match *b { Cyan => true, _ => false }, .. }`), which
dispatches correctly.

Tractability: TRACTABLE — wrong-arm selection in tuple-pattern lowering when a
tuple element is an enum discriminant test. The single-scrutinee enum switch is
correct; the tuple form (combining two discriminant tests) mis-routes — likely
the per-element discriminant comparison for a ref/C-like element isn't reading
through the right place (ref-not-derefed, or the fieldless-enum tuple element is
compared by slot rather than discriminant). Parallel-mapping to the working
single-scrutinee + struct-variant-tuple paths. NOT a deep gap.

### G140-3 — `Self` does not resolve inside a concrete/blanket impl method body (TRACTABLE)

In a method body of an `impl A<2> { .. }` (const-generic concrete impl) or a
blanket `impl<T: Baz> Foo for T { .. }`, a `self: &Self` parameter annotation
errors `unknown type 'Self'` at the method's *emitted* mangled name. Writing the
concrete receiver type instead works: `self: &A<2>` (concrete-const impl) and
`self: &T` (blanket impl). So `Self` resolves in plain `impl Trait for Type`
blocks (used throughout earlier batches) but NOT in these two impl shapes.

Affected (worked around): `const-generics/concrete-const-impl-method-b140`
(`self: &A<2>`), `autoref-autoderef/auto-ref-bounded-ty-param-b140` (`self: &T`).

Tractability: TRACTABLE — missing-case. The `Self`→receiver-type alias binding
is established for the ordinary `impl Trait for Concrete` form but not threaded
into the impl-context for const-generic-arg impls (`impl A<2>`) or
type-parameter-target blanket impls (`impl<T:..> Tr for T`). Parallel-mapping to
the working impl form. NOT a deep gap.

Also re-confirmed (return-type-driven trait static dispatch): calling a trait
STATIC method by the trait path `HasNew::new()` and letting the binding's
declared type pick the impl errors `call to undefined static method 'HasNew::new'`
— the call must go through the concrete impl type `Foo::new()` / `Bar::new()`.
Recorded under `traits/static-method-by-return-type-b140`. Same missing-case
family (no return-type-driven impl selection for an unparameterised trait static).

### G140-4 — tuple `..` rest-ignore is rejected in a `let` pattern (re-confirms known-open)

`let (e, f, ..) = (1, 2, 3, 4);` errors `'let <pattern> = expr;' currently
supports struct patterns only (other shapes are refutable; use 'match' or
'let-else')`. The same `(e, f, ..)` / `(.., g, h)` rest-ignore tuple patterns
WORK as `match` arms. (This is the let-pattern-refutability family already noted
for `&x`/`&y` ref-destructure in B139.) The `ignore-all-the-things` source it
came from turned out already imported, so this is a re-confirmation, not a kept
test.

## Re-confirmed known-open / blessed-divergence (NOT re-reported)

- **refutable struct-field sub-pattern in a match arm** (`T2 { x: T1::A(m), .. }`)
  — `record-pat-b140` binds the struct field by name and matches the inner enum
  in a SECOND match (the upstream nested refutable struct-field pattern is the
  B137/B118 known-open). Likewise `struct-variant-literal-field-8351-b140` turns
  the refutable literal-field arm `E::Foo { f: 1 }` into a guard
  `E::Foo { f } if f == 1`.
- **named slice rest-binding `ref xs @ ..`** in a slice pattern is a parse error
  (`syntax error near 'xs'`) — `slice-pattern-recursion-15104.rs` was DROPPED on
  this (named slice-bind / rest-binding family, B139/B-pt-13 known-open).
- **1-tuple trailing comma** `((x, y),)` — the second half of
  `pattern/issue-12582.rs` was dropped; only the anonymous 2-tuple half (incl.
  the inclusive-range tuple-element arm `(1..=2, 2)`) kept (B139 known-open).
- **byte literals** `b'a'` / `b"..."` and **byte-range patterns** `b'a'..=b'z'`
  are parse errors (`syntax error near 'b'`) — `array-slice-vec/byte-literals.rs`
  was DROPPED on this (new observation; recorded here as known-open going forward).
- **array repeat-count from a cast expression** `[true; 1 as usize]` /
  `['\n' as usize]` is a parse error (the repeat count must be a literal) —
  `array-slice-vec/cast-in-array-size.rs` DROPPED (const-eval/metacall §A area).
- **`Box<T>`** receivers/fields → stack `T` or distilled `trait Drop`;
  **`==` on Option/Result** AVOIDED (match / nonzero-ret guards) — B111/B135
  known-open.
- **two impls of a generic trait `Trait<T>` for the SAME type** collide on the
  emitted method symbol (`duplicate function 'MyType__get'`) — return-type-driven
  multidispatch on a generic trait (`traits/multidispatch1.rs`) was DROPPED on
  this (trait-aware method mangling does not yet key on the trait's TYPE-ARG).
- **in-language `mod m { .. }`** — `traits/static-method-overwriting.rs` items
  hoisted to top level.

## Mechanical port rules applied (per batch conventions, not gaps)

- `package <name>;` header; `pub fn main()` → `fn main() -> i32 { …; return 0i32; }`;
  `assert!`/`assert_eq!`/`panic!`/`println!` → distinct nonzero return codes.
- `isize`/`usize` → `i64`/`u64`; all integer literals suffixed; negatives `0 - n`
  (negative enum DISCRIMINANTS kept as the literal `-1`).
- `&self` → `self: &Self` (or the concrete receiver type where `Self` fails,
  G140-3); `match self` → `match *self`.
- `#[repr]`/`#[derive]`/`Box`/`Rc`/`RefCell`/`format!`/`String`/`mem::size_of`
  facets dropped where incidental; Drop modeled via the distilled local
  `trait Drop { fn drop(self: Self) }` + a `*mut i64` counter cell.
- const-generic params `<const N: u32>` / `<T, const N: usize>` kept verbatim;
  bodyless unit struct `A<const N>;` → `A<const N> { _x: i32 }`.

## Source dups dropped (checked by exact basename vs existing pass/ imports)

operator-overloading (B133), cast (B135 cast-char-int-roundtrip), guards (match),
enum-disr-val-pretty (enum), generic-temporary (B3/B136), rec-tup, expr-if-struct,
expr-match-struct (structs-enums), auto-ref (autoref-autoderef),
repeated-vector-syntax (nested-bool-array-av), enum-to-numeric-cast (B136),
coerce-reborrow-imm-ptr-arg (B136), ignore-all-the-things (tuple-wildcard-mid-pat),
generic-derived-type (g2 generic-fn-returns-generic-struct), mir_codegen_switch
(mir), destructure-array-1, vec-matching-fixed (array-slice-vec).

## Final test set (13)

autoref-autoderef: autoderef-method-twice, auto-ref-bounded-ty-param.
const-generics: concrete-const-impl-method, array-wrapper-struct-ctor.
drop: destructor-run-for-expression-4734 (let-binding half; bare-stmt half = G140-1).
match: match-tuple-of-refs (deref struct-variant tuple; refs form = G140-2),
mir-build-match-comparisons (i8 range arm + generic struct-variant literal field).
pattern: match-tuple-ranges-12582 (2-tuple literal+range arms),
record-pat (nested enum/struct/enum destructure), struct-variant-literal-field-8351.
slice: fixed-length-array-destructure (array PARAM `[x,y,z]` + `let [a,b,c,d]`).
traits: static-method-by-return-type (concrete-type static call; G140-3 note),
typeclasses-eq-static (C-like-enum static `is_eq`; nested-match workaround = G140-2).
