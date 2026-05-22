# B117 — UI run-pass import (methods + coercion): surfaced gaps

Batch B117 imported 34 run-pass tests from `tests/ui/methods/` and
`tests/ui/coercion/` (rustc commit `4b0c9d76ae7d387229caea55cfa73c280b08b8a7`),
all under `tests/imported/pass/{methods,coercion,autoref-autoderef}/`
(suffixes `-m` / `-co`). All 34 compile, link, and exit 0.

This note records ONLY the **newly surfaced** gaps (not already in the
known-open list given for the batch, and not already tracked under
B114/B115/B116). Each is classified §A (blessed divergence) vs §B
(catch-up TODO). **All B117 gaps are §B catch-up — no new §A.**

## G1 — `&Primitive as &dyn Trait` does not coerce
- **Symptom**: `let a: &dyn Bar = &five;` where `five: u8` and `impl Bar for u8`
  → `let 'a': type mismatch — expected &dyn Bar, got &u8`. Same at argument
  position (`use_bar(&five)` → `arg 1: expected &dyn Bar, got &u8`).
- **Contrast**: `&Struct → &dyn Trait` works at BOTH let-binding and arg
  position (verified; cf. existing coerce-ref-trait-object + the reworked
  trait-object-coercion-distribution-9951-co which uses a 1-field struct).
- **Needed feature**: trait-object (fat-ptr/vtable) construction for an impl on
  a NON-struct receiver type (primitive scalars; presumably also enums/tuples).
  Today the unsize-to-`&dyn` path is keyed to struct receivers only.
- **Classification**: §B catch-up (Rust coerces `&u8 → &dyn Bar` fine).
- **Workaround used**: 1-field struct wrapper (`struct N { v: u8 }`), keeping the
  test's actual point (redistribution of a `&dyn` through further bindings).

## G2 — deref-coercion at ARGUMENT position
- **Symptom**: with `impl Deref<Target> for Newtype`, calling
  `use_ref(&nt)` where `use_ref(t: &Target)` and `nt: Newtype`
  → `call to 'use_ref' arg 1: expected &Target, got &Newtype`.
- **Contrast**: the SAME user `Deref` impl drives method-receiver autoderef
  correctly (`nt.target_method()` works — see existing
  deref-newtype-method-call). So deref-coercion is wired for the
  method-receiver path but NOT for a plain `&Target` function argument.
- **Needed feature**: apply user-`Deref` coercion at call-argument coercion
  sites (and, by extension, let/return coercion sites), not only at method
  receiver resolution.
- **Classification**: §B catch-up (Rust RFC 241 deref coercions apply at any
  coercion site).
- **Disposition**: candidate test (coerce-deref-arg) deleted; not imported.

## G3 — `where TypeExpr: Trait` (bound on a constructed type) unparsed
- **Symptom**: `fn check<T>(x: Option<T>) -> i32 where Option<T>: Foo { … }`
  → `syntax error near 'fn'`.
- **Note**: this is the SAME gap the existing `pass/methods/method-where-clause.logos`
  header already documents (it dropped the generic indirection). Re-confirmed
  still open at the B117 SHA. Recorded here for the catalog tail; the
  method-on-concrete-option-m import keeps only the concrete-instantiation
  dispatch (`impl Foo for Option<i32>` called on `Some`/`None`).
- **Needed feature**: grammar + sema for a where-clause predicate whose
  subject is a type EXPRESSION (`Option<T>`, `Vec<T>`, `(A,B)`, …) rather than
  a bare type parameter.
- **Classification**: §B catch-up.

## G4 — never-type coercion (diverging branch does not unify)
- **Symptom**: `let v: i64 = if b { 42i64 } else { return -1i64; };`
  → `if-expression branches have incompatible types: i64 vs void`.
- **Needed feature**: a diverging expression (`return`, and by extension
  `panic!`/`loop {}`/`break`) should coerce to ANY type so the other branch's
  type wins the if/else (and match arm, and array element) unification. Today
  the diverging arm is typed `void` and clashes.
- **Classification**: §B catch-up (Rust `!` never-type coercion).
- **Disposition**: candidate test (coerce-never-to-any) deleted; not imported.

## G5 — inherent-vs-(single)-trait same-name method collision
- **Symptom**: `impl Foo { fn tag(self:&Foo)->i64 }` together with
  `impl Bar for Foo { fn tag(self:&Foo)->i64 }`
  → `duplicate function 'Foo__tag'` (both mangle to the same symbol).
- **Note**: the trait-aware method-mangling work (per memory) re-keys methods
  ONLY when ≥2 *traits* declare the same name on a type; an inherent method
  colliding with a single trait method is not covered, so the inherent-shadows-
  trait priority case can't be exercised.
- **Needed feature**: extend collision detection / lazy-qualified mangling to
  the inherent-vs-trait case (and resolve `value.tag()` to the inherent one,
  matching Rust's inherent-wins lookup order).
- **Classification**: §B catch-up.
- **Disposition**: candidate test (inherent-vs-trait-priority) deleted; not imported.

## G6 — trait-qualified UFCS `Trait::method(recv)`
- **Symptom**: `Speak::say(&d)` (trait-qualified UFCS)
  → `call to undefined static method 'Speak::say'`.
- **Contrast**: TYPE-qualified UFCS works (`Type::method(recv)` — exercised by
  the existing ufcs-explicit-method-call.logos and the new
  method-self-arg-ufcs-m).
- **Needed feature**: resolve a trait-name-qualified static call by selecting
  the impl from the receiver's concrete type (Rust's `Trait::method(x)` /
  `<T as Trait>::method(x)` UFCS form). Likely overlaps the trait-method
  mangling / `<T as Trait>::Assoc` qualified-path work (RB115-G1).
- **Classification**: §B catch-up.

---

### Skipped candidates (feature/surface, NOT new gaps)
- coerce-overloaded-autoderef — `&&&&&`/`&mut &&&` deref chains + `Rc` (Rc = other gap).
- coerce-unsize-subtype, issue-26905-rpass, no_local_for_coerced_const — `CoerceUnsized`/`Unsize` custom-DST (B2/B3).
- method-projection — projection-type receiver codegen SIGSEGV (RB115-G3, already tracked).
- method-lookup-order — 31-permutation cfg/macro matrix (§A macros).
- supertrait-shadowing/* — unstable `supertrait_item_shadowing` feature.
- method-normalize-bounds-issue-20604 — assoc-projection winnowing in method resolution (RB115-G3 area).
- trait-object-arrays-11205, coerce-trait-object-removes-send-bound — `Box<[dyn]>`/`Arc<dyn>` + `Send`-bound erasure (B3).
- issue-39823 — aux-build cross-crate.
