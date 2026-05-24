# B167 — Adversarial Depth-Probe Gap Census

> STATUS 2026-05-24: 10/15 ports PASS. FIXED: G167-4 (drop×break, f719c90d), G167-5 (IndexMut compound-assign, b9ce0ee9) — both silent miscompiles. OPEN: G167-1/-2/-3 (closure-param inference from expected Fn context — one shared root, 3 sites: method Fn-bound formal / generic struct field / Box<dyn Fn> return), G167-6 (slice suffix-after-`..` + named-rest re-use), G167-7 (Vec<Box<dyn>> dispatch SIGSEGV — fat-handle round-trip through generic T). Repros in b167-repros/.

Provenance: rust-lang/rust@4b0c9d76ae7d387229caea55cfa73c280b08b8a7 (2026-05-24).
Compiler used as-is: `build/bin/logosc` (no rebuild). Repros live in `b167-repros/<slug>.logos`.

This batch probed fragile feature *intersections* (closures×structs/returns, iterators×
custom-Iterator adapters, operator-overload Index×compound-assign, Drop×loop/break, trait
objects×Vec, slice patterns) rather than maximizing green count. 15 ports → **7 gaps**:
3 NEW silent/runtime defects (2 MISCOMPILE, 1 CRASH-SIGSEGV), 1 MLIR-gen CRASH, 1 split
CRASH+REJECT, 2 closure-inference REJECTs that share one root. 7 ports PASS (the
intersection works): t01, t05, t07, t09, t12, t13, t14, t15.

Legend: **MISCOMPILE** = links + wrong result; **CRASH** = SIGSEGV/MLIR-gen fail on
legitimate code; **REJECT** = parse/sema rejection of code that should work per Rust.

---

## NEW gaps

