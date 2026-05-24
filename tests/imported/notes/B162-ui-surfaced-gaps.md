# B162 — rustc UI run-pass import: surfaced gaps

Batch B162 imported **33 NEW DISTINCT run-pass tests** from `tests/ui/`, mined for
FEATURE COVERAGE across FRESH / under-mined areas:
traits (4: assoc-const, assoc-type, dyn-object array dispatch, supertrait+default),
structs-enums (2: recursive binary tree, generic-struct method chain),
ops (2: Mul/Shl/Shr/BitAnd/BitOr/BitXor operator overloading, user `Index` trait),
closures-extra (2: FnMut mutating counter, Fn capture-by-value scalar+array),
and one each in: type-alias, tuple, structs (generic FSU), slice, shadowed,
result, recursion, pattern, or-patterns, num, methods, match, iterators, intops,
generics, for-loop-while, enum-discriminant, enum, drop, convert, coercion, char,
binop.

(`convert`, `ops`, `result`, `slice`, `shadowed`, `or-patterns`, `intops`,
`enum-discriminant` are fresh/under-mined relative to B158 closures/coercion/self/
dst, B159 binop/generics/structs-enums/regions/ufcs, B160 numbers-arithmetic/mir/
char/cast/recursion, B161 functions-closures/let-else/match/tuple/deref/autoref.)

Workflow matches B149–B161: faithful semantic ports, `pub fn main()` → `fn main()
-> i32 { …; return 0i32; }`, isize/usize → i64/u64, integer/float literals
suffixed, `assert!`/`assert_eq!` → early-return sentinels (distinct nonzero
codes), Box/Rc/Vec/PhantomData/named-lifetimes/`#[repr]`/println! dropped or
reshaped where incidental, nested fns/type decls hoisted to module scope, unit
structs `struct S;` → `struct S {}`, `&self`/`&mut self` → `self: &Self` /
`self: &mut Self`, by-value `self` → `self: Self`. No module statics/consts: Drop
counters modeled via a `*mut i64` threaded through fns. All 33 compile + link +
exit 0 against the as-is `build/bin/logosc` (no compiler changes). Link line uses
`-Wl,--gc-sections` (as for B149–B161).

