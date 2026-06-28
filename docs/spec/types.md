# Types, Layout, and Coercion

Scope: type identity & formation, memory layout/ABI, and implicit coercions in Logos. Rules below are mechanically extracted from the compiler source layers (grammar, sema, mono) and addressed by stable `id`. Domains covered: `type`, `layout`, `coerce`.

> **ID collision flagged:** the following id(s) are emitted by more than one artifact with differing evidence/wording. All occurrences are preserved verbatim below and disambiguated with a parenthetical occurrence marker; resolve upstream by splitting the id: `coerce.method-recv.auto-ref`.

## Type Identity

### `type.identity.array-size` — Array identity includes the length

Array `[T; N]` identity = (length N, symbolic-length variable name, element T). Arrays with different lengths (concrete or symbolic) are distinct types.

**Source:** `src/compiler/sema.cpp#L822-L826`, `src/compiler/sema.cpp#L980-L983`

### `type.identity.array-size-significant` — Array length is part of structural identity; tuple arity too

[T; N] structural identity mixes the element type identity AND N. (T1,...,Tn) mixes the arity n and each element identity in order. Arrays/tuples differing only in length/arity have distinct identity.

**Source:** `src/compiler/mono_clone.cpp#L110-L122`

### `type.identity.assoc-type` — Associated-type identity = (trait, assoc name, base, GAT args)

An associated/projection type identity = (trait name, associated-type name, base type, generic-associated-type arguments). GATs differing in their args are distinct projection types.

**Source:** `src/compiler/sema.cpp#L903-L908`, `src/compiler/sema.cpp#L1044-L1049`

### `type.identity.cfg-slot` — Config-slot type identity = (cfg-typevar name, slot key)

A config-slot type is identified by the pair (config type-variable name, slot key); distinct slots intern to distinct types.

**Divergence:** Logos addition (zone/config slots)

**Source:** `src/compiler/sema.cpp#L923-L929`, `src/compiler/sema.cpp#L1050-L1052`

### `type.identity.dstref` — Custom-DST reference identity = (package, name, mutability, owning kind, type-args)

A custom-DST reference type's identity = (package, struct name, mutability, owning kind {Borrow/Box}, type-args); an owning `Box<Foo>` custom-DST is distinct from a borrowed `&Foo`.

**Divergence:** A3 (custom-DST)

**Source:** `src/compiler/sema.cpp#L855-L863`, `src/compiler/sema.cpp#L1009-L1014`

### `type.identity.dyn-trait` — Trait-object identity = (owning kind, auto-traits, trait, type-args)

A trait-object `dyn Trait<..>` identity = (owning kind {Borrow/Box/Rc/Arc} in const_val low byte, `+Send` bit 8, `+Sync` bit 9, trait name, trait type-args). `&dyn T` and `&dyn T + Send` are distinct types; the same trait behind Box vs Rc vs Arc are distinct.

**Source:** `src/compiler/sema.cpp#L884-L892`, `src/compiler/sema.cpp#L1032-L1035`

### `type.identity.fnitem-distinct` — Each function item is a distinct zero-sized type

A function-item type's identity = (function symbol name, turbofish type-args, signature params, return). Two distinct functions with identical signatures get distinct fn-item types, and distinct instantiations of one generic function (even when the resulting fn-ptr signature collapses, e.g. unused type param) get distinct fn-item types.

**Source:** `src/compiler/sema.cpp#L874-L883`, `src/compiler/sema.cpp#L1023-L1031`

### `type.identity.fnptr-abi` — Function-pointer identity = (ABI tag, params, return)

A function-pointer type identity = (extern-ABI tag where empty = default Rust ABI, ordered parameter types, return type). Function pointers differing only in ABI are distinct types.

**Source:** `src/compiler/sema.cpp#L864-L869`, `src/compiler/sema.cpp#L1015-L1019`

### `type.identity.int-lit-value` — Integer-literal placeholder identity carries its value

An inferred integer-literal type `{integer}` carrying a const value is identified by that value (const_val); two literal placeholders with different values do not collapse to one type.

**Source:** `src/compiler/sema.cpp#L909-L916`

### `type.identity.intern-canonical` — Types are interned by canonical structural identity

Every type has a canonical identity: two types constructed structurally identically (per the per-kind identity fields below, computed bottom-up over already-canonical sub-types) denote the same type and intern to one shared representative; structurally distinct types intern to distinct representatives.

**Source:** `src/compiler/sema.cpp#L801-L940`, `src/compiler/sema.cpp#L1099-L1109`, `src/compiler/sema.cpp#L1345-L1354`

**Related:** `type.identity.ref-vs-typeuid`, `type.identity.lifetime-ignored`

### `type.identity.lifetime-ignored` — Lifetimes excluded from type identity for & / &mut

Reference types `&'a T` and `&mut 'a T` have identity determined solely by mutability and pointee `T`; the lifetime `'a` is NOT part of type identity (matches types_equal). Lifetime args on struct/enum/assoc types likewise do not affect type equality.

**Divergence:** Rust treats lifetimes as part of the type but as a separate region-check phase; identity-collapse of lifetimes here matches Rust's type-equality-modulo-regions.

**Source:** `src/compiler/sema.cpp#L817-L821`, `src/compiler/sema.cpp#L954-L959`

### `type.identity.nominal-args` — Struct/enum identity = (package, name, type-args)

A nominal struct or enum type's identity = (package name, type/enum name, ordered type arguments). Two instantiations of a generic nominal type with different type arguments are distinct types; zoned structs share this scheme.

**Source:** `src/compiler/sema.cpp#L827-L837`, `src/compiler/sema.cpp#L984-L994`

### `type.identity.primitive-kind` — Primitive types identified by kind alone

Primitive types carry no structural fields; their kind tag alone identifies them, so all occurrences of a given primitive are the same interned type.

**Source:** `src/compiler/sema.cpp#L930-L932`, `src/compiler/sema.cpp#L1053-L1055`

### `type.identity.ptr-distinct-by-mut` — Raw pointer identity = (mutability, pointee, zoned-flag)

Raw pointer `*const T`, `*mut T`, and `*zoned T` are mutually distinct types: identity = (mut flag, zoned flag carried in const_val bit 0, pointee T). `*zoned T` interns distinctly from a plain `*T`.

**Source:** `src/compiler/sema.cpp#L808-L816`, `src/compiler/sema.cpp#L974-L975`

### `type.identity.recursive-cycle-guard` — Recursive struct types terminate identity computation with a cycle marker

When structural identity recursion re-enters a struct type already on the current walk path (recursive/self-referential types), the recursion is cut with a fixed marker rather than diverging; identity computation always terminates.

**Source:** `src/compiler/mono_clone.cpp#L143-L147`, `src/compiler/mono_clone.cpp#L176-L177`

### `type.identity.ref-vs-typeuid` — Post-interning, type equality = pointer/UID equality

After interning, every type has a unique representative, so type equality reduces to representative-identity: equal hash/UID implies type-equal, and identical reference trivially implies type-equal (lifetime, package, lifetime-args, const_val being the only fields that may share a hash bucket while differing).

**Source:** `src/compiler/sema.cpp#L1345-L1354`, `src/compiler/sema.cpp#L1099-L1104`

### `type.identity.slice-mut-owning` — Slice identity = (mutability, owning kind, element)

Slice types are distinguished by element T, mutability, and owning kind (const_val): `&[T]`, `&mut [T]`, and owning `Box<[T]>` are mutually distinct types.

**Divergence:** A3 (custom-DST / Box<[T]> as owning slice kind)

**Source:** `src/compiler/sema.cpp#L841-L847`, `src/compiler/sema.cpp#L997-L1003`

### `type.identity.struct-field-recursion` — Struct identity recurses through substituted field types

Structural identity of a struct type S<A...> mixes the struct shape tag, the field count, and the identity of each field type after substituting S's type-params by the concrete type-args A.... Generic struct instances thus get distinct identity per instantiation by their concrete field layouts.

**Source:** `src/compiler/mono_clone.cpp#L141-L178`

### `type.identity.structural-hash-layout-stable` — Structural type identity is layout-stable, name-independent

A type's structural identity (used for wire/persistent identity) is computed by a tag-prefixed structural walk that bears no struct/field NAME: two types with identical physical layout (same primitive leaves, same field types in order, same array sizes) have equal identity regardless of struct/field renames. Each primitive kind has a distinct code; aggregate shapes carry distinct shape tags (struct/tuple/array/ptr/&/&mut/slice/enum/fnptr/void/wstatic).

**Uncertainty:** Concrete code values are an implementation detail; the normative content is name-independence + per-shape distinctness.

**Source:** `src/compiler/mono_clone.cpp#L14-L21`, `src/compiler/mono_clone.cpp#L56-L78`, `src/compiler/mono_clone.cpp#L80-L185`

### `type.identity.tuple` — Tuple identity = ordered element types

A tuple type's identity is the ordered sequence of its element types; tuples are equal iff same arity and pairwise-equal elements.

**Source:** `src/compiler/sema.cpp#L838-L840`, `src/compiler/sema.cpp#L995-L996`

### `type.identity.typevar-name` — Type/const variable identity = name

A type variable or const variable is identified by its name (plus const_val); two type parameters with the same name denote the same type variable.

**Source:** `src/compiler/sema.cpp#L899-L902`, `src/compiler/sema.cpp#L1040-L1043`

### `type.identity.wstatic-config` — WritStatic-literal type identity = its byte-hash

A type parameterized by a WritStatic literal config (`Foo::<@{...}>`) is identified by the byte-hash of that literal; distinct configurations instantiate to distinct types and do not dedupe.

**Divergence:** Logos addition (WritStatic const-config type parameters)

**Source:** `src/compiler/sema.cpp#L917-L922`

## Layout & ABI

### `layout.abi.aggregate-byte-size` — ABI byte size of arrays, tuples, structs, enums

Array size = N × elem size. Tuple/struct size = fields laid out sequentially, each aligned to min(field-size, 8), with the total padded to the max field alignment. Enum size = 4 (i32 tag) + the maximum total payload size across variants (void payload components contribute 0). Recursive struct fields are cycle-guarded to pointer size 8.

**Uncertainty:** Comment notes enum layout is a simplification mirroring mlir-gen.

**Source:** `src/compiler/sema.cpp#L3884-L3931`

### `layout.abi.aggregate-field-alignment` — Tuples and structs lay fields out sequentially with natural alignment, capped at 8

For a tuple/struct, fields are placed in declaration order; before each field the running offset is rounded up to that field's alignment, where alignment = min(field-size, 8) (treating zero-size as alignment 1). The aggregate's size is the final offset rounded up to the maximum field alignment encountered.

**Uncertainty:** Alignment is derived as min(size,8) rather than a separate per-type alignment; matches a same-as-size convention for scalars but may diverge for over-aligned types.

**Source:** `src/compiler/mono_clone.cpp#L365-L390`

### `layout.abi.array-size` — Array ABI size is element-size times length

