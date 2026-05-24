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

- **G162-1** (CLOSED) — a nested refutable pattern inside an enum-variant payload
  (`match m { Msg::Num(n @ 1i64..=5i64) => … }`) now works. Two sub-fixes in the
  variant-payload nested-pattern path (`sema_stmt.cpp build_pattern`):
  1. `synth_refutable_inner` gained a **PAT_RANGE** arm — binds the payload to a
     synth (or @-name) and emits a `synth >= lo && synth <= hi` arm guard
     (exclusive `lo..hi` → `lo..=(hi-1)`), mirroring `build_pattern`'s PAT_RANGE.
  2. `synth_refutable_inner` gained an optional `explicit_name` parameter, and a
     **PAT_AT** dispatch case extracts the `@`-name + sub-pattern, binds the
     payload to that name, and builds the sub-pattern's refutable guard against
     it (range / literal / variant; `n @ _` binds with no guard).

  Covers bare range payloads (`Num(1..=5)`), `@`-literal (`Num(n @ 10)`), and
  `@`-range (`Num(n @ 1..=5)`) across multiple variants. Reuses the existing
  `current_pat_refutable_guards_` plumbing (same as `Some(1)` literal inners).

  Original test restored: `pattern/binding-at-pattern-b162` now exercises the
  enum-payload `@`-binding facet (`Msg::Num(n @ 1..=5)`) alongside the top-level
  `x @ range` form.

- **G162-2** (CLOSED) — an indexed WRITE through a `&mut [T;N]` / `&mut [T]`
  function parameter is now supported. Three sub-fixes:
  1. `&mut <array-local>` now lowers to `&mut [T;N]` (a ref to the WHOLE array)
     instead of a thin `&mut elem` (`sema_expr.cpp` ADDR_OF_MUT array case), so it
     satisfies a `&mut [T;N]` parameter and the array-ref→slice coercion can lift
     it to `&mut [T]`.
  2. The index-write sema check (`sema_stmt.cpp lower_index_write`) accepts a
     `Kind::Slice` receiver and a `&mut/&/*[T;N]`-pointee (peels the Array pointee
     to its element for the write type).
  3. mlir-gen: a `&[T;N]`-pointer param strides the GEP by the ELEMENT type (peel
     the Array pointee — `mlir_gen_fn.cpp`); a `&mut [T]` SLICE param is recorded
     in `var_slice_` so `gen_index_write` GEPs field 0 of the fat `{ptr, len}`
     descriptor + loads the data pointer before striding by element
     (mirror of the `ESliceIndexView` read path).

  Both the `&mut [T;N]` array-param and the `&mut [T]` slice-param forms work.
  Original test restored: `slice-rev-swap-inplace-b162` now factors the swap loop
  into `fn reverse(a: &mut [i64], n: i64)` (the original Rust shape).

  KNOWN DIVERGENCE (separate, broad type-system feature — NOT introduced by this
  fix): Logos does not track slice mutability at the type level — `&[T]` and
  `&mut [T]` both canonicalise to `Kind::Slice` — so an indexed WRITE through an
  immutable `&[T]` is not rejected. This matches the existing (loose) slice model
  (Logos has never tracked slice mutability). Recorded in DIVERGENCES.

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

- `pattern/bindings-after-at` (enum-payload `@`-binding facet) — G162-1 CLOSED.
  `binding-at-pattern-b162` RESTORED with the enum-payload `Msg::Num(n @ 1..=5)`
  facet alongside the top-level `x @ range` form.
- `slice/` in-place reverse (`fn reverse(a: &mut [i64])` factoring) — G162-2 CLOSED.
  `slice-rev-swap-inplace-b162` RESTORED to the original Rust shape with the swap
  loop factored into `fn reverse(a: &mut [i64], n: i64)`.
- `impl-trait/universal_in_parameters` shape — `impl Trait` at parameter position
  is a deliberate Logos rejection (workaround named in the diagnostic). Dropped
  wholesale; not counted as a gap (the generic-bound facet is covered by
  generic-multi-bound-where-b162).
- `numbers-arithmetic/` u8-wraparound facet — integer overflow traps (matches Rust
  debug). Facet dropped from `int-min-max-wrapping-b162`; boundary/cast facets kept.

Total: **33 KEPT / passing** tests. Both surfaced gaps are now CLOSED: G162-1
(nested refutable pattern in an enum-variant payload — PAT_RANGE + PAT_AT) and
G162-2 (indexed write through a `&mut [T;N]`/`&mut [T]` parameter). Both affected
tests were RESTORED to their original Rust shapes (`Msg::Num(n @ 1..=5)` and
`fn reverse(a: &mut [i64])`). One residual divergence recorded: slice mutability
is not tracked at the type level (DIVERGENCES B6 — does not affect any imported
run-pass test).