Coverage highlights: user operator overloading over a fresh set of operator
traits — `*` (Mul), `<<`/`>>` (Shl/Shr), `&`/`|`/`^` (BitAnd/BitOr/BitXor) on a
newtype (operator-multi-trait); a user `Index<Idx, Output>` impl returning `&i64`
into an inner array via `m.index(i)` (user-index-trait); stdlib `From<i64>` impl +
the blanket-derived `.into()` reaching the same value with the destination
inferred (from-into-newtype); a trait associated const `ID: i64` overridden
per-impl and read through a generic type param (assoc-const-in-trait); a trait
associated TYPE `Item` bound in an impl + the `T::Item` projection as a generic
fn's return type (assoc-type-iterator-pair); `&dyn Shape` dynamic dispatch through
an array of heterogeneous trait objects (dyn-trait-object-vec-dispatch); a
supertrait relation `Loud: Greet` with default methods calling required /
supertrait methods (supertrait-default-method); a recursive binary TREE enum via
`*const Tree` raw-pointer children folded by a recursive fn (recursive-enum-tree-
sum); a generic `Wrapper<T>` with chained inherent methods returning `Self`-typed
values (generic-struct-method-chain); a `Fn`-bound read-only closure capturing a
scalar + a fixed array by value (closure-capture-by-move-sum); a `FnMut` closure
mutating a captured counter, invoked N times through a generic driver (closure-
fnmut-counter); `type` aliases for a generic-enum instantiation + a struct
(type-alias-generic-fn); tuple-pattern fn parameter `fn manh((x, y): (i64, i64))`
+ flat/nested let-destructure (tuple-param-destructure); functional struct update
`{ field: new, ..base }` over a GENERIC struct (struct-update-generic-fsu); in-
place two-pointer array reverse via index-swap (slice-rev-swap-inplace); `let`-
shadowing incl. a type-changing rebind i64→bool + inner-scope shadow (shadow-let-
rebind-types); a fallible `Result<T,E>` pipeline with explicit match-based and_then
chaining + Ok/Err short-circuit (result-and-then-chain); mutual recursion
`is_even`/`is_odd` + recursive `fib` (mutual-recursion-even-odd); top-level
`name @ range` `@`-bindings in match (binding-at-pattern); or-patterns in match
incl. tuple-nested + guarded arms (or-pattern-guard-match); integer min/max
boundary literals across i8/u8/i16/i32/u32 + truncating cross-width `as` cast (int-
min-max-wrapping); a `&mut self` builder mutating fields in place across calls
(method-mut-self-builder); default-binding-mode field binding through `match
&Struct`/`match &Enum` (match-ref-struct-fields); inclusive-range `for x in 0..=5`
+ manual reverse/strided counting (for-range-step-rev); hand-rolled popcount +
bit set/clear/test via `& | ^ << >>` (bit-manip-popcount); a multi-bound `where`
clause `T: Describe + Weigh` on a generic fn (generic-multi-bound-where); a `while
let Some(x) = pop()` loop draining an Option source (while-let-stack-pop); explicit
+ EXPRESSION (`1<<4`) enum discriminants with `Variant as i64` cast (explicit-
discr-cast-roundtrip); a two-type-param generic enum `Either<L,R>` constructed/
matched at distinct instantiations + threaded through a generic fn (generic-enum-
either-map); deterministic reverse drop order of block locals observed via a base-
10 accumulator (drop-order-locals); `&[T;N]` → `&[T]` array-to-slice coercion at a
call boundary (array-to-slice-arg); char literals incl. `\n`/`\t`/`\u{2603}` +
`char as i64` code-point cast + range classification (char-methods-classify);
`&&`/`||` short-circuit evaluation order observed via a suppressed RHS side effect
(short-circuit-eval-order).

## Gaps surfaced

- **G162-1** (TRACTABLE) — a nested pattern inside an enum-variant payload is
  rejected: `match m { Msg::Num(n @ 1i64..=5i64) => … }` errors
  "pattern Msg::Num: nested patterns inside enum-variant payloads are not yet
  supported; bind to a name and match in the body" (plus the follow-on
  "expected 1 bindings, got 0" / "undefined variable 'n'"). This is a clean
  compile-time diagnostic (not a crash) and the error already points at the
  workaround. The variant payload binding currently only accepts a bare name; the
  fix is to recurse `build_pattern` into the payload sub-pattern (the same
  machinery already supported at let-else after G161-3 and in tuple/struct
  positions). MINIMAL repro:

  ```
  enum M { Num(i64), Nil }
  fn d(m: M) -> i64 {
      match m {
          M::Num(n @ 1i64..=5i64) => { return 1000i64 + n; }  // rejected
          M::Num(n) => { return n; }
          M::Nil => { return -1i64; }
      }
  }
  ```

  Original test affected: `pattern/bindings-after-at` shape — the enum-payload
  `@`-binding facet was DROPPED; `binding-at-pattern-b162` was KEPT covering the
  TOP-LEVEL `n @ range` form (which works).

