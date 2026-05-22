# B118 — surfaced gaps (tests/ui/structs-enums + tests/ui/generics run-pass import)

Upstream pin: `4b0c9d76ae7d387229caea55cfa73c280b08b8a7`. 20 NEW passing tests imported
(14 `generics/` `-g2`, 6 `structs-enums/` `-se`). All gaps below are §B catch-up
(no §A const-fn/macro/async involved). They are reported here because they are NOT
in the KNOWN-OPEN list given for this batch.

## NEW surfaced gaps (not previously catalogued for this area)

### G118-1 — functional-struct-update (`..base`) on a GENERIC struct
- **Symptom**: `let c: Point<i64> = Point { x: 10i64, ..origin };` →
  `struct literal 'Point': field 'y' not initialized` / `field 'z' not initialized`.
- **Repro**: `struct P<T>{x:T,y:T}` ; `P{x:4i64, ..a}` on `a: P<i64>`.
- **Contrast**: the SAME FRU works on a NON-generic struct (the imported
  `functional-struct-upd.logos` passes). So FRU rest-field propagation is wired only
  for non-generic struct literals; for a generic-struct literal the `..base` rest does
  not enumerate the remaining (substituted) fields.
- **Needed feature**: in struct-literal lowering, when the struct is a generic
  instantiation, expand the `..base` rest against the *substituted* field list.
- **Classification**: §B catch-up. Blocked the `generic-struct-update-se` candidate
  (deleted).

### G118-2 — empty type-parameter list `<>` entirely unparsed
- **Symptom**: `syntax error near 'fn'/'struct'/'<'`.
- **Repro**: `fn foo<>(){}`, `struct S<>{}`, `bar::<>()` (turbofish on a non-generic fn) —
  all rejected by the parser.
- **Contrast**: Rust treats `<>` as equivalent to omitting type params everywhere
  (decl + turbofish).
- **Needed feature**: grammar — accept an empty `<>` angle-bracket list in generic-param
  decls and in turbofish call/path positions (lower to "no type args").
- **Classification**: §B catch-up. Blocked `empty-generic-brackets-equiv-g2` (deleted).

### G118-3 — default-method result used DIRECTLY in an expression context miscompiles
- **Symptom**: a trait DEFAULT method `get_twice(&self){ self.get() }` (calling the
  trait's REQUIRED method on `self`) returns garbage when its call is used directly as
  an operand (`if c.get_twice() == 13 {…}`) — non-generic trait returns the *wrong* int;
  generic trait `Container<T>` returns garbage / SIGSEGVs (value 139 instead of 13).
- **Repro (generic-trait, garbage)**:
  ```
  trait Container<T> { fn get(self:&Self)->T; fn get_twice(self:&Self)->T { return self.get(); } }
  impl Container<i64> for Cell { fn get(self:&Cell)->i64 { return (*self).x; } }
  // let b = c.get_twice();  → garbage (139), even let-bound, for the GENERIC trait
  ```
- **Contrast**:
  - NON-generic trait: `let b = c.get_twice();` (let-bound, separate `if`) is CORRECT;
    only `if c.get_twice() == 13` (call as a direct operand) is wrong.
  - GENERIC trait `Container<T>`: wrong even let-bound → overlaps the known
    `generic_trait_method` mono emission gap (MEMORY: prelude-default-on R3 residual,
    `generic_trait_method`/`generic_method_infer_struct func.call invalid`).
- **Needed feature**: (a) the generic-trait default-method-calling-required-method
  emission path (mono `func.call` for the required method through `self` in a default
  body); (b) for the non-generic trait, the temporary holding a default-method result
  used directly as an operand is being read at the wrong slot.
- **Classification**: §B catch-up. Blocked `generic-trait-default-method-g2` (deleted).

## Already-known gaps re-confirmed (NOT re-reported; on the KNOWN-OPEN list)

- **Nested patterns in enum-variant payloads / refutable struct-field sub-pattern**:
  `record-pat` shape `T3::C(T2{x: t1::a(m), ..}, _)` →
  `refutable inner pattern not yet supported in struct-shape variant patterns`
  + `undefined variable` for names declared only in the inner sub-pattern. Blocked
  `record-pat-flat-se` (deleted; the inner-bind workaround still hit the field-rest
  refutability path).

## Notes on candidate exhaustion

The `tests/ui/structs-enums` and `tests/ui/generics` run-pass corpora are heavily mined
by B110–B117 (most filenames already exist under `pass/{generics,structs-enums,struct,enum}`).
Remaining unimported upstream files are dominated by:
`#[repr]`/`align`/`size_of`/`align_of` layout assertions (§A — Logos does not expose
`mem::size_of`/`align_of` as a parity surface), `macro_rules!`/`#[derive(Debug)]` driver
shells (§A), `Box`-heap + `as Box<dyn …>` unsizing (B3 custom-DST), `mod{}` items
(unparsed, B-known), `thread`/`channel` (runtime), and `<P as Deref>::Target`/default-type-
params (`<A = …>`) qualified-path / default-generic gaps already catalogued in B111/B115.
This batch therefore DISTILLS distinct *features* from those files (generic-struct method
spreads, generic newtype destructure, generic enums with 1/2 type params, generic fn-ptr
fields, method-generic inference, multi-bound where-clauses, impl-level where-clauses,
nested generic structs, phantom marker params, recursive generic cons-lists, generic
trait impls for generic structs) rather than copying whole layout/macro-driven files.

## Skipped (feature/surface, not new gaps)
- `align-struct` / `enum-discrim-autosizing` / `multiple-reprs` / `type-sizes` / `issue-32498`
  — `#[repr]`/`size_of`/`align_of` layout assertions (§A; no parity surface).
- `small-enums-with-fields` / `generic-derived-type` / `generic-tup` — `format!`/`{:?}` Debug
  + `macro_rules!` driver (§A); the generic-enum payload feature is captured distilled in
  `generic-enum-two-params-se`.
- `empty-struct-braces` / `enum-export-inheritance` / `generic-fn-twice` — `mod{}` items
  + extern-crate aux-build (unparsed / cross-crate).
- `ivec-tag` / `resource-in-struct` — `thread`/`channel` (runtime) / Drop-in-enum (known
  Drop-glue gap, B110).
- `tuple-struct-constructor-pointer` — tuple-struct ctor as a fn value (known gap, B111).
- `generic-object` — `as Box<dyn Foo<T>>` unsize cast (B3); the generic-trait-impl-of-
  generic-struct dispatch is captured distilled in `generic-trait-impl-for-generic-struct-g2`.
- `generic-static-methods` — generic STATIC trait method with `where F: FnMut` via UFCS →
  mono `unresolved TypeVar 'F'` + `func.call incorrect number of operands` (overlaps the
  `generic_static_methods` / method-generic-on-trait mono area; not isolated cleanly enough
  to be a crisp new repro — left for the existing method-generic mono baghunt).
- `generic-default-type-params` / `default-type-params-well-formedness` — default generic
  params `<A = T>` unparsed (known, B111).
- `generic-associated-type-deref-target-56237` — `<P as Deref>::Target` qualified path
  (known, B115-G1).
