# B111 — surfaced gaps (tests/ui run-pass: structs-enums / self / where-clauses / privacy / consts / unboxed-closures / for-loop-while / string)

Batch B111 mined run-pass tests from FRESH `tests/ui/` source categories not yet
covered (or only lightly covered) by B1-B110: `structs-enums`, `self`,
`where-clauses`, `privacy`, `consts`, `unboxed-closures`, plus two genuinely-new
items in already-mined dirs (`for-loop-while/break-value`,
`for-loop-while/for-destruct`) and one `string` item. Upstream commit
`4b0c9d76ae7d387229caea55cfa73c280b08b8a7`.

**39 tests landed:**
- structs-enums (16): struct-field-shorthand, tuple-struct-destructuring,
  functional-struct-upd, enum-discr, rec, rec-tup, borrow-tuple-fields,
  expr-if-struct, expr-match-struct, issue-1701, class-typarams, struct-aliases,
  tag-variant-disr-val, simple-match-generic-tag, discrim-explicit-23030,
  numeric-fields, tag, nonzero-enum, compare-generic-enums  *(18 — see manifest)*
- self (6): by-value-self-in-mut-slot, move-self, explicit-self-generic,
  string-self-append, ufcs-explicit-self, objects-owned-object-owned-method,
  explicit-self  *(7)*
- where-clauses (3): where-clauses, where-clauses-method,
  where-clause-region-outlives
- privacy (2): private-class-field, private-method-rpass
- consts (1): consts-in-patterns
- unboxed-closures (1): unboxed-closures-simple
- for-loop-while (2): break-value, for-destruct
- string (1): byte-string-literal-index

(The structs-enums/self counts above include items split across lines; the
authoritative list is the manifest row + the per-file headers.)

Categories below: `compiler-bug` (crash / wrong runtime value / verifier fail),
`missing-feature` (language feature absent or unparsed), `missing-stdlib`
(library API absent), `intentional-divergence` (Logos design differs from Rust).

---

## compiler-bug

### 1. `&mut tuple.N` place-borrow writeback does not persist — FIXED (2026-05-21)
**Fixed.** `&mut x.N` on a tuple field loaded the element by value, so the
default addr_of_temp took the address of a fresh temp holding the copy and
`*b = v` never reached the tuple. Fix: the EAddrOfTemp codegen now GEPs the
tuple element address directly for a `TupleIndex` child (mirrors the existing
struct-field place-borrow), gated to `&mut` (the immutable `&x.N` path is
relied on to spill a value-copy by the variadic tuple Eq/Debug recursion).
Regression `tests/logos/pass/mut_borrow_tuple_field.logos`.

Original report:
- Surfaced while porting `structs-enums/borrow-tuple-fields.rs`.
- Minimal trigger (compiles + links; wrong runtime value):
  ```
  fn main() -> i32 {
      let mut x = (1i64, 2i64);
      let b = &mut x.1;
      *b = 5i64;
      if x.1 != 5i64 { return 1; }   // FAILS: x.1 still 2
      return 0;
  }
  ```
- Symptom: writing through `&mut x.1` on a **tuple** does not update the tuple;
  the same shape over a **named-field struct** (`&mut xf.f1; *b = 5`) writes back
  correctly. Narrowed: struct-field mut-borrow writeback is fine, only tuple
  fields are affected. The port routes the disjoint-mut-borrow demonstration
  through a named-field struct instead of a tuple; the tuple half keeps only
  shared `&x.0` borrows. Likely a sibling of the B108 `&mut x[i]` writeback bug.