- **G162-2** (TRACTABLE) — an indexed WRITE through a `&mut [T;N]` / `&mut [T]`
  function parameter is rejected. A `&mut <array-local>` argument lowers to a thin
  `&mut elem` (`&mut i64`), so at the call site `expected &mut [i64; 5], got
  &mut i64`, and inside the callee `a[i] = v` errors "index write to 'a':
  expected [i64; 5], got i64" (or for a `&[i64]` param "index write: 'a' is not an
  array or pointer (got &[i64])"). This is the SAME root as B161's G161-2 (`for x
  in &mut arr` mis-typed the array reference) — `&mut <array>` does not yet carry
  the array shape across a parameter boundary. Indexed write through a `&mut [T;N]`
  is the un-fixed sibling of G161-2 (which fixed the for-loop iter form only).
  In-place index-write over a LOCAL array (no fn boundary) works. MINIMAL repro:

  ```
  fn rev(a: &mut [i64; 5]) {
      let mut i: i64 = 0i64;
      while i < 5i64 { a[i] = a[i] + 100i64; i = i + 1i64; }  // rejected
  }
  fn main() -> i32 {
      let mut arr: [i64; 5] = [1i64,2i64,3i64,4i64,5i64];
      rev(&mut arr);  // call: expected &mut [i64;5], got &mut i64
      return 0i32;
  }
  ```

  Original test affected: `slice/` in-place reverse shape — the `fn reverse(a:
  &mut [i64])` factoring was DROPPED; `slice-rev-swap-inplace-b162` was KEPT with
  the swap loop run INLINE over the local array.

## Other observations (NOT counted as new gaps — consistent with documented conventions/limits)

- **`impl Trait` at PARAMETER position** — `fn show(x: impl Named) -> i64` is
  REJECTED by design: "parameter 'x': 'impl Trait' is not yet supported at
  parameter position; use an explicit generic 'fn f<T: Named>(x: T)' or
  '&dyn Named'". The error names both workarounds. impl-Trait at RETURN position
  works (see impl-trait/*-it2). Dropped a universal-impl-Trait-arg test wholesale
  (the generic-bound rewrite is exactly what's already covered by
  generic-multi-bound-where-b162, so no distinct facet lost).

- **Integer overflow TRAPS (does not wrap)** — `255u8 + 1u8` SIGILLs (Illegal
  instruction) rather than wrapping to 0; same for `200u8 + 100u8`. This matches
  Rust's *debug-mode* overflow behaviour (panic), not release wraparound. No
  `wrapping_add` intrinsic is ported, so the explicit-wraparound facet of the num
  test was dropped; `int-min-max-wrapping-b162` KEPT the boundary-literal + cast
  facets (incl. the truncating narrowing cast `300i64 as u8 == 44`, which DOES
  work — `as`-narrowing is distinct from arithmetic overflow). Not a gap.

- **Raw-pointer `.offset(i)` is not a method** — `p.offset(i)` on a `*mut i64`
  errors "method call: receiver is not a struct". Pointer arithmetic via a method
  isn't surfaced; the drop-order test was reshaped to a single `*mut i64`
  accumulator (`*c = *c * 10 + id`) instead of an indexed log array. Consistent
  with the raw-pointer surface; not counted as a fresh gap.

## Dropped tests (and the gap/limit that caused each drop)

- `pattern/bindings-after-at` (enum-payload `@`-binding facet) — G162-1. KEPT
  `binding-at-pattern-b162` with the top-level `n @ range` form.
- `slice/` in-place reverse (`fn reverse(a: &mut [i64])` factoring) — G162-2. KEPT
  `slice-rev-swap-inplace-b162` with the swap loop inline in `main`.
- `impl-trait/universal_in_parameters` shape — `impl Trait` at parameter position
  is a deliberate Logos rejection (workaround named in the diagnostic). Dropped
  wholesale; not counted as a gap (the generic-bound facet is covered by
  generic-multi-bound-where-b162).
- `numbers-arithmetic/` u8-wraparound facet — integer overflow traps (matches Rust
  debug). Facet dropped from `int-min-max-wrapping-b162`; boundary/cast facets kept.

Total: **33 KEPT / passing** tests. The two surfaced gaps (G162-1 nested enum-
payload pattern, G162-2 indexed write through a `&mut [T;N]`/`&mut [T]` parameter)
are both clean compile-time diagnostics with named workarounds — TRACTABLE, no
silent miscompiles or crashes in this batch. Each affected only one facet of an
otherwise-portable test, so those tests were kept with the unsupported facet
reshaped.
