# B121 — UI-surfaced gaps (tests/ui/traits run-pass)

Source: `tests/ui/traits/` `//@ run-pass` (corpus ~1168, only ~87 imported —
distilled to DISTINCT trait features not already covered by existing
`tests/imported/pass/traits/`). All gaps below are **§B catch-up TODOs** — no
new §A blessed divergences. Pinned commit
`4b0c9d76ae7d387229caea55cfa73c280b08b8a7`. Suffix `-tr2`.

31 tests imported (all compile + link + exit 0).

## NEW gaps (precise, all §B)

### G121-1 — associated const projection through a generic bound / `Self` fails
- **Symptom:** clean compile error
  `error [fn <f>]: unknown enum 'T'`  (for `T::ID`), and inside a default method
  `error [fn <Impl>__id]: unknown enum 'Self'`  (for `Self::ID`).
- **Repro (minimal):**
  ```
  trait HasId { const ID: i64; }
  struct Alpha {}
  impl HasId for Alpha { const ID: i64 = 1i64; }
  fn read_id<T: HasId>() -> i64 { return T::ID; }   // error: unknown enum 'T'
  // and: trait default `fn id(&self)->i64 { return Self::ID; }` -> unknown enum 'Self'
  ```
- **Discriminator:** the CONCRETE-receiver read works:
  `Alpha::ID == 1i64`  OK. Only the type-param / `Self` projection of an
  associated const is unresolved (the path resolver treats the leading
  type-param/`Self` segment as an enum name).
- **Feature:** associated-const projection `<T as Trait>::CONST` / `T::CONST` /
  `Self::CONST` through a generic bound.
- **Classification:** §B (Rust resolves these). `assoc-const-in-trait-tr2`
  imported with the concrete-receiver form only (the supported subset);
  the projection arm is the gap. Recurs with the assoc-const feature group.
- **Note:** assoc-TYPE projection `Self::Output` as a method *return type* DOES
  work (see `assoc-type-method-tr2`) — the gap is specific to assoc-CONST value
  reads through a type-param/`Self` path.

### G121-2 — `impl Trait for fn(...) -> ...` (trait impl on a function-pointer type) is a syntax error
- **Symptom:** clean parse error
  `syntax error near 'impl' at line N col 1`.
- **Repro (minimal):**
  ```
  trait MyTrait { fn foo(self: &Self) -> i64; }
  impl MyTrait for fn(i64, i64) -> i64 { fn foo(self: &fn(i64,i64)->i64)->i64 { return 99i64; } }
  ```
- **Feature:** implementing a user trait for a function-pointer TYPE as the impl
  target (rustc `impl<A,B,C> MyTrait for fn(A,B)->C`). The grammar's impl-target
  type production does not accept a `fn(...)->...` type.
- **Classification:** §B (Rust parity — fn-ptr types are valid impl targets).
  Source `tests/ui/traits/fn-type-trait-impl-15444.rs` left UNIMPORTED.

### G121-3 — `match self` where `self: &Enum` (match over an enum reference without explicit deref) miscompiles in mlir-gen
- **Symptom:** mlir-gen verifier error
  `'arith.cmpi' op operand #0 must be signless-integer-like, but got '!llvm.ptr'`
  (the discriminant compare receives the reference pointer instead of the
  loaded discriminant).
- **Repro (minimal):**
  ```
  enum Light { Red, Green, Yellow }
  impl Cost for Light {
      fn cost(self: &Light) -> i64 {
          match self { Light::Red => {...} ... }   // ptr-vs-int cmpi error
      }
  }
  ```
- **Discriminator:** the EXPLICIT-deref form works:
  `match *self { Light::Red => ... }`  OK. So enum-pattern matching over a
  `&Enum` scrutinee is missing the auto-deref that `&Struct` field-access enjoys
  (cf. the enum two-level convention — `&Enum` is ptr-to-slot-holding-ptr).
