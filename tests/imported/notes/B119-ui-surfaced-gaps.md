# B119 — UI-surfaced gaps (closures + typeck run-pass)

Source: `tests/ui/closures/` + `tests/ui/typeck/` `//@ run-pass`, distilled
to DISTINCT features (the two corpora are heavily mined by prior batches —
this batch distils features rather than copying macro/Box-dyn/thread-driven
files). All gaps below are **§B catch-up TODOs** — no new §A blessed
divergences. Pinned commit `4b0c9d76ae7d387229caea55cfa73c280b08b8a7`.

## NEW gaps (precise, all §B)

### G119-1 — plain `=` assignment to a captured outer `mut` local from inside a closure
- **Symptom:** `mlir_gen: assign to undefined 'seen'` then the assignment is
  silently dropped (value never written; binding reads its prior value).
- **Repro (minimal):**
  ```
  let mut seen: i64 = 0i64;
  let mut f = || { seen = 5i64; };   // plain `=`
  f();                                // seen stays 0
  ```
- **Discriminator:** the **compound-assign** form WORKS:
  ```
  let mut hit: i64 = 0i64;
  let mut f = || { hit += 1i64; };   // OK — captured by mut-ref, writes through
  ```
  So the capture-by-mut-ref machinery exists; only the **plain-`=` store path**
  in closure lowering fails to resolve the captured upvar (the compound-assign
  read-modify-write path resolves it). Independent of whether the closure is
  called directly or passed through an `F: FnMut()` generic bound.
- **Feature:** closure upvar capture — write-only plain assignment.
- **Classification:** §B (catch-up; Rust allows `seen = 5` in a `FnMut`).
- **Workaround used in this batch:** the closure-capture-clone-then-move and
  the disjoint-mut tests use `+=` (which works) or move-capture + read-only.

### G119-2 — capturing a non-Copy `String` into a closure passed to an `F: FnOnce`/`F: FnMut` generic bound + calling a method on it
- **Symptom:** `error: 'llvm.return' op expected 1 operand` →
  `mlir_gen: module verification failed` (codegen, not sema).
- **Repro (minimal):**
  ```
  fn run<F: FnOnce() -> i64>(f: F) -> i64 { return f(); }
  let s: String = String::from("hello");
  let n: i64 = run(|| -> i64 { return s.len(); });   // crash at codegen
  ```
- **Discriminator:** the analogous test with an `i64` capture
  (`once-move-out-on-heap.logos`, already imported) compiles + runs. Adding/
  removing `move` does not change the outcome — it is the **non-Copy heap
  value (`String`) captured into the closure environment of a closure passed to
  a generic Fn-family bound** that breaks the generated thunk's return.
- **Feature:** FnOnce/FnMut closures over non-Copy heap captures through a
  generic bound.
- **Classification:** §B (catch-up).

### G119-3 — `where`-bound on an **impl-level** type-param does not provide that param's trait methods inside the impl's method bodies
- **Symptom:** `type parameter 'A' has no trait bound providing method 'up'`
  (sema), even though the impl header declares `where A: Up<X>`.
- **Repro (minimal):**
  ```
  trait Up<T> { fn up(self: Self) -> T; }
  struct Wrap<A> { a: A }
  impl<A, X> Up<X> for Wrap<A> where A: Up<X> {
      fn up(self: Wrap<A>) -> X { return self.a.up(); }   // 'A' has no bound providing 'up'
  }
  ```
- **Discriminator:** the SAME bound works on a **free generic fn**:
  `fn convert<A: Up<i64>>(a: A) -> i64 { return a.up(); }` resolves fine.
  So `where`-bound method dispatch is wired for free-fn type-params but NOT for
  impl-block type-params — the impl method body's bound environment does not
  include the impl's own `where`-clause bounds when resolving methods on a
  field/local of an impl type-param.
- **Feature:** recursive / structural generic trait impls (the upstream
  `nested-generic-traits-performance` `Upcast<(T1,T2)> for (S1,S2) where S1:
  Upcast<T1>, S2: Upcast<T2>` shape).
- **Classification:** §B (catch-up).

### G119-4 — raw-pointer cast with placeholder pointee `as *const _`
- **Symptom:** `error: unknown type '_'` (sema) on `ptr as *const _`.
- **Repro (minimal):**
  ```
  let ptr: &u64 = &v;
  let ptr2 = ptr as *const _;   // unknown type '_'
  ```
- **Discriminator:** the explicit form `ptr as *const u64` works. Only the
  `_` placeholder pointee in a cast target is rejected (general `_` type
  inference at `let` works elsewhere).
- **Feature:** type-placeholder inference in a cast target (from upstream
  `typeck_type_placeholder_1`).
- **Classification:** §B (catch-up).

## Re-confirmed KNOWN-OPEN (NOT re-reported, per task instructions)
- closure→fn-ptr coercion at a `let` with explicit `fn(..)`-typed binding from
  a closure value (`let cp: fn(i64)->i64 = c;` → `expected fn(i64)->i64, got
  |i64|->i64`) — closure-to-fn-ptr coercion at a binding. (Distinct from the
  WORKING fn-item→fn-ptr coercion; the non-capturing-closure→fn-ptr coercion is
  the part that fails. Tracked in the closures fn-family area.)
- `Vec::get`/`Vec` methods on a `Vec<U>` whose element type is an unbound
  generic type-param inside a trait-impl method → `unresolved TypeVar 'U' —
  mono_pass required` (generic-Vec-method-in-generic-impl mono area).
- `Foo::<i32, _>` turbofish with a `_` placeholder type-arg → `unknown type
  '_'` (placeholder in turbofish).

## Wobble / soundness observation (NOT imported as a passing test)
- **Nested closures capturing the same `&Vec` over ≥4 elements miscompute.**
  The upstream `old-closure-iter-2` shape — `iter_vec<T, F: FnMut(T)>(v:&Vec<T>,
  mut f)` nested twice, both closures capturing the same outer `&Vec` and a
  `mut sum` accumulator — yields the correct sum-of-products for a 3-element
  vec (sum=36) but `sum == 0` for 4- and 5-element vecs (expected 100 / 225).
  Single (non-nested) iteration is correct at all lengths. Looks like the inner
  iteration corrupts the outer `&Vec` iterator state once the element count
  crosses a small threshold (re-borrow / capture-environment aliasing). §B
  catch-up; left UNIMPORTED because the test would be a silent miscompile.
  Worth a focused baghunt (nested-FnMut-over-shared-&Vec capture).
