# B164 — rustc UI run-pass import: surfaced gaps

Batch B164 imported **43 NEW DISTINCT run-pass tests** from `tests/ui/`, mined for
FEATURE COVERAGE across a SPREAD of areas (one each in array-slice-vec, binop, bool,
char, closures, cmp, coercion, consts, control, convert, deref, destructuring-
assignment, enum, enum-discriminant, floatops, for-loop-while, functions-closures,
intops, iterators, let-else, loops, match, methods, moves, mut, numbers-arithmetic,
ops, option, pattern, range, recursion, refs, result, shadowed, str, struct,
structs-enums, traits, tuple, typeck, ufcs; generics ×2). Workflow matches
B149–B163: faithful semantic ports, `pub fn main()` → `fn main() -> i32 { …; return
0i32; }`, isize/usize → i64/u64, integer/float literals suffixed, `assert!`/
`assert_eq!` → early-return sentinels (distinct nonzero codes), Box/Rc/Vec/String/
PhantomData/named-lifetimes/`#[repr]`/println!/derive dropped or reshaped where
incidental, nested fns/types hoisted, `&self`/`&mut self` → `self:&Self`/`self:&mut
Self`, by-value `self` → `self:Self`. Module `const` items DO work (used as `[i64;N]`
array length + in arithmetic); only `static`/`static mut` are rejected (G153-3). All
43 KEPT tests compile + link + exit 0 against the as-is `build/bin/logosc` (no
compiler changes). Link line uses `-Wl,--gc-sections`.

Coverage highlights: unsigned modular (byte) arithmetic via masking + signed
MIN/MAX bounds; boolean bitwise (`& | ^`) vs short-circuit (`&& ||`) agreement; char
literal ordering + `as u32` codepoint casts; `loop { break value }` as an expression
+ nested-loop inner-break scoping; `let Some(x) = … else { <diverging return> }`;
`move` closure consumed via a generic FnOnce driver + Fn closure read twice; Option
`map`/`unwrap_or`/`and_then`; the `?` operator early-returning `Err` through a helper;
functional-update-style struct rebuild; an enum with payload variants + inherent
match-on-self method; a trait DEFAULT method built on a REQUIRED one across two impls,
dispatched through a generic bound; a generic `swap<T,U>` returning a swapped tuple at
two monomorphizations + generic identity; a two-type-param generic struct
`Entry<K,V>`; a by-value-`self` builder method chain returning `Self`; the `name @
subpattern` match binding; nested-tuple field indexing `t.0.1` + nested destructure;
an or-pattern binding the SAME variable across enum alternatives; `a..b`/`a..=b`
range iteration; user `Add` operator overloading (`impl Add<Rhs> for T` form);
`if let … else` / `while let Some(…)` as typed forms; array→slice unsized coercion
`&[T;N]`→`&[T]`; operator precedence/associativity incl. shifts + bitwise; signed
integer div/rem truncation-toward-zero identity; min/max + clamp composition;
`if`/`else if`/`else` chains as statements and value expressions; a user conversion
trait with two impls dispatched through a bound; non-Copy struct move-through-fns
ownership transfer; in-place array element writes `a[i] = …` in a loop; map-then-sum
and filtered-count over a fixed array; explicit enum discriminants read via `as i64`
+ used as a bitmask; type-qualified + trait-qualified UFCS (`Type::m(&r)` /
`Trait::m(&r)`); user `Deref` (`impl Deref<Target> for T`) reached via explicit `*w`;
type-changing `let` shadow chains; `str` len + byte-index iteration.

## Gaps surfaced

This batch surfaced **NO new compiler gaps**. Every facet that initially failed was
a HOUSE-IDIOM mismatch on my part (the FEATURE works once written in the Logos form),
not a compiler limitation. They are recorded below so future imports use the right
idiom immediately, and so the (already-tracked) limits are not re-surfaced as "new".

### Idiom corrections (NOT gaps — author error, fixed in-batch)

