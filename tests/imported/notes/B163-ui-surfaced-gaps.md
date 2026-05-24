# B163 — rustc UI run-pass import: surfaced gaps

Batch B163 imported **41 NEW DISTINCT run-pass tests** from `tests/ui/`, mined for
FEATURE COVERAGE across FRESH / under-mined feature facets, spread across 40 areas
(generics ×2; one each in array-slice-vec, associated-consts, binding, bool,
borrowck, cast, char, closures-extra, coercion, consts, deref, enum,
enum-discriminant, floatops, for-loop-while, functions-closures, inference, intops,
match, methods, moves, mut, numbers-arithmetic, option, or-patterns, pattern, range,
recursion, refs, return, shadowed, str, struct, structs-enums, traits, tuple,
type-alias, typeck, ufcs). Workflow matches B149–B162: faithful semantic ports,
`pub fn main()` → `fn main() -> i32 { …; return 0i32; }`, isize/usize → i64/u64,
integer/float literals suffixed, `assert!`/`assert_eq!` → early-return sentinels
(distinct nonzero codes), Box/Rc/Vec/PhantomData/named-lifetimes/`#[repr]`/println!/
derive dropped or reshaped where incidental, nested fns/types hoisted, `&self`/
`&mut self` → `self:&Self`/`self:&mut Self`, by-value `self` → `self:Self`. Module
`const` items DO work (used as `[i64; N]` array length + in arithmetic); only
`static`/`static mut` are rejected (G153-3). All 41 KEPT tests compile + link +
exit 0 against the as-is `build/bin/logosc` (no compiler changes). Link line uses
`-Wl,--gc-sections`.

Coverage highlights: value-carrying control flow (loop-break-value already covered
elsewhere, so omitted) — instead `while`-loop `continue`/`break`, early `return`
from nested loops, tail-vs-`return` parity; a trait DEFAULT method calling a
REQUIRED method on `self` across two concrete impls; a generic fn bounded by a user
trait dispatching the bound method (`max2<T: Ord2>`); a `FnMut(i64)` closure
mutating a captured accumulator over a generic driver; bare fn items as
`fn(i64)->i64` args + higher-order `apply_twice` + branch-selected fn ptr; a user
`Deref` impl reaching the target's method via autoderef; a recursive linked list via
`*const Node` raw-pointer links walked under `unsafe`; a 2-D fixed array
`[[i64;3];2]`; type aliases for a fn-pointer type + a tuple type; a generic struct
`Pair<T>` at two instantiations; tuple-struct positional destructuring in `let` +
`match`; match-guard struct-field bindings with correct ordering; disjoint-field
`&mut` borrow; mixed-width int struct fields; explicit enum discriminants `as i64`;
f64 arithmetic/ordering/abs; `&str` len/byte-index/lexicographic ordering; numeric
`as`-cast chains; multi-level struct field write through `&mut`; recursive GCD +
factorial; `let`-shadow chains; bool truth tables; or-pattern literal alternatives +
guards; type-qualified UFCS instance calls.

## Gaps surfaced

- **G163-1** (⚠️ SILENT CRASH, TRACTABLE) — explicitly dereferencing a tuple-
  REFERENCE parameter (`&(i64,i64)`) SIGSEGVs at runtime, in BOTH the copy-bind form
  `let t = *p;` and the field-access form `(*p).0`. Auto-deref field access
  `p.0` / `p.1` through the SAME `&(i64,i64)` parameter works fine, and the
  analogous struct form (`let q = *p;` over a `&Struct`) ALSO works — so the crash
  is specific to an explicit deref (`*p`) of a tuple-typed reference.

  Minimal repro (SIGSEGV):
  ```
  fn dot(p: &(i64, i64)) -> i64 {
      let (a, b) = *p;   // OR: return (*p).0 + (*p).1;
      return a + b;
  }
  fn main() -> i32 {
      let x: (i64, i64) = (2i64, 3i64);
      if dot(&x) != 5i64 { return 1i32; }
      return 0i32;
  }
  ```
  Works (auto-deref, no explicit `*`):
  ```
  fn dot(p: &(i64, i64)) -> i64 { return p.0 + p.1; }
  ```
  Hypothesis: mlir-gen lowers `*p` on a tuple-typed reference as a value LOAD of the
  whole tuple aggregate from the reference slot, but tuple aggregates appear to be
  handled by-pointer (cf. the struct path, which copies correctly); the tuple deref
  likely loses a level of indirection (loads the pointer bits AS a tuple, then GEPs
  through them). Compare with `gen_struct_lit`/struct-deref handling vs the tuple
  aggregate deref path in `mlir_gen_expr.cpp`. Affected: `func-arg-ref-pattern-b163`
  RESHAPED to auto-deref field access (`p.0`/`p.1`) — the explicit-deref destructure
  facet dropped.

