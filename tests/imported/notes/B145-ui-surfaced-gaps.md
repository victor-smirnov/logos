# B145 — UI-surfaced gaps

Batch B145 imported 23 DISTINCT rustc UI run-pass tests (pinned SHA
`4b0c9d76ae7d387229caea55cfa73c280b08b8a7`) across less-tapped run-pass areas:
numbers-arithmetic (4), binding (7), for-loop-while (4), statics (2),
unboxed-closures (1), tuple (1), match (1), generics (1), enum (1),
destructuring-assignment (1).
Do NOT modify the compiler/stdlib. All 23 compile + link + exit 0.

Suffix `-b145` on every file (global ctest-name uniqueness). De-duplicated against
`RUSTC-PROVENANCE.md`, `pass/<area>/`, and per-file `Original path:` headers
(B6/B100+/B107/B133–B144/Bnever).

## NEW gaps surfaced

### G145-1 — a no-decimal-mantissa exponent float literal `1e6f64` / `1E6f64` is rejected (TRACTABLE)

A float literal with an integer mantissa (no decimal point) and an exponent —
`1e6f64`, `1E6f64` — is a syntax error (`syntax error near '1'`). The forms with a
decimal point work for BOTH exponent cases: `1.5e6f64` and `1.5E6f64` compile fine,
and `1.0e6f64` / `1.0E6f64` (added `.0`) work. So the lexer only accepts an exponent
after a mantissa that already contains a `.`.

```
let c = 1e6f64;   // syntax error
let d = 1E6f64;   // syntax error
let a = 1.5e6f64; // OK
let c = 1.0e6f64; // OK (workaround)
```

Tractability: TRACTABLE — lexer/grammar missing-case. The float-literal production
should allow `<digits> e <exp>` (no fractional part) the same way it already allows
`<digits>.<digits> e <exp>`. Localized to the numeric-literal lexer. `float2.rs` was
adapted to write the integer-mantissa exponent forms with an explicit `.0`.

### G145-2 — a LITERAL pattern inside an enum-variant payload is silently NOT tested (match) (TRACTABLE — soundness)

A match arm whose enum-variant payload pattern is a LITERAL (`Foo::FooUint(1u64)`)
matches the variant REGARDLESS of the payload value — the inner literal is ignored:

```
enum Foo { FooUint(u64), FooNullary }
match Foo::FooUint(0u64) {
    Foo::FooUint(1u64) => 1,   // <-- WRONGLY matches FooUint(0)
    _ => 0
}
// returns 1, should return 0
```

Only the variant TAG is checked; the literal sub-pattern in the payload position
contributes no test. This is a SILENT wrong-result (not an error). It also affects the
tuple-of-(enum, char-range) form (`(Foo::FooUint(1u64), 'a'..='z')`). NOTE the
distinction from the B135/B137/B139/B144 family — those were `if let`/`while let`
"nested patterns inside enum-variant payloads are not yet supported" ERRORS or a
literal-OR/wildcard count mismatch; this is a plain literal sub-pattern in MATCH
position that compiles but mis-dispatches.

Tractability: TRACTABLE — missing-case in match codegen for an enum-payload literal
sub-pattern: after the tag test, emit the literal-equality test on the bound payload.
The variant-binding path (`Foo::FooUint(_t)`) and the char-range path both work, so
the structure is in place. `multiple-refutable-patterns-13867.rs` was distilled to use
variant-tag + char-range arms only (literal-payload arms removed).

### G145-3 — turbofish in a PATTERN `Clam::A::<i64>(_)` is a parse error (TRACTABLE)

A turbofish on an enum-variant PATTERN — `Clam::A::<i64>(_)` /
`myoption::some::<T>(_t)` — is a syntax error (`syntax error near 'A'`). The plain
form `Clam::A(_)` matches the same generic enum value fine, and turbofish on a
CONSTRUCTION (`MyOption::None::<i64>`) and a CALL (`foo::<i64>(..)`) both work.