### G167-4 — Drop skipped on the `break`-exiting iteration of a `loop` — ✅ FIXED 2026-05-24  [repro t06]
`loop { let _g = Guard{..}; i += 1; if i == 3 { break; } }` runs 3 iterations, each binding
a fresh droppable `_g`. Expected counter (sum of Drop side effects) = 3; **observed 2**. The
iteration that executes `break` bypasses its loop-body scope's drop glue. A `while i < 3 { let
_g = .. }` loop (no break) correctly drops 3 times (counter = 3) — so the bug is specifically
the `break` edge out of a `loop`, not loop-body drops in general. Silent, no crash.
**High value** — corrupts resource accounting on any early-exit loop holding RAII guards.
Intersection: Drop × loop/break.

### G167-5 — compound-assign through user IndexMut returning an `else`-branch place — ✅ FIXED 2026-05-24  [repro t08]
`g[i] op= v` on a `struct Grid` with `impl Index/IndexMut` works ONLY when `index_mut`
returns its **first** conditional branch (`&mut self.a`, i==0). When `index_mut` returns the
`else` branch (`&mut self.b`, i==1) the read-modify-write is silently lost **and** adjacent
state is corrupted: probe `g[1] += 100` (from `{a:1,b:2}`) left `g.b == 2` (unchanged) and
zeroed `g.a`. Boundary: plain `g[1] = v` and read `g[1]` BOTH work correctly; only the
compound (`+=`/`*=`/`-=`/`/=`) path mis-targets when the IndexMut return is a conditional
else-branch place. The compound desugar appears to reuse the index-0 / first-branch place for
the write-back. Silent. **High value.** Intersection: operator-overload Index/IndexMut ×
compound-assign × conditional-return place.

### G167-7 — `Vec<Box<dyn Trait>>` (fat-pointer element) — CRASH (SIGSEGV)  [repro t10]
A Vec whose element type is a fat-pointer `Box<dyn Shape>` SIGSEGVs at runtime — even a single
`push` + `get` + `area()` dispatch crashes (exit 139). Isolated: `Vec<Box<i64>>` (thin element)
works end-to-end, and a bare `let b: Box<dyn Shape> = box_new(..); b.area()` dispatch works.
Only the fat `Box<dyn>` stored through Vec's generic `T` element slot fails to round-trip — Vec
element storage/copy assumes a thin/scalar `T` and truncates the vtable half (garbage vtable on
dispatch). **High value** — `Vec<Box<dyn Trait>>` is the canonical heterogeneous-collection
idiom. Intersection: trait objects (fat pointer) × generic container element storage.

### G167-2 — closure value stored in a generic struct field `F: Fn(..)` — CRASH (MLIR-gen)  [repro t02]
`struct Holder<F: Fn(i64)->i64> { f: F }` with `Holder { f: |x| {..} }` fails: sema cannot
monomorphize the closure into the field, leaving the struct type-arg as `|<error>| -> i64`;
MLIR-gen then emits `unknown struct 'Holder$G1$|<error>| -> i64'` and a body-less `__closure_0`
(`'llvm.return' op expected 1 operand`). A non-generic `f: fn(i64)->i64` field works fine, and
`(h.f)(5)` calls correctly through it (contrast B166-N7, which is now improved). So closures
only round-trip through fn-pointer-typed fields today, not generic `Fn`-bounded fields.
Intersection: closures × generic struct fields. (Shares the closure-inference root with G167-3
/ G167-1.)

### G167-3 — closure literal in `box_new(..)` return position not inferred from `Box<dyn Fn>` — REJECT  [repro t03]
`fn adder(n) -> Box<dyn Fn(i64)->i64> { return box_new(|x| { x + n }); }` →
*"expected Box<|i64|->i64>, got Box<|<error>|->i64>"*. The closure literal's parameter types are
not inferred from the function's `Box<dyn Fn(i64)->i64>` return type; the param defaults to
`<error>`. Affects both capturing and non-capturing closures. Workaround (verified PASS): bind to
a local with an explicit param type first — `let cl = |x: i64| -> i64 {..}; return box_new(cl);`.
Intersection: closures × Box<dyn Fn> return. Same root as G167-1/G167-2.

### G167-1 — closure-param inference from a method's Fn-bound type (`.fold`) — REJECT  [repro t04]
`it.fold::<i32>(0i32, |a, x| { a + x })` on a custom Iterator → *"operator '+': type mismatch
(Acc vs i32)"* + *"return type mismatch — expected i32, got Acc"*. The fold closure's params
(`a`, `x`) are left to infer from the `FnMut(Acc, Item) -> Acc` parameter bound (with `Acc`
pinned by the turbofish to `i32`), but they stay unresolved type-vars in the closure body.
Boundary: a **named fn** `fn adder(a:i32,x:i32)->i32` passed to the same `.fold::<i32>` works,
and an explicit-typed closure `|a: i32, x: i32| {..}` works. Only inferred closure params fail.
Intersection: iterators (method-level type-params) × closures. Same root as G167-2/G167-3:
**no closure-parameter type inference from an expected Fn-typed context.**

### G167-6 — slice patterns: suffix-after-`..` + local re-use of a named rest sub-slice — REJECT + CRASH  [repro t11]
Two distinct cracks in dynamic-slice (`&[T]`) patterns:
- **G167-6a (REJECT):** `[first, .., last]` (binding elements AFTER the `..` rest) →
  *"slice pattern: suffix after '..' not supported for dynamic slices"*. Only prefix elements
  before a trailing `..` are supported.
- **G167-6b (CRASH, MLIR-gen):** binding a named rest `[a, rest @ ..]` and then USING the rest
  sub-slice locally — `rest.len()` OR re-matching `match rest { [x, ..] => .. }` — fails
  MLIR-gen: *"'llvm.getelementptr' op operand #0 must be LLVM pointer type … but got
  '!llvm.struct<(ptr, i64)>'"* (GEP applied to a `{ptr,i64}` slice value instead of its data
  pointer). Boundary: passing the rest sub-slice to a **separate function** works (the existing
  `slice-pattern-recursion-15104` test recurses that way); the crack is treating the bound rest
  as a slice **place** in the same function. Intersection: slice patterns × sub-slice place use.

---

## PASS — intersections that work (banked as green candidates)

- **t01** FnMut mutating a captured local across 3 calls (accumulates 1,2,3; local reflects 3). ✓
- **t05** `?` converting between TWO distinct custom error types via `From<ParseErr> for AppErr`
  (variant→variant, not just primitive→struct). ✓
- **t07** match with or-pattern `1|2|3`, range arm `4..=10`, binding+guard `n if n>100`, wildcard. ✓
- **t09** supertrait method call through a generic bound (`describe<T: Named>` calls `Base::tag`). ✓
- **t12** `iter_zip` over two distinct custom `Iterator` types, stops at shorter, `ZipPair` sum. ✓
- **t13** Drop on early `return` through a nested `Outer{Inner}` (both Drops fire: +1 then +100). ✓
- **t14** `impl Add for Point` with associated `Output = i64` (Output ≠ Self). ✓
- **t15** generic method `combine<U: Tag>` on generic struct `Wrap<T: Tag>` — both bounds dispatch. ✓

---

## Summary
- **7 gaps** from 15 ports (high hit rate — adversarial intersections, not breadth):
  - **2 silent MISCOMPILES** (G167-4 Drop-on-break, G167-5 IndexMut compound-assign) — the
    most dangerous; both corrupt state with no diagnostic.
  - **1 runtime SIGSEGV** (G167-7 Vec<Box<dyn>>).
  - **1 MLIR-gen CRASH** (G167-2 closure-in-generic-field) + **1 split CRASH/REJECT** (G167-6).
  - **3 REJECTs from one shared root** (G167-1/-2/-3): **no closure-parameter type inference
    from an expected Fn-typed context** (method Fn-bound, generic struct field, Box<dyn Fn>
    return). Named-fn and explicit-typed-closure forms all work — inference is the single hole.
- **8 PASS** — Drop-on-early-return, `?`-cross-custom-From, or/range/guard match, supertrait
  generic bounds, zip on custom iters, assoc-Output Add, and generic-method-on-generic-struct
  are all mature.
- **Fragile boundaries this batch:** (1) **closure-param inference** from expected Fn types —
  one root, three surfaces; (2) **fat-pointer values through generic containers** (Vec<Box<dyn>>);
  (3) **place mis-targeting in desugared writes** — compound-assign reuses the wrong IndexMut
  branch, and `break` skips loop-body drops; (4) **dynamic-slice rest sub-slices** as local places.

### Highest-impact (block the most downstream / silent)
1. **G167-5 (IndexMut compound-assign MISCOMPILE)** — silent wrong result + adjacent corruption;
   `c[i] += v` over any user container with a branching `index_mut` is extremely common.
2. **G167-4 (Drop-on-break MISCOMPILE)** — silent; any RAII guard in a `loop`+`break` leaks/
   double-counts. Touches every resource-management idiom.
3. **G167-1/-2/-3 (closure-param inference, shared root)** — one fix unblocks closures-in-fields,
   returned-boxed-closures, and inferred fold/map closures simultaneously; gates idiomatic
   iterator pipelines and stored-callback structs.
