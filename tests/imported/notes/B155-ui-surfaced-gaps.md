# B155 — rustc UI run-pass import: surfaced gaps

Batch B155 imported 26 run-pass tests from `tests/ui/` across fresh / lightly
mined areas: unboxed-closures (4), traits-inheritance (3), match (4), structs (2),
self (2), functions-closures (3), overloaded (2), regions (1), iterators (1),
ptr_ops (1), array-slice-vec (1), type-alias-enum-variants (1), coercion (1).
Workflow matches B149–B154: faithful ports, `pub fn main()` → `fn main() -> i32
{ …; return 0; }`, isize/usize → i64/u64, integer/float literals suffixed,
`assert!`/`assert_eq!` → early-return sentinels (distinct nonzero codes),
println!/derive/Box/Rc/RefCell/Vec/PhantomData/named-lifetimes dropped or
reshaped where incidental, nested type decls hoisted. All 26 compile + link +
exit 0 against the as-is `build/bin/logosc` (no compiler changes). Link line uses
`-Wl,--gc-sections` (as for B149–B154).

Operator-overloading traits use the Logos type-param-list form (`Index<Idx,
Output>`, `Deref<Target>`, `DerefMut<Target>`) with the trait in scope via
`use logos.lang.ops;`. `swap` is the safe `swap_ref::<T>` form from
`logos.lang.mem` (per B101).

## Gaps surfaced

### G155-1 — `Self::Variant` for a NO-payload variant inside an impl errors
A no-payload (unit) enum variant referenced via `Self::` in an impl method fails;
the payload-carrying form `Self::Bar(x)` works fine.
```
enum Foo { Bar(i64), Qux }
impl Foo { fn qux() -> Foo { return Self::Qux; } }   // error: unknown enum 'Self'
```
Observed: `error [fn Foo__qux]: unknown enum 'Self'`. Workaround: write the
canonical enum name (`Foo::Qux`). Reshaped in
`type-alias-enum-variants/self-variant-resolution-b155.logos`.

### G155-2 — a type alias cannot be used as an enum-variant constructor
`type FooAlias = Foo;` then `FooAlias::Bar(..)` fails to resolve.
```
enum Foo { Bar(i64), Qux }
type FooAlias = Foo;
let b = FooAlias::Bar(1i64);   // error: call to undefined static method 'FooAlias::Bar'
```
Observed: `error [fn main]: call to undefined static method 'FooAlias::Bar'`.
Workaround: construct through the real enum name. Reshaped in the same test as
G155-1. (Upstream `type-alias-enum-variants-pass.rs` also exercises `<Alias>::Variant`
turbofish forms — not retried, blocked by this.)

### G155-3 — compound-assignment through a user `DerefMut` place does not dispatch
`*n -= k` (and other compound ops) where `n` has a user `DerefMut` impl fails;
plain `*n = expr` write and `*n` read both dispatch correctly.
```
struct B1 { v: i64 }
impl Deref<i64> for B1 { fn deref(&self) -> &i64 { return &self.v; } }
impl DerefMut<i64> for B1 { fn deref_mut(&mut self) -> &mut i64 { return &mut self.v; } }
let mut n = B1 { v: 5i64 };
*n -= 3i64;   // error: deref-compound: left side must be a pointer or mutable reference
```
Observed: `error [fn main]: deref-compound: left side must be a pointer or
mutable reference`. Workaround: spell out `*n = *n - k`. Used in
`overloaded/overloaded-deref-mut-b155.logos`.

### G155-4 — `Self { .. }` struct literal in an impl method errors
Constructing the impl's own type via the `Self` alias as a struct literal fails;
`Self` as a *return type* works, and `Self::method()` works.
```
struct S { a: i64, b: i64 }
impl S { fn new() -> Self { return Self { a: 0i64, b: 1i64 }; } }   // error
```
Observed: `error [fn S__new]: struct literal: unknown struct 'Self'`. This blocks
faithful import of `structs/struct-path-self-2.rs` (which leans on `Self { a, b }`
literals AND `Self { a, b }` patterns) — dropped. Workaround: name the struct
explicitly (`S { a: .., b: .. }`).

### G155-5 — ⚠️ SILENT MISCOMPILE + CRASH: `match` over `&Enum` ref-patterns
Two related facets of matching enums by reference with `&Pattern` arms:
  (a) ⚠️ CRASH — a single `&Enum` matched with a `&Variant { .. }` struct-variant
      ref-pattern fails mlir-gen verification:
```
enum Enum { Foo { foo: u64 }, Bar { bar: u64 } }
fn which(e: &Enum) -> u64 {
    match e { &Enum::Foo { foo: _ } => 0u64, &Enum::Bar { bar: _ } => 1u64 }
}
```
      Observed: `error: 'llvm.load' op operand #0 must be LLVM pointer type, but
      got 'i32'` → `mlir_gen: module verification failed`.
  (b) ⚠️ SILENT — matching a tuple of two enum refs `(e1, e2): (&E, &E)` with
      `(&E::A, &E::B)`-style arms mis-discriminates the SECOND element: it returns
      the value of an earlier arm:
```
enum E { A, B }
fn f(e1: &E, e2: &E) -> u64 {
    match (e1, e2) {
        (&E::A, &E::A) => 0u64, (&E::A, &E::B) => 1u64,
        (&E::B, &E::B) => 2u64, (&E::B, &E::A) => 3u64,
    }
}
// f(&E::A, &E::B) returns 0 (should be 1)
```
This is the `&Enum` / default-binding-mode family already on record
([[baghunt_match_ref_enum_default_binding_modes]]); the struct-variant crash and
the second-tuple-element mis-discrimination are concrete repros. Note `match *e {
E::A => .. }` (explicit deref, no `&`-pattern) over a by-ref unit-variant enum
works correctly. Blocks faithful import of `match/issue-5530.rs` — dropped.

### G155-6 — a `const` identifier cannot be used as a range-pattern bound
`const LOW: char = '0'; ... LOW..=HIGH => ..` fails to parse.
```
const LOW: char = '0';
const HIGH: char = '9';
match '5' { LOW..=HIGH => true, _ => false };   // syntax error near 'LOW'
```
Observed: `syntax error near 'LOW'`. Workaround: use the literal bounds directly
(`'0'..='9'`). Reshaped in `match/match-range-char-const-b155.logos`.

## Re-confirmed known gaps / divergences (brief)
- Unit-like struct with no body `struct Foo;` does not parse
  (`'struct Foo;': 'Foo' is not defined — did you mean 'struct Foo { ... }'`).
  Blocks `structs/unit-like-struct.rs` (and its `match x { Foo => .. }`) — dropped.
  (Known: Logos requires a struct body.)
- `&Enum` default-binding-mode family — see G155-5 (refines the existing baghunt).

## Dropped tests (and why)
- `structs/struct-path-self-2.rs` — needs `Self { .. }` struct literals + `Self { .. }`
  patterns (G155-4).
- `match/issue-5530.rs` — `match (&Enum, &Enum)` with struct-variant ref-patterns
  (G155-5: crash on single-ref form, silent miscompile on the tuple form).
- `structs/unit-like-struct.rs` — `struct Foo;` body-less unit struct does not parse.
- `type-alias-enum-variants-pass.rs` — full faithful form needs both G155-1
  (`Self::UnitVariant`) and G155-2 (alias-as-constructor); a reduced version that
  exercises the working `Self::Bar(x)` + canonical-name construction was imported
  instead (`self-variant-resolution-b155.logos`).