Tractability: TRACTABLE — grammar/parser missing-case in the pattern-path production:
allow a `::<TypeArgs>` segment on an enum-variant pattern path (parse + discard, since
the type args are redundant with the scrutinee type). `simple-generic-match.rs` and
`use-uninit-match.rs` were distilled to the plain (non-turbofish) pattern form.

### G145-4 — destructuring ASSIGNMENT `(a, b) = expr` (not a `let`) is a parse error (TRACTABLE)

A tuple/struct destructuring ASSIGNMENT to pre-declared bindings —
`(a, b) = (0, 1);` — is a syntax error (`syntax error near ')'`). Only the `let`
binding form `let (a, b) = (0, 1);` is accepted (verified — kept
`let-binding-tuple-destructuring-b145`).

Tractability: TRACTABLE — grammar/sema missing-case. Rust desugars a destructuring
assignment to a sequence of element assignments; Logos would need the parser to accept
a tuple/struct pattern on an assignment LHS and lower it to per-element stores.
Moderate scope (parser + a desugar pass). `destructuring-assignment/{tuple_destructure,
struct_destructure}.rs` DROPPED on this.

## Re-confirmed known-open / blessed-divergence (NOT re-reported as new)

- **`..` rest sub-pattern in a MATCH tuple/struct pattern** — `(2i64, ..)` /
  `S { v: 2u8, .. }` in a match arm is a syntax error (`syntax error near '..'`); the
  `..` rest in a `let` binding works (B144 USABLE note). `pat-tuple-2.rs` was distilled
  to explicit `_` element patterns (no `..`). (Tuple-rest works in `let`, not in match —
  a narrower facet of the rest-pattern surface.)
- **string-literal MATCH pattern** `match s { "abcd" => .. }` is a syntax error; `s ==
  "abcd"` equality works. `match/issue-11940.rs` DROPPED (string equality already
  covered by existing str tests; not distinct enough to keep as a `==` rewrite).
- **calling a `&mut F: FnMut` param value** `x(2)` where `x: &mut F` errors `call to
  undefined function 'x'`; the by-value `F: FnMut` param call works (kept the related
  `unboxed-closures-infer-fnmut-calling-fnmut-b145`). Same fn-family / `&mut dyn FnMut`
  autoderef-call area as B142/B143 (B107+). `unboxed-closures-call-sugar-autoderef.rs`
  DROPPED.
- **return-type-driven static trait-method dispatch** `let x: T = Foo::foo()` (resolve
  the impl from the let-annotation type) — known-open; `static-methods-in-traits.rs`
  adapted to the concrete-type call `A::foo()` / `B::foo()` (which dispatches fine).
- **a bare `128i8` literal overflows its suffix** (i8 max 127) — the upstream
  `-128`-as-MIN idiom can't be written as `0i8 - 128i8`;
  `integer-literal-suffix-inference.rs` uses in-range representative magnitudes
  (`127i8`, `32767i16`, …). Rust allows `-128i8` because the `-` binds the literal;
  Logos validates the magnitude pre-negation. (Negative-min-literal surface.)
- **`Box<T>`** fields/receivers/args → by-value `T` (generic-fn-unique,
  expr-match-generic-unique2, match-reassign, generic-tag) — B111 known-open.
- **in-language `mod`** → module-scope items (static-methods-in-traits) — B-mod
  known-open.
- **const-eval in array-length type position** `[0; E as usize]` → Divergence (metacall
  replaces const-eval); `enum-vec-initializer.rs` distilled to the run-pass core
  (`Flopsy::Bunny as u64` discriminant read).
- **`String`/`format!`** → distinct i64 codes (inferred-suffix-in-pattern-range) —
  B111/B135 known-open.

## Mechanical port rules applied (per batch conventions, not gaps)

- `package <name>;` header; `pub fn main()` → `fn main() -> i32 { …; return 0i32; }`;
  `assert!`/`assert_eq!`/`panic!`/`println!`/`unreachable!` → distinct nonzero returns.
