# B161 — rustc UI run-pass import: surfaced gaps

Batch B161 imported **33 NEW DISTINCT run-pass tests** from `tests/ui/`, mined for
FEATURE COVERAGE across FRESH / under-mined areas:
functions-closures (5), structs (3), match (3), binding (3),
array-slice-vec (2), tuple (2), let-else (2), methods (2),
autoref-autoderef (1), deref (1), drop (1), enum (1), for-loop-while (1),
generics (1), loops (1), mir (1), overloaded (1), str (1), traits (1).
(`functions-closures`, `let-else`, `tuple`, `loops`, `array-slice-vec`,
`autoref-autoderef`, `deref` are fresh/under-mined relative to B158's
closures/coercion/self/dst, B159's binop/generics/structs-enums/regions/ufcs, and
B160's numbers-arithmetic/mir/char/cast/recursion/type-alias-enum-variants.
`let-else` is a NEW import dir.)

Workflow matches B149–B160: faithful ports, `pub fn main()` → `fn main() -> i32
{ …; return 0i32; }`, isize/usize → i64/u64, integer/float literals suffixed,
`assert!`/`assert_eq!` → early-return sentinels (distinct nonzero codes),
println!/derive/Box/Rc/RefCell/Cell/Vec/PhantomData/named-lifetimes/`#[repr]`
dropped or reshaped where incidental, nested fns/type decls hoisted to module
scope, unit structs `struct S;` → `struct S {}`, `&self`/`&mut self` →
`self: &Self` / `self: &mut Self`, by-value `self` → `self: Self`. No module
statics/consts (G153-3/G158-3): Drop counters modeled via a `*mut i64` threaded
through fns; `#[derive(Clone)]` → an explicit `impl Clone`; `#[derive(Copy)]`
dropped (Copy by layout). All 33 compile + link + exit 0 against the as-is
`build/bin/logosc` (no compiler changes). Link line uses `-Wl,--gc-sections` (as
for B149–B160).

Coverage highlights: a closure that returns and then immediately invokes an inner
closure (closure-returning-closure); FnOnce-inferred `|i| foo(i)` through a
generic `apply<A,F:FnOnce(A)->A>` (closure-inference); fn-item→fn-pointer-type
cast `foo as IntMap` incl. an if/else LUB of two fn items (fn-item-type-cast); a
closure taking three MIXED-width int args u8/u16/u8 (closure-immediate); a Copy
closure passed by value to a `FnOnce`-bounded fn three times (copy-closure);
auto-ref of an owned receiver at a `&self` trait-method call site (auto-ref-printme);
a user `Deref` chain of depth 2 `Root→JSRef→Node` with method autoderef
(deref-chain-method-calls); overloaded-autoderef ORDER — a wrapper's own field/
method wins over the Deref-target's, `(*w).x` reaches one level down
(overloaded-autoderef-order); MIR match lowering of overlapping inclusive AND
exclusive integer range patterns with arm guards, plus range-vs-const ordering
(mir-match-range-guards); match-arm guards on ints + struct-field bindings
(match-guards); negative signed inclusive range patterns `-128i8..=-101i8` in
match/if-let (match-negative-int-ranges); a tuple of two `&Enum` struct-variant
patterns — full cross product + wildcard-fallback arms (match-tuple-of-enum-refs);
`match`-as-expression in let-init/reassign/match-head/block-result
(expr-match-corners); exhaustive `(bool, bool)` tuple match with wildcard arms
(exhaustive-bool-match); `if let` + `else if`/`else if let` chains with a
non-matching literal arm (if-let-chains); refutable let-else (Option bind +
variant-level refutation `let None=Some(_) else` + closure literal `let 1=2 else`)
(let-else-bindings); NESTED let-else over integer literals inside a `loop` whose
inner else drives `continue`/`break` (let-else-nested); a fixed-size array is Copy
(`let arr2=arr`) (fixed-length-copy); mutability inherits through a fixed-length
array — indexed compound-assign + shared-borrow `for i in &arr` read-back
(mutability-through-fixed-vec); chained nested-tuple positional indexing
`(1,(2,(3,4))).1.1.1` (nested-index); tuple-struct AND plain-tuple positional
field indexing/compound-assign + `&mut` borrow of one field while mutating the
other (tuple-index-mut); multiple inherent methods of the SAME name on different
concrete type-args of a generic struct (inherent-methods-same-name); UFCS method
calls passing self explicitly — trait `&self`/by-value-self, trait default, plus
inherent by-value-self, observed via a shared `*mut i64` counter
(method-self-arg-ufcs); a generic identity fn + a `Clone`-bounded `f<T>` building a
generic `Pair<T>` cloned into both fields and threaded through `g::<Pair<T>>`
(generic-derived-type); a method-generic trait DEFAULT method `g<U>` on a trait
generic over `T`, dispatched through `f<T,U,V:A<T>>` (bound-subst-default-method); a
2-level enum discriminant chained via `as`-cast `Y::A = X::A as i64`
(enum-discrim-chained); `loop` used as an EXPRESSION via value-carrying `break`
(plain + labeled `break 'outer V` + nested) (loop-break-value); a labeled `loop {
break 'outer; }` exiting to the fn body (loop-with-label-break); a newtype
tuple-struct wrapping a struct with a fn-pointer field — destructure + fn-ptr
field call via a local (basic-newtype-pattern); a 12-field struct construction +
field access (large-records); struct-literal fields listed OUT of declaration
order with a moved String field (struct-order-of-eval); a Drop value moved into
`Some(..)` drops exactly once at block end (drop-through-option); `str` byte
indexing + value-equality (`str_eq`) + lexicographic `< <= > >=` over string
literals (estr-slice-cmp).