- **String-literal binding type is `str`, not `&str`.** Writing `let s: &str = "abc";`
  fails sema with `type mismatch — expected &&[u8], got &[u8]` (and the follow-on
  `method call: receiver is not a struct (got &&[u8])`). A string literal in Logos
  already has type `str` (an alias for `&[u8]` at the value level); the correct
  binding is `let s: str = "abc";`. Byte-index `s[i]` yields `u64`, `s.len()` yields
  `i64` (documented in B163's observations). Affected: `str/str-char-iteration-b164`
  (fixed to `let s: str = …`).

- **User `Deref` is `impl Deref<Target> for T`, not `impl core::ops::Deref … { type
  Target = …; }`.** The Rust associated-type form `impl core::ops::Deref for Wrap {
  type Target = Inner; fn deref(&self) -> &Inner … }` is a `syntax error near 'impl'`.
  The Logos house idiom (cf. `overloaded-autoderef-order-b161`) is a trait with the
  target as a TYPE-ARG plus `use logos.lang.ops;`: `impl Deref<Inner> for Wrap { fn
  deref(self: &Self) -> &Inner … }`, and `*w` yields the target. Affected:
  `deref/deref-impl-method-b164` (rewritten to the type-arg form; reaches the target
  via explicit `*w`). The same type-arg idiom applies to `Add`/`Index`/etc. (cf.
  `ops/ops-add-overload-b164` using `impl Add<Vec2> for Vec2 { type Output = …; }`).

## Other observations (NOT counted as new gaps — documented conventions/limits)

- **Destructuring ASSIGNMENT `(a, b) = (b, a)` is not in the assignment grammar.**
  Rust's destructuring-assignment (assigning into an already-bound tuple/struct
  pattern) is not a Logos assignment form. The equivalent effect is a destructuring
  `let (a, b) = (b, a);` (re-binding via shadow), which works. This is a known
  surface-syntax difference, not a semantic gap. Affected:
  `destructuring-assignment/destructure-assign-swap-b164` expresses the swap/rotate
  via destructuring `let` rebinds.

- **`str` length vs byte-index value types differ** — `s.len()` yields `i64` while a
  byte index `s[i]` yields `u64` (re-confirming B163's note). Not a gap; recorded so
  the typing in `str-char-iteration-b164` reads intentionally.

## Dropped / reshaped tests

- `cast/numeric-cast-chains` — DROPPED before keeping: it overlapped almost exactly
  with the existing `cast/numeric-cast-chains-b163` (same area, same widen/truncate/
  float-hop operations). Replaced with a distinct `ops/ops-add-overload-b164` (user
  `Add` operator overloading) to keep the batch fresh and avoid a near-duplicate.
- `deref/deref-impl-method` — RESHAPED from the Rust `impl core::ops::Deref { type
  Target … }` associated-type form to the Logos `impl Deref<Target> for T` type-arg
  form (idiom correction above; feature works).
- `str/str-char-iteration` — RESHAPED `&str` annotation to `str` (idiom correction).
- `destructuring-assignment/destructure-assign-swap` — RESHAPED destructuring-
  ASSIGNMENT `(a,b)=(b,a)` to destructuring `let` rebinds (surface-syntax difference).
- `generics/generic-struct-pair-method` — DROPPED before keeping (overlapped with the
  existing `generics/generic-pair-methods-b163`); replaced with a distinct
  `generics/generic-two-param-box-b164` (`Entry<K,V>`, two type parameters).

Total: **43 KEPT / passing** tests; **0 DROPPED wholesale that left a hole** (2
candidates dropped as near-duplicates were each replaced by a distinct fresh test);
**4 RESHAPED** (each keeping its portable core). **0 NEW compiler gaps surfaced** —
the four initial failures were all author idiom mismatches (string-literal `str`
binding, `impl Deref<Target>` type-arg form, no destructuring-assignment grammar,
near-duplicate avoidance), every underlying FEATURE confirmed WORKING. No silent
miscompiles observed this batch.