- `isize`/`usize` → `i64`/`u64`; all integer literals suffixed; negatives `0 - N`.
- `&self`→`self: &Self`; `&mut self`→`self: &mut <Type>`; `match self`→`match *self`.
- empty unit struct `struct S;` → `struct S { _z: i64 }`; `static`/`static mut` →
  local / returned value; closures given block bodies.

## Source dups dropped (checked by exact `Original path:` vs RUSTC-PROVENANCE.md + ls pass/)

- `numbers-arithmetic/{div-mod, arith-unsigned}` — ALREADY imported (verified by
  header match); a 64-bit-extended `arith-unsigned-2` variant was written then dropped
  (too close to the existing import).
- candidates requiring `format!`/`black_box`/`extern crate test`
  (`numbers-arithmetic/{i128, u128, u128-as-f32}`), threads/channels
  (`structs-enums/ivec-tag`, `numbers-arithmetic/issue-8460`), or `mem::size_of_val`
  (`integer-literal-suffix-inference-3`) — skipped (out of scope per batch rules).

## Final test set (23)

numbers-arithmetic (4): u8-incr (u8 +1/-1 round-trip), usize-base (u64 literal/cast/
inference forms), float2 (float exponent literal forms 1.5e6/1.5E6/1.0e6 + f32-vs-f64
orderings), integer-literal-suffix-inference (unsuffixed literal infers type from the
fn-param / let-annotation expected type, all 9 int widths).
binding (7): pat-tuple-2 (a `(2,_)` tuple arm + a struct field arm that miss, falling
to the catch-all), simple-generic-match (match a generic enum value against a payload
pattern), use-uninit-match (generic `foo<T>(MyOption<T>)` matching with a payload-bind
arm mutating an accumulator), match-range-infer (inclusive `1..=3` range pattern over
i64/u16/u32 scrutinees), inferred-suffix-in-pattern-range (`0..=1` range arm vs wildcard
across i64/u64), match-reassign (reassign the scrutinee inside a bound match arm w/ an
intervening unrelated assignment — #23698), expr-match-generic-unique2 (generic
`test_generic<T:Copy, F:FnOnce(T,T)->bool>` matching a bool to produce a `T`, then
calling the FnOnce).
for-loop-while (4): for-loop-has-unit-body (`()` unit tail-statement loop body),
loop-no-reinit-needed-post-bot (a moved-out local + a `-> !` diverging call on the dead
branch + continue-reinit on the live branch + a no-op Drop impl), nested-labeled-loops-
2216 (three nested loops threaded by `break 'foo`/`break 'bar`/`continue 'foo` — #2216),
while-let-scope (a `while let Some(foo)` binding does not leak past the loop; a later
free `foo()` resolves to the outer fn — #40235).
statics (2): static-function-pointer (bind a fn item to a `fn(i64)->i64`-typed local +
call through it + reassign the slot to another fn item), static-methods-in-traits (a
static `fn foo() -> Self` trait method impl'd for two types, called via the concrete
type).
unboxed-closures (1): unboxed-closures-infer-fnmut-calling-fnmut (a closure capturing
`&mut counter` inferred FnMut + a second closure CALLING the first also inferred FnMut).
tuple (1): tup (a tuple type alias used as a fn-param type + `let (a,b) = p`
destructure, at fn-entry and in a callee, with the Copy tuple reused).
match (1): multiple-refutable-patterns-13867 (multiple refutable patterns over tuples
mixing an enum-variant pattern with char-range / char-literal patterns — #13867).
generics (1): generic-fn-unique (identity `fn f<T>(x:T)->T` instantiated at i64).
enum (1): enum-discrim-as-int (read an explicit enum discriminant `Flopsy::Bunny as u64`).
destructuring-assignment (1): let-binding-tuple-destructuring (basic `let (x,y) = (10,20)`).