## Gaps surfaced

- **G161-1** ✅ CLOSED (2026-05-23) — passing a `[StructType; N]` array BY VALUE
  as a fn parameter no longer SIGSEGVs. Root (mlir_gen_fn param setup): an array
  param registered `var_subscript_[name] = logos_to_mlir(elem)`, but
  `logos_to_mlir(Struct)` is `ptr` — so the index GEP strided the `[C;N]` as
  array-of-pointers (8B), read each inline struct's first word as a pointer, and
  dereffed garbage. Fix: for a struct/zoned-struct element, register the
  struct's LLVM type (sizeof(Struct) stride, inline-element address) — mirrors
  the slice-param branch. Verified with multi-field (24B) struct elements.
  Re-imported `copy-out-of-array-1`.

- **G161-2** ✅ CLOSED (2026-05-23) — `for x in &mut arr` over a fixed-size
  array now iterates by mutable reference (yields `&mut T`). A bare `&mut
  <array>` lowers to a thin `&mut elem` (stdlib-compat, kept), which for-in
  rejected ("'&mut i64' is not iterable"). Fix (lower_for_each): detect a
  `&mut <array-var>` iter, build a mutable slice over the array, and bind the
  loop var as `&mut T` (mut). The shared `for x in &arr` (`&T`) and by-value
  forms are unchanged. Regression `for-mut-ref-array`. (Also restored the
  `for &mut` facet in `mutability-through-fixed-vec` is left as-is — the
  dedicated regression covers it.)

- **G161-3** ✅ CLOSED (2026-05-23) — a let-else with a variant carrying a
  REFINED literal/sub-pattern (`let Some(1) = Some(2) else {…}`) no longer
  silently ignores the inner test. Root: the let-else codegen tested only the
  variant discriminant; build_pattern's `synth_refutable_inner` guards
  (`__refut_N == value`) were collected by `match` but dropped by let-else. Fix:
  thread the refutable-inner guards onto `SLetElse` (new `guards` expr-array on
  the LIR struct + schema key + emit/view/mono-subst), and in codegen test them
  in the match_block AFTER the payload bindings are bound — branching to the
  else block on failure. Regression `let-else-refined-inner`. Full suite green.

- **G161-4** ✅ CLOSED (2026-05-23) — a generic `T: Clone` value (instantiated at
  a struct) produced by a `match`-EXPRESSION arm whose body is a by-value-
  returning call (`e.clone()`) no longer corrupts/SIGSEGVs. Root (EMatchExpr
  codegen): the result slot is `logos_to_mlir(T-after-mono=struct)` = `ptr`
  (8B), but the arm produced the struct BY VALUE (16B) — storing the wide value
  into the ptr slot overflowed the stack, and the merge `load ptr` read garbage.
  A struct-LIT arm returns a pointer (worked); a call-result struct value did
  not. Fix: when the result slot is a by-pointer aggregate but the arm value is
  an aggregate VALUE, spill it to an alloca and store the pointer
  (`store_arm_result` helper, both guarded and plain arm paths). Re-imported
  `expr-match-generic`. Full suite green.