- **G163-2** (TRACTABLE — parse) — a CHAINED two-level indexed WRITE
  `grid[0][1] = ..` is a syntax error (`syntax error near ']'`), although the
  matching two-level indexed READ `grid[0][1]` parses + runs fine, and a
  single-level indexed write `row[1] = ..` works. The parser appears to reject an
  index-suffix that immediately follows another index-suffix on the LHS of an
  assignment (the assignment-target grammar only admits one trailing `[..]`).

  Minimal repro (parse error):
  ```
  let mut grid: [[i64; 3]; 2] = [[1i64,2i64,3i64],[4i64,5i64,6i64]];
  grid[0][1] = 20i64;   // syntax error near ']'
  ```
  Works:
  ```
  let _ = grid[0][1];       // two-level READ ok
  let mut row = grid[0];    // extract row
  row[1] = 20i64;           // single-level write ok
  ```
  Hypothesis: the place-expression / lvalue production in the grammar accepts a
  primary + a SINGLE index/field suffix but does not recurse for a second index
  suffix when the whole thing is an assignment target (the READ path uses the full
  postfix-expr grammar). Affected: `array-of-arrays-b163` RESHAPED — the in-place
  mutation goes through an extracted `row` binding instead of `grid[0][1] = ..`;
  the two-level READ facet is KEPT.

- **G163-3** (TRACTABLE — same root as B121) — projecting a trait associated CONST
  through a generic type parameter `T::SIDES` inside a generic fn `fn f<T: Poly>()`
  fails with `unknown enum 'T'`. The CONCRETE type path `Tri::SIDES` works, as does
  reading the const through a per-impl `&self` method. This is the already-recorded
  generic-assoc-const-projection gap (B121); re-surfaced here. Affected:
  `assoc-const-per-impl-b163` reads the const via the concrete path + a `sides()`
  method instead of `T::SIDES`.

## Other observations (NOT counted as new gaps — documented conventions/limits)

- **`str` length vs byte-index value types differ** — `s.len()` yields an `i64`
  while a byte index `s[i]` yields a `u64` (an `i64` literal compares against the
  former, a `u64` literal against the latter). Not a gap; recorded so the typing in
  `str-literal-ops-b163` reads intentionally.

## Dropped / reshaped tests

- `binding/func-arg-ref-pattern` (explicit `*p`-destructure of a `&(i64,i64)` tuple
  param) — RESHAPED to auto-deref field access `p.0`/`p.1` (G163-1: explicit deref of
  a tuple reference SIGSEGVs; auto-deref works).
- `array-slice-vec/array-of-arrays` (in-place `grid[0][1] = ..`) — RESHAPED to write
  through an extracted `row` binding (G163-2: chained two-level indexed write is a
  parse error; two-level read works).
- `associated-consts/assoc-const-per-impl` (`T::SIDES` generic projection) — RESHAPED
  to read via the concrete `Tri::SIDES`/`Quad::SIDES` path + a `&self` method
  (G163-3: generic assoc-const projection unsupported, same root as B121).

Total: **41 KEPT / passing** tests; **0 DROPPED wholesale**; **3 RESHAPED** (each
keeping its portable core). **3 NEW gaps surfaced**: G163-1 (⚠️ SILENT CRASH —
explicit `*p` deref of a tuple reference SIGSEGVs; auto-deref works), G163-2
(TRACTABLE parse — chained two-level indexed write `a[i][j] = ..` is a syntax error;
the read parses), and G163-3 (TRACTABLE — generic assoc-const projection `T::SIDES`
unsupported, same root as the existing B121 gap). No silent miscompiles this batch
beyond the G163-1 crash.