### 2. Multi-field struct payload extracted from an enum variant misreads later fields — FIXED (2026-05-21)
**Fixed.** `register_tagged_enum` stored `vp.field_types[i] = logos_to_mlir(pt)`,
which collapses a struct payload field to `ptr` (8 bytes) — but the constructor
memcpy's the struct INLINE (full ABI size, correctly counted in `variant_bytes`).
So the payload struct type `{ptr, i64}` used for field GEPs put the field after a
struct at offset 8 (inside the struct's bytes), clobbering it on write and
misreading it on read. Fix: new `variant_payload_struct(vp)` builds the payload
struct literal using the INLINE aggregate type (identified struct / tuple) for
struct/tuple fields; routed all four payload-struct-literal sites (write
EEnumLitData, read extract_payload ×2, bind_enum_payload) through it. Regression
`tests/logos/pass/enum_struct_payload_offset.logos`. `record-pat.rs` now portable.

Original report:
- Surfaced while porting `structs-enums/record-pat.rs` (test SKIPPED).
- Minimal trigger (compiles + links; wrong runtime value):
  ```
  struct T2 { x: i64, y: i64 }
  enum T3 { C(T2, i64) }
  fn m(input: T3) -> i64 {
      match input { T3::C(t2, z) => { return t2.y; } }   // returns z, not t2.y
  }
  fn main() -> i32 { return m(T3::C(T2 { x: 1i64, y: 2i64 }, 4i64)) as i32; }
  ```
- Symptom: binding a struct payload `t2` from `T3::C(t2, z)` and reading its
  **second** field `t2.y` yields the enum variant's **second payload slot** `z`
  (got 4, expected 2). `t2.x` (first field) and `z` both read correctly. So a
  multi-field struct bound out of a multi-payload enum variant has a wrong field
  offset for fields past the first — it aliases the enum's later payload slots.
  `record-pat` (whose point is exactly `T3::c(T2 { x: t1::a(m), .. }, _)`) can't
  be ported cleanly; skipped. HIGH-VALUE: struct-in-enum-payload is common.

### 3. `if let None` / match-stmt with a no-payload `Option` arm mis-codegens (`func.return` parent error) — FIXED (2026-05-21)
**FIXED.** Root cause: a bare identifier pattern that names a no-payload
variant of the scrutinee's enum (`None`, or any unqualified user-enum variant)
was lowered as an irrefutable WILDCARD BINDING (named after the variant), not a
variant pattern — so the `None` arm caught everything and the dispatch/codegen
broke. Fix in build_pattern: a bare NAME that matches a no-payload variant of
the scrutinee's enum becomes a `PatVariant`. Bonus: passing the prelude `None`
VALUE directly as a call arg (`f(None)`) also failed — it lowers to
`Option<TypeVar>`, which `try_retype_bare_enum_arg` now treats as incomplete and
retypes to the param's concrete spec. Regression
`tests/logos/pass/bare_variant_pattern.logos`; nonzero-enum portable.

**Corrected diagnosis (2026-05-21):** the custom-enum framing was a red herring.
The trigger is a **statement match (or `if let`) over `Option<T>` with a
no-payload `None` arm** — e.g. `if let None = opt { … }`, which desugars to
`match opt { None => {…} _ => {} }`. Narrowed:
- `if let Some(x) = opt` / `match { Some(_) => … _ => {} }` → OK.
- `match opt { None => { return 0; } _ => {} } return 1;` → `'func.return' op
  expects parent op 'func.func'` (or a `cf.br` translation error when the None
  arm is non-diverging).
- A plain C-like USER enum (`enum E2 { A, B }`, by-value disc — not a heap-ptr
  tagged enum) with the same shape works fine. So it is specific to a heap-ptr
  tagged enum (`Option<T>`) + a no-payload `Variant` arm in `gen_match`.
Root cause not yet isolated (it lives in the large match-statement codegen
`gen_match`; the MLIR verify-abort prevents an IR dump and several narrowing
passes have not pinned the orphaned op). Deferred to a focused gen_match pass.
Workaround in ports: use `if let Some(..)` / a `Some`-first arm / a match-EXPR.

Original report:
### (orig) stdlib `Option<E>` carrying a custom enum mis-codegens (`func.return` parent error)
- Surfaced while porting `structs-enums/nonzero-enum.rs`.
- Minimal trigger (MLIR verify fail at compile time):
  ```
  use logos.lang.option;
  enum E { A = 1, B = 2 }
  fn main() -> i32 {
      let esome = Some(E::A);              // error: 'func.return' op expects parent op 'func.func'
      if let None = esome { return 1; }
      return 0;
  }
  ```
- Symptom: `Some(E::A)` over a custom (C-like, explicit-discriminant) enum
  produces a malformed function during emit. A local generic `Opt<X>` enum with
  the same enum payload (`Opt::Some(E::A)`) works fine. The nonzero-enum port
  uses the local-`Opt` form. Related to the broader stdlib-`Option`-over-generic
  fragility (see #4).

### 4. A bare generic-enum literal passed *directly* as a call argument mis-codegens — FIXED (2026-05-21)
**Fixed.** Same root as the B110 lower_assign retype, in call-argument position:
`cmp(Opt::Some(3), Opt::None)` lowered the bare/partially-typed enum literal
without the parameter type, so it carried no (or `<error>`) type-args and
mlir-gen emitted a C-style i32 discriminant (operand mismatch vs the heap-ptr
param). Fix: new `try_retype_bare_enum_arg(arg, param_type)` (mirrors the
try_coerce_* family), called at the resolved-function arg-coercion sites; it
retypes an incompletely-typed enum-literal arg to the concrete param enum when
the known type-args match. Regression `tests/logos/pass/enum_lit_call_arg.logos`.
compare-generic-enums now portable without the typed-local workaround.

Original report:
- Surfaced while porting `structs-enums/compare-generic-enums.rs`.
- Minimal trigger (MLIR verify fail / operand-type mismatch at compile time):
  ```
  enum Opt<X> { None, Some(X) }
  fn cmp(x: Opt<i64>, y: Opt<i64>) -> bool { /* match both */ }
  fn main() -> i32 {
      if cmp(Opt::Some(3i64), Opt::None) { return 1; }   // error: operand type mismatch (i32 vs ptr)
      return 0;
  }
  ```
- Symptom: passing `Opt::Some(3i64)` / `Opt::None` *directly* as a call argument
  emits a C-style i32 discriminant where a heap-ptr enum is expected. Binding to
  a type-annotated local first (`let s: Opt<i64> = Opt::Some(3i64); cmp(s, ..)`)
  works — so the `let`/assign retype path is correct but the call-argument retype
  path is not (cf. the B110 `lower_assign`/`finish_generic_call` retype work; the
  bare-literal-arg case for a *user* generic enum still slips through). The
  compare-generic-enums port uses typed locals as the workaround. Also note the
  stdlib `Option<i64> == Option<i64>` derived `==` mis-codegens (a giant
  `OptionIter`/`ParseIntError` operand-mismatch dump), so the port avoids `==`
  on Option entirely and uses a hand-written `cmp`.

---

## missing-feature

### 5. In-file `mod { .. }` blocks do not parse
- Upstream: all of `modules/*` (29 run-pass), `privacy/{privacy-ns, mod-pub-access,
  pub-extern-privacy, privacy1-rpass}`, `structs-enums/{enum-export-inheritance,
  tag-exports}`, `self/self-shadowing-import`.
- Trigger: `mod foo { pub fn x() -> i64 { .. } }` → `syntax error near 'mod'`.
  Logos organizes namespaces via separate package files, not in-file `mod`
  blocks, so the whole `modules/` category and the module-dependent `privacy`
  tests are out of scope for the single-file import harness.

### 6. Shift expression in enum discriminant position does not parse
- Upstream: `structs-enums/tag-variant-disr-val.rs` (`purple = 1 << 1`,
  `orange = 8 >> 1`).
- Trigger: `enum Color { Purple = 1 << 1 }` → `syntax error near 'enum'`. Hex,
  decimal and negative literal discriminants parse fine; only a `<<`/`>>`
  (and presumably other binary-op) discriminant initializer breaks the parse.
  The port drops the two shift variants.

### 7. Array-repeat length from a non-literal const expression does not parse
- Upstream: `structs-enums/enum-vec-initializer.rs` (`[0; Flopsy::Bunny as usize]`,
  `[0; BAR]`).
- Trigger: `let v = [0i64; Flopsy::Bunny as u64];` → `syntax error near 'Flopsy'`.
  A const-valued array length that is not a plain integer literal is not
  accepted as the repeat count. This is effectively const-eval-in-type-position
  (an intentional Divergence — Logos has no const-eval; metacall is the channel).
  Test skipped (its whole point is the const-sized array).

### 8. Method-call turbofish (`x.method::<T>()`) — re-confirm of B110 #5
- Upstream: `where-clauses/where-clause-method-substituion-rpass.rs`
  (`1.method::<S>()`).
- Trigger: unchanged from B110 — the method-level turbofish does not parse.
  Test skipped (its point is the method-generic + where-clause substitution).

### 9. `where F: FnMut(..)` generic fn-parameter — re-confirm of B110 #6
- Upstream: most of `unboxed-closures/*` (by-ref, single-word-env, manual-impl,
  monomorphization, prelude, …), `where-clauses/where-clauses-unboxed-closures.rs`,
  `statics/static-impl.rs`.
- Trigger: `fn call_fn<F: Fn()>(f: F) { f() }` + `mut f: F` params do not parse /
  resolve; combined with `Box<dyn Fn>` returns these are out of scope. Only the
  direct-closure-call `unboxed-closures-simple` landed.

### 10. `static` / `static mut` item declarations do not parse
- Upstream: most of `statics/*` (static-function-pointer, small-enum-range-edge's
  `static CLu: Eu`, where-for-self's `static mut COUNT`, …).
- Trigger: `static F: fn(i64) -> i64 = f;` → `syntax error near 'static'`. No
  module-level mutable/immutable `static` items. (`const` items work and are used
  in consts-in-patterns.) The statics category is largely blocked on this.

### 11. Type-alias name not accepted in a struct pattern
- Upstream: `structs-enums/struct-aliases.rs` (`match s { S2 { .. } => .. }`).
- Trigger: with `type S2 = Srec`, `match s { S2 { x, y } => .. }` →
  `struct pattern: 'S2' != scrutinee 'Srec'`. The alias works as a type
  annotation and in a struct *literal*, but a pattern must spell the base name.
  The port uses `Srec` in the pattern; the generic-alias half
  (`type S4<U> = S3<U, char>` + `size_of_val`) is dropped.

### 12. Tuple-struct constructor as a first-class function value
- Upstream: `structs-enums/tuple-struct-constructor-pointer.rs`
  (`let f: fn(isize) -> Foo = Foo;`).
- Trigger: `let f: fn(i64) -> Foo = Foo;` → `undefined variable 'Foo'`. A
  struct/tuple-struct name is not usable as a constructor function value. Test
  skipped.

### 13. Forward-reference to a not-yet-declared method within the same impl
- Upstream: `regions/regions-scope-chain-example.rs` (and a latent risk in any
  impl that calls a sibling method declared later).
- Trigger: `impl C { fn foo(&self) { self.take_scope(..) } fn take_scope(..) {} }`
  → `'C' has no method 'take_scope'` at the call site. Method resolution within
  an impl appears order-dependent (a method must be declared before it is called
  from another method in the same impl). This test is typecheck-only with an
  empty `main`; not ported (low value to reorder). NOTE: `private-method-rpass`
  works because `play` is declared *before* `nap` — reordering is the fix when a
  test is worth it.

### 14. Nested patterns inside enum-variant payloads — re-confirm of B109
- Upstream: `structs-enums/{record-pat, issue-38002}.rs`.
- Trigger: `Foo::B(42u64, Bar::C)` and `t1::a(m)` inside an outer pattern →
  "nested patterns inside enum-variant payloads are not yet supported; bind to a
  name and match in the body". issue-38002 (an array-of-tuples-of-enums match)
  skipped; record-pat skipped (and additionally blocked by compiler-bug #2).

### 15. Single-field variant rest pattern `Variant(..)` does not parse — re-confirm of B109
- Upstream: `structs-enums/issue-1701.rs` (`animal::cat(..)`).
- Trigger: `Animal::Cat(..)` → `syntax error near '('`. Worked around with
  explicit `Animal::Cat(_)` / `Animal::Rabbit(_, _)` (semantically equivalent).
  issue-1701 landed with this adaptation.

---

## intentional-divergence / out-of-scope skips

- `consts/consts-in-patterns.rs` ZST/`mem::transmute` const-pointer arms and the
  `const fn foo()` row — dropped (no transmute/ZST-ref; `const fn` is a
  Divergence, metacall is the comptime channel). The integer-const match arms
  landed.
- `consts/const-block-non-item-statement-rpass.rs` — the point is a block-expr
  discriminant `Bar = { let x = 1; 3 }`; reducing it to `Bar = 3` removes the
  point, so skipped (block-expr in discriminant position not pursued).
- `structs-enums/{multiple-reprs, type-sizes, small-enums-with-fields,
  align-*, tag-align-*}.rs` — `size_of`/`align_of` layout assertions; no
  `size_of`/`align_of` on arbitrary types. Skipped.
- `structs-enums/ivec-tag.rs` — `std::sync::mpsc` channels + threads. Skipped.
- `structs-enums/{class-poly-methods, class-impl-very-parameterized-trait}.rs` —
  method-level generics over `Vec<T>` payloads + `Option<&T>`/`panic!` map
  methods; too much surface for a clean port. class-typarams (the simpler
  generic-struct + non-generic-method sibling) landed instead.
- `self/{arbitrary_self_types_*, self-impl, self-impl-2}.rs` — arbitrary self
  types / `Rc`/`Box` receivers / associated types / `Self::Assoc` paths.
  Out of scope.
- `self/self-re-assign.rs` — `x = x` self-assignment of `Box`/`Rc`; needs
  Box/Rc. Skipped.
- `self/self-in-mut-slot-immediate-value.rs` — relies on `Copy` (reads `x` after
  `x.squared()` consumes it); Logos structs move. Skipped.
- `derives/*` (deriving-in-fn, derive-type-with-reference, …) — Logos derive is
  `#[derive_<trait>]` + a `#[metaprog_handler]` in scope, not Rust
  `#[derive(Debug, ..)]`; `#[derive(Debug)]` + `format!("{:?}")` is non-trivial
  to set up per-file. Skipped this batch.
- `unboxed-closures/{manual-impl, prelude, boxed, …}.rs` — `extern "rust-call"`
  Fn-trait impls / `Box<dyn Fn>` / `where F: FnMut` (see #9). Skipped.
