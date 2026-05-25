# B167 — Adversarial Depth-Probe Gap Census

> STATUS 2026-05-24: **ALL gaps CLOSED.** FIXED: G167-4 (drop×break, f719c90d), G167-5 (IndexMut compound-assign, b9ce0ee9) — both silent miscompiles; G167-1/-2/-3 closure-param INFERENCE root (0b68aed1, one fix/3 sites); G167-3b capturing-closure env escape (3961c1e4, heap-promote boxed env); G167-6 dynamic-slice suffix-after-`..` + named-rest sub-slice place (8d9168ec); G167-7 Vec<Box<dyn>> dispatch (8d9168ec, concrete→dyn coercion through generic container element). Full suite 5158/5158. Repros in b167-repros/.

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

### G167-7 — `Vec<Box<dyn Trait>>` (fat-pointer element) — ✅ FIXED 2026-05-24  [repro t10]
A `Vec<Box<dyn Shape>>` SIGSEGV'd: `v.push(box_new(Square{..}))` stored a THIN `Box<Square>`
handle (no `{data,vtable}` fat slot), so later `v.get(i).area()` dispatch read a garbage vtable.
Root: the unsize coercion `Box<Concrete> → Box<dyn Trait>` was applied only when the formal was
SYNTACTICALLY a trait object (a concrete `Box<dyn>`/`&dyn` param, e.g. a plain fn arg); it was
NOT applied when the formal was a GENERIC param `T` later bound to `Box<dyn Trait>` (Vec::push's
`val: T`). Diagnosis matrix: push ✓, get ✓, dispatch ✗; `disp(box_new(Square))` ✓ (concrete
formal); coercing to dyn BEFORE push ✓. Fix (mlir_gen_expr): (1) the method-call arg path now
does concrete→dyn coercion at all (it did none before), looking up the callee's Logos param
types by the RESOLVED FuncOp name (which carries the `__g__<arg>` generic-instance suffix), not
the un-suffixed base; (2) both the method and free-fn arg paths peel a `Box<TraitObject>` formal
to its inner `TraitObject` so a generic param that resolved to `Box<dyn>` is recognized. Test:
traits/vec-box-dyn-dispatch-b167 (push two distinct concrete types, loop-dispatch `area()`, sum).
**High value** — `Vec<Box<dyn Trait>>` is the canonical heterogeneous-collection idiom.

### G167-2 — closure value stored in a generic struct field `F: Fn(..)` — ✅ FIXED 2026-05-24  [repro t02]
`struct Holder<F: Fn(i64)->i64> { f: F }` with `Holder { f: |x| {..} }` previously failed: the
untyped closure left the struct type-arg `|<error>| -> i64` → MLIR-gen "unknown struct". FIXED:
lower_struct_lit now hints the field-value closure literal from the field type-param's Fn-family
bound (`closure_hint_from_fn_bound`), so `|x|` infers `x: i64`. Test:
closures/closure-in-struct-field-fn-bound-b167. (NOTE: `let h: Holder<_>` with a `_` type-arg in
a LET annotation is a separate, still-open `_`-inference-hole gap — the idiomatic no-annotation
form works; K-misc only generalized `_` to turbofish, not let-annotations.)

### G167-3 — closure literal in `box_new(..)` return not inferred from `Box<dyn Fn>` — ✅ FIXED (inference) 2026-05-24  [repro t03]
`fn adder(n) -> Box<dyn Fn(i64)->i64> { return box_new(|x| { x + n }); }` previously →
*"expected Box<|i64|->i64>, got Box<|<error>|->i64>"*. FIXED: `peel_to_callable` unwraps
`Box<dyn Fn>`/`&dyn Fn` to the inner Fn signature at the closure-literal hint site + the
return-stmt hint, so `|x|` infers `x: i64`. Test (NON-capturing):
closures/boxed-noncapturing-closure-return-b167.

### G167-3b — a CAPTURING closure boxed/returned dangles & aliases — MISCOMPILE (NEW, exposed by G167-3)  [repro t03]
With inference fixed, `return box_new(move |x| x + n)` now COMPILES but mis-runs: two `adder(5)`
/`adder(10)` boxes both read `n=10`. Root (confirmed in LLVM IR): a capturing closure is a fat
value `{fn_ptr, env_ptr}` whose env (`{i64}` holding `n`) is `alloca`'d on the **stack** of the
creating fn; `box_new` heap-copies only the 16-byte fat handle, so `env_ptr` still points at the
caller's stack frame → after return the slot is reused and both boxes alias the latest call.
`move` does not help (env is still stack-resident). NON-capturing boxed closures are fine (no
env). FIX needs escape-aware env promotion: heap-allocate (and free / RAII) a capturing closure's
env when it escapes its frame — or make the closure value own its captures inline so boxing copies
them. Same fat-pointer-storage family as G167-7. **High value** — returned/stored capturing
closures (callbacks, factories) are a core idiom.

### G167-1 — closure-param inference from a method's Fn-bound type (`.fold`) — ✅ FIXED 2026-05-24  [repro t04]
`it.fold::<i32>(0i32, |a, x| { a + x })` on a custom Iterator previously → *"operator '+': type
mismatch (Acc vs i32)"*. FIXED: `preload_formals` now threads the method-level turbofish args into
the closure-formal hint. A trait-default method cloned into a concrete iterator stores its
type-params as `[<struct-inherited…>, <method-level…>]` (e.g. `[I, T, Acc, FoldFn]`); the
turbofish supplies only the method-level ones, so each turbofish arg binds to the next type-param
NOT already bound by the receiver (`Acc` here). The Fn-bound synthesis then resolves
`FnMut(Acc, Item)->Acc` to `FnMut(i32, i32)->i32`. Test:
iterators/fold-inferred-closure-params-b167. **The shared closure-param-inference root behind
G167-1/-2/-3 is now closed; only the orthogonal env-escape defect (G167-3b) remains.**

### G167-6 — slice patterns: suffix-after-`..` + local re-use of a named rest sub-slice — ✅ FIXED 2026-05-24  [repro t11]
Two distinct cracks in dynamic-slice (`&[T]`) patterns, both fixed:
- **G167-6a:** `[first, .., last]` (binding elements AFTER the `..` rest) was REJECTED at sema.
  Now supported: sema reject removed; codegen gates the arm on `len >= prefix + suffix` (already
  did) and binds/checks each suffix element from the runtime length at `len - suf_n + i`.
- **G167-6b:** binding a named rest `[a, rest @ ..]` and USING the rest sub-slice locally
  (`rest.len()`, re-match) crashed MLIR-gen (GEP on a `{ptr,i64}` slice value). Root: the
  statement-match binder typed the rest as `var_elem_types_=<{ptr,i64} struct>` (a let-scalar)
  instead of `var_slice_=<elem>` (a first-class slice place), so `.len()` GEP'd the struct value.
  Now bound as a proper `&[T]` place {data + pre_n, len - pre_n - suf_n}; suffix accounted for in
  the rest length. Both the statement-match (mlir_gen_stmt) and value-match (mlir_gen_expr) paths
  fixed in parallel. Test: slice/slice-suffix-and-named-rest-b167.

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
