# B159 — rustc UI run-pass import: surfaced gaps

Batch B159 imported **30 NEW DISTINCT run-pass tests** from `tests/ui/`, mined
for FEATURE COVERAGE across FRESH / lightly-mined areas:
binop (3), generics (4), structs-enums (5), traits (3), regions (3),
overloaded (1), methods (2), moves (1), privacy (2), cast (1), match (1),
binding (1), nll (1), ufcs (1), borrowck (1).
(`binop`, `regions`, `privacy`, `ufcs`, `structs-enums`, `moves` are
under-mined areas distinct from B158's unboxed-closures/coercion/traits/self.)

Workflow matches B149–B158: faithful ports, `pub fn main()` → `fn main() -> i32
{ …; return 0i32; }`, isize/usize → i64/u64, integer/float literals suffixed
(float literals written with a decimal point — `2.0f64`), `assert!`/`assert_eq!`
→ early-return sentinels (distinct nonzero codes), println!/derive/Box/Rc/
RefCell/Vec/PhantomData/named-lifetimes/`#[repr]` dropped or reshaped where
incidental, nested fns/type decls hoisted, `&self`/`&mut self` →
`self: &Self` / `self: &mut Self`, by-value `self` → `self: Self`. Drop modeled
where needed via a `*mut i64` counter threaded through fns (no module statics —
G153-3/G158-3). All 30 compile + link + exit 0 against the as-is
`build/bin/logosc` (no compiler changes). Link line uses `-Wl,--gc-sections`
(as for B149–B158).

Coverage highlights: operator overloading via user traits (Add/Sub/Neg/Not/
Index<bool>) on a 2-field struct (operator-overloading); bool ordering + bool
bitwise corner cases + struct field mutation (binops-corner-cases); compound
assignment across widths/types `+= -= *= /= %= |= &= ^= <<= >>=` over
i8/i16/f32/f64/i64/u8/u16/u32/u64 (compound-assign-by-ref); a generic fn taking
a homogeneous 3-tuple destructured by let-pattern (generic-tup); a generic fn
`apply<T>(produce: fn()->T, consume: fn(T,…))` driving bare fn-pointer args
(generic-temporary); a generic struct built inside a generic fn via turbofish
(generic-exterior-unique); a generic enum with a two-field tuple variant built
via turbofish variant constructors + match (generic-recursive-tag); functional
struct update `A { f: …, ..x }` mixing overridden + carried-over fields
(fsu-field-sensitivity); `as`-cast value semantics — C-like enum discriminants
(implicit/explicit/signed/hex) to int widths, bool/char to ints, u8↔char
(cast-enum-and-char); char inclusive-range patterns `'a'..='b'` with a `if false`
guard skipping an overlapping arm (match-char-range-guard); `match` as an
expression in let-init / reassign / match-head / block-result positions
(expr-match); a match guard that borrows the scrutinee immutably while taking a
`&mut` of a local in the same guard call (match-guard-mutable-borrow); a
recursive blanket impl `impl<T: Foo> Foo for &T` that autoderefs and
re-dispatches (method-recursive-blanket-impl); selecting between two trait impls
by the inferred element type of a generic wrapper (method-two-trait-defer-
resolution); a conditional move (re-bind from a moved local vs a fresh literal)
returning the survivor's field (move-3-conditional); overloaded indexing through
a nested struct field `f.foo[1]` then a by-value-self method on the result
(overloaded-index-in-field); a pub inherent method reading a private field
(private-class-field); a pub method calling a private one on `self: &mut Self`
across two impl blocks (private-method); borrowing a fixed-size array as a
`&[i64]` slice and indexing (regions-borrow-evec-fixed); returning a reference to
a struct field `&p.x` (regions-infer-borrow-scope); an enum variant holding a
reference payload `Ctor(&i64)` matched to extract + deref (regions-self-in-
enums); a generic struct with a method-level-generic method mutating a field
(class-poly-methods); `match` as an expression producing struct + enum values
(expr-match-struct); a tuple type alias `Rect = (Point, Point)` of two Copy
structs destructured (rec-tup); struct field-init shorthand mixed/trailing-comma/
out-of-order (struct-field-shorthand); a C-like enum with explicit (hex/negative)
discriminants compared via a user equality trait + match + if-chain
(tag-variant-disr-val); a blanket `impl<T: MyCopy> Get for T` driven through a
generic `get_it<T: Get>` over primitives (conditional-dispatch); a trait impl on
a primitive whose default method calls the required method on self (default-
method-on-primitive); a trait static method `new()->Self` with two impls each
selected by the let-annotation type (static-method-overwriting, exercises
G158-9); UFCS polymorphic paths — a free fn item as an fn-pointer value, a
type-qualified instance method `Type::method(&recv)`, a static method
(ufcs-polymorphic-paths).

## Gaps surfaced

- **G159-1** ✅ CLOSED (2026-05-23) — a blanket `impl<T: MyCopy> Get for T`
  instantiated at a GENERIC-ENUM receiver (`Option<u16>`) now monomorphizes the
  blanket method. Root: the EAGER blanket-instantiation pass (mono.cpp) only
  emits `<Concrete>__<method>` for NON-generic candidate types — a generic
  instance like `Option<u16>` reaching the blanket via a call site got no
  `Option__u16__get` (→ mlir-gen "does not reference a valid function"). Fix
  (mono_clone.cpp, the TypeVar-receiver→concrete retargeting block): when the
  resolved template key is still the unresolved bare `<cname>__<method>` and the
  (peeled) receiver is a generic struct/enum, search blanket_impls_ for a
  blanket whose `$blanket$<trait>$<bound>$<tv>__<method>` template exists and
  whose bound the receiver satisfies (mono_concrete_satisfies_bound), then clone
  it with {tv → receiver type} and enqueue `<cname>__<method>`. The worklist
  drain re-enters this same hook for any blanket method the cloned body calls
  (`self.copy()` → `Option__u16__copy` via the Option MyCopy blanket), so the
  whole chain resolves from one hook. Restored the Option arm in
  `conditional-dispatch-b159` (Some(7u16) round-trips). Original repro:
  ```
  trait Get { fn get(self: &Self) -> Self; }
  trait MyCopy { fn copy(self: &Self) -> Self; }
  impl MyCopy for u16 { fn copy(self: &Self) -> u16 { return *self; } }
  impl<T: Copy> MyCopy for Option<T> { fn copy(self: &Self) -> Option<T> { return *self; } }
  impl<T: MyCopy> Get for T { fn get(self: &Self) -> T { return self.copy(); } }
  fn get_it<T: Get>(t: &T) -> T { return (*t).get(); }
  fn main() -> i32 { let s = Some(7u16); let _ = get_it(&s); return 0i32; }
  ```
  Original test `traits/conditional-dispatch.rs`; the Option arm was DROPPED and
  the kept `conditional-dispatch-b159` covers the primitive arms only.
  Assessment: TRACTABLE-ish mono gap (blanket-method-over-generic-enum
  instantiation enqueue), in the same family as prior blanket-impl mono fixes.

- **G159-2** ✅ CLOSED (2026-05-23) — TRAIT-qualified UFCS instance-method call
  `Trait::method(&recv)` now resolves for a PRIMITIVE receiver too. There was
  already a trait-qualified-UFCS handler in `lower_static_call` (maps
  `Trait::method(recv,…)` → `<recv-type>__<method>` from the first arg's type),
  but it only recognised Struct/ZonedStruct/Enum receivers; a primitive
  receiver (`Doubler::dbl(&n)`, n: i64) left `rname` empty → "call to undefined
  static method 'Doubler::dbl'". Added a primitive-scalar arm
  (int/float/bool/char widths) keying on `type_str(rt)` (`i64__dbl`). Verified
  struct + primitive + multi-arg forms. Restored the trait-qualified instance
  call to `ufcs-polymorphic-paths-b159`.

- **G159-3** ✅ CLOSED (2026-05-23) — EXPRESSION-valued enum discriminants now
  parse + evaluate: `enum Color { Purple = 1 << 1, Orange = 8 >> 1 }`. Grammar:
  the bare-literal discriminant alts (`= INTEGER` / `= -INTEGER`) gained a
  negative lookahead `!(SHL/SHR/PIPE/AMP/CARET/PLUS/MINUS/STAR/SLASH/PERCENT)`
  so a literal followed by a binary operator falls through to a new `IDENT
  ASSIGN expr` alt (BODY = expr node). Sema (sema_collect discriminant lowering)
  evaluates that BODY via the EXISTING CTFE channel `ctfe::eval_expr` — the same
  one metacall discriminants use, so no separate const-eval engine (consistent
  with the no-const-eval policy). Verified `<<`/`>>`/`|`/`&`/`-`/hex const-exprs
  compute correctly. Restored the `1 << 1` / `8 >> 1` discriminants in
  `tag-variant-disr-val-b159`.

## Other observations (NOT counted as new gaps)

- **By-value self-recursive enum payload** — `enum List<T> { Cons(T, List<T>),
  Nil }` is rejected with a clean diagnostic: `infinite-size enum 'List'
  (variant payload contains itself by value); box the payload with
  '*const List'`. This matches Rust (which requires `Box`/indirection too); the
  diagnostic even suggests the fix. `generic-recursive-tag-b159` was reshaped to
  a non-recursive generic enum. Not a gap — correct behavior.

- **`let x: &T;` then assign requires `let mut x`** — a declare-then-assign local
  whose value flows in from a match arm (`let mut z: &i64; match … { … => { z =
  zz; } }`) must be declared `mut`, else `assignment to immutable variable`.
  Worked around with `let mut` in `regions-self-in-enums-b159`. Plausibly a
  divergence from Rust's deferred-init `let z;` (Rust allows single-assignment of
  a non-`mut` deferred local); noted, not blocking. Left as a `let mut`.

## Dropped tests (and why)

- (Option arm of) `traits/conditional-dispatch.rs` — G159-1 (blanket-method-over-
  generic-enum mono failure). Kept test covers the primitive arms.
- (trait-qualified instance form of) `ufcs/ufcs-polymorphic-paths.rs` — G159-2
  (`Trait::method(&recv)` unresolved). Kept test uses the type-qualified form.
- (expression discriminants of) `structs-enums/tag-variant-disr-val.rs` — G159-3
  (`1 << 1` discriminant parse error). Kept test uses literal discriminants.

No tests were dropped WHOLESALE — every surfaced gap was a single facet of an
otherwise-portable test, so each test was kept with the unsupported facet
reshaped and the gap recorded above.