- **Feature:** auto-deref of a `&Enum` scrutinee in a `match` against
  enum-variant patterns.
- **Classification:** §B (Rust matches `&Enum` against non-ref variant patterns
  via match-ergonomics / default-binding-modes). `enum-impls-trait-tr2` imported
  with the explicit `match *self` form (the supported subset).

## WORKING (verified this batch — distinct features now covered)
- Multi-subtrait bound `T: B + C` with common supertrait A (diamond reach).
- Three-level inheritance chain `Baz: Bar: Foo`, grand-supertrait method via `T: Baz`.
- Default method calling sibling required methods that chain into the default (mutual recursion meow→scratch→purr).
- Blanket impl `impl<T: Foo> Quux for T {}` exposing the supertrait method on a `T: Quux` bound.
- Blanket over THREE supertraits `impl<T: Foo+Bar+Baz> Quux for T {}`.
- Generic-bound dispatch over a value param with two distinct primitive impls (u32 / f32).
- Generic trait `A<T>` default method substitution under nested bound `f<T, V: A<T>>` + turbofish `f::<f64,i64>`.
- `(&a) as &dyn Foo` explicit trait-object cast then dynamic dispatch.
- Inherent method shadowing a CONDITIONAL blanket impl `impl<T: Clone> Foo for T` when receiver isn't `Clone`.
- Associated const read via concrete receiver `Alpha::ID`.
- Generic trait METHOD `fn pick<U>(&self, ...)` (method-level type param, monomorphised per call).
- `where T: Speak` where-clause bound (vs inline bound).
- Unary `Neg` operator-trait impl for a user struct (`-p`).
- Method-less marker trait used as a generic bound (`T: Marked + Val`).
- Generic impl of a trait for a generic wrapper `impl<A: Clone> Repeat<A> for Holder<A>`.
- Associated TYPE as method return type `fn produce(&self) -> Self::Output`, two distinct bindings.
- Two-type-param trait `Pair<T,U>` returning a tuple `(T,U)`.
- `&dyn Tr` with a MULTI-method vtable (legs + tail).
- Default method overridden in one impl, kept in another.
- Two distinct type params each with its own bound `combine<A: Val, B: Val>`.
- Trait method returning `Self` by value, chained.
- Generic struct with a bound-carrying field `Box1<T: Val>` + method delegating to field method.
- Enum implementing a trait, matching its own variants (via `match *self`).
- One struct implementing TWO unrelated traits, both reachable via `T: Width + Height`.
- Trait associated FUNCTION with no `self` (`S::make()`).
- Default method taking an argument combined with a required-method result.
- Generic fn calling a trait associated fn through the bound `T::from_int(i)`.
- Trait-object reference stored in a struct field `Holder { item: &dyn A }`, dispatched.
- UFCS trait-method call `Speak::say(&d)`.
- Conditional/recursive impl `impl<T: Val> Val for Wrap<T>` (nested wrapper recursion).
- `&dyn Tr` calling a vtable DEFAULT method (`describe` -> dyn `kind`).

## G121-3 follow-up (2026-05-22 investigation)
`match self` where `self: &Enum` → cmpi-on-ptr. Root: TWO interacting issues —
(a) `resolve_tagged_enum("E")` returns nil in the method's codegen context
(mono didn't register E as a tagged-enum there; even `match *self` has te_info=nil
but works via the fallback disc-dispatch), and (b) a `&E` PARAMETER is ONE
pointer level (the enum heap-ptr directly), whereas `&local_enum` is TWO levels
(ptr-to-slot-holding-ptr) — so the `via_ref` deref in gen_match (gated on
te_info) is calibrated for locals, not params. An unconditional via_ref deref
over-derefs the param → still cmpi-on-ptr. Proper fix needs param-vs-local enum
ref-level reconciliation + te_info availability. Reverted the attempt.
Workaround in imports: `match *self`. Delicate (enum pointer-level convention) —
focused follow-up. See [[ref_enum_two_level_convention]].