- **G161-5** TRACTABLE — functional struct update `..base` over a GENERIC struct
  (`Partial<T>`) is rejected: the spread does not carry the remaining fields, so
  `Partial { y: …, ..p }` errors "struct literal 'Partial': field 'x' not
  initialized". Concrete-struct FSU `P { y: …, ..p }` works (B159
  fsu-field-sensitivity). The original test also exercised a PARTIAL move
  (`..p` after moving `p.y`), but the generic-FSU root fires first. Minimal repro:
  ```
  struct P<T> { x: T, y: T }
  fn f<T>(b1: T, b2: T) -> P<T> { let p = P { x: b1, y: b2 }; return P { y: b2, ..p }; }  // error: field 'x' not initialized
  ```
  Dropped: `structs/struct-partial-move-1.rs`. Assessment: TRACTABLE — the
  struct-literal FSU field-fill resolves the carried fields off the base type;
  for a generic-typevar-typed base it isn't enumerating the template's remaining
  fields. (Turbofish placeholder `f::<i64, _>(…)` — `_` in a turbofish slot — is
  also an unrelated parse-reject seen here.)

## Other observations (NOT counted as new gaps — consistent with documented conventions/limits)

- **Unit-typed parameters** — `fn f(u: ())` is REJECTED by design: "parameter 'u'
  has unit type '()'; unit-typed parameters carry no information". This is a
  deliberate Logos divergence (not a gap). Dropped:
  `type/unit-type-basic-usages.rs`.

- **Closure `.clone()` / `.into()`** — a closure value is not a struct receiver, so
  `hello.clone()` fails "method call: receiver is not a struct (got || -> i64)".
  Closures ARE Copy (pass-by-value-twice works — see copy-closure), but have no
  callable `.clone()` method. Dropped the `.clone()` facet of `copy-closure.rs`
  (kept the Copy facet). Matches the closures-aren't-struct-method-receivers limit.

- **OR-pattern in a let-(else) BINDING position** — `let (A(ref x) | B { f: ref x })
  = … else {…}` → syntax error near `{` / `}`. Or-patterns work in `match`/`if let`;
  the let-binding position (like the B160 for-loop or-binding) doesn't accept them.
  Dropped that facet from `let-else-run-pass.rs` (kept the nested let-else).

- **Parenthesized fn-pointer FIELD call** — `(data.compute)(arg)` → syntax error
  near `)`. Binding the field to a local first (`let f = data.compute; f(arg)`)
  works. Reshaped `basic-newtype-pattern` accordingly.

- **Deferred (branch-filled) `let` needs `let mut`** — `let clause: i64;` then
  assigning `clause` in every if/else branch → "assignment to immutable variable".
  Logos requires `let mut clause` for deferred init (matches the B159 `let x:&T;`
  observation). Reshaped `if-let-chains` with `let mut`. Possible divergence from
  Rust's definite-assignment analysis; noted, not counted as a fresh gap.

## Dropped tests (and the gap that caused each drop)

- `array-slice-vec/copy-out-of-array-1.rs` — G161-1 ⚠️ (struct-array by-value param
  SIGSEGV). Dropped wholesale. (`destructure-array-1.rs` ruled out for the same
  reason without being ported.)
- `binding/expr-match-generic.rs` — G161-4 ⚠️ (generic struct value from a
  match-expr arm crashes). Dropped wholesale.
- `structs/struct-partial-move-1.rs` — G161-5 (generic-struct FSU `..base` doesn't
  carry fields). Dropped wholesale.
- `array-slice-vec/mutability-inherits-through-fixed-length-vec.rs` (`for x in
  &mut arr` facet) — G161-2. KEPT a reshaped `mutability-through-fixed-vec-b161`
  that mutates via a `while`-index loop and reads back via `for i in &arr`.
- `let-else/let-else-run-pass.rs` (or-pattern-bind + `Some(1)=Some(2) else` facets)
  — or-pattern-in-let-binding limit + G161-3 ⚠️. KEPT `let-else-nested-b161` with
  the nested integer-literal let-else.
- `functions-closures/copy-closure.rs` (`hello.clone()` facet) — closures aren't
  struct receivers. KEPT the Copy-pass-by-value facet as `copy-closure-b161`.
- `structs/basic-newtype-pattern.rs` (`(data.compute)(arg)` facet) — parenthesized
  fn-ptr field call parse-reject. KEPT via a local-bound fn-ptr call.
- `type/unit-type-basic-usages.rs` — unit-typed parameter is a deliberate Logos
  rejection (divergence). Dropped wholesale; not counted as a gap.

Total: **33 KEPT / passing** tests. Most surfaced gaps are single facets of an
otherwise-portable test, so those tests were kept with the unsupported facet
reshaped or dropped; the wholesale drops are the two ⚠️ crash gaps (G161-1
struct-array param, G161-4 generic-struct match-expr value), one ⚠️ silent
miscompile facet (G161-3 let-else refined sub-pattern, facet-dropped), and the
generic-FSU test (G161-5).