sizeof([T; N]) = N * sizeof(T) (no per-element padding beyond the element's own size).

**Source:** `src/compiler/mono_clone.cpp#L363-L364`

### `layout.abi.fat-pointer-16` — Slices, closures, trait objects, and DST refs are 16-byte fat values

A slice value, a closure, a trait object, and a DST reference each occupy 16 bytes (a two-word fat representation: data/pointer + metadata such as length, environment, or vtable).

**Source:** `src/compiler/mono_clone.cpp#L362`

### `layout.abi.scalar-byte-sizes` — ABI byte sizes of scalar and pointer types

ABI byte sizes: void=0; bool/u8/i8=1; i16/u16=2; i24/u24=3; i32/u32/f32/char/int-literal=4; i56/u56=7; i64/u64/f64/float-literal/usize/isize and all thin pointers (raw/ref/fn-ptr/fn-item/tagged-ptr)=8; i128/u128=16; fat values (slice/closure/trait-object/dst-ref)=16; unsized slice/dyn=0; unknown types default to pointer size 8.

**Source:** `src/compiler/sema.cpp#L3862-L3883`, `src/compiler/sema.cpp#L3932`

### `layout.abi.scalar-sizes` — Scalar ABI byte sizes

ABI size: void/never = 0; bool/u8/i8 = 1; i16/u16 = 2; i24/u24 = 3; i32/u32/f32/char = 4; i56/u56 = 7; i64/u64/f64/usize/isize/pointer/&/&mut/fnptr/fn-item/tagged-ptr = 8; i128/u128 = 16. The Writ-fabric widths I24/U24/I56/U56 occupy their narrow byte sizes (3 and 7).

**Divergence:** A11 (I24/U24/I56/U56 are Logos-only widths)

**Source:** `src/compiler/mono_clone.cpp#L348-L361`

## Dynamically Sized Types

### `layout.dst.effective-dst-detection` — Effective-DST classification of a struct instance

A struct/zoned-struct type is an (effective) DST iff: it is declared unsized; or, after substituting its type-args into the template's LAST field, that field type is UnsizedSlice or UnsizedDyn; or the last field is the bare tail type-var bound to a borrow-owning TraitObject. A field reached only through a pointer is always sized (a self-referential struct is not a DST via its pointer tail).

**Source:** `src/compiler/sema.cpp#L3740-L3791`

### `layout.dst.owned-tail-needs-fat-dstref` — An owned dyn-tail drop only fires through a fat DST reference

A let-bound value initialized from a field read drops as an owned dyn tail only when the (substituted) receiver type is a fat custom-DST reference (DstRef) and the projected field is an unsized dyn (UnsizedDyn, or a borrow-owning TraitObject); a thin pointer/reference receiver (sized inner, genuine Arc<&dyn>) is NOT a DST tail and its drop is a no-op.

**Source:** `src/compiler/mono_clone.cpp#L395-L414`

### `layout.dst.prefix-field-offset` — Custom-DST prefix field offsets use sequential aligned layout

For a custom-DST struct (header fields + unsized tail), a named field's byte offset is computed by the same sequential aligned-layout walk as a normal struct (offset rounded up to min(size,8) before each field); the unsized tail field, when reached, yields its aligned offset and substituted type for fat-pair projection reusing the DstRef's carried metadata.

**Source:** `src/compiler/mono_clone.cpp#L416-L441`

### `type.dst.self-describing-fat-ref-requires-impl` — Self-describing DST borrowed as fat ref must impl SelfDescribing

Borrowing a `#[self_describing]` effective-DST struct as a fat reference `&S`/`&mut S` (which materializes by recovering the tail length via `dst_len`) requires the struct to `impl SelfDescribing`; otherwise it is an error. A self-describing DST used only via raw pointers/byte arithmetic is not subject to this requirement.

**Source:** `src/compiler/sema.cpp#L3831-L3860`

## Primitives

### `type.primitive.set` — Built-in primitive scalar types

The language has primitive scalar types: void, bool, char, the floats f32/f64, and the integers i8/u8, i16/u16, i24/u24, i32/u32, i56/u56, i64/u64, i128/u128, isize/usize. Each is a distinct type identified by its keyword name.

**Divergence:** A: extra fixed-width widths i24/u24/i56/u56 and 128-bit i128/u128 beyond Rust's standard set.

**Source:** `src/compiler/sema.cpp#L2077-L2097`, `src/compiler/sema.cpp#L2530-L2551`

## Int

### `coerce.int.safe-widening` — Value-preserving integer widening is implicit

A concrete integer from-type coerces implicitly to a wider integer target when value preservation is guaranteed (e.g. u32→i64, i32→i64, u8→u32); signed→unsigned widening is never accepted.

**Source:** `src/compiler/sema.cpp#L1935-L1937`

## Never Type

### `coerce.never.subtype-of-all` — Never (!) coerces to every type, one direction only

from = Never (!) is compatible with any target type T (Never <: T). The reverse T → Never is NOT accepted.

**Source:** `src/compiler/sema.cpp#L1827-L1835`

### `type.never.bang` — Never type `!`

`!` is a type (the never type), parsed as a type reference named `!`.

**Source:** `tools/peg_gen/grammars/logos.peg#L1719-L1720`

### `type.never.param-uninhabited` — Never type forbidden in parameter position

A function parameter typed `!` (never) is rejected: `!` is uninhabited, so no value can be supplied. `!` remains valid in return position (a diverging function).

```logos
fn f(x: !) {}  // error
```

```logos
fn g() -> ! { loop {} }  // ok
```

**Source:** `src/compiler/sema_decl.cpp#L589-L595`

## Tuple Types

### `coerce.tuple.elementwise` — Tuple compatibility is element-wise

Two Tuple types are compatible iff they have equal arity and each pair of corresponding element types is compatible.

**Source:** `src/compiler/sema.cpp#L1969-L1975`

### `type.tuple.multi` — Tuple type

A tuple type is `(T1, T2, ...)` with ≥2 comma-separated element types (optional trailing comma), or a 1-element tuple `(T,)` requiring the trailing comma.

**Source:** `tools/peg_gen/grammars/logos.peg#L1732-L1735`

### `type.tuple.unit` — Unit type `()`

`()` denotes the unit type, the empty tuple type.

**Source:** `tools/peg_gen/grammars/logos.peg#L1722-L1723`, `tools/peg_gen/grammars/logos.peg#L1816-L1817`

### `type.tuple.unit-and-elements` — Tuple type, unit, and variadic pack

`()` (or an empty tuple) resolves to the unit/void type; `(T1,...,Tn)` resolves to a tuple of the element types; `(A...)` resolves to a Tuple of one TypeVar naming the variadic pack.

**Source:** `src/compiler/sema.cpp#L5902-L5926`

### `type.tuple.variadic-arity` — Variadic-arity tuple target `(A...)`

`(A...)` is a variadic-arity tuple type naming pack-typevar A; used as an impl target `impl<A...> Trait for (A...)`. Resolves to a Tuple type with one variadic element naming A.

**Divergence:** Logos addition: variadic tuple impls (no direct Rust equivalent).

**Source:** `tools/peg_gen/grammars/logos.peg#L1726-L1731`

## Parenthesized Types

### `type.paren.transparent` — Parenthesized type is transparent

`(T)` resolves structurally identical to `T`.

**Source:** `src/compiler/sema.cpp#L5896-L5900`

### `type.paren.unwrap` — Parenthesized type

`( T )` is a parenthesized type, distinct from `()` (unit), `(T,)` (1-tuple) and `(T1,T2)` (n-tuple); sema unwraps it to its inner type T.

**Source:** `tools/peg_gen/grammars/logos.peg#L1423-L1426`

### `type.paren.unwrap-to-inner` — Parenthesized type is structurally its inner type

A parenthesized type `(T)` is unwrapped to its inner type `T`; `(T)` and `T` are structurally identical.

**Divergence:** B-ty-09

**Source:** `tools/peg_gen/grammars/logos.peg#L290`

## Reference Types

### `coerce.ref.permission-and-pointee` — Reference/pointer coercions follow permission lattice over compatible pointees

&T↔*const T, &mut T↔*mut T (and *T→&T reverse) coerce when pointees are compatible; &mut T→&T (exclusive→shared) is allowed; *mut T→*const T (drop write) is allowed; &T→&T and &mut T→&mut T propagate over compatible pointees. Mutability is only ever dropped, never gained.

**Source:** `src/compiler/sema.cpp#L2020-L2061`

### `type.ref.borrow` — Reference types

`&T`, `&mut T`, `&'a T`, `&'a mut T` are safe borrow-checked reference types. `&&T` / `&&mut T` (no whitespace, tokenized as AND) denote double-references; arbitrary-depth `& & … T` stacks are accepted at type position.

**Source:** `tools/peg_gen/grammars/logos.peg#L1631-L1655`

### `type.ref.canonicalize-unsized-pointee` — Reference to bare unsized pointee folds to the fat-pointer form

`&T`/`&mut T` whose immediate pointee is bare `[U]` folds to `Slice<U>` (mut-tracked); bare `dyn Tr` folds to TraitObject; an effective-DST struct folds to DstRef. `&str` (`str` pointee) is treated as `&[u8]` and folds to `Slice<u8>`.

**Source:** `src/compiler/sema.cpp#L5744-L5847`

**Related:** `type.ptr.dyn-is-fat`, `type.slice.str-is-byte-slice`

### `type.ref.dotted-path` — Fully-qualified non-generic type path

A fully-qualified non-generic type in type position is written `pkg.path.Type` (dotted); the last path segment is the type. Matched before bare-IDENT alternatives so the whole dotted form is claimed. The generic dotted form `pkg.path.Type<A>` is not supported (use a `use` import + short name).

**Divergence:** Logos path model: `.` for package/module path, `::` for items.

**Source:** `tools/peg_gen/grammars/logos.peg#L1805-L1813`

### `type.ref.double-ref` — Double-reference types

`&&T` resolves to `&(&T)` and `&&mut T` to `&(&mut T)` (lexer collapses `&&`).

**Source:** `src/compiler/sema.cpp#L5849-L5861`

### `type.ref.double-ref-nesting` — Double reference types desugar to nested references

`&&T` resolves to a nested reference `&(&T)`; `&&mut T` resolves to `&(&mut T)`.

**Divergence:** B-ty-07

**Source:** `tools/peg_gen/grammars/logos.peg#L286-L287`

### `type.ref.metavar` — Metavariable type reference

`#Ident` and `#(expr)` are type references whose name is supplied by a metaprogram variable/expression rather than a literal identifier.

**Divergence:** Logos metaprogramming addition.

**Source:** `tools/peg_gen/grammars/logos.peg#L1801-L1804`

### `type.ref.ordered-choice` — Type-reference ordered choice

A type reference resolves by ordered choice: antiquot, typeof, cfg-slot-assoc, cfg-slot, writ-array, writ-map, pointer, array, slice, tagged, dyn, reference, impl-Trait, unit, never, closure, fn-pointer, tuple, paren, qualified-assoc, assoc-type-ref, then simple type. Associated-type forms precede simple_type so `T::Item` matches before `T`.

**Source:** `tools/peg_gen/grammars/logos.peg#L1421`

### `type.ref.unsized-pointee-gated` — Unsized reference pointees allowed only at the immediate position

A bare unsized pointee (`[T]`, `dyn`, `str`) is permitted directly under `&`/`&mut` but the unsized-ok relaxation does not leak into nested type-arg resolution (e.g. `dyn` inside `&Box<dyn>` is still subject to the Box Sized bound).

**Source:** `src/compiler/sema.cpp#L5744-L5777`, `src/compiler/sema.cpp#L5810-L5822`

## Pointer Types

### `type.ptr.dst-thin-if-self-describing` — Raw pointer to a DST struct: thin iff self-describing

For a raw pointer whose pointee is an effective-DST struct: if the struct is self-describing (tail metadata recoverable in-band) the pointer stays thin (8B `Ptr<T>`); otherwise it becomes a fat DstRef carrying the tail length.

**Uncertainty:** `is_effective_dst`/`self_describing` are per-instance struct properties evaluated outside this unit.

**Source:** `src/compiler/sema.cpp#L5714-L5741`

### `type.ptr.dyn-is-fat` — Raw pointer to bare dyn is a fat trait object

`*const dyn Trait` / `*mut dyn Trait` (immediate `dyn` pointee) canonicalises to the inline fat {data,vtable} TraitObject, identical to `&dyn Trait`'s representation.

**Source:** `src/compiler/sema.cpp#L5703-L5713`

### `type.ptr.modifier-set` — Raw-pointer modifiers

A raw pointer type is written `*const T`, `*mut T`, or `*zoned T`/`*zoned mut T`; any other word after `*` is a hard error (`unknown raw-pointer modifier`).

**Divergence:** `*zoned` is a Logos-only zoned-pointer modifier (F3).

**Source:** `src/compiler/sema.cpp#L5685-L5699`, `src/compiler/sema.cpp#L5741`

### `type.ptr.raw` — Raw pointer type

`*const T` is an immutable raw pointer and `*mut T` is a mutable raw pointer to T.

**Source:** `tools/peg_gen/grammars/logos.peg#L1746-L1749`

### `type.ptr.raw-slice` — Raw fat-pointer to slice

`*const [T]` and `*mut [T]` are raw fat pointers to a slice, sharing the `{*const T, usize}` ABI of `&[T]` but without borrow-check guarantees. These alternatives precede plain pointer/array forms so the bare `[T]` parses.

**Source:** `tools/peg_gen/grammars/logos.peg#L1737-L1745`

### `type.ptr.zoned` — Zoned raw pointer `*zoned [mut] T`

`*zoned T` / `*zoned mut T` is a zoned raw pointer (Ref-arm self-relative at rest; deref/assign runs the storage↔compute bridge). `zoned` is a contextual keyword recognized only in pointer position (a bare IDENT after `*`), validated as NAME=="zoned" by sema; it is not globally reserved.

**Divergence:** Logos addition (F3 ref-repr design): zoned pointers, no Rust equivalent.

**Source:** `tools/peg_gen/grammars/logos.peg#L1750-L1759`

## Slice Types

### `coerce.slice.exact-scalar-no-mut-widen` — Slice compatibility: no shared→mut widening, exact concrete scalar elements

Two Slice types are compatible iff: a shared (&[T]) slice never coerces to a mutable (&mut[T]) slice; and for elements, two concrete scalars (concrete integer ≠ IntLit ≠ Enum, or F32/F64/Bool/Char) must match exactly (slices alias raw memory, so no stride-changing widening), while inference holes unify via types_compatible.

**Source:** `src/compiler/sema.cpp#L1946-L1968`

### `type.slice.ref` — Slice type

`&[T]` and `&mut [T]` are slice types (fat pointer: ptr + len); an explicit lifetime `&'a [T]` / `&'a mut [T]` is accepted (captured but not distinctly enforced).

**Source:** `tools/peg_gen/grammars/logos.peg#L1618-L1629`

### `type.slice.sized-vs-unsized` — Sized slice vs bare unsized slice

A `[T]` written under a reference/pointer (SLICE_TYPE) resolves to a sized fat Slice (mut bit tracked); a bare `[T]` by value (UNSIZED_SLICE_TYPE) resolves to an unsized slice.

**Source:** `src/compiler/sema.cpp#L5863-L5894`

### `type.slice.unsized` — Unsized slice type `[T]`

Bare `[T]` (no size) is the unsized slice type. The size-bearing array forms are tried first, so `[T; N]` always wins over the unsized fallback.

**Source:** `tools/peg_gen/grammars/logos.peg#L1764-L1774`

### `type.slice.unsized-only-behind-pointer` — Unsized slice [T] cannot appear by value

The bare unsized slice type `[T]` (Kind::UnsizedSlice) may not appear by value; it is only legal behind `&`/`*const`/`*mut` (canonicalised to a sized Slice) or as a `T: ?Sized` substitution.

**Source:** `tools/peg_gen/grammars/logos.peg#L304`

## Array Types

### `coerce.array.elementwise` — Array compatibility: equal size, compatible elements

Two Array types are compatible iff they have equal arr_size and their element types are compatible (recursively, handling nested arrays).

**Source:** `src/compiler/sema.cpp#L1942-L1945`

### `coerce.array.to-pointer-decay` — Array decays to pointer to element

An Array [T;N] coerces to a raw Ptr *T iff the element type equals the pointee. Additionally &[T;N] / &mut [T;N] decay to *const T/*mut T (and to &T/&mut T) with compatible element pointee, never widening mutability (shared ref only decays to const/shared element pointer).

**Source:** `src/compiler/sema.cpp#L1938-L1941`, `src/compiler/sema.cpp#L1999-L2019`

### `type.array.length-forms` — Array type length forms

`[T; N]` length is determined by: a `metacall { expr }` block whose tail integer is CTFE-evaluated; `sizeof...(P)` over an in-scope type-param pack (symbolic `__sizeof_pack:P`); a literal integer; or a symbolic const parameter name. A missing/empty metacall tail or an unknown pack/op is a hard error.

**Divergence:** Array length via `metacall {..}` replaces Rust const-eval at this position (MP-mc-01).

**Source:** `src/compiler/sema.cpp#L6140-L6226`

### `type.array.size-from-metacall` — Array size from metacall

`[T; metacall { ... }]` permits a compile-time metacall block as the array size expression.

**Divergence:** Logos: comptime sizing via explicit metacall (see explicit-metacall design).

**Source:** `tools/peg_gen/grammars/logos.peg#L1769-L1770`

### `type.array.size-from-pack` — Array size from variadic pack length

`[T; P...(P)]` sizes the array from a variadic pack length; lowered to symbolic array-size-var `__sizeof_pack:P` and resolved at monomorphization.

**Divergence:** Logos addition: pack-length array sizing.

**Source:** `tools/peg_gen/grammars/logos.peg#L1762-L1768`

### `type.array.sized` — Fixed-size array type `[T; N]`

`[T; N]` is a fixed-size array type where N is an integer literal or an identifier (const generic). Size-bearing forms are matched before the unsized fallback.

**Source:** `tools/peg_gen/grammars/logos.peg#L1761-L1772`

## String/str Types

### `type.str.default-fat-slice` — str defaults to &[u8] fat-slice shape

The `str` keyword resolves to the fat-pointer slice form `Slice<u8>` (the &[u8] shape) by default; in a context that explicitly permits an unsized result (e.g. a `T: ?Sized` turbofish position) it resolves to the unsized `[u8]` form so `&T` routes to the same Slice<u8> ABI without double-wrapping.

**Uncertainty:** str modeled as u8 slice rather than a distinct str primitive; unsized vs fat-slice choice is context-driven via unsized_ok_.

**Source:** `src/compiler/sema.cpp#L2552-L2561`

### `type.str.slice-alias` — str is an alias for Slice<u8>; impls aliased to &[u8]

`str` is a built-in that resolves to Slice<u8> (printed `&[u8]`); a trait impl whose target is `str` is also registered under target `&[u8]` so trait-satisfaction checks keyed on the printed slice type find the impl.

**Divergence:** Logos models `str` as Slice<u8>; Rust `str` is a distinct DST.

**Source:** `src/compiler/sema_collect.cpp#L3777-L3787`

## Struct Types

### `coerce.struct.elementwise-typeargs` — Same-name struct compatibility is element-wise over type args

Two Struct types with equal struct_name and equal pkg_name and equal type-arg arity are compatible iff each pair of corresponding type-args is compatible (allowing inference holes, e.g. Vec<i32> ~ Vec<_>).

**Source:** `src/compiler/sema.cpp#L1841-L1858`

### `type.struct.bare-all-default-inst` — Bare generic struct name with all-default params instantiates defaults

A bare struct name N (written without `<...>`) referring to a generic struct whose every type parameter has a default resolves to the defaulted instantiation `N<d0, d1, ...>`, where each default may reference earlier defaults via substitution. If any parameter lacks a default, the bare name resolves to the uninstantiated struct type.

**Source:** `src/compiler/sema.cpp#L2583-L2610`

### `type.struct.dst-tail-slice-last-field` — Custom-DST slice tail only at last field

A struct field may have an unsized slice type `[T]` only at the last field position; such a struct is marked dynamically-sized (DST). Unsized field types are otherwise rejected.

**Divergence:** B2: custom-DST tail-slice (DONE) — Logos supports `struct Foo { hdr: H, tail: [T] }`.

**Source:** `src/compiler/sema_collect.cpp#L4023-L4029`, `src/compiler/sema_collect.cpp#L4055-L4069`

**Related:** `item.struct.tuple-struct-fields`

### `type.struct.non-null-niche` — non_null single-pointer wrapper yields Option niche

A struct annotated `#[non_null]` wrapping a single non-null pointer makes `Option<T>` use the null-pointer value as the None niche (no discriminant overhead).

**Divergence:** A: #[non_null] attribute is a Logos addition mirroring Rust NonNull niche.

**Source:** `src/compiler/sema_decl.cpp#L1200-L1201`

### `type.struct.rel-ptr-offset-storage` — rel_ptr struct is a self-relative pointer

A struct annotated `#[rel_ptr]` is classified as a self-relative pointer using 8-byte offset storage.

**Divergence:** A: RefRepr RelOffset Logos addition, no Rust analog.

**Source:** `src/compiler/sema_decl.cpp#L1194-L1196`

### `type.struct.self-describing-thin-ptr` — self_describing keeps *Self thin

A struct annotated `#[self_describing]` keeps `*Self` a thin pointer (no DstRef fattening) under Ptr→DstRef canonicalization.

**Divergence:** A: Writ/RefRepr Logos addition, no Rust analog.

**Source:** `src/compiler/sema_decl.cpp#L1186-L1189`

### `type.struct.zone-mut-fat-ref` — zone_mut makes &mut T fat carrying its allocator

For a struct annotated `#[zone_mut]`, a `&mut T` reference is a fat `{data, zone}` pair carrying the value's allocator/zone.

**Divergence:** A: Writ zone model Logos addition; Rust &mut is thin.

**Source:** `src/compiler/sema_decl.cpp#L1190-L1192`

### `type.struct.zoned2-relative-fields` — zoned2 struct fields use relative pointers

A struct annotated `#[zoned2]` stores its pointer fields as self-relative offsets (RelOffset) rather than absolute addresses.

**Divergence:** A: Writ zoned2 Logos addition, no Rust analog.

**Source:** `src/compiler/sema_decl.cpp#L1193`

## Enum Types

### `coerce.enum.elementwise-typeargs-no-widen` — Same-name enum compatibility: exact concrete scalar args, no by-value widening

Two Enum types with equal enum_name and pkg_name and equal nonzero type-arg arity are compatible iff for each arg pair: an unresolved arg (TypeVar/InferredType/CfgSlotType/Error) on either side unifies freely; two concrete scalars (concrete integer ≠ IntLit ≠ Enum, or F32/F64/Bool/Char) must have EXACTLY the same kind (no widening); otherwise the pair must be types_compatible.

**Source:** `src/compiler/sema.cpp#L1859-L1901`

### `coerce.enum.to-integer-discriminant` — C-style enum coerces to integer (discriminant) but never to another enum

An Enum from-type coerces to an integer-kind target that is itself not an Enum. Implicit integer→enum is NOT permitted (requires explicit cast / variant), and enum→enum via this rule is excluded to prevent reinterpreting incompatible layouts.

**Source:** `src/compiler/sema.cpp#L1922-L1934`

### `type.enum.backing-integer` — enum backing type must be integer

An explicit enum backing type `enum Foo : T { … }` must be an integer kind; a non-integer T is rejected.

```logos
enum E : u64 { A }
```

**Source:** `src/compiler/sema_collect.cpp#L1917-L1925`

## Union Types

### `layout.union.max-of-fields` — Union layout is max-size at max-alignment

A struct marked as a union (`#[repr(...)]` union) is laid out as the maximum field size aligned to the maximum field alignment; all fields overlap at offset 0.

**Divergence:** Logos union via #[repr]/union attribute; layout semantics match C/Rust unions.

**Source:** `src/compiler/sema_decl.cpp#L1202-L1204`

## Function Types

### `coerce.fn.fnitem-to-fnptr` — FnItem coerces to FnPtr of matching signature

A FnItem (the zero-size identity type of a named fn) coerces to a FnPtr iff arity matches and every parameter and the return type are element-wise compatible. The reverse FnPtr→FnItem is rejected, and two DIFFERENT FnItems with identical signatures are NOT compatible (they do not collapse).

**Divergence:** A6/logos-core 1.4: FnItem vs FnPtr split is a Logos addition; FnItem→FnPtr decay mirrors Rust fn-item→fn-pointer coercion.

**Source:** `src/compiler/sema.cpp#L1816-L1826`

## Closure Types

### `coerce.closure.hint-from-fn-bound` — Closure type hint derived from a Fn-family type-param bound

When the expected type is a type parameter bounded by an Fn-family trait (Fn/FnMut/FnOnce(params)->ret), a closure literal in that position is given the inferred closure type with parameter and return types taken from the bound (after generic substitution).

**Source:** `src/compiler/sema_expr.cpp#L14061-L14080`

### `coerce.closure.ref-to-closure` — &Closure / &mut Closure coerce to Closure

A Ref or MutRef whose pointee is a Closure coerces to a bare Closure target (a `dyn FnMut(..)` annotation parses to Closure, so `take_ref(&cl)` type-checks against `f: &dyn FnMut(..)`).

**Divergence:** A10

**Source:** `src/compiler/sema.cpp#L1976-L1987`

### `type.closure.type` — Closure type

`|T1, T2| -> R` is a closure type used in parameter annotations; the zero-arg form `|| -> R` is accepted (the `||` token is split).

**Divergence:** A6: Rust spells closures via Fn-family bounds; Logos has a dedicated `|..|->R` closure type syntax.

**Source:** `tools/peg_gen/grammars/logos.peg#L1657-L1664`

## Generic Types

### `type.generic.instantiation` — Generic type instantiation `T<...>`

`Name<arg, ...>` (optional trailing comma) instantiates a generic type. The type name may also be a metavariable: `#Ident<...>` or `#(expr)<...>`.

**Source:** `tools/peg_gen/grammars/logos.peg#L1797-L1815`

### `type.generic.type-arg-kinds` — Generic type-argument kinds

A generic type argument may be a lifetime `'a` (stored as LIFETIME_PARAM and skipped during concrete-type resolution), a pack expansion `Ident...`, an antiquote `$Ident` or `$Ident...`, an integer literal (optionally negated), a writ literal, or a type.

**Source:** `tools/peg_gen/grammars/logos.peg#L1776-L1795`

## Associated Types

### `type.assoc.normalize-via-where-eq` — Associated-type projection normalized by where-clause equality bound

An associated-type projection `<TV as Trait>::A` (where `TV` is a generic type-param) is normalized to the concrete type `C` whenever an in-scope where-clause bound on `TV` records an associated-type equality `Trait::A = C`. When the projection records a trait, only equalities from a bound whose trait matches (or whose trait is unrecorded) apply; the first matching equality wins. If no matching equality exists, the projection is left unchanged.

**Source:** `src/compiler/sema.cpp#L2689-L2704`

### `type.assoc.projection` — Associated-type projection

`T::Item` and `T::Item<A,B>` (GAT with type args) are associated-type references; the `::Name[<args>]` tail may chain one or more times. `<T as Trait>::Assoc` is the fully-qualified form, with the disambiguating trait recorded for resolution.

```logos
<Vec<T> as IntoIterator>::IntoIter
```

**Source:** `tools/peg_gen/grammars/logos.peg#L1475-L1488`

## Type Aliases

### `type.alias.generic-alias-inlined` — Generic type aliases are inlined at use sites

A type alias with type parameters has no concrete standalone type; it is inlined at each use site. Only non-generic aliases resolve to a concrete type.

**Source:** `src/compiler/sema_decl.cpp#L1570-L1577`

### `type.alias.impl-target-unfold` — Non-generic type aliases unfold at an impl target position

When the impl target names a non-generic type alias `type A = B;`, the impl is treated as an impl on the aliased struct/datatype B (the alias is transparent): `impl Tr for A` ≡ `impl Tr for B`, including concrete-generic mangling of B.

**Source:** `src/compiler/sema_decl.cpp#L1823-L1837`

### `type.alias.name-shadowing-order` — Type-alias name resolution shadowing order

A bare type name N resolves to a 0-arg type alias by probing in order: (1) the current package's own alias `pkg::N`, (2) the bare/unqualified alias N, (3) aliases from wildcard-imported packages. The current package's alias thus shadows a same-named imported/stdlib alias (Rust scoping).

**Source:** `src/compiler/sema.cpp#L2562-L2581`

## Type Inference

### `coerce.infer.placeholder-unifies` — Inferred-type placeholder unifies either direction

If either from or to has kind InferredType (`_`), the pair is compatible; concrete resolution is deferred to the surrounding annotation/RHS unifier.

**Source:** `src/compiler/sema.cpp#L1836-L1840`

### `type.infer.fill-annotation-from-rhs` — Inferred holes in let-annotation filled from RHS type

An `_` (inferred) hole in a let annotation is filled from the structurally-matching concrete RHS type (e.g. `let v: Vec<_> = vec![1]` binds as `Vec<i32>`); the annotation's concrete parts win, holes take the RHS side. Mismatched shapes leave the annotation unchanged. A bare `_` against an integer-literal RHS defaults to i32 and against a float-literal RHS to f64.

```logos
let v: Vec<_> = vec![1];  // Vec<i32>
```

**Source:** `src/compiler/sema.cpp#L4395-L4402`, `src/compiler/sema.cpp#L4310-L4318`

### `type.infer.hole-detection` — A type contains an inferred hole transitively

A type is considered to contain an inferred hole if it is `_` or if any of its type arguments, tuple elements, element type, or pointee transitively contains one.

**Source:** `src/compiler/sema.cpp#L4343-L4351`

## Unsizing

### `coerce.unsize.array-to-slice` — &array → slice

`&a` where `a: [T; N]` yields a slice value `&[T]` with len = N (an unsized coercion at the point of borrow), not `&[T; N]`. This applies to array variables, array statics, and bare array literals `&[e0, .., e_{N-1}]`.

**Source:** `src/compiler/sema_expr.cpp#L2510-L2514`, `src/compiler/sema_expr.cpp#L2570-L2585`

### `coerce.unsize.box-consumes-source` — Unsize to owning trait object moves the source

An unsize cast to an owning trait object (e.g. `box_val as Box<dyn Trait>`) consumes/moves the operand; ownership of the heap data transfers to the result so the source's own drop does not also run (avoiding double-free).

**Source:** `src/compiler/sema_expr.cpp#L969-L974`

**Related:** `coerce.unsize.struct-coerce-unsized`

### `coerce.unsize.lifetime-diff-not-unsize` — Type-arg differences that are not unsizes fall through to variance

A struct type-arg difference that is not a sized→fat unsize (e.g. lifetime-only variance `Foo<&'a>` vs `Foo<&'b>`, or multi-field structs) is NOT handled as CoerceUnsized; it must be resolved by the variance/compat machinery. CoerceUnsized requires exactly one field and that field's type to genuinely become fat.

**Source:** `src/compiler/sema_expr.cpp#L669-L688`

**Related:** `coerce.unsize.struct-coerce-unsized`

### `coerce.unsize.struct-coerce-unsized` — CoerceUnsized for single-field smart-pointer structs

A value of struct type S<..A> coerces to S<..B> (same struct, equal type-arg arity) when S has exactly one field whose substituted type changes from sized/thin to fat-unsized: target field kind is DstRef, or (TraitObject while source isn't), or (Slice while source isn't). The coercion reads the single field, casts it to the target field type, and repacks into the target struct.

```logos
let r: Rc<dyn Tr> = rc_a as Rc<dyn Tr>;
```

**Source:** `src/compiler/sema_expr.cpp#L651-L699`

**Related:** `coerce.unsize.box-consumes-source`

### `coerce.unsize.struct-to-dyn-trait` — Struct and references to struct coerce to &dyn Trait

A TraitObject target accepts a from-type that is a Struct, a Ptr to anything, or a Ref/MutRef whose pointee is a Struct; the actual trait-impl satisfaction is checked at codegen.

**Source:** `src/compiler/sema.cpp#L1988-L1998`

## Method Receiver Coercion

### `coerce.method-recv.auto-ref` — Auto-ref of receiver to match &self / &mut self (occurrence 1 of 2)

> **Collision:** id `coerce.method-recv.auto-ref` is shared by 2 extracted rules. Both are reproduced; they overlap but cite different source spans.

If the dispatched method's first formal is a reference (`&self`/`&mut self`) and the receiver value is neither a reference nor a raw pointer, the receiver is implicitly wrapped in an address-of (`&` or `&mut` per the formal's mutability) producing a reference-typed receiver.

**Source:** `src/compiler/sema_expr.cpp#L7696-L7711`, `src/compiler/sema_expr.cpp#L8061-L8080`

### `coerce.method-recv.auto-ref` — Method-call receiver auto-ref (occurrence 2 of 2)

> **Collision:** id `coerce.method-recv.auto-ref` is shared by 2 extracted rules. Both are reproduced; they overlap but cite different source spans.

When a resolved method's receiver formal is `&Self`/`&mut Self` (or otherwise ref-like) but the actual receiver value is non-reference and non-raw-pointer, the receiver is implicitly auto-referenced (`&`/`&mut` per the formal's mutability) before the call.

**Source:** `src/compiler/sema_expr.cpp#L8666-L8674`, `src/compiler/sema_expr.cpp#L9026-L9037`, `src/compiler/sema_expr.cpp#L9122-L9131`

### `coerce.method-recv.deref-bound-fallthrough` — Autoderef through a Deref/DerefMut bound when no bound provides the method

If no in-scope bound on type-parameter T provides method `m`, but T has a bound `T: Deref<C>` (or `DerefMut<C>`), the receiver is rewritten to `recv.deref()` (resp. `deref_mut()`), typed `&C` (resp. `&mut C`), and method resolution falls through to the ordinary inherent/struct-method path on C.

**Source:** `src/compiler/sema_expr.cpp#L7798-L7821`

### `type.method-recv.deref-before-lookup` — Receiver reference stripped to its pointee for nominal method lookup

For method-formal hinting and dispatch, a receiver of reference type (`&T`/`&mut T`) is reduced to its pointee T before extracting the struct/enum name and binding the receiver's nominal type-arguments into the substitution.

**Source:** `src/compiler/sema_expr.cpp#L7874-L7894`

## Deref Coercion

### `coerce.deref.box-slice-borrow` — &Box<[T]> deref-coerces to &[T]

`&b` where `b` is an owning slice (`Box<[T]>`) yields a borrowed `&[T]` view over the same {data,len} storage with no copy or move; mutability is inherited from the owning slice.

**Source:** `src/compiler/sema_expr.cpp#L2516-L2521`

### `coerce.deref.box-struct-borrow` — &Box<Foo> deref-coerces to &Foo

`&b` where `b` is an owning DST reference (`Box<Foo>`) yields a borrowed `&Foo` with the same reference value re-typed non-owning; mutability is inherited.

**Source:** `src/compiler/sema_expr.cpp#L2522-L2532`

### `coerce.deref.ref-vec-to-slice` — &Vec<T> / &mut Vec<T> coerce to &[T]

A Ref/MutRef whose pointee is the stdlib struct Vec<T> coerces to a Slice whose element is compatible with Vec's first type-arg (Vec's {ptr,len,cap} layout has the {ptr,len} slice fat-pointer as a prefix). Hardcoded to the stdlib Vec; general Deref is not yet covered.

**Uncertainty:** Hardcoded special-case for struct named "Vec"; not a general Deref-trait rule.

**Source:** `src/compiler/sema.cpp#L2034-L2049`

### `coerce.deref.user-deref-chain` — Deref coercion through user/stdlib Deref(Mut) impls

A receiver of struct type with a Deref/DerefMut impl deref-coerces by calling `deref`/`deref_mut` to obtain `&Target`; for an unsized Target (dyn or slice) the returned fat reference is the value itself with no further place-deref. A mutable deref step may fall back to the Deref Target when only Deref is implemented (shared supertrait Target).

**Source:** `src/compiler/sema_expr.cpp#L100-L217`

## Casts

### `coerce.cast.aggregate-scalar-forbidden` — as-cast forbidden between aggregates and scalars

An `as`-cast where the source is an aggregate (struct/array/tuple/enum) and the target is a scalar is rejected, EXCEPT a payload-free (C-style) enum cast to integer/bool (discriminant cast). Symmetrically, casting a scalar/pointer to an aggregate target (struct/zoned-struct/array/tuple/enum) is rejected as a non-primitive cast target.

**Source:** `src/compiler/sema_expr.cpp#L881-L956`

### `coerce.cast.as-bool-forbidden` — as bool is not a permitted cast

Casting any non-bool value (integer, float, C-style enum, etc.) to `bool` via `as` is rejected; only the reverse `bool as <int>` (true→1, false→0) is valid. Use `x != 0` / `x != 0.0` instead.

```logos
let b: bool = (i as bool);  // error
```

**Source:** `src/compiler/sema_expr.cpp#L923-L941`

### `coerce.cast.ref-to-scalar-autoderef` — &T as scalar auto-derefs the reference

When casting a value of type `&T`/`&mut T` (with scalar pointee T) to a scalar target (any integer/usize/isize/f32/f64/char/bool), the operand is auto-dereferenced before the cast, so the pointee value is converted, not the pointer bits. Pointer→pointer casts and `&T as *T`/`as usize` reinterpretations are unaffected.

```logos
let n: &f64 = &1.0; let x = n as i64;
```

**Divergence:** RFC 2005 match-ergonomics interaction; behaviorally Rust-conformant (note T2-26)

**Source:** `src/compiler/sema_expr.cpp#L841-L875`

### `coerce.cast.str-to-mut-ptr-forbidden` — str as *mut u8 is forbidden

Casting a `str` (Slice<u8>) to `*mut u8` is rejected because str data is read-only (rodata); `*const u8` must be used instead.

**Source:** `src/compiler/sema_expr.cpp#L957-L967`

## Antiquot

### `type.antiquot.quote-only` — Antiquotation valid only inside quote_ty!

A type antiquotation `$name` or pack-splice `$name...` is a hard error outside a `quote_ty! { ... }` context.

**Source:** `src/compiler/sema.cpp#L5660-L5671`

### `type.antiquot.quote-ty-only` — Type antiquotation

`$ident` in type position is a type antiquotation valid only inside `quote_ty! { ... }`; resolving it elsewhere is an error.

**Divergence:** A6

**Source:** `tools/peg_gen/grammars/logos.peg#L1456-L1459`

## Arg

### `coerce.arg.method-canonical-coercions` — Method arguments coerced in canonical order

Each method argument is coerced toward its substituted parameter type in canonical order applying: unsize-to-dyn, implicit reborrow, and integer widening. After coercion, a non-Error/non-TypeVar/non-AssocType param type that is incompatible with the argument type is a type-mismatch error.

**Source:** `src/compiler/sema_expr.cpp#L7525-L7540`

## Assoc Ref

### `type.assoc-ref.bound-and-supertrait-lookup` — Associated-type projection on a type-parameter

For `base::Item` where `base` is a type-variable, the owning trait of `Item` is found by searching the bounds of `base` (with `Self` mapped to the enclosing trait) and walking each bound trait's supertrait chain; the first trait declaring an associated type named `Item` is selected, carrying that bound's concrete trait type-args.

**Source:** `src/compiler/sema.cpp#L5108-L5112`, `src/compiler/sema.cpp#L5146-L5185`

### `type.assoc-ref.concrete-impl-fallback` — Assoc projection fallback to implementing trait

If no owning trait is found from bounds or impl context, the projection's owning trait is found among traits that have an impl for the concrete base type (tried under both the full concrete type name and the bare struct name); if still none declares `Assoc`, a diagnostic 'no associated type Assoc found for <base>' is raised.

**Source:** `src/compiler/sema.cpp#L5232-L5257`

### `type.assoc-ref.deferred-node` — Deferred associated-type node carries trait args

An unresolved projection yields a deferred AssocType node {base, trait, name, gat_args}; the trait name is suffixed with the concrete trait type-args so distinct `Trait<T>` instantiations produce distinct nodes (empty suffix for non-generic traits preserves the bare name). Bounds declared on the assoc type are propagated into the projection's bound context.

**Source:** `src/compiler/sema.cpp#L5308-L5337`

### `type.assoc-ref.eager-concrete-projection` — Eager projection for concrete base with generic trait

When the base is a concrete type and the resolved trait is generic (has type-args), the projection is resolved immediately by looking up the trait+args-suffixed assoc-type impl and substituting the base's type-args; this disambiguates two `Trait<T>` impls on one type that would otherwise intern to a single trait-arg-less deferred node and collapse.

**Divergence:** G156-1 disambiguation of multiple Trait<T> impls.

**Source:** `src/compiler/sema.cpp#L5275-L5307`

### `type.assoc-ref.equality-bound-normalization` — Associated-type equality bound normalization

If the base type-param carries an equality bound `Trait<A = V>`, the projection `T::A` is normalized directly to `V` at resolution time.

**Source:** `src/compiler/sema.cpp#L5338-L5342`

### `type.assoc-ref.gat-args` — Generic associated type arguments

An associated-type reference may carry type arguments (`T::Item<i32>`, a GAT) and lifetime arguments (`T::Item<'a>`); lifetime args are collected separately from type args. The number of supplied GAT type-args must equal the associated type's declared GAT type-param count, and those args must satisfy the GAT type-params' trait bounds.

**Source:** `src/compiler/sema.cpp#L5113-L5139`, `src/compiler/sema.cpp#L5258-L5274`, `src/compiler/sema.cpp#L5320-L5325`

### `type.assoc-ref.impl-trait-context` — Assoc projection resolves against the enclosing impl trait

Inside an `impl Trait<Args> for C`, an unresolved projection `Self::Assoc` resolves to the impl's trait when that trait declares `Assoc`, binding the projection to this impl's concrete trait type-args.

**Source:** `src/compiler/sema.cpp#L5212-L5231`

## Binop

### `coerce.binop.autoderef-numeric-ref` — Auto-deref reference operand to primitive in scalar binops

For binary operators in {+,-,*,/,%,<,<=,>,>=,==,!=,&,|,^,<<,>>}, an operand of type &T or &mut T whose pointee T is an integer, f32, f64, bool, or char is implicitly dereferenced to T before operator resolution; struct pointees are not peeled.

```logos
fn f(r: &i32) -> i32 { r + 1 }
```

**Divergence:** Models Rust's `impl Add<i32> for &i32` family via auto-deref rather than blanket ref impls.

**Source:** `src/compiler/sema_expr.cpp#L1718-L1742`

### `coerce.binop.bitwise-ref-scalar-deref` — Auto-deref &T for bitwise/shift when T is integer (or bool for bitwise-only)

For bitwise/shift operators {&,|,^,<<,>>}, an operand of type &T is implicitly dereferenced to T when T is an integer type, or when the operator is one of {&,|,^} and T is bool; shift operators never deref a bool pointee.

**Source:** `src/compiler/sema_expr.cpp#L2389-L2402`

### `type.binop.arith-numeric` — Arithmetic operators require numeric operands

Arithmetic operators {+,-,*,/,%} require both operands to be numeric; the result type is the unified integer type of the operands when both are integers, otherwise unify_numeric, with a TypeVar operand propagated as the result when the other is an integer literal.

**Source:** `src/compiler/sema_expr.cpp#L2304-L2383`

### `type.binop.bitwise-integer-or-bool` — Bitwise/shift operands must be integer (or bool for bitwise-only)

Bitwise operators {&,|,^} require integer or bool operands; shift operators {<<,>>} require integer operands only. The result type is the unified integer type of the operands.

**Divergence:** Matches Rust `impl BitAnd/BitOr/BitXor for bool`.

**Source:** `src/compiler/sema_expr.cpp#L2384-L2416`, `src/compiler/sema_expr.cpp#L2454-L2454`

### `type.binop.comparison-bool` — Comparison operators yield bool with compatible operands

Comparison operators {==,!=,<,<=,>,>=} require the two operand types to be mutually compatible (in either direction) and produce type bool.

**Source:** `src/compiler/sema_expr.cpp#L2272-L2303`

### `type.binop.enum-lit-rehint` — Bare enum-literal operand re-lowered with peer's concrete type

In an enum == / != where one operand is a bare enum literal (no type-args, e.g. Option::None) and the other carries concrete type-args, the bare operand is re-lowered with the peer's enum type as the hint so both sides share the same concrete layout for the eq impl.

**Source:** `src/compiler/sema_expr.cpp#L2124-L2150`

### `type.binop.error-propagation` — Error operand yields error type

If either operand has the error type, the binary expression's result type is the error type (error already reported upstream; no cascade).

**Source:** `src/compiler/sema_expr.cpp#L2253-L2254`

### `type.binop.intlit-fit-arith` — Arithmetic literal operand must fit the peer integer type

In integer arithmetic where one operand is an integer literal and the other a concrete integer type, the literal value must fit in that concrete type's range.

**Source:** `src/compiler/sema_expr.cpp#L2367-L2382`

### `type.binop.intlit-fit-comparison` — Comparison literal must fit the peer integer type

In a comparison where one operand is an integer literal and the other a concrete integer type, the literal value must fit in that type's range; otherwise the comparison is rejected (it could never hold).

```logos
let x: i32; x == 10000000000
```

**Source:** `src/compiler/sema_expr.cpp#L2290-L2302`

### `type.binop.logical-bool` — && and || require bool operands, yield bool

Operators && and || require each operand to be bool or the never type !; the result type is bool.

**Source:** `src/compiler/sema_expr.cpp#L2262-L2271`

### `type.binop.never-operand` — Diverging operand makes binop type !

If either operand has the never type !, the binary expression type-checks against any operator (no numeric/bool requirement) and its result type is !.

```logos
1 + return 7
```

```logos
x * break
```

**Source:** `src/compiler/sema_expr.cpp#L2255-L2261`

## Canonicalize

### `type.canonicalize.global-substitution` — Global type canonicalization after collection

After item collection, every declared type position is canonicalized by applying the empty substitution (resolving aliases/concrete forms while leaving TypeVar-bearing types unchanged for later mono): struct field types, enum-variant payload types, every fn/generic-fn param and return type, type-alias bodies, module-const types, associated-const-impl types, and associated-type-impl body types.

**Uncertainty:** Concrete behavior of subst_type_sema({}) is inferred from comments (L724-L726): non-generic bodies become concrete; TypeVar-bearing types are left for later substitution.

**Source:** `src/compiler/sema_collect.cpp#L703-L728`

## Cfg Slot

### `type.cfg-slot.const-generic-defer` — Deferred cfg-slot when base is a const type-param

When `CFG` names a const-generic type-parameter of the enclosing item, `<type:CFG.path>` is NOT resolved eagerly; it yields a deferred CfgSlotType carrying the CFG ident and an encoded path, which monomorphization resolves once the parameter is bound to a concrete WritStatic value.

**Uncertainty:** Logos-specific; const-generic-of-WritStatic kind.

**Source:** `src/compiler/sema.cpp#L4972-L4981`, `src/compiler/sema.cpp#L4982-L4983`, `src/compiler/sema.cpp#L5055`, `src/compiler/sema.cpp#L5101-L5105`

### `type.cfg-slot.const-param-must-be-writstatic` — cfg-slot base type-param must be const WritStatic

If `CFG` in `<type:CFG.path>` names a type-parameter, that parameter must be declared `const CFG: WritStatic`; otherwise a diagnostic is raised (the param must be a const-generic whose type is the WritStatic struct).

**Uncertainty:** Logos-specific WritStatic const-generic requirement.

**Source:** `src/compiler/sema.cpp#L4985-L5004`

### `type.cfg-slot.eager-alias-resolution` — Eager cfg-slot resolution against a WStaticLit alias

When `CFG` is not a type-param but resolves to a type alias bound to a WStaticLit (`pub type Cfg = @{...};`), the path is walked eagerly through that literal's registered Writ value at resolution time, producing the concrete projected type directly.

**Uncertainty:** Logos-specific.

**Source:** `src/compiler/sema.cpp#L4974-L4976`, `src/compiler/sema.cpp#L5055-L5099`

### `type.cfg-slot.path-extraction` — Config-slot type projection

`<type:CFG.path>` extracts a type from a WritStatic-typed binding `CFG` by walking a path of steps; each step is a struct-field access by name (on a string-keyed Writ map), an integer-field access by index (on an int-keyed Writ map), or an array index (on a Writ array). The path must be non-empty. The final reached Writ value must be a Type value; its named type is then resolved as the result.

**Uncertainty:** Logos-specific construct (no Rust analogue); semantics inferred from path-walk logic.

**Source:** `src/compiler/sema.cpp#L4969-L4981`, `src/compiler/sema.cpp#L5038-L5041`, `src/compiler/sema.cpp#L5067-L5096`

### `type.cfg-slot.projection` — Type-level cfg-slot projection

`<type:CFG.path>` projects, at mono-time, the type stored at a path within a WritStatic-typed type-level binding. Path steps are `.IDENT` (string key), `.INTEGER` (int key) and `.[INTEGER]` (array index). At least one path step is required. `<type:CFG.SLOT>::Assoc` projects an associated type on the slot base.

**Divergence:** A6

**Source:** `tools/peg_gen/grammars/logos.peg#L1428-L1449`

## Cfgslot

### `coerce.cfgslot.numeric-bidirectional` — Cfg-slot type behaves as numeric placeholder bidirectionally

A CfgSlotType (deferred WritStatic-bound primitive placeholder) is compatible bidirectionally with any integer kind and with IntLit/FloatLit/F32/F64; the resolved-type compatibility is enforced later at monomorphization.

**Source:** `src/compiler/sema.cpp#L1909-L1921`

## Closure Arg

### `type.closure-arg.hint-from-formal` — Closure/literal argument types hinted from method formal parameter

Each argument is lowered with a type hint derived from the corresponding method formal: a single Ref/MutRef wrapper on the formal is stripped, then a function/closure formal seeds the closure hint, a generic Enum/Struct (with type-args) seeds the enum/struct hint, and a Tuple formal seeds the tuple hint. An Fn-family-bounded bare type-parameter formal synthesizes a Closure hint from the bound's signature so an untyped closure (`|i|`) infers its parameter types.

**Source:** `src/compiler/sema_expr.cpp#L7942-L7986`, `src/compiler/sema_expr.cpp#L7958-L7979`

## Closure Type

### `type.closure-type.params-ret` — Closure type literal

A closure type literal resolves to Closure with the listed parameter types and a return type defaulting to unit/void when absent.

**Source:** `src/compiler/sema.cpp#L6067-L6082`

## Compatible

### `coerce.compatible.equal-implies-compatible` — Type equality implies compatibility

types_compatible(from,to) holds whenever types_equal(from,to); compatibility is the reflexive superset of equality augmented with the directed coercions below. Null on either side ⇒ incompatible.

**Source:** `src/compiler/sema.cpp#L1797-L1799`

## Copy

### `type.copy.drop-mutually-exclusive` — Copy and Drop are mutually exclusive (E0184)

A type may not both implement Copy and Drop. An `impl Drop for X` blocks X from auto-Copy; an explicit `impl Copy for X` coexisting with `impl Drop for X` is a compile error (E0184), since bitwise duplication of a Copy value would re-run the destructor on each copy (double-free).

**Source:** `src/compiler/sema.cpp#L2878-L2879`, `src/compiler/sema.cpp#L2971`, `src/compiler/sema.cpp#L2983-L3000`

### `type.copy.field-kinds` — Copy field-type classification

For auto-Copy, a field type counts as Copy iff it is: a primitive integer/float/bool/char/usize/isize; a raw pointer (`*const`/`*mut`); a shared reference (`&T`); a function pointer or fn-item; a payload-less enum (no variant carries a payload and the enum has no `impl Drop`); a non-owning slice (`&[T]`); a struct already classified Copy; or a tuple all of whose elements are Copy. A `&mut T` exclusive reference is NOT Copy (move-only). Owning slices `Box<[T]>`, arrays, closures, type-vars, trait-objects, and payload-bearing enums are not Copy.

**Source:** `src/compiler/sema.cpp#L2883-L2953`

### `type.copy.struct-structural-auto` — Structural auto-Copy for plain-data structs

A plain-data `struct` with no `impl Drop` and at least one field, whose every field type is Copy, is itself Copy — no `#[derive(Copy)]` opt-in is required. Determined by fixpoint over the struct dependency graph (a struct may become Copy once all its struct-typed fields are known Copy). Zero-field structs are not auto-promoted.

**Divergence:** Logos auto-derives Copy structurally; Rust requires explicit `#[derive(Copy)]`. Capability-equivalent (a Copy type stays usable after by-value use).

**Source:** `src/compiler/sema.cpp#L2867-L2880`, `src/compiler/sema.cpp#L2955-L2981`

### `type.copy.structural-auto` — non-Drop struct of all-Copy fields is automatically Copy

A struct that does not implement Drop and whose every field type is Copy is automatically Copy, without an explicit `impl Copy`.

**Divergence:** A: diverges from Rust, which requires an explicit `#[derive(Copy)]`/`impl Copy`.

**Uncertainty:** Exact DIVERGENCES.md tag not confirmed; promotion logic lives in compute_auto_copy_types outside this unit.

**Source:** `src/compiler/sema_collect.cpp#L674-L678`

## Datatype

### `type.datatype.data-plain-inference` — DataPlain vs DataNode inference for datatypes

A datatype is DataPlain unless it (transitively, through array element types) embeds a datatype field that is not itself DataPlain, or a generic/unknown datatype field; such fields demote the enclosing datatype to DataNode. A by-value concrete DataPlain nested datatype does NOT demote the outer type; generic datatype fields (non-empty type args, e.g. `RelPtr<Node>`) and forward-/cross-package-referenced datatypes are treated conservatively as DataNode.

**Divergence:** A6: Writ datatype DataPlain/DataNode classification is Logos-only.

**Source:** `src/compiler/sema_collect.cpp#L3945-L3964`

### `type.datatype.pod-field-restriction` — Writ datatype fields must be POD-compatible

A field of a `datatype` (Writ fabric type) must be one of: a primitive scalar (i8..i128/u8..u128 incl. packed i24/u24/i56/u56, f32/f64, bool, integer/float literal types), an array whose element is datatype-safe, another datatype (ZonedStruct), a plain struct that is a `#[rel_ptr]` self-relative pointer, or an unresolved type variable (checked later by mono). Any other field type is rejected. Annotation types (compile-time only) are exempt and may hold non-POD fields such as `str`.

**Divergence:** A6/A11: Writ datatype fabric is a Logos-only feature; uses extra packed int widths.

**Source:** `src/compiler/sema_collect.cpp#L3892-L3933`

## Drop

### `type.drop.copy-bounded-typevar-not-droppable` — Copy-bounded type-param is non-droppable

A generic type-param `T` with an explicit `Copy` bound is provably non-droppable (Copy and Drop are mutually exclusive), so it contributes no drop glue when it appears as a tuple element, array element, or enum payload — even though a bare type-param otherwise defers its drop decision to monomorphization.

**Source:** `src/compiler/sema.cpp#L2784-L2802`

### `type.drop.move-closure-captures` — Captures moved into a move-closure still drop, at the closure binding's slot

A variable moved into a `move` closure remains use-after-move-checked, but its destructor still runs (the closure only borrows its storage): such captures drop at their owning closure binding's slot, in capture order, even if the binding's own drop was skipped — same-frame owners only. A `return` inside a closure body drops only the closure's own frames, never the enclosing function's captured locals.

**Source:** `src/compiler/sema.cpp#L3205-L3240`, `src/compiler/sema.cpp#L3254-L3258`

### `type.drop.moved-out-fields-skipped` — Partially-moved fields are excluded from a value's drop

When a local is dropped, fields (at any depth) that were moved out of it are excluded from its destructor: an exact field-path match skips that field, while a deeper moved path recurses and still drops the field's non-moved siblings.

**Source:** `src/compiler/sema.cpp#L3181-L3202`

### `type.drop.no-auto-drop-suppresses-fields` — #[no_auto_drop] suppresses field destructors

A struct marked `#[no_auto_drop]` (the `ManuallyDrop<T>` lang-item shape) is treated as having no droppable fields: the compiler does not run its inner field destructors at scope exit.

**Source:** `src/compiler/sema.cpp#L2856-L2859`

### `type.drop.no-self-recursion` — self of a Drop body is not auto-dropped

The `self` parameter of a `Drop::drop` method is not auto-dropped at the end of that method's body — calling drop on `self` from inside its own drop body would be infinite recursion. Detected when the resolved drop fn equals the function currently being lowered (modulo package prefix and overload-disambiguation suffix).

**Source:** `src/compiler/sema.cpp#L3157-L3180`

### `type.drop.order-reverse-declaration` — Locals drop in reverse declaration order at scope exit

At scope exit, a frame's live (non-moved) locals are dropped in reverse of declaration order. Drops respect early-exit edges: `return` collects drops across enclosing frames up to (and not across) a closure boundary; `break`/`continue` collects drops up to and including the loop-body frame, stopping at a loop or closure boundary.

**Source:** `src/compiler/sema.cpp#L3213-L3273`

### `type.drop.receiver-shapes` — Drop method accepted by-value or by-reference receiver

The drop method for type `T` is matched whether its single parameter is `T` by value, `&T`, or `&mut T` (`fn drop(&mut self)` / `fn drop(&self)` are the canonical stdlib shapes); the by-reference forms are accepted by peeling one reference level. A generic `impl<T> Drop for Foo<T>` is matched against a concrete `Foo<C>` by struct base-name (re-mangled to the concrete name at monomorphization).

**Source:** `src/compiler/sema.cpp#L2742-L2780`

### `type.drop.same-package-impl` — Drop impl must belong to the same package as the type

A candidate `Drop` impl is selected for type `t` only if its target type belongs to the same package as `t` (an empty package on either side acts as a wildcard). Two distinct types sharing a bare concrete name across packages do not borrow each other's Drop impl.

**Source:** `src/compiler/sema.cpp#L2720-L2731`, `src/compiler/sema.cpp#L2778`

### `type.drop.transitive-aggregate-droppable` — Aggregate types are droppable if any owned member is

A type owns drop responsibility for its members: an array `[T;N]` is droppable iff `T` is; a tuple is droppable iff any element is; an enum is droppable iff any variant's payload field is (generic payloads concretized through the enum's type-params); a struct is droppable iff it has a drop fn or any field is (transitively) droppable. Owning `Box<dyn Trait>`, owning `Box<[T]>`, and owning `Box<Foo>` custom-DST are always droppable; their borrowed (`&dyn`, `&[T]`) counterparts are not.

**Source:** `src/compiler/sema.cpp#L2804-L2864`

## Dyn

### `coerce.dyn.arg-to-trait-object` — Implicit unsize coercion of a concrete argument to a `dyn Trait` formal

When a formal is a trait object (bare or behind `&`/`&mut`) and the argument is not already compatible, the argument coerces by: (a) CoerceUnsized of a smart-pointer/wrapper struct (`Rc<A>` → `Rc<dyn Tr>`) rebuilt by unsizing the inner field; or (b) cast to the dyn type when the argument's (ref) type satisfies the target trait object.

**Source:** `src/compiler/sema_expr.cpp#L12996-L13014`

### `coerce.dyn.upcast-to-supertrait` — Implicit `dyn Sub` → `dyn Super` upcast

An argument of trait-object type `dyn Sub` (bare or behind `&`/`&mut`) implicitly coerces (via cast) to a distinct formal trait-object type `dyn Super` iff Super is a transitive supertrait of Sub (Sub != Super). Identical dyn types do not coerce.

**Source:** `src/compiler/sema_expr.cpp#L12958-L12993`

### `type.dyn.auto-trait-and-lifetime-bounds` — dyn auto-trait and lifetime bounds

In `dyn Trait<T,...> + Send + Sync + 'a`, type-args drive the TraitObject's type_args; `+ Send`/`+ Sync` set marker bits on the object; lifetime bounds (`+ 'a`, LIFETIME_PARAM) are recorded but not enforced and are excluded from the trait dispatch identity.

**Source:** `src/compiler/sema.cpp#L5966-L5998`, `src/compiler/sema.cpp#L6013-L6018`

### `type.dyn.fn-family-is-closure` — dyn Fn/FnMut/FnOnce resolves to Closure

`dyn Fn(P...) -> R`, `dyn FnMut(...)`, `dyn FnOnce(...)` resolve directly to the Closure type {fn_ptr, env_ptr}; there is no distinct Fn-trait-object vtable layer.

**Divergence:** A10

**Source:** `src/compiler/sema.cpp#L5928-L5952`

### `type.dyn.object-safety-required` — Forming &dyn Trait requires object safety

Forming a fat `&dyn Trait` (non-unsized-ok context) requires Trait to be object-safe (dyn-compatible); a non-object-safe trait is rejected at type resolution.

**Source:** `src/compiler/sema.cpp#L6009-L6012`

### `type.dyn.trait-object` — dyn trait-object type

`dyn Trait`, `&dyn Trait`, `&mut dyn Trait` (optionally lifetime-annotated and HRTB-quantified `for<'a>`) form trait objects (fat pointer: data + vtable). Type args use `<...>`; Fn-family `dyn Fn(args)[-> R]` puts args in PARAMS/return in RET_TYPE. Trailing `+ Ident` (auto-trait, e.g. Send/Sync) and `+ 'lt` (lifetime) bounds are accepted; auto-trait bounds are enforced at unsize coercion, lifetime bounds are recorded but not enforced.

```logos
&dyn Display + Send
```

```logos
Box<dyn for<'a> Fn(&'a i32) -> i32>
```

**Source:** `tools/peg_gen/grammars/logos.peg#L1496-L1616`

### `type.dyn.unknown-trait-error` — dyn over an unknown trait is an error

`dyn Trait` where Trait is not a registered trait (and not an Fn-family name) is a hard error (`unknown trait '...' in &dyn type`).

**Uncertainty:** Bare-name lookup: a package-local trait shadowed by a prelude trait of the same name resolves to the prelude trait (known gap, dyn-local-trait-shadowing).

**Source:** `src/compiler/sema.cpp#L5964-L5965`

### `type.dyn.unsized-vs-fat` — Bare dyn unsized vs fat trait object

In an unsized-ok context (turbofish for `T: ?Sized`), bare `dyn Trait` resolves to the unsized-dyn form; otherwise it resolves to the fat-pointer TraitObject.

**Source:** `src/compiler/sema.cpp#L5999-L6018`

## Enum Lit

### `type.enum-lit.type-bounds-checked` — Generic enum type args are bound-checked

The inferred type arguments of a generic enum literal are checked against the enum's type-parameter bounds.

**Source:** `src/compiler/sema_expr.cpp#L12145`

## Equal

### `type.equal.uid-identity` — Type equality is interned-UID identity

types_equal(a,b) holds iff a and b are interned in the same type pool and share the same type-UID. Distinct UIDs are never equal even when structurally similar (e.g. Vec<i32> vs Vec<_>, two same-signature FnItems). Null on either side is unequal.

**Source:** `src/compiler/sema.cpp#L1355-L1362`

## Field

### `type.field.placeholder-type-rejected` — Field types may not be inferred

Struct/datatype field types are item signatures: the inference placeholder `_` is rejected at field-type resolution (E0121).

**Source:** `src/compiler/sema_collect.cpp#L4062-L4067`

## Field Align

### `layout.field-align.unsized-tail` — Alignment of unsized tail fields

Field alignment is min(byte_size,8) for sized fields (treating size-0 as align 1); an unsized `[T]` slice tail aligns to min(sizeof(T),8); an unsized `dyn` tail aligns to 8 (pointer width).

**Source:** `src/compiler/sema_expr.cpp#L17666-L17675`

## Fn Ptr

### `type.fn-ptr.abi-identity` — Function-pointer type and ABI identity

`fn(P...) -> R` resolves to a single-pointer FnPtr; an `extern "ABI"` prefix is part of the type identity. Accepted ABIs are `C`/`C-unwind`/`system`/`Rust`; default and `"Rust"` normalize to the same identity, a foreign ABI is tagged, any other ABI string is a hard error. Return type defaults to void.

**Source:** `src/compiler/sema.cpp#L6084-L6125`

### `type.fn-ptr.type` — Function-pointer type

`fn(T1,T2) -> R` is a bare function-pointer type. Qualifiers/prefixes are accepted: `unsafe fn(...)` (IS_UNSAFE), `extern "ABI" fn(...)` (ABI threaded to the calling convention), and `for<'a> fn(...)` (HRTB binders captured for future region inference).

```logos
extern "C" fn(i32) -> i32
```

**Source:** `tools/peg_gen/grammars/logos.peg#L1666-L1715`

## Fnptr

### `type.fnptr.methods-emit-non-generic` — Function-pointer impl methods emit once, non-generic

A function-pointer impl target (`$fnptr$...`) is type-erased to a uniform pointer, so its impl methods are emitted once as non-generic functions; impl-level type parameters are cleared so no never-instantiated generic template is produced.

**Source:** `src/compiler/sema_decl.cpp#L2183-L2187`

## Generic Inst

### `type.generic-inst.arity-and-bounds` — Generic instantiation arity and bound checks

After default-filling, the type-argument count must match the struct/enum/datatype declared type-param count, and each argument must satisfy its param's trait bounds.

**Source:** `src/compiler/sema.cpp#L5636-L5654`

### `type.generic-inst.box-slice-dst-collapse` — Box<[T]> and Box<DST-struct> collapse to owning fat references

`Box<[T]>` collapses to an owning fat slice {data, len} (same layout as `&[T]`, move-only, droppable). `Box<Foo>` where Foo is a custom-DST tail-slice struct collapses to an owning DstRef {data, len}.

**Uncertainty:** Logos custom-DST machinery; analogous to Rust CoerceUnsized.

**Source:** `src/compiler/sema.cpp#L5478-L5501`

### `type.generic-inst.default-type-args` — Trailing default type arguments

When fewer type-args are supplied than the generic has params, trailing params are filled from their declared defaults (`struct S<T, U = i64>`: `S<A>` ≡ `S<A, i64>`); a default may reference an earlier param and is substituted with the already-bound args.

**Source:** `src/compiler/sema.cpp#L5588-L5604`

### `type.generic-inst.generic-const` — Generic compile-time const instantiation

Applying type-args to a generic const `pub const X<T..>: WritStatic = @{...}` re-evaluates the const's value AST under the supplied type-arg bindings, yielding a fresh per-instantiation WStaticLit identity. The argument count must equal the const's type-param count.

**Uncertainty:** Logos-specific WritStatic generic const.

**Source:** `src/compiler/sema.cpp#L5345-L5392`

### `type.generic-inst.generic-type-alias` — Generic type alias instantiation

A generic type alias `type Foo<T> = Bar<T>` instantiated as `Foo<A>` resolves to its RHS with type- and lifetime-args substituted; the supplied type-arg count and lifetime-arg count must equal the alias's declared type-param and lifetime-param counts respectively.

**Source:** `src/compiler/sema.cpp#L5394-L5429`

### `type.generic-inst.kind-disambiguation-local-shadow` — Local declaration shadows imported same-named type

When a name resolves to multiple kinds (struct/datatype/enum) across packages, a declaration local to the current package wins over any non-local same-named declaration of another kind.

**Source:** `src/compiler/sema.cpp#L5505-L5526`

### `type.generic-inst.smart-pointer-dyn-collapse` — Box<dyn Trait> collapses to an owning trait object

`Box<dyn Trait>` (FQN-gated to the stdlib Box) collapses to an owning fat-pair trait object {data, vtable} tagged Box. Rc/Arc no longer collapse and instead resolve as ordinary generic structs whose inner pointer is a fat DST reference.

**Uncertainty:** Mirrors Rust owned_box + CoerceUnsized lang item; Rc/Arc flip is Logos-specific.

**Source:** `src/compiler/sema.cpp#L5432-L5477`

### `type.generic-inst.unknown-type-metaprog-defer` — Unknown generic type deferred during metaprog discovery

An unknown generic type name is an error, except during the metaprog discovery pass (before derive hooks emit items), where it silently yields error-type so a later non-metaprog pass can re-resolve once synthesized items exist.

**Source:** `src/compiler/sema.cpp#L5527-L5538`

### `type.generic-inst.unsized-arg-gating` — ?Sized type-param relaxes unsized type-args

A type-argument at a generic param declared `?Sized` (implicit_sized=false) may be a bare unsized type (`[T]`, `dyn Trait`); a type-arg at a `Sized` param must not be unsized. Passing an unsized type, or a `?Sized` outer type-param, to a `Sized` param is a diagnostic.

**Source:** `src/compiler/sema.cpp#L5562-L5586`, `src/compiler/sema.cpp#L5605-L5635`

## If

### `expr.if.fnitem-branches-lub-to-fnptr` — Two fn-item branches join to a fn pointer

When both `if` branches are distinct fn-item values with the same signature, the result type is the corresponding fn-pointer type `fn(params)->ret` (fn-item-to-fn-pointer coercion at the join), since two distinct fn-items are not directly type-compatible.

**Source:** `src/compiler/sema_expr.cpp#L14007-L14027`

### `expr.if.intlit-result-overflow-i64` — Integer-literal if-result widens to i64 on i32 overflow

If an `if` expression's result type is an unresolved integer literal and either branch's literal value exceeds the i32 range, the result type is i64.

**Source:** `src/compiler/sema_expr.cpp#L14038-L14052`

## Impl Method

### `trait.impl-method.unsized-self-seed` — Self seeded for unsized and str impl targets

For an impl whose target is unsized (slice/dyn) or `str`, `Self` is seeded before method lowering: an unsized-slice/unsized-dyn target binds `Self` to that type, and `impl ... for str` binds `Self` to an unsized slice of `u8`, so `self: &Self` / `&Self` / `Self::...` in method bodies resolve.

**Source:** `src/compiler/sema_decl.cpp#L2142-L2145`, `src/compiler/sema_decl.cpp#L2160-L2173`

## Impl Trait

### `type.impl-trait.param` — impl Trait type

`impl Trait`, `impl Trait<args>`, and `impl Fn(args) [-> R]` are accepted in type position; an impl-Trait parameter desugars to a synthetic generic parameter bounded by the same trait (Fn-family args→PARAMS, return→RET_TYPE, generic args→TYPE_PARAMS).

```logos
fn f(x: impl Display) {}
```

**Source:** `tools/peg_gen/grammars/logos.peg#L1395-L1414`

### `type.impl-trait.param-desugar` — impl Trait position semantics

`impl Trait` in parameter position desugars to a fresh implicitly-Sized synthetic generic type-param bounded by Trait (a once-used generic, capturing full bound args); in return position it resolves to the dedicated ImplTrait type.

**Source:** `src/compiler/sema.cpp#L6041-L6065`

### `type.impl-trait.param-position-forbidden` — `impl Trait` not allowed at parameter position

`impl Trait` is not supported in parameter position; use an explicit generic `fn f<T: Trait>(x: T)` or `&dyn Trait` instead.

**Divergence:** Logos restriction: Rust supports argument-position impl Trait (APIT).

**Source:** `src/compiler/sema_decl.cpp#L309-L318`

## Inhabited

### `type.inhabited.enum` — Enum inhabitedness

An enum is uninhabited iff it has zero variants, or every variant has at least one uninhabited payload type; it is inhabited as soon as one variant is constructable (all its payload types inhabited).

**Source:** `src/compiler/sema.cpp#L4363-L4376`

### `type.inhabited.never-uninhabited` — The Never type is uninhabited

The Never type `!` is uninhabited.

**Source:** `src/compiler/sema.cpp#L4357-L4358`

### `type.inhabited.ref-conservative` — References to uninhabited types are treated as inhabited

A reference or pointer to an uninhabited type is conservatively treated as inhabited (only value-carrying composites are marked uninhabited).

**Divergence:** Rust treats `&!` as uninhabited; Logos stays conservative and treats `&Never` as inhabited.

**Source:** `src/compiler/sema.cpp#L4359-L4362`

### `type.inhabited.struct-tuple-array` — Composite inhabitedness

A struct is uninhabited iff any field type is uninhabited; a tuple iff any element type is uninhabited; an array `[T; N]` iff N > 0 and T is uninhabited (zero-length arrays are always inhabited).

**Source:** `src/compiler/sema.cpp#L4377-L4392`

## Intlit

### `coerce.intlit.to-integer-typevar-float` — Integer literal coerces to any integer, TypeVar, or float

An IntLit is compatible with any integer-kind target, with a TypeVar, and with F32/F64. A FloatLit is compatible with F32/F64 and TypeVar.

**Source:** `src/compiler/sema.cpp#L1902-L1908`

## Let

### `coerce.let.impl-trait-uses-concrete` — impl Trait let annotation adopts the concrete RHS type

When the annotation is `impl Trait`, the binding's type is the concrete RHS type (so inherent/method calls resolve), rather than the abstract impl-Trait type.

**Source:** `src/compiler/sema_stmt.cpp#L2004-L2007`, `src/compiler/sema_stmt.cpp#L2179-L2183`

### `coerce.let.implicit-int-widening` — Implicit safe integer widening at let-init

At a let-init coercion site, a concrete (non-IntLit, non-enum) integer RHS whose type can safely widen to the annotated integer type is implicitly widened (e.g. u32→i64, i32→i64, u8→u32) without an explicit `as`.

**Divergence:** Rust requires an explicit `as` cast for any integer width change; Logos performs implicit safe widening.

**Source:** `src/compiler/sema_stmt.cpp#L2054-L2061`

### `coerce.let.literal-retype-to-float` — Numeric literal RHS retyped to float annotation

A FloatLit RHS is retyped to an `f32`/`f64` annotation; an IntLit RHS under a float annotation becomes a float literal (simple literal) or an `as`-cast to float (non-literal IntLit expression).

**Source:** `src/compiler/sema_stmt.cpp#L2062-L2082`

### `coerce.let.reborrow-mut-at-ascription` — Type-ascription let reborrows &mut RHS

A type-ascribed let `let _: T = rhs` is a coercion site: when rhs is `&mut U` and the annotation is a reference/pointer kind (`&mut`, `&`, `*`), the RHS is implicitly reborrowed, so the original `&mut` is restored after the binding's last use (NLL).

**Source:** `src/compiler/sema_stmt.cpp#L1991-L2003`

### `coerce.let.unsize-and-decays` — Implicit coercions at let-init when RHS type differs from annotation

When the RHS type is not directly compatible with the annotation, the binding applies, in order: CoerceUnsized for smart-pointer structs (`Rc<A>` → `Rc<dyn Tr>`); `&mut [T;N]` → `&mut [T]` array-ref-to-slice decay; non-capturing closure literal → `fn(..)->T`. If none apply (and not impl-Trait / not ExprBlob) a type-mismatch error is reported.

**Source:** `src/compiler/sema_stmt.cpp#L1991-L2044`

### `type.let.floatlit-default-f64` — Unannotated float literal binding defaults to f64

An unannotated let whose RHS is a float literal binds at type f64.

**Source:** `src/compiler/sema_stmt.cpp#L2203-L2207`

### `type.let.intlit-default-i32` — Unannotated integer literal binding defaults to i32 (i64 on overflow)

An unannotated let whose RHS is an integer literal binds at type i32, upgraded to i64 when the literal value falls outside the i32 range.

**Divergence:** Rust defaults unconstrained integer literals to i32 but never silently widens to i64 on overflow (it is a compile error); Logos auto-upgrades to i64.

**Source:** `src/compiler/sema_stmt.cpp#L2191-L2202`

## Lit Int

### `type.lit-int.const-generic-arg` — Integer literal as type

An integer literal in type position resolves to an IntLit type carrying the (optionally negated) parsed value, for use as a const-generic argument.

**Source:** `src/compiler/sema.cpp#L6127-L6138`

## Method

### `type.method.recv-autoderef-resolution` — Receiver dereferenced for method resolution

For method resolution and struct-type-arg extraction, a receiver of reference type (`&`/`&mut`) or raw-pointer type is dereferenced to its pointee.

**Source:** `src/compiler/sema_expr.cpp#L8742-L8749`, `src/compiler/sema_expr.cpp#L8805-L8810`, `src/compiler/sema_expr.cpp#L8988-L8989`

### `type.method.return-subst` — Method return type substitution

The type of a method-call expression is the method's declared return type with the receiver/method type-var substitution and lifetime substitution applied.

**Source:** `src/compiler/sema_expr.cpp#L9102-L9105`, `src/compiler/sema_expr.cpp#L9143`

## Method Arg

### `coerce.method-arg.pipeline` — Method argument coercions

Each method argument is coerced to its (substituted) parameter type via the canonical coercion pipeline supporting closure-to-fn-ptr, `&Concrete`-to-`&dyn Trait` unsize, implicit reborrow, and integer widening, with widening applied last.

**Source:** `src/compiler/sema_expr.cpp#L8873-L8887`

## Move

### `type.move.enum-droppable-payload` — Enum is a move type iff droppable

An enum is a move type iff it has a user `impl Drop` or carries a droppable payload field; a C-like enum or one whose payloads are all Copy is non-move.

**Source:** `src/compiler/sema.cpp#L2676-L2680`

### `type.move.owning-heap-pointers` — Owning heap pointers are move types

Owning heap-backed types are move types: an owning `Box<dyn Trait>`, an owning `Box<[T]>` slice, and an owning `Box<Foo>` custom-DST each own heap data and are non-Copy, hence move. The corresponding borrowed forms (`&dyn`, `&[T]`) are Copy-like and not move types.

**Source:** `src/compiler/sema.cpp#L2646-L2656`

### `type.move.struct-non-copy` — Struct is a move type unless Copy

A struct-typed value is a move type (its source slot is invalidated on by-value use and dropped on scope exit) unless the struct implements Copy. Copy holds either unconditionally or conditionally (e.g. `impl<P: Copy> Copy for Pin<P>`), the latter requiring every recorded copy-relevant type-argument position to hold a non-move (Copy) type.

**Source:** `src/compiler/sema.cpp#L2630-L2641`, `src/compiler/sema.cpp#L2681-L2685`

### `type.move.typevar-conservative` — Generic type parameter is move unless bounded Copy

A type parameter T is treated as a move type within a generic body unless its bounds include `Copy`, in which case T is provably Copy (Copy and Drop are mutually exclusive) and by-value use of `x: T` does not move. Only an explicit Copy bound makes T non-move; otherwise the conservative move classification holds.

**Source:** `src/compiler/sema.cpp#L2663-L2673`

## Name

### `type.name.inference-placeholder` — `_` placeholder type

`_` resolves to an inferred-type placeholder, but is a hard error (E0121 analog) in item-signature position (fn params/return, const item type) where no inference context exists.

**Source:** `src/compiler/sema.cpp#L6326-L6344`

### `type.name.lookup-namespaces` — Type-name lookup precedence across namespaces

An unqualified type name resolves with precedence: primitive keyword > in-scope generic type parameter > type alias > struct > datatype > enum; the first match wins. An unresolved name yields no type.

**Source:** `src/compiler/sema.cpp#L2530-L2620`

### `type.name.lookup-or-error` — Named-type resolution and unknown-type diagnostics

A type name resolves via name lookup; if not found it is a hard error (`unknown type`), specialized to `generic type alias requires type arguments` when the name is a parameterized alias used without args. In metaprog discovery mode unknown names resolve silently to error_t (may be synthesised by a later hook).

**Source:** `src/compiler/sema.cpp#L6349-L6366`

### `type.name.qualified-by-last-segment` — Qualified type path resolves by its last segment

A fully-qualified type `pkg.path.Type` is resolved by the final path segment alone; the package prefix is dropped.

**Source:** `src/compiler/sema.cpp#L6312-L6325`

### `type.name.resolution-order` — Type-name resolution precedence

A bare type name resolves in order: (1) an in-scope type parameter wins over all global lookups; (2) built-in primitive (i8..i128/u8..u128, i24/u24/i56/u56, usize/isize, f32/f64, bool, char, void); (3) a non-generic type alias (generic aliases are deferred to use-site); (4) a struct, then datatype, then enum of that name. Unresolved names yield no type.

**Source:** `src/compiler/sema_collect.cpp#L4132-L4178`

### `type.name.self-typevar` — Self resolves to the bound Self type parameter

`Self` resolves to the current `Self` type-param binding when one is in scope.

**Source:** `src/compiler/sema.cpp#L6345-L6348`

## Pack Expand

### `type.pack-expand.in-scope-typevar` — Pack expansion in type-arg position

`T...` in type-arg position resolves to the in-scope variadic type parameter's TypeVar; an undefined pack name is a hard error.

**Source:** `src/compiler/sema.cpp#L6299-L6310`

## Param

### `type.param.unit-type-forbidden` — Unit-typed parameters forbidden

A function parameter may not have the unit type `()`; a unit-typed parameter carries no information and is ill-formed.

**Divergence:** Logos restriction: Rust permits `()`-typed parameters.

**Source:** `src/compiler/sema_decl.cpp#L303-L308`

## Primitive Method

### `type.primitive-method.mangled-lookup` — Primitive-receiver methods resolved via TypeName__method with receiver-shape variants

For a receiver with no struct name, the method is looked up as `<type-name>__<method>` matched against the actual argument signature; if no direct match, receiver-shape variants are tried in order: `&T`, `&mut T`, `*const T`, `*mut T`, and (for reference receivers) the `$ref_<...>` / `$mut_ref_<...>` mangling used to register `impl Trait for &T` / `&mut T`.

**Source:** `src/compiler/sema_expr.cpp#L8089-L8130`

## Reborrow

### `coerce.reborrow.downgrade-mut-to-shared` — Downgrading reborrow `&mut T` → `&T` gated on allow_downgrade

When the formal is `&T` (shared), a `&mut T` argument may be implicitly reborrowed as a shared `&T` only when downgrade is permitted (fn-arg coercion). At method-receiver position downgrade is forbidden, because a formal `&Self` whose Self IS a `&mut X` (impl on a ref type) would otherwise dispatch through the wrong impl key. For a `*U` formal, dest mutability follows the formal pointer's mutability bit.

**Source:** `src/compiler/sema_expr.cpp#L12926-L12939`

### `coerce.reborrow.mut-place-at-coercion-site` — Implicit reborrow of `&mut T` place at call/method argument sites

At an argument coercion site, an expression of type `&mut T` that is a PLACE (VarRef, FieldRead, or IndexRead) and whose formal parameter is ref-shaped — `&mut U`, `&U`, or `*U` — is implicitly reborrowed as `AddrOfTemp(Deref(e))` rather than moved, registering a borrow on the original `&mut T` binding instead of consuming it. Reborrow is structural: the result has the SAME pointee type as the source; the genuine argument type-check runs afterward.

```logos
fn f(x: &mut T) { g(x); h(x); }  // x reborrowed, not moved
```

**Source:** `src/compiler/sema_expr.cpp#L12924-L12955`

### `coerce.reborrow.no-reborrow-of-fresh-borrow` — No reborrow of a fresh borrow expression

Implicit reborrow applies only when the `&mut T` operand is a place expression (VarRef / FieldRead / IndexRead). A fresh borrow expression (e.g. `&mut x`, `&mut p.f`) is left as-is and never wrapped in a reborrow shape, so its borrow is recorded through the normal path.

**Source:** `src/compiler/sema_expr.cpp#L12947-L12951`

## Rec

### `type.rec.no-by-value-cycle` — recursive by-value type cycles are rejected

A struct/enum graph that contains a by-value (non-indirected) cycle is an error; recursion through a type of statically unknown/infinite size must be broken by an indirection (e.g. a pointer/box).

**Source:** `src/compiler/sema_collect.cpp#L544-L546`

## Rel Ptr

### `coerce.rel-ptr.pointer-compatibility` — #[rel_ptr] struct / GAT pointer ↔ raw pointer compatibility

A concrete `#[rel_ptr]` struct `RP<U>` is pointer-compatible with `*U`/`&U`/`&mut U` (pointee type-equal to its type-arg); a type-erased rel_ptr (no type-arg) is compatible only with a thin `*u8` pointer. An abstract GAT projection `Z::Ptr<U>` (assoc base a type-var) is compatible with a raw pointer iff its GAT arg equals the pointee. Compatibility is symmetric.

**Source:** `src/compiler/sema.cpp#L3793-L3829`

## Return

### `coerce.return.closure-to-fnptr` — Non-capturing closure coerces to fn-ptr at return

A non-capturing closure literal returned where a fn-value type is expected coerces to that fn-pointer type (same coercion as let-annotation and call-arg sites).

**Source:** `src/compiler/sema_stmt.cpp#L2819-L2825`

### `coerce.return.float-lit` — Float-literal return retyped to return type

A float-literal return value is retyped to the concrete f32/f64 return type when the return type is a float; otherwise it defaults to f64.

**Source:** `src/compiler/sema_stmt.cpp#L2852-L2857`

### `coerce.return.unsize-struct` — CoerceUnsized applied implicitly at return

A return value whose type can be unsized to the return type (e.g. `Rc<T>` → `Rc<dyn Tr>`) is implicitly coerced by rebuilding the smart-pointer struct, without an explicit `as`.

**Source:** `src/compiler/sema_stmt.cpp#L2826-L2829`

### `type.return.datanode-by-value-forbidden` — DataNode eidos cannot be returned by value

A non-plain zoned-struct DataNode type (`#[data]` node) cannot be a by-value return type; the function must return `DataRef<T>` instead. The check looks through array nesting to the innermost element.

**Source:** `src/compiler/sema_decl.cpp#L479-L500`

### `type.return.non-movable-by-value-forbidden` — Location-anchored types cannot be returned by value

A type that is non-movable — containing a self-relative `#[rel_ptr]` field, or being `#[pinned]` — may not be returned by value; return a pointer (`*mut T` / `&T`) into its zone segment instead. (Crossing a function boundary by value would invalidate the self-relative anchor.)

**Divergence:** A8

**Source:** `src/compiler/sema_decl.cpp#L501-L513`

## Self

### `type.self.impl-binding-precedence` — `Self` resolves to the enclosing impl's type

Within a method body, `Self` denotes the impl's target type; when the impl fixed `Self = Foo<T>` with type-args, a bare same-named `Self` from an unrelated impl is treated as stale and replaced. A datatype binding for `Self` takes precedence over a struct binding when both names exist; a primitive target binds `Self` to that primitive.

**Uncertainty:** Precedence/staleness handling inferred from impl-context heuristics; observable effect is Self resolution.

**Source:** `src/compiler/sema_decl.cpp#L207-L242`

### `type.self.implicit-self-param-ref` — Bare `self` parameter is `&Self` / `&mut Self`

A method parameter written as `self` (no explicit type) has type `&Self` by default, or `&mut Self` when marked mutable.

**Source:** `src/compiler/sema_decl.cpp#L321-L328`

## Tagged

### `type.tagged.thin-pointer` — tagged thin pointer type

`&tagged<T> Name` is a thin tag-dispatched pointer: a type_code tag is stored in memory before the object, and call sites read the tag, look up the dispatch table, and call indirectly.

**Divergence:** A6

**Source:** `tools/peg_gen/grammars/logos.peg#L1490-L1494`

### `type.tagged.thin-ptr-dispatch` — &tagged<TS> Trait

`&tagged<TS> Trait` resolves to a thin TaggedPtr with tag-based dispatch; Trait must be a registered trait and TS must resolve to a concrete struct type, else hard error.

**Divergence:** Logos-only tagged-dispatch pointer.

**Source:** `src/compiler/sema.cpp#L6021-L6039`

## Taggedptr

### `coerce.taggedptr.from-raw-ptr` — Raw pointer coerces to a tagged trait pointer

Any raw Ptr coerces to a TaggedPtr target (&tagged<TS> Trait is a thin pointer to a tagged object; the tag is read at dispatch time).

**Source:** `src/compiler/sema.cpp#L2062-L2066`

## Type Code

### `layout.type-code.auto-hash-assign` — Concrete datatype auto type code from name hash

A concrete (non-generic) zoned datatype/struct with no explicit type code is auto-assigned a 56-bit code derived from the hash of its canonical name `pkg::Name`; codes below 128 are shifted into the >=128 range to avoid the reserved inline-AnyVal range 1..127. Generic templates receive their code at instantiation time.

**Divergence:** Logos addition: type codes have no Rust analog.

**Source:** `src/compiler/sema.cpp#L7649-L7658`, `src/compiler/sema.cpp#L7770-L7781`

## Typeof

### `type.typeof.expr` — typeof type

`typeof(expr)` is the compile-time type of expr; the expression is not evaluated.

**Divergence:** A6

**Source:** `tools/peg_gen/grammars/logos.peg#L1461-L1463`

### `type.typeof.expr-type-no-eval` — typeof(expr) yields the sema type without evaluation

`typeof(expr)` resolves to the sema-computed type of `expr`; the expression is type-checked but never evaluated at runtime.

**Divergence:** Logos addition: Rust has no `typeof` operator.

**Source:** `src/compiler/sema.cpp#L5673-L5681`

## Unsized

### `type.unsized.by-value-rejected` — Unsized type by value is an error

A bare unsized slice `[T]` in a value position (param/return/field/alias/local) is a hard error unless an explicit unsized-ok context (e.g. a turbofish arg for a `T: ?Sized` parameter) is active; it must be wrapped in `&[T]`/`*const [T]`/`*mut [T]`.

**Source:** `src/compiler/sema.cpp#L5870-L5894`, `src/compiler/sema.cpp#L5999-L6008`

## Writ

### `coerce.writ.mapslice-to-typed-map` — MapSlice as <K,AnyVal>{} builds a typed Writ map

`src as <K,V>{}` (target struct WritMap) is permitted only for V = AnyVal and K in {I32,U32,I64,U64}, with source the matching MapSlice<K> struct; it lowers to a stdlib writ_build_map_<k>_anyval call returning Rc<Writ>. Any other key/value combination, a mismatched source, or a missing builder is an error.

**Source:** `src/compiler/sema_expr.cpp#L775-L838`

### `coerce.writ.slice-to-typed-array` — &[T] as <T>[] builds a typed Writ array

`src as <T>[]` (target struct WritArr) requires `src: &[T]` (a Slice) whose element kind equals the target element kind; element T must be one of i8/u8/i16/u16/i32/u32/i64/u64/f32/f64. It lowers to a stdlib writ_build_array_<T> call returning the builder's Rc<Writ> type; missing builder (no `use logos.lang.writ.typed_arr`) or non-slice source or element mismatch or unsupported element is an error.

**Source:** `src/compiler/sema_expr.cpp#L716-L772`

### `type.writ.lit-and-array-map` — Writ literal / typed array / typed map types

`@{...}` at type position is a WritStatic value literal type (LIT_WSTATIC). `<Elem>[]` is a Writ typed-array type and `<K[,V]>{}` is a Writ typed-map type (used in `as` casts).

**Divergence:** A6

**Source:** `tools/peg_gen/grammars/logos.peg#L1451-L1473`

## Writ Anyval

### `coerce.writ-anyval.scalar-helpers` — Implicit coercion of comprehension element to AnyVal

Inside a Writ comprehension element/value, the value is coerced to AnyVal: WAny and legacy AnyVal struct values pass through unchanged; bool/i8/i16/i32/IntLit/u8/u16/u32 are wrapped via the matching `writ_coerce_<ty>` helper; `str` (`&[u8]`) is wrapped via `writ_coerce_str` (taking `&ctr` first). Any other type is rejected with a message to cast explicitly or wrap with AnyVal::embed_*.

**Divergence:** Logos-specific Writ value model.

**Source:** `src/compiler/sema_expr.cpp#L11382-L11458`

### `coerce.writ-anyval.wide-int-no-implicit` — Wide integers not implicitly coerced to AnyVal

i64/u64/i24/u24/i56/u56/i128/u128 are intentionally NOT auto-coerced to AnyVal (implicit i32 embedding would silently truncate); the user must cast explicitly (`x as i32`) or wrap with WAny::from.

**Divergence:** Logos-specific anti-truncation rule.

**Source:** `src/compiler/sema_expr.cpp#L11418-L11427`, `src/compiler/sema_expr.cpp#L11430-L11436`

## Writ Arr

### `type.writ-arr.elem-set` — Writ typed array type <Elem>[]

`<Elem>[]` resolves to a generic struct `WritArr<elem>`; Elem must be one of I8/U8/I16/U16/I32/U32/I64/U64/F32/F64 (mapped to the Logos primitive), else hard error.

**Divergence:** Logos-only Writ container type-expression.

**Source:** `src/compiler/sema.cpp#L6234-L6266`

## Writ Map

### `type.writ-map.key-val-set` — Writ typed map type <K,V>{}

`<K,V>{}` resolves to `WritMap<key,val>`; key must be I32/U32/I64/U64 and value must be `AnyVal` (default), else hard error.

**Divergence:** Logos-only Writ container type-expression.

**Source:** `src/compiler/sema.cpp#L6267-L6297`

## Wstatic

### `type.wstatic.literal-arg` — WritStatic literal in type-arg position

A WritStatic literal `Foo::<@{...}>` (or a bare writ-lit value-AST in const recognition) resolves to the value's WritStatic type; a missing payload is a hard error.

**Divergence:** Logos-only WritStatic value-as-type-arg.

**Source:** `src/compiler/sema.cpp#L6370-L6386`
