# Types, Layout & Coercion

_Normative rules for the Logos type system, extracted from the compiler (grammar, sema, mono, mlir-gen, layout passes). Scope: domains `type`, `layout`, `coerce`. Rules are grouped under `## <domain> · <group>` headings keyed on the middle segment of each rule id; every `###` heading is a rule's permanent linkable id. Auto-assembled from `tools/spec-extract/rules/`._


---

## Types

What types exist, how their identity / equality / subtyping is decided, and the marker properties (Copy / Move / Drop / Sized / inhabited) the compiler derives from each type.


## Types · `primitive`

### `type.primitive.set` — Built-in primitive scalar types

The language has primitive scalar types: void, bool, char, the floats f32/f64, and the integers i8/u8, i16/u16, i24/u24, i32/u32, i56/u56, i64/u64, i128/u128, isize/usize. Each is a distinct type identified by its keyword name.

**Divergence:** A: extra fixed-width widths i24/u24/i56/u56 and 128-bit i128/u128 beyond Rust's standard set.

**Source:** `src/compiler/sema.cpp#L2077-L2097`, `src/compiler/sema.cpp#L2530-L2551`


## Types · `integer`

### `type.integer.bit-width` — Integer bit-width and signedness

Each concrete integer kind has a fixed bit width and signedness: i8/u8=8, i16/u16=16, i24/u24=24, i32/u32=32, i56/u56=56, i64/u64=64, i128/u128=128; signed forms are signed, unsigned forms unsigned. usize/isize have width equal to the target pointer width (isize signed, usize unsigned). IntLit, Enum, and non-integers have no defined rank (width 0).

**Divergence:** usize/isize width is target-dependent (pointer bits) as in Rust; the exotic 24/56-bit widths are a Logos addition.

**Source:** `src/compiler/sema_impl.hpp#L4453-L4474`

### `type.integer.kind-set` — Integer-class type kinds

The integer type class comprises the fixed-width signed/unsigned kinds {i8,u8,i16,u16,i24,u24,i32,u32,i56,u56,i64,u64,i128,u128}, the pointer-sized {usize,isize}, the unsuffixed-literal type IntLit, and Enum. An enum type is treated as an integer kind for these classifications.

**Divergence:** Logos adds non-power-of-two integer widths i24/u24/i56/u56 (not in Rust); also classifies Enum as an integer kind.

**Note:** Whether Enum membership here reflects a general language rule or only this classifier's use sites is not determinable from this unit alone.

**Source:** `src/compiler/sema_impl.hpp#L4439-L4449`


## Types · `char`

### `type.char.is-u32` — `char` is a 4-byte scalar

`char` lowers to a 32-bit integer SSA type and has layout {4,4} (size 4, align 4) — grouped with i32/u32/f32, not with the 1-byte types.

**Source:** `src/compiler/mlir_gen_types.cpp#L63`, `src/compiler/mlir_gen_types.cpp#L459-L460`


## Types · `numeric`

### `type.numeric.classification` — Numeric-type classification includes unbound TypeVar and deferred CfgSlotType

A type is classified numeric iff its kind is F64, F32, FloatLit, any integer kind, an unbound TypeVar (provisionally numeric), or CfgSlotType (a cfg-bound slot type deferred to mono, trusted to resolve to a numeric primitive there or fail with a precise mono-time error). `is_integer` holds iff the kind is an integer kind (excludes TypeVar/CfgSlotType/float).

**Source:** `src/compiler/sema_impl.hpp#L3766-L3782`


## Types · `str`

### `type.str.default-fat-slice` — str defaults to `&[u8]` fat-slice shape

The `str` keyword resolves to the fat-pointer slice form `Slice<u8>` (the `&[u8]` shape) by default; in a context that explicitly permits an unsized result (e.g. a `T: ?Sized` turbofish position) it resolves to the unsized `[u8]` form so `&T` routes to the same `Slice<u8>` ABI without double-wrapping.

**Note:** str modeled as u8 slice rather than a distinct str primitive; unsized vs fat-slice choice is context-driven via unsized_ok_.

**Source:** `src/compiler/sema.cpp#L2552-L2561`

### `type.str.slice-alias` — str is an alias for `Slice<u8>`; impls aliased to `&[u8]`

`str` is a built-in that resolves to `Slice<u8>` (printed `&[u8]`); a trait impl whose target is `str` is also registered under target `&[u8]` so trait-satisfaction checks keyed on the printed slice type find the impl.

**Divergence:** Logos models `str` as `Slice<u8>`; Rust `str` is a distinct DST.

**Source:** `src/compiler/sema_collect.cpp#L3777-L3787`


## Types · `tuple`

### `type.tuple.multi` — Tuple type

A tuple type is `(T1, T2, ...)` with ≥2 comma-separated element types (optional trailing comma), or a 1-element tuple `(T,)` requiring the trailing comma.

**Source:** `tools/peg_gen_cpp/grammars/logos.peg#L1732-L1735`, `tools/peg_gen_cpp/grammars/logos.peg#L1792-L1795`

### `type.tuple.unit` — Unit type `()`

`()` denotes the unit type, the empty tuple type; both the dedicated unit-type production and the simple-type `LPAREN RPAREN` fallback yield the same TUPLE_TYPE (no items).

**Source:** `tools/peg_gen_cpp/grammars/logos.peg#L1782-L1783`, `tools/peg_gen_cpp/grammars/logos.peg#L1876-L1877`, `tools/peg_gen_cpp/grammars/logos.peg#L1722-L1723`, `tools/peg_gen_cpp/grammars/logos.peg#L1816-L1817`

### `type.tuple.unit-and-elements` — Tuple type, unit, and variadic pack

`()` (or an empty tuple) resolves to the unit/void type; `(T1,...,Tn)` resolves to a tuple of the element types; `(A...)` resolves to a Tuple of one TypeVar naming the variadic pack.

**Source:** `src/compiler/sema.cpp#L5902-L5926`

### `type.tuple.variadic-arity` — Variadic-arity tuple target `(A...)`

`(A...)` is a variadic-arity tuple type naming pack-typevar A; used as an impl target `impl<A...> Trait for (A...)`. Resolves to a Tuple type with one variadic element naming A.

**Divergence:** A6

**Source:** `tools/peg_gen_cpp/grammars/logos.peg#L1726-L1731`, `tools/peg_gen_cpp/grammars/logos.peg#L1786-L1791`


## Types · `array`

### `type.array.length-forms` — Array type length forms

`[T; N]` length is determined by: a `metacall { expr }` block whose tail integer is CTFE-evaluated; `sizeof...(P)` over an in-scope type-param pack (symbolic `__sizeof_pack:P`); a literal integer; or a symbolic const parameter name. A missing/empty metacall tail or an unknown pack/op is a hard error.

**Divergence:** Array length via `metacall {..}` replaces Rust const-eval at this position (MP-mc-01).

**Source:** `src/compiler/sema.cpp#L6140-L6226`

### `type.array.size-from-metacall` — Array size from metacall block

`[T; metacall { ... }]` computes the array size from a metacall block evaluated at compile time (const array-length is expressed via explicit `metacall`, not implicit const-eval).

**Divergence:** A1

**Source:** `tools/peg_gen_cpp/grammars/logos.peg#L1769-L1770`, `tools/peg_gen_cpp/grammars/logos.peg#L1829-L1830`

### `type.array.size-from-pack` — Array size from variadic pack length

`[T; P...(P)]` sizes the array from a variadic pack's length; lowered to a symbolic array-size-var `__sizeof_pack:P`, resolved at monomorphization.

**Divergence:** A6

**Source:** `tools/peg_gen_cpp/grammars/logos.peg#L1762-L1768`, `tools/peg_gen_cpp/grammars/logos.peg#L1827-L1828`

### `type.array.sized` — Fixed-size array type `[T; N]`

`[T; N]` is a fixed-size array type. N may be an integer literal, an identifier (const generic), a variadic-pack-length form `sizeof...(P)` (lowered to a symbolic arr_size_var resolved at mono), or a `metacall { ... }` block computing the size. All size-bearing alternatives are tried before the unsized-slice fallback `[T]`; PEG alternation is ordered left-to-right so size-bearing forms always match first when a size is present.

**Source:** `tools/peg_gen_cpp/grammars/logos.peg#L1821-L1826`, `tools/peg_gen_cpp/grammars/logos.peg#L1827-L1828`, `tools/peg_gen_cpp/grammars/logos.peg#L1829-L1830`, `tools/peg_gen_cpp/grammars/logos.peg#L1831-L1832`


## Types · `slice`

### `type.slice.ref` — Slice type

`&[T]` and `&mut [T]` are slice types (fat pointer: ptr + len); an explicit lifetime `&'a [T]` / `&'a mut [T]` is accepted (captured but not distinctly enforced).

**Source:** `tools/peg_gen_cpp/grammars/logos.peg#L1618-L1629`

### `type.slice.ref-form` — Slice reference type

`&[T]` and `&mut [T]` are slice types (fat pointer: data pointer + length); each accepts an optional explicit lifetime prefix, `&'a [T]` / `&'a mut [T]`, which sema does not currently enforce distinctly from the unannotated form.

**Source:** `tools/peg_gen_cpp/grammars/logos.peg#L1678-L1689`

### `type.slice.sized-vs-unsized` — Sized slice vs bare unsized slice

A `[T]` written under a reference/pointer (SLICE_TYPE) resolves to a sized fat Slice (mut bit tracked); a bare `[T]` by value (UNSIZED_SLICE_TYPE) resolves to an unsized slice.

**Source:** `src/compiler/sema.cpp#L5863-L5894`

### `type.slice.unsized` — Unsized slice type `[T]`

Bare `[T]` (no size) is the unsized slice type. The size-bearing array forms are tried first, so `[T; N]` always wins over the unsized fallback.

**Source:** `tools/peg_gen_cpp/grammars/logos.peg#L1764-L1774`, `tools/peg_gen_cpp/grammars/logos.peg#L1833-L1834`

### `type.slice.unsized-not-a-value-type` — Bare [T] is unsized and not a value type

The bare slice type `[T]` (UnsizedSlice) is distinct from `&[T]`; it cannot appear as a value type and may occur only behind a reference (where it canonicalises to the borrowed slice form) or as a `T: ?Sized` substitution.

**Source:** `src/compiler/sema_impl.hpp#L650-L657`

### `type.slice.unsized-only-behind-pointer` — Bare [T] is unsized; legal only behind a pointer or as ?Sized subst

A bare `[T]` type expression (UNSIZED_SLICE_TYPE) produces Kind::UnsizedSlice and cannot appear by value; it is only legal behind `&`, `*const`, or `*mut` (where resolve canonicalises it back to SLICE_TYPE / Kind::Slice) or as the concrete type substituted for a `T: ?Sized` parameter.

**Source:** `tools/peg_gen_cpp/grammars/logos.peg#L305`


## Types · `ptr`

### `type.ptr.dst-thin-if-self-describing` — Raw pointer to a DST struct: thin iff self-describing

For a raw pointer whose pointee is an effective-DST struct: if the struct is self-describing (tail metadata recoverable in-band) the pointer stays thin (8B `Ptr<T>`); otherwise it becomes a fat DstRef carrying the tail length.

**Note:** `is_effective_dst`/`self_describing` are per-instance struct properties evaluated outside this unit.

**Source:** `src/compiler/sema.cpp#L5714-L5741`

### `type.ptr.dyn-is-fat` — Raw pointer to bare dyn is a fat trait object

`*const dyn Trait` / `*mut dyn Trait` (immediate `dyn` pointee) canonicalises to the inline fat {data,vtable} TraitObject, identical to `&dyn Trait`'s representation.

**Source:** `src/compiler/sema.cpp#L5703-L5713`

### `type.ptr.modifier-set` — Raw-pointer modifiers

A raw pointer type is written `*const T`, `*mut T`, or `*zoned T`/`*zoned mut T`; any other word after `*` is a hard error (`unknown raw-pointer modifier`).

**Divergence:** `*zoned` is a Logos-only zoned-pointer modifier (F3).

**Source:** `src/compiler/sema.cpp#L5685-L5699`, `src/compiler/sema.cpp#L5741`

### `type.ptr.raw` — Raw pointer type

`*const T` is an immutable raw pointer to `T`; `*mut T` is a mutable raw pointer to `T`.

**Source:** `tools/peg_gen_cpp/grammars/logos.peg#L1806-L1809`

### `type.ptr.raw-slice` — Raw fat-pointer to slice

`*const [T]` and `*mut [T]` are raw fat pointers to a slice, sharing the `{*const T, usize}` ABI of `&[T]` but without borrow-check guarantees. These alternatives must precede the plain pointer/array forms in `ptr_type` so that a bare `[T]` still parses as the unsized-slice type rather than falling through to `arr_type` (which requires `[T; N]` with an explicit size).

**Source:** `tools/peg_gen_cpp/grammars/logos.peg#L1797-L1805`

### `type.ptr.zoned` — Zoned raw pointer `*zoned [mut] T`

`*zoned T` / `*zoned mut T` is a zoned raw pointer (Ref-arm self-relative at rest; deref/assign runs the storage↔compute bridge). `zoned` is a contextual keyword recognized only in pointer position: after `*`, the only words otherwise valid there are the `mut`/`const` keyword tokens, so a bare IDENT there can only be `zoned` (sema validates NAME=="zoned"). It is not reserved globally — `#[zoned]` attributes still parse as IDENT.

**Divergence:** A6 (addition — zone-relative raw pointer; no Rust equivalent; ref-repr-design §6/§8)

**Source:** `tools/peg_gen_cpp/grammars/logos.peg#L1810-L1819`

### `type.ptr.zoned-pointer-distinct` — *zoned T is a distinct pointer type

A zoned raw pointer `*zoned T` is a type distinct from `*T`; the zoned bit participates in type identity (interning, serialization, equality). Deref/assignment through a `*zoned T` runs the zoned storage↔compute bridge rather than a plain load/store.

**Divergence:** Logos addition (F3 ref-repr/zoned types); no Rust equivalent.

**Source:** `src/compiler/sema_impl.hpp#L222-L231`


## Types · `ref`

### `type.ref.borrow` — Reference types

`&T`, `&mut T`, `&'a T`, `&'a mut T` are safe borrow-checked reference types. `&&T` / `&&mut T` (no whitespace, tokenized as AND) denote double-references; arbitrary-depth `& & … T` stacks are accepted at type position.

**Source:** `tools/peg_gen_cpp/grammars/logos.peg#L1631-L1655`

### `type.ref.canonicalize-unsized-pointee` — Reference to bare unsized pointee folds to the fat-pointer form

`&T`/`&mut T` whose immediate pointee is bare `[U]` folds to `Slice<U>` (mut-tracked); bare `dyn Tr` folds to TraitObject; an effective-DST struct folds to DstRef. `&str` (`str` pointee) is treated as `&[u8]` and folds to `Slice<u8>`.

**Related:** `type.ptr.dyn-is-fat`, `type.slice.str-is-byte-slice`

**Source:** `src/compiler/sema.cpp#L5744-L5847`

### `type.ref.dotted-path` — Fully-qualified non-generic type path

A fully-qualified non-generic type in type position is written `pkg.path.Type` (dotted); the last path segment is the type name (QUAL_PARTS holds the prefix). Matched before bare-IDENT alternatives so the whole dotted form is claimed as one type reference. The generic dotted form `pkg.path.Type<A>` is not accepted here (use a `use` import + short name instead).

**Divergence:** A9

**Source:** `tools/peg_gen_cpp/grammars/logos.peg#L1865-L1873`

### `type.ref.double-ref` — Double-reference types

Because the lexer greedily tokenizes `&&` as a single logical-AND token, `&&T` and `&&mut T` (written with no intervening whitespace) are recognized as a special case and desugared by sema to `&(&T)` and `&(&mut T)` respectively. Writing the two tokens with whitespace between them (`& &T`, `& &mut T`) requires no special-casing: it is handled directly by ref_type's ordinary recursive nesting over ref_pointee, which supports arbitrary-depth `& & ... T` reference stacks.

**Source:** `src/compiler/sema.cpp#L5849-L5861`, `tools/peg_gen_cpp/grammars/logos.peg#L1691-L1706`

### `type.ref.double-ref-nesting` — &&T / &&mut T resolve to nested single refs

`&&T` (DOUBLE_REF_TYPE) resolves as a REF_TYPE wrapping a REF_TYPE; `&&mut T` (DOUBLE_REF_MUT_TYPE) resolves as a REF_TYPE wrapping a MUT_REF_TYPE. Both are sema-level rewrites of a single lexed double-`&` token, not a distinct type former.

**Source:** `tools/peg_gen_cpp/grammars/logos.peg#L287-L288`

### `type.ref.form-priority` — type_ref alternative ordering

A type reference is parsed, in PEG-ordered-choice priority: antiquote-type, typeof(...), CFG-slot associated-type, CFG-slot type, Writ array type, Writ map type, pointer, array, slice, tagged, dyn, safe-reference, impl-Trait, unit, never, closure, fn-pointer, tuple, paren, fully-qualified assoc-type, plain assoc-type (`T::Item`), then simple named type. Assoc-type forms are tried before simple_type so that `T::Item` matches before falling back to a bare `T`.

**Source:** `tools/peg_gen_cpp/grammars/logos.peg#L1481`, `tools/peg_gen_cpp/grammars/logos.peg#L1456-L1462`

### `type.ref.metavar` — Metavariable type reference

`#Ident` and `#(expr)` are (non-generic) type references whose name is supplied by a metaprogram variable/expression rather than a literal identifier.

**Divergence:** A6

**Source:** `tools/peg_gen_cpp/grammars/logos.peg#L1861-L1864`

### `type.ref.ordered-choice` — Type-reference ordered choice

A type reference resolves by ordered choice: antiquot, typeof, cfg-slot-assoc, cfg-slot, writ-array, writ-map, pointer, array, slice, tagged, dyn, reference, impl-Trait, unit, never, closure, fn-pointer, tuple, paren, qualified-assoc, assoc-type-ref, then simple type. Associated-type forms precede simple_type so `T::Item` matches before `T`.

**Source:** `tools/peg_gen_cpp/grammars/logos.peg#L1421`

### `type.ref.unsized-pointee-gated` — Unsized reference pointees allowed only at the immediate position

A bare unsized pointee (`[T]`, `dyn`, `str`) is permitted directly under `&`/`&mut` but the unsized-ok relaxation does not leak into nested type-arg resolution (e.g. `dyn` inside `&Box<dyn>` is still subject to the Box Sized bound).

**Source:** `src/compiler/sema.cpp#L5744-L5777`, `src/compiler/sema.cpp#L5810-L5822`


## Types · `fn-ptr`

### `type.fn-ptr.abi-identity` — Function-pointer type and ABI identity

`fn(P...) -> R` resolves to a single-pointer FnPtr; an `extern "ABI"` prefix is part of the type identity. Accepted ABIs are `C`/`C-unwind`/`system`/`Rust`; default and `"Rust"` normalize to the same identity, a foreign ABI is tagged, any other ABI string is a hard error. Return type defaults to void.

**Source:** `src/compiler/sema.cpp#L6084-L6125`

### `type.fn-ptr.bare` — Bare fn-pointer type `fn(...) -> R`

`fn(T1, T2, ...) -> R` / `fn(T1, T2, ...)` / `fn() -> R` / `fn()` is a bare function-pointer type; omitted return defaults to no declared return type.

**Source:** `tools/peg_gen_cpp/grammars/logos.peg#L1726-L1728`, `tools/peg_gen_cpp/grammars/logos.peg#L1765-L1774`

### `type.fn-ptr.extern-abi` — extern-ABI fn-pointer type

`extern "ABI" fn(...) -> R` is a fn-pointer type carrying an ABI string; sema tags the FnPtr type with it and threads it to the MLIR calling convention (mirrors the extern-fn declaration convention).

**Source:** `tools/peg_gen_cpp/grammars/logos.peg#L1737-L1740`, `tools/peg_gen_cpp/grammars/logos.peg#L1741-L1748`

### `type.fn-ptr.hrtb` — HRTB-quantified fn-pointer type

`for<'a> fn(&'a T) -> R` is a HRTB-quantified fn-pointer type; the `for<>` binders are parsed and captured (HRTB_BINDERS) but sema does not yet skolemize per-type binders — captured for future region inference.

**Note:** Binders are parsed/captured but not yet semantically enforced (per source comment); observable effect is limited to successful parsing today.

**Source:** `tools/peg_gen_cpp/grammars/logos.peg#L1729-L1732`, `tools/peg_gen_cpp/grammars/logos.peg#L1757-L1764`

### `type.fn-ptr.type` — Function-pointer type

`fn(T1,T2) -> R` is a bare function-pointer type. Qualifiers/prefixes are accepted: `unsafe fn(...)` (IS_UNSAFE), `extern "ABI" fn(...)` (ABI threaded to the calling convention), and `for<'a> fn(...)` (HRTB binders captured for future region inference).

```logos
extern "C" fn(i32) -> i32
```

**Source:** `tools/peg_gen_cpp/grammars/logos.peg#L1666-L1715`

### `type.fn-ptr.unsafe` — unsafe fn-pointer type

`unsafe fn(...) -> R` is a fn-pointer type with IS_UNSAFE set; otherwise structurally identical to the safe fn-pointer type. Must be tried before the bare (non-`unsafe`) alternatives so `unsafe` is consumed here.

**Source:** `tools/peg_gen_cpp/grammars/logos.peg#L1733-L1736`, `tools/peg_gen_cpp/grammars/logos.peg#L1749-L1756`


## Types · `fnptr`

### `type.fnptr.methods-emit-non-generic` — Function-pointer impl methods emit once, non-generic

If the impl target name has the `$fnptr$` prefix, impl_type_params_ is cleared before lowering the impl's items: a fn-ptr is type-erased to a uniform pointer, so its methods must emit exactly once as non-generic functions rather than a never-instantiated generic template.

**Source:** `src/compiler/sema_decl.cpp#L2227-L2231`


## Types · `struct`

### `type.struct.bare-all-default-inst` — Bare generic struct name with all-default params instantiates defaults

A bare struct name N (written without `<...>`) referring to a generic struct whose every type parameter has a default resolves to the defaulted instantiation `N<d0, d1, ...>`, where each default may reference earlier defaults via substitution. If any parameter lacks a default, the bare name resolves to the uninstantiated struct type.

**Source:** `src/compiler/sema.cpp#L2583-L2610`

### `type.struct.dst-tail-slice-last-field` — Custom-DST slice tail only at last field

An unsized slice type (`[T]`, UNSIZED_SLICE_TYPE node) is permitted as a struct field's type only when that field is the last FIELD_DEF in the struct; the unsized-allowed flag is set only for resolving that one field's type node and restored immediately after. When used there, the struct is marked is_dst.

**Divergence:** B2: custom-DST tail-slice (DONE) — Logos supports `struct Foo { hdr: H, tail: [T] }`.

**Related:** `item.struct.tuple-struct-fields`

**Source:** `src/compiler/sema_collect.cpp#L4226-L4272`

### `type.struct.non-null-niche` — non_null single-pointer wrapper yields Option niche

A struct annotated `#[non_null]` wrapping a single non-null pointer makes `Option<T>` use the null-pointer value as the None niche (no discriminant overhead).

**Divergence:** A: #[non_null] attribute is a Logos addition mirroring Rust NonNull niche.

**Source:** `src/compiler/sema_decl.cpp#L1229-L1230`

### `type.struct.package-qualified-identity` — Struct types are identified by package-qualified name

A struct/ZonedStruct's identity for MLIR layout is `<pkg>.<concrete_struct_name>` (qualify_pkg); the struct-layout registries look up the package-qualified key first, falling back to the bare (package-agnostic) name only as a back-compat, first-registered-wins alias. Same-named structs from different packages are distinct types with independent layouts; only the bare-name fallback can alias them together.

**Source:** `src/compiler/mlir_gen_impl.hpp#L729-L774`

### `type.struct.rel-ptr-offset-storage` — rel_ptr struct is a self-relative pointer

A struct annotated `#[rel_ptr]` is classified as a self-relative pointer using 8-byte offset storage.

**Divergence:** A: RefRepr RelOffset Logos addition, no Rust analog.

**Source:** `src/compiler/sema_decl.cpp#L1223-L1225`

### `type.struct.self-describing-thin-ptr` — self_describing keeps *Self thin

A struct annotated `#[self_describing]` keeps `*Self` a thin pointer (no DstRef fattening) under Ptr→DstRef canonicalization.

**Divergence:** A: Writ/RefRepr Logos addition, no Rust analog.

**Source:** `src/compiler/sema_decl.cpp#L1216-L1218`

### `type.struct.zone-mut-fat-ref` — zone_mut makes &mut T fat carrying its allocator

For a struct annotated `#[zone_mut]`, a `&mut T` reference is a fat `{data, zone}` pair carrying the value's allocator/zone.

**Divergence:** A: Writ zone model Logos addition; Rust &mut is thin.

**Source:** `src/compiler/sema_decl.cpp#L1219-L1221`

### `type.struct.zoned2-relative-fields` — zoned2 struct fields use relative pointers

A struct annotated `#[zoned2]` stores its pointer fields as self-relative offsets (RelOffset) rather than absolute addresses.

**Divergence:** A: Writ zoned2 Logos addition, no Rust analog.

**Source:** `src/compiler/sema_decl.cpp#L1222`


## Types · `enum`

### `type.enum.backing-integer` — enum backing type must be integer

An explicit enum backing type `enum Foo : T { … }` must resolve to an integer kind; a non-integer T is rejected and no backing type is set.

```logos
enum E : u64 { A }
```

**Source:** `src/compiler/sema_collect.cpp#L1956-L1963`

### `type.enum.unresolved-when-fewer-args-or-nested-typevar` — Enum/struct type is unresolved if under-applied or nests an unresolved type

A type is treated as incompletely resolved when it is `Error` or a type-variable, when an enum/struct carries fewer type-args than its declared params (notably zero, e.g. a bare `Option`), or when any nested type-arg, tuple element, or reference pointee is itself unresolved. The check is recursive, not shallow.

**Source:** `src/compiler/sema_impl.hpp#L509-L544`


## Types · `enum-lit`

### `type.enum-lit.type-bounds-checked` — Generic enum type args are bound-checked

The inferred type arguments of a generic enum literal are checked against the enum's type-parameter bounds.

**Source:** `src/compiler/sema_expr.cpp#L12145`


## Types · `datatype`

### `type.datatype.data-plain-inference` — DataPlain flag propagates through nested datatype fields

A datatype is DataPlain (info.is_data_plain) unless disproved by a field: for a (possibly array-wrapped) ZonedStruct field, if its type is generic (non-empty type_args) or its base name is not yet found in datatypes_ (forward reference / cross-package), the outer type is conservatively marked non-DataPlain (DataNode); if the nested type IS found and itself is_data_plain, embedding it by value does not clear the outer type's DataPlain flag. Array wrapping is stripped before the check, so a DataNode array element also demotes the owner.

**Divergence:** A6: Writ datatype DataPlain/DataNode classification is Logos-only.

**Source:** `src/compiler/sema_collect.cpp#L3974-L4003`

### `type.datatype.pod-field-restriction` — Writ datatype fields must be POD-compatible

A non-annotation `datatype` field type must be one of: an integer/float/bool/int-or-float-literal primitive kind (incl. packed i24/u24/i56/u56); an Array whose element type is itself datatype-safe (recursively); a ZonedStruct (nested datatype, always OK); a Struct only if it is a `#[rel_ptr]` self-relative pointer type (RelAny/`RelPtr<T>`, an 8-byte offset — plain structs that may carry heap/absolute pointers are rejected); or a TypeVar (deferred, resolved later by monomorphization). Any other field type raises a diagnostic error. Annotation types (is_annotation_type) are exempt (compile-time only, may hold e.g. str fields).

**Divergence:** A6/A11: Writ datatype fabric is a Logos-only feature; uses extra packed int widths.

**Source:** `src/compiler/sema_collect.cpp#L3931-L3973`


## Types · `dyn`

### `type.dyn.auto-bound` — Trailing auto-trait/lifetime bound on a dyn type

A `dyn Trait` type may carry zero or more trailing bounds `+ IDENT` (auto-trait marker, e.g. Send/Sync) or `+ 'lifetime` (outlives bound), each producing its own AUTO_TRAIT_BOUND / AUTO_LIFE_BOUND node collected alongside the trait's type-args; auto-trait bounds are folded into the trait-object's representation and enforced at unsize-coercion time, while lifetime bounds are recorded but not yet enforced (pending region-inference wiring).

**Note:** Enforcement status ('not yet enforced') is as stated by the accompanying comment for this slice; not independently verified against the region-inference code.

**Source:** `tools/peg_gen_cpp/grammars/logos.peg#L1570-L1583`

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

### `type.dyn.trait-object` — `dyn Trait` trait-object type

`&[mut] dyn Trait[<Args>]`, `dyn Trait(Args) -> Ret` / `dyn Trait(Args)` (Fn-family paren form, args in PARAMS, return in RET_TYPE), and bare `dyn Trait[<Args>]` are trait-object types — a fat pointer of (data, vtable) when reference-prefixed. Type args (for traits with type/const-generic params) use the LT_TYPE/GT_TYPE pair like other type-position generic instances. Each form optionally carries a leading explicit lifetime on the outer `&` (`&'a dyn Trait`, `&'a mut dyn Trait` — recorded in LIFETIME, informational only since the language's lifetime model is elision-based) and/or a leading HRTB binder (`dyn for<'a> Trait<...>`, recorded in HRTB_BINDERS). Trailing `+ Ident` (auto-trait bound, e.g. Send/Sync) and `+ 'lt` (lifetime outlives bound) are accepted after the trait/args; auto-trait bounds are enforced at unsize coercion (logos-core §2.4(c)), lifetime bounds are recorded but not yet enforced (depends on §2.1 region_infer wiring).

```logos
&dyn Display + Send
Box<dyn for<'a> Fn(&'a i32) -> i32>
```

**Source:** `tools/peg_gen_cpp/grammars/logos.peg#L1496-L1616`, `tools/peg_gen_cpp/grammars/logos.peg#L1556-L1676`

### `type.dyn.unknown-trait-error` — dyn over an unknown trait is an error

`dyn Trait` where Trait is not a registered trait (and not an Fn-family name) is a hard error (`unknown trait '...' in &dyn type`).

**Note:** Bare-name lookup: a package-local trait shadowed by a prelude trait of the same name resolves to the prelude trait (known gap, dyn-local-trait-shadowing).

**Source:** `src/compiler/sema.cpp#L5964-L5965`

### `type.dyn.unsized-bound-bits-preserved` — dyn Trait + Send/Sync auto-bounds preserved and folded into identity

The bare `dyn Trait` type may carry `+ Send` and/or `+ Sync` auto-trait bounds (bit 8 = Send, bit 9 = Sync); these bits are preserved through type construction and folded into type identity so e.g. `Box<dyn T + Send>` interns distinctly from `Box<dyn T>` and the unsize coercion can enforce the bound.

**Related:** `type.traitobject.owning-kind-distinct`

**Source:** `src/compiler/sema_impl.hpp#L658-L678`, `src/compiler/sema_impl.hpp#L819-L820`

### `type.dyn.unsized-vs-fat` — Bare dyn unsized vs fat trait object

In an unsized-ok context (turbofish for `T: ?Sized`), bare `dyn Trait` resolves to the unsized-dyn form; otherwise it resolves to the fat-pointer TraitObject.

**Source:** `src/compiler/sema.cpp#L5999-L6018`


## Types · `traitobject`

### `type.traitobject.owning-kind-distinct` — Trait-object owning kinds intern distinctly

A trait object has one of four owning kinds (Borrow/Box/Rc/Arc), all sharing the fat {data,vtable} layout and dispatch but differing in release semantics; the owning kind together with `+Send`/`+Sync` bits is folded into type identity so the forms intern distinctly.

**Related:** `type.dyn.unsized-bound-bits-preserved`

**Source:** `src/compiler/sema_impl.hpp#L810-L835`


## Types · `generic`

### `type.generic.instantiation` — Generic type instantiation `T<...>`

`Name<arg, ...>` (optional trailing comma) instantiates a generic type. The type name may also be a metavariable: `#Ident<...>` or `#(expr)<...>`.

**Source:** `tools/peg_gen_cpp/grammars/logos.peg#L1797-L1815`, `tools/peg_gen_cpp/grammars/logos.peg#L1857-L1860`, `tools/peg_gen_cpp/grammars/logos.peg#L1874-L1875`

### `type.generic.type-arg-kinds` — Generic type-argument kinds

A generic type argument is one of: a repeat-group `#(arg),*` (metaprogram repetition over type args), a lifetime `'a` (stored as LIFETIME_PARAM, skipped during concrete-type resolution), a pack expansion `Ident...`, an antiquote `$Ident` or `$Ident...`, an integer literal (optionally negated), a writ literal, or a plain type.

**Divergence:** A6

**Source:** `tools/peg_gen_cpp/grammars/logos.peg#L1836-L1855`


## Types · `generic-inst`

### `type.generic-inst.arity-and-bounds` — Generic instantiation arity and bound checks

After default-filling, the type-argument count must match the struct/enum/datatype declared type-param count, and each argument must satisfy its param's trait bounds.

**Source:** `src/compiler/sema.cpp#L5650-L5668`

### `type.generic-inst.box-slice-dst-collapse` — `Box<[T]>` and `Box<DST-struct>` collapse to owning fat references

`Box<[T]>` collapses to an owning fat slice {data, len} (same layout as `&[T]`, move-only, droppable). `Box<Foo>` where Foo is a custom-DST tail-slice struct collapses to an owning DstRef {data, len}.

**Note:** Logos custom-DST machinery; analogous to Rust CoerceUnsized.

**Source:** `src/compiler/sema.cpp#L5478-L5501`

### `type.generic-inst.default-type-args` — Trailing default type arguments

When fewer type-args are supplied than the generic has params, trailing params are filled from their declared defaults (`struct S<T, U = i64>`: `S<A>` ≡ `S<A, i64>`); a default may reference an earlier param and is substituted with the already-bound args.

**Source:** `src/compiler/sema.cpp#L5602-L5618`

### `type.generic-inst.generic-const` — Generic compile-time const instantiation

Applying type-args to a generic const `pub const X<T1,T2>: WritStatic = @{...}` re-evaluates the const's value AST under the supplied type-arg bindings, yielding a fresh per-instantiation WStaticLit identity. The argument count must equal the const's type-param count.

**Divergence:** A6

**Note:** Logos-specific WritStatic generic const.

**Source:** `src/compiler/sema.cpp#L5345-L5392`

### `type.generic-inst.generic-type-alias` — Generic type alias instantiation

A generic type alias `type Foo<T> = Bar<T>` instantiated as `Foo<A>` resolves to its RHS with type- and lifetime-args substituted; the supplied type-arg count and lifetime-arg count must equal the alias's declared type-param and lifetime-param counts respectively.

**Source:** `src/compiler/sema.cpp#L5394-L5429`

### `type.generic-inst.kind-disambiguation-local-shadow` — Local declaration shadows imported same-named type

When a name resolves to multiple kinds (struct/datatype/enum) across packages, a declaration local to the current package wins over any non-local same-named declaration of another kind.

**Source:** `src/compiler/sema.cpp#L5505-L5526`

### `type.generic-inst.schema-unsized-arg-canonicalization` — Generic schema struct canonicalizes unsized type-args to sized fat form

When instantiating a generic `schema` struct, an unsized type-argument (`UnsizedSlice<T>`, e.g. produced for `Wrap<str>` under `?Sized`/turbofish) is canonicalized to its sized fat-slice form `Slice<T>`, matching the schema's WAny-handle field storage and `impl WritField for str` (= `Slice<u8>`). Non-schema generics (e.g. `Box<str>`) are unaffected.

**Divergence:** A6

**Note:** Writ schema (ADR 0011) mechanism, no Rust analogue.

**Source:** `src/compiler/sema.cpp#L5588-L5601`

### `type.generic-inst.smart-pointer-dyn-collapse` — `Box<dyn Trait>` collapses to an owning trait object

`Box<dyn Trait>` (FQN-gated to the stdlib Box) collapses to an owning fat-pair trait object {data, vtable} tagged Box. Rc/Arc no longer collapse and instead resolve as ordinary generic structs whose inner pointer is a fat DST reference.

**Note:** Mirrors Rust owned_box + CoerceUnsized lang item; Rc/Arc flip is a Logos-specific representation choice, not an observable-behavior divergence.

**Source:** `src/compiler/sema.cpp#L5432-L5477`

### `type.generic-inst.unknown-type-metaprog-defer` — Unknown generic type deferred during metaprog discovery

An unknown generic type name is an error, except during the metaprog discovery pass (before derive hooks emit items), where it silently yields error-type so a later non-metaprog pass can re-resolve once synthesized items exist.

**Source:** `src/compiler/sema.cpp#L5527-L5538`

### `type.generic-inst.unsized-arg-gating` — ?Sized type-param relaxes unsized type-args

A type-argument at a generic param declared `?Sized` (implicit_sized=false) may be a bare unsized type (`[T]`, `dyn Trait`); a type-arg at a `Sized` param must not be unsized. Passing an unsized type, or a `?Sized` outer type-param, to a `Sized` param is a diagnostic.

**Source:** `src/compiler/sema.cpp#L5562-L5586`, `src/compiler/sema.cpp#L5619-L5649`


## Types · `param`

### `type.param.never-type-forbidden` — Never type `!` forbidden as a parameter type

A function parameter may not have the never type `!`: `!` is uninhabited (has no values), so a `!`-typed parameter makes the function uncallable and has no codegen representation. `!` remains valid in return position, denoting a diverging function.

**Source:** `src/compiler/sema_decl.cpp#L584-L595`

### `type.param.unit-type-forbidden` — Unit-typed parameters forbidden

A function parameter may not have the unit type `()`; a unit-typed parameter carries no information and is ill-formed.

**Divergence:** Logos restriction: Rust permits `()`-typed parameters.

**Source:** `src/compiler/sema_decl.cpp#L303-L308`


## Types · `assoc`

### `type.assoc.normalize-via-where-eq` — Associated-type projection normalized by where-clause equality bound

An associated-type projection `<TV as Trait>::A` (where `TV` is a generic type-param) is normalized to the concrete type `C` whenever an in-scope where-clause bound on `TV` records an associated-type equality `Trait::A = C`. When the projection records a trait, only equalities from a bound whose trait matches (or whose trait is unrecorded) apply; the first matching equality wins. If no matching equality exists, the projection is left unchanged.

**Source:** `src/compiler/sema.cpp#L2689-L2704`

### `type.assoc.path` — Associated-type path reference

`T::Item` and `T::Item<A, B>` (GAT form) are associated-type references (ASSOC_TYPE_REF); the `::IDENT[<Args>]` suffix may repeat one-or-more times, chaining projections off a simple_type base.

**Source:** `tools/peg_gen_cpp/grammars/logos.peg#L1542-L1548`

### `type.assoc.projection` — Associated-type projection

`T::Item` and `T::Item<A,B>` (GAT with type args) are associated-type references; the `::Name[<args>]` tail may chain one or more times. `<T as Trait>::Assoc` is the fully-qualified form, with the disambiguating trait recorded for resolution.

```logos
<Vec<T> as IntoIterator>::IntoIter
```

**Source:** `tools/peg_gen_cpp/grammars/logos.peg#L1475-L1488`

### `type.assoc.qualified` — Fully-qualified associated-type projection

`<TypeRef as SimpleType>::IDENT` is a fully-qualified associated-type reference: RECEIVER=TypeRef, FIELD=IDENT, with the disambiguating trait recorded in NAME; sema resolves it the same as the unqualified `T::Item` sugar but uses the recorded trait when multiple traits provide the same associated-name.

**Source:** `tools/peg_gen_cpp/grammars/logos.peg#L1535-L1540`


## Types · `assoc-ref`

### `type.assoc-ref.bound-and-supertrait-lookup` — Associated-type projection on a type-parameter

For `base::Item` where `base` is a type-variable, the owning trait of `Item` is found by searching the bounds of `base` (with `Self` mapped to the enclosing trait) and walking each bound trait's supertrait chain; the first trait declaring an associated type named `Item` is selected, carrying that bound's concrete trait type-args.

**Source:** `src/compiler/sema.cpp#L5108-L5112`, `src/compiler/sema.cpp#L5146-L5185`

### `type.assoc-ref.concrete-impl-fallback` — Assoc projection fallback to implementing trait

If no owning trait is found from bounds or impl context, the projection's owning trait is found among traits that have an impl for the concrete base type (tried under both the full concrete type name and the bare struct name); if still none declares `Assoc`, a diagnostic 'no associated type Assoc found for `<base>`' is raised.

**Source:** `src/compiler/sema.cpp#L5232-L5257`

### `type.assoc-ref.deferred-node` — Deferred associated-type node carries trait args

An unresolved projection yields a deferred AssocType node {base, trait, name, gat_args}; the trait name is suffixed with the concrete trait type-args so distinct `Trait<T>` instantiations produce distinct nodes (empty suffix for non-generic traits preserves the bare name). Bounds declared on the assoc type are propagated into the projection's bound context.

**Divergence:** B-assoc

**Source:** `src/compiler/sema.cpp#L5308-L5337`

### `type.assoc-ref.eager-concrete-projection` — Eager projection for concrete base with generic trait

When the base is a concrete type and the resolved trait is generic (has type-args), the projection is resolved immediately by looking up the trait+args-suffixed assoc-type impl and substituting the base's type-args; this disambiguates two `Trait<T>` impls on one type that would otherwise intern to a single trait-arg-less deferred node and collapse.

**Divergence:** B-assoc

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


## Types · `alias`

### `type.alias.generic-alias-inlined` — Generic type aliases are inlined at use sites

A type alias with type parameters has no concrete standalone type; it is inlined at each use site. Only non-generic aliases resolve to a concrete type.

**Source:** `src/compiler/sema_decl.cpp#L1602-L1608`

### `type.alias.impl-target-unfold` — Non-generic type aliases unfold at an impl target position

When the impl target names a non-generic type alias `type A = B;`, the impl is treated as an impl on the aliased Struct/ZonedStruct B (the alias is transparent): `impl Tr for A` ≡ `impl Tr for B`, including concrete-generic mangling of B when B carries type args.

**Source:** `src/compiler/sema_decl.cpp#L1865-L1882`

### `type.alias.name-shadowing-order` — Type-alias name resolution shadowing order

A bare type name N resolves to a 0-arg type alias by probing in order: (1) the current package's own alias `pkg::N`, (2) the bare/unqualified alias N, (3) aliases from wildcard-imported packages. The current package's alias thus shadows a same-named imported/stdlib alias (Rust scoping).

**Source:** `src/compiler/sema.cpp#L2562-L2581`


## Types · `self`

### `type.self.impl-binding-precedence` — `Self` resolves to the enclosing impl's type

Within a method body, `Self` denotes the impl's target type; when the impl fixed `Self = Foo<T>` with type-args, a bare same-named `Self` from an unrelated impl is treated as stale and replaced. A datatype binding for `Self` takes precedence over a struct binding when both names exist; a primitive target binds `Self` to that primitive.

**Note:** Precedence/staleness handling inferred from impl-context heuristics; observable effect is Self resolution.

**Source:** `src/compiler/sema_decl.cpp#L207-L242`

### `type.self.implicit-self-param-ref` — Bare `self` parameter is `&Self` / `&mut Self`

A method parameter written as `self` (no explicit type) has type `&Self` by default, or `&mut Self` when marked mutable.

**Source:** `src/compiler/sema_decl.cpp#L321-L328`


## Types · `infer`

### `type.infer.fill-annotation-from-rhs` — Inferred holes in let-annotation filled from RHS type

An `_` (inferred) hole in a let annotation is filled from the structurally-matching concrete RHS type (e.g. `let v: Vec<_> = vec![1]` binds as `Vec<i32>`); the annotation's concrete parts win, holes take the RHS side. Mismatched shapes leave the annotation unchanged. A bare `_` against an integer-literal RHS defaults to i32 and against a float-literal RHS to f64.

```logos
let v: Vec<_> = vec![1];  // Vec<i32>
```

**Source:** `src/compiler/sema.cpp#L4395-L4402`, `src/compiler/sema.cpp#L4310-L4318`

### `type.infer.hole-detection` — A type contains an inferred hole transitively

A type is considered to contain an inferred hole if it is `_` or if any of its type arguments, tuple elements, element type, or pointee transitively contains one.

**Source:** `src/compiler/sema.cpp#L4343-L4351`

### `type.infer.let-hole-from-rhs` — `_` holes in a let annotation are filled from the RHS type

An inference hole `_` at any depth in a `let` type annotation is filled from the corresponding position of the initializer's inferred type.

**Source:** `src/compiler/sema_impl.hpp#L739-L742`

### `type.infer.never-fallback-on-divergent-body` — ! fallback for unbound type-param of always-diverging callee

If a callee's body always diverges (panic-tail or `loop {}`-tail) and a type-parameter is otherwise unbound at the call site, the inference variable falls back to `!` (Never). A non-diverging body leaves an unbound type-param as an ambiguity error: `fn f<T>()->T{panic();}` infers T=! while `fn f<T>()->T{return 0;}` is ambiguous.

**Divergence:** Rust-2024 `!`-fallback semantics (logos-core 1.1).

**Source:** `src/compiler/sema_impl.hpp#L2574-L2584`

### `type.infer.never-fallback-precompute` — Body-always-diverges flag precomputed for `!` fallback

At fn-collection time, a cheap AST check determines whether a fn's body always diverges (based on its trailing statement) and stores the result on the fn's info; this feeds the Rust-2024 never-type (`!`) fallback rule applied later during type-argument inference.

**Source:** `src/compiler/sema_collect.cpp#L4885-L4891`

### `type.infer.no-underscore-in-item-signature` — `_` placeholder type forbidden in item signatures

The inferred-type placeholder `_` is not permitted within types in item signature positions (function parameter types, return type, const item type), including nested occurrences (`Vec<_>`, `&_`, `[_; N]`); such occurrences are an error rather than an inference hole.

**Source:** `src/compiler/sema_impl.hpp#L1965-L1976`


## Types · `typeof`

### `type.typeof.compile-time-type-of-expr` — typeof(expr) as a type-position expression

`typeof(expr)` (TYPEOF_TYPE, VALUE=expr AST) is usable in type position and denotes the compile-time-resolved type of `expr`, without evaluating `expr` at runtime.

**Source:** `tools/peg_gen_cpp/grammars/logos.peg#L260`

### `type.typeof.expr` — `typeof(expr)` type expression

`typeof(expr)` denotes, at type position, the compile-time type of `expr`; `expr` itself is not evaluated.

**Divergence:** A6

**Source:** `tools/peg_gen_cpp/grammars/logos.peg#L1461-L1463`, `tools/peg_gen_cpp/grammars/logos.peg#L1521-L1523`

### `type.typeof.expr-type-no-eval` — typeof(expr) yields the sema type without evaluation

`typeof(expr)` resolves to the sema-computed type of `expr`; the expression is type-checked but never evaluated at runtime.

**Divergence:** Logos addition: Rust has no `typeof` operator.

**Source:** `src/compiler/sema.cpp#L5673-L5681`


## Types · `identity`

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

### `type.identity.enum-hash-tag-only` — Enum structural identity is currently tag-only

An enum type's structural identity mixes only a fixed ENUM shape tag; it does not currently walk variant names or payload types, so two enums with different variant sets/payloads may collide on this identity hash.

**Note:** Source comment explicitly flags this as incomplete ('refine when block_type_hash needs discriminate variants — when first persistent enum lands'); not a finished rule.

**Source:** `src/compiler/mono_clone.cpp#L136-L140`

### `type.identity.fnitem-distinct` — Each function item is a distinct zero-sized type

A function-item type's identity = (function symbol name, turbofish type-args, signature params, return). Two distinct functions with identical signatures get distinct fn-item types, and distinct instantiations of one generic function (even when the resulting fn-ptr signature collapses, e.g. unused type param) get distinct fn-item types.

**Source:** `src/compiler/sema.cpp#L874-L883`, `src/compiler/sema.cpp#L1023-L1031`

### `type.identity.fnptr-abi` — Function-pointer identity = (ABI tag, params, return)

A function-pointer type identity = (extern-ABI tag where empty = default Rust ABI, ordered parameter types, return type). Function pointers differing only in ABI are distinct types.

**Source:** `src/compiler/sema.cpp#L864-L869`, `src/compiler/sema.cpp#L1015-L1019`

### `type.identity.fnptr-hash-opaque` — Function-pointer structural identity is signature-opaque

An FnPtr type's structural identity is a single fixed tag independent of parameter/return signature: two FnPtr types with different signatures currently have equal structural identity.

**Note:** Source comment marks this as a deliberate placeholder ('conservative choice... refine if a wire format pins it down'); may change once fn-pointer identity needs to distinguish signatures.

**Source:** `src/compiler/mono_clone.cpp#L123-L127`

### `type.identity.int-lit-value` — Integer-literal placeholder identity carries its value

An inferred integer-literal type `{integer}` carrying a const value is identified by that value (const_val); two literal placeholders with different values do not collapse to one type.

**Source:** `src/compiler/sema.cpp#L909-L916`

### `type.identity.intern-canonical` — Types are interned by canonical structural identity

Every type has a canonical identity: two types constructed structurally identically (per the per-kind identity fields below, computed bottom-up over already-canonical sub-types) denote the same type and intern to one shared representative; structurally distinct types intern to distinct representatives.

**Related:** `type.identity.ref-vs-typeuid`, `type.identity.lifetime-ignored`

**Source:** `src/compiler/sema.cpp#L801-L940`, `src/compiler/sema.cpp#L1099-L1109`, `src/compiler/sema.cpp#L1345-L1354`

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

**Divergence:** A3 (custom-DST / `Box<[T]>` as owning slice kind)

**Source:** `src/compiler/sema.cpp#L841-L847`, `src/compiler/sema.cpp#L997-L1003`

### `type.identity.stdlib-box-pkg-qualified` — Stdlib `Box` identity is package-qualified (not bare-name)

Unlike other stdlib intrinsics, the owning-box type is recognised as struct name `Box` AND (package empty OR package == `logos.mem.boxed`). A user struct literally named `Box` in any other package is NOT recognised as the stdlib box and receives none of the box special-casing (unsize, owning-drop, deref).

```logos
struct Box<T> { v: T } // user's own package: not the stdlib Box, no owning-drop/unsize special-casing
```

**Source:** `src/compiler/sema_impl.hpp#L1261-L1272`

### `type.identity.stdlib-intrinsic-bare-name-match` — Stdlib intrinsic type predicates match by bare struct name

Compiler special-casing for the stdlib intrinsic structs AnyVal, WritStatic, Writ, StringView, Ident, ExprBlob, DataRef, QuoteItemBlob, ItemList is keyed on bare struct name alone (Struct or ZonedStruct kind), independent of declaring package: any struct sharing one of these names is treated as the intrinsic.

**Note:** Inferred from an internal helper/comment ("intentional, pkg-blind"); no diagnostic fires on a user name collision, so this is a silent-shadowing risk rather than a checked rule.

**Source:** `src/compiler/sema_impl.hpp#L1249-L1255`, `src/compiler/sema_impl.hpp#L1273-L1281`

### `type.identity.struct-field-recursion` — Struct identity recurses through substituted field types

Structural identity of a struct type `S<A...>` mixes the struct shape tag, the field count, and the identity of each field type after substituting S's type-params by the concrete type-args A.... Generic struct instances thus get distinct identity per instantiation by their concrete field layouts.

**Source:** `src/compiler/mono_clone.cpp#L141-L178`

### `type.identity.structural-hash-layout-stable` — Structural type identity is layout-stable, name-independent

A type's structural identity (used for wire/persistent identity) is computed by a tag-prefixed structural walk that bears no struct/field NAME: two types with identical physical layout (same primitive leaves, same field types in order, same array sizes) have equal identity regardless of struct/field renames. Each primitive kind has a distinct code; aggregate shapes carry distinct shape tags (struct/tuple/array/ptr/&/&mut/slice/enum/fnptr/void/wstatic).

**Note:** Concrete code values are an implementation detail; the normative content is name-independence + per-shape distinctness.

**Source:** `src/compiler/mono_clone.cpp#L14-L21`, `src/compiler/mono_clone.cpp#L56-L78`, `src/compiler/mono_clone.cpp#L80-L185`

### `type.identity.tuple` — Tuple identity = ordered element types

A tuple type's identity is the ordered sequence of its element types; tuples are equal iff same arity and pairwise-equal elements.

**Source:** `src/compiler/sema.cpp#L838-L840`, `src/compiler/sema.cpp#L995-L996`

### `type.identity.tuple-hash-structural` — Tuple structural identity mixes arity and each element identity in order

A tuple type's structural identity = tag(TUPLE) + element-count + identity(elem_0) + ... + identity(elem_n-1); tuples differing in arity, element types, or element order have distinct identity.

**Source:** `src/compiler/mono_clone.cpp#L115-L122`

### `type.identity.typevar-name` — Type/const variable identity = name

A type variable or const variable is identified by its name (plus const_val); two type parameters with the same name denote the same type variable.

**Source:** `src/compiler/sema.cpp#L899-L902`, `src/compiler/sema.cpp#L1040-L1043`

### `type.identity.writstatic-hash-by-value` — WritStatic literal type identity is its content hash, not a structural walk

A WStaticLit type's structural identity = tag(HSTAT) + the literal's own stored const_val (a hash of the underlying CFG value); the compiler does not recurse into the value's shape for this identity at the mono layer.

**Source:** `src/compiler/mono_clone.cpp#L128-L135`

### `type.identity.wstatic-config` — WritStatic-literal type identity = its byte-hash

A type parameterized by a WritStatic literal config (`Foo::<@{...}>`) is identified by the byte-hash of that literal; distinct configurations instantiate to distinct types and do not dedupe.

**Divergence:** Logos addition (WritStatic const-config type parameters)

**Source:** `src/compiler/sema.cpp#L917-L922`


## Types · `equal`

### `type.equal.lifetime-aware-structural` — Lifetime-aware structural equality is stronger than lifetime-erased TypeUID

Structural type equality used at invariant positions includes lifetime fragments and recurses through Ref/MutRef/Ptr (pointee + lifetime), Tuple/Array/Slice (elements), FnPtr/Closure (params + return), Struct/ZonedStruct/Enum (name, package, type-args, lifetime-args), and AssocType (trait, name, base, GAT type-args + lifetime-args). Two types differing only by an inner lifetime are unequal here even though TypeUID erases the lifetime. Primitives, TypeVars, and other kinds use TypeUID identity.

**Source:** `include/logos/compiler/subtype.hpp#L54-L151`

### `type.equal.uid-identity` — Type equality is canonical-UID identity within one pool

types_equal(a,b) holds iff both are non-null, drawn from the same type pool, and have equal canonical UIDs (uid_of(a)==uid_of(b)); types from different pools are never equal. Pointer-identical refs are trivially equal.

**Source:** `src/compiler/sema.cpp#L1355-L1362`


## Types · `subtype`

### `type.subtype.assoc-gat-lifetime-invariant` — Associated/GAT types: covariant args, invariant GAT lifetime args

For an associated type projection (same trait_name and assoc_type_name): the base is covariant, each GAT type-arg is covariant, and each GAT lifetime-arg is invariant (must be equal; GAT lifetime variance is not user-controllable). Differing trait/name or arity is a shape difference.

**Source:** `include/logos/compiler/subtype.hpp#L310-L329`, `include/logos/compiler/subtype.hpp#L93-L113`

### `type.subtype.depth-cap-accept` — Recursion depth cap conservatively accepts the subtype relation

If subtype recursion exceeds depth 64, or either operand is null, the relation is accepted (returns true), deferring soundness to the caller's separate compatibility check.

**Note:** Conservative termination guard, not a language-design choice.

**Source:** `include/logos/compiler/subtype.hpp#L203-L204`

### `type.subtype.enum-covariant` — Enums covariant in all type-arg and lifetime-arg positions

For same enum (matching pkg_name+enum_name), every type-arg and lifetime-arg position is treated as covariant (Co); there is no per-enum variance table. Matches the covariant shape of Option/Result/Box. Differing name, package, or arity is a shape difference.

**Note:** Per-enum variance table not yet wired; Co is a conservative fallback (B81 compiler tag).

**Source:** `include/logos/compiler/subtype.hpp#L279-L298`

### `type.subtype.fn-contra-params-co-ret` — Function pointers and closures: contravariant params, covariant return

FnPtr and Closure subtype identically: sub <: sup iff each param position is contravariant (sup_param <: sub_param) and the return type is covariant (sub_ret <: sup_ret), with matching param arity. Arity mismatch is deferred to the compatibility check.

**Source:** `include/logos/compiler/subtype.hpp#L299-L309`, `include/logos/compiler/subtype.hpp#L10`

### `type.subtype.inferred-wildcard` — Inferred-type placeholder `_` is variance-compatible with any type

An InferredType (`_`) on either side of a structural-equality-with-lifetimes comparison is treated as a wildcard matching any type at any nesting depth (e.g. `Vec<_>` compares equal to `Vec<i32>`), letting region/type inference resolve it later.

**Source:** `include/logos/compiler/subtype.hpp#L62-L63`, `include/logos/compiler/subtype.hpp#L57-L61`

### `type.subtype.rawptr-variance` — *const covariant, *mut invariant; mut/const mismatch is shape diff

Raw pointers carry no lifetime. *const T is covariant in pointee (*const T <: *const U iff T <: U); *mut T is invariant in pointee (*mut T <: *mut U iff T == U with lifetimes). A const-vs-mut pointer-kind mismatch is a shape difference, deferred to the compatibility check (subtype returns true).

**Source:** `include/logos/compiler/subtype.hpp#L226-L235`

### `type.subtype.relation-purpose` — Subtyping refines kind-equality with lifetime variance

sub <: sup holds when a value of type sub may be used where sup is expected. Subtyping augments lifetime-erased kind/structural compatibility with lifetime-aware variance constraints; it returns true for any cross-kind pair (leaving legitimate cross-kind coercions, e.g. IntLit→i32, &mut→&, Vec→slice, to the separate compatibility check) and only fails when sub and sup share a kind that has a variance rule and their lifetime-aware structure disagrees.

**Related:** `coerce.compatible.equal-implies-compatible`

**Source:** `include/logos/compiler/subtype.hpp#L37-L41`, `include/logos/compiler/subtype.hpp#L197-L211`

### `type.subtype.struct-variance-table` — Struct variance from per-def table keyed by package+name, default covariant

For same struct (matching pkg_name+struct_name), each type-arg position i and lifetime-arg position i is checked at its variance looked up from the per-definition variance table (key 'pkg.Name', subkeys '#i' for type args, '@i' for lifetime args). Absent table or absent entry defaults to covariant (Co). Differing struct name, package, or arg-list arity is a shape difference (subtype returns true).

**Note:** Variance table is user/compiler-supplied; this unit only consumes it.

**Source:** `include/logos/compiler/subtype.hpp#L247-L278`, `include/logos/compiler/subtype.hpp#L11-L13`

### `type.subtype.tuple-array-slice-covariant` — Tuples covariant per element; arrays and slices covariant in element

(S0,..,Sn) <: (P0,..,Pn) iff each Si <: Pi (same arity). [T; N] and [T] are covariant in element type: sub <: sup iff elem(sub) <: elem(sup). Arity/shape mismatch is deferred to the compatibility check.

**Source:** `include/logos/compiler/subtype.hpp#L236-L246`


## Types · `canonicalize`

### `type.canonicalize.global-substitution` — Global type simplification pass

After collection, every declared type position is canonicalized by an identity substitution: struct field types, enum variant payload types, free + generic function parameter/return types, type-alias bodies, module-const types, associated-const impl types, and associated-type impl bodies. Non-generic forms (e.g. `type Inner<T> = i32`) resolve to their concrete type; forms still mentioning a TypeVar are left unchanged for later substitution.

**Source:** `src/compiler/sema_collect.cpp#L703-L729`


## Types · `name`

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

A bare type name resolves in order: (1) a scope-local binding in current_type_params_ (generic-const instantiation / generic fn / generic method scope wins over any global); (2) a fixed builtin primitive name table (i32,i64,f64,f32,bool,u8,i8,i16,u16,u32,u64,i24,u24,i56,u56,i128,u128,usize,isize,char,void); (3) a non-generic type alias (empty type_params — generic aliases are resolved at use sites elsewhere); (4) a struct found by name; (5) a datatype found by name; (6) an enum found by name. Failing all, the name does not resolve.

**Source:** `src/compiler/sema_collect.cpp#L4335-L4381`

### `type.name.self-typevar` — Self resolves to the bound Self type parameter

`Self` resolves to the current `Self` type-param binding when one is in scope.

**Source:** `src/compiler/sema.cpp#L6345-L6348`


## Types · `copy`

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

A struct that does not implement Drop and whose every field type is Copy is automatically Copy, without an explicit `impl Copy`; this runs after manually-written `impl Copy` entries are collected, so it only fills gaps rather than overriding explicit impls.

**Divergence:** Rust requires an explicit `#[derive(Copy)]`/`impl Copy`; Logos structurally auto-derives Copy for non-Drop, all-Copy-field structs.

**Note:** compute_auto_copy_types() body (the exact promotion algorithm) is defined outside this unit; only its invocation/purpose is evidenced here.

**Source:** `src/compiler/sema_collect.cpp#L695-L699`


## Types · `move`

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


## Types · `drop`

### `type.drop.aggregate-recursive` — Aggregate types need drop transitively

A Struct/ZonedStruct, Tuple, Enum, or Array type requires drop iff it declares an explicit `drop` method OR any of its constituent parts (struct fields, tuple elements, enum-variant payload fields, array element type) recursively requires drop.

**Source:** `src/compiler/mlir_gen_stmt.cpp#L485-L505`

### `type.drop.closure-value-not-auto-dropped` — Closure value type is excluded from the generic recursive drop check

A `Closure` value type is never reported as needing drop by the generic recursive `value_needs_drop` check. A closure held as a struct field or iterator-adapter field is stored through one level of indirection (a pointer to the {fn, env} pair); treating it as an inline aggregate value in a generic recursive drop would misread the pointer bytes as the pair itself. Closure environment release is instead driven narrowly by the owning `Box<Closure>` release path.

**Source:** `src/compiler/mlir_gen_stmt.cpp#L506-L514`

### `type.drop.copy-bounded-typevar-not-droppable` — Copy-bounded type-param is non-droppable

A generic type-param `T` with an explicit `Copy` bound is provably non-droppable (Copy and Drop are mutually exclusive), so it contributes no drop glue when it appears as a tuple element, array element, or enum payload — even though a bare type-param otherwise defers its drop decision to monomorphization.

**Source:** `src/compiler/sema.cpp#L2784-L2802`

### `type.drop.move-closure-captures` — Captures moved into a move-closure still drop, at the closure binding's slot

A variable moved into a `move` closure remains use-after-move-checked, but its destructor still runs (the closure only borrows its storage): such captures drop at their owning closure binding's slot, in capture order, even if the binding's own drop was skipped — same-frame owners only. A `return` inside a closure body drops only the closure's own frames, never the enclosing function's captured locals.

**Source:** `src/compiler/sema.cpp#L3205-L3240`, `src/compiler/sema.cpp#L3254-L3258`

### `type.drop.moved-out-fields-skipped` — Partially-moved fields are excluded from a value's drop

When a local is dropped, fields (at any depth) that were moved out of it are excluded from its destructor: an exact field-path match skips that field, while a deeper moved path recurses and still drops the field's non-moved siblings.

**Source:** `src/compiler/sema.cpp#L3181-L3202`

### `type.drop.needs-drop-composition` — needs_drop is custom-drop-fn OR any droppable field

A type needs Drop iff it has a custom drop function, or it has fields that (transitively) need Drop: `needs_drop(T) = has_custom_drop(T) OR has_droppable_fields(T)`.

**Related:** `borrow.move.no-move-out-of-array-index`, `trait.copy.auto-derive-conditions`

**Source:** `src/compiler/sema_impl.hpp#L2260-L2264`

### `type.drop.no-auto-drop-suppresses-fields` — #[no_auto_drop] suppresses field destructors

A struct marked `#[no_auto_drop]` (the `ManuallyDrop<T>` lang-item shape) is treated as having no droppable fields: the compiler does not run its inner field destructors at scope exit.

**Source:** `src/compiler/sema.cpp#L2856-L2859`

### `type.drop.no-self-recursion` — self of a Drop body is not auto-dropped

The `self` parameter of a `Drop::drop` method is not auto-dropped at the end of that method's body — calling drop on `self` from inside its own drop body would be infinite recursion. Detected when the resolved drop fn equals the function currently being lowered (modulo package prefix and overload-disambiguation suffix).

**Source:** `src/compiler/sema.cpp#L3157-L3180`

### `type.drop.order-reverse-declaration` — Locals drop in reverse declaration order at scope exit

At scope exit, a frame's live (non-moved) locals are dropped in reverse of declaration order. Drops respect early-exit edges: `return` collects drops across enclosing frames up to (and not across) a closure boundary; `break`/`continue` collects drops up to and including the loop-body frame, stopping at a loop or closure boundary.

**Source:** `src/compiler/sema.cpp#L3213-L3273`

### `type.drop.owning-dst-droppable` — DST-backed value needs drop iff it is the owning form

A DST-backed value — `dyn Trait`, a slice, or a custom unsized struct (`DstRef`) — requires drop iff it is the OWNING form (`Box<dyn Trait>`/owning trait object, `Box<[T]>`/owning slice, `Box<CustomDst>`/owning DST), as tracked by the type's owning bit. The corresponding borrowed forms (`&dyn Trait`, `&[T]`, `&CustomDst`) never require drop.

**Source:** `src/compiler/mlir_gen_stmt.cpp#L479-L484`

### `type.drop.receiver-shapes` — Drop method accepted by-value or by-reference receiver

The drop method for type `T` is matched whether its single parameter is `T` by value, `&T`, or `&mut T` (`fn drop(&mut self)` / `fn drop(&self)` are the canonical stdlib shapes); the by-reference forms are accepted by peeling one reference level. A generic `impl<T> Drop for Foo<T>` is matched against a concrete `Foo<C>` by struct base-name (re-mangled to the concrete name at monomorphization).

**Source:** `src/compiler/sema.cpp#L2742-L2780`

### `type.drop.references-never-drop` — References and raw pointers never need drop

`&T`, `&mut T`, and raw pointer types never require a drop — a reference or raw pointer never owns its pointee.

**Source:** `src/compiler/mlir_gen_stmt.cpp#L476`

### `type.drop.same-package-impl` — Drop impl must belong to the same package as the type

A candidate `Drop` impl is selected for type `t` only if its target type belongs to the same package as `t` (an empty package on either side acts as a wildcard). Two distinct types sharing a bare concrete name across packages do not borrow each other's Drop impl.

**Source:** `src/compiler/sema.cpp#L2720-L2731`, `src/compiler/sema.cpp#L2778`

### `type.drop.transitive-aggregate-droppable` — Aggregate types are droppable if any owned member is

A type owns drop responsibility for its members: an array `[T;N]` is droppable iff `T` is; a tuple is droppable iff any element is; an enum is droppable iff any variant's payload field is (generic payloads concretized through the enum's type-params); a struct is droppable iff it has a drop fn or any field is (transitively) droppable. Owning `Box<dyn Trait>`, owning `Box<[T]>`, and owning `Box<Foo>` custom-DST are always droppable; their borrowed (`&dyn`, `&[T]`) counterparts are not.

**Source:** `src/compiler/sema.cpp#L2804-L2864`


## Types · `default`

### `type.default.array-elementwise-default` — Default for [E;N] is elementwise

The default value of an array type `[E; N]` is `[E::default(); N]`, recursing on the element type. A type has a default only if it (and every element) has a `Default` impl in scope.

**Source:** `src/compiler/sema_impl.hpp#L494-L498`


## Types · `freeze`

### `type.freeze.transitive-inline-no-cell` — Freeze = no interior mutability reachable through inline (non-pointer) structure

A type T is Freeze iff no `UnsafeCell` is reachable through T's own transitively-inline bytes (fields/payload/array elements) without crossing a pointer or reference; a pointer/reference field stops the recursion, so a container holding `&UnsafeCell`/`*UnsafeCell` (e.g. Arc/Rc) stays Freeze. The check is conservative: an unknown/unresolvable type is treated as NOT Freeze, so it is never wrongly given readonly/noalias attributes on a `&T` of that type.

**Note:** Doc-comment on a declaration; type_is_freeze's implementation body is defined outside this unit's line range.

**Source:** `src/compiler/mlir_gen_impl.hpp#L849-L857`


## Types · `inhabited`

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


## Types · `uninhabited`

### `type.uninhabited.definition` — Uninhabited type classification

A type is uninhabited (no value can exist) if it is `Never`, an empty enum or one whose every variant has an uninhabited payload, a struct/tuple with an uninhabited field, or `[T; N]` with N>0 and uninhabited T. Match arms over an uninhabited variant are elided from exhaustiveness checking.

**Source:** `src/compiler/sema_impl.hpp#L752-L757`


## Types · `never`

### `type.never.bang` — Never type `!`

`!` is a type (the never type, e.g. `fn diverge() -> !`), parsed as a type reference named `!`. A standalone BANG in type position is unambiguous since negation only occurs in expression position, never in type position.

**Source:** `tools/peg_gen_cpp/grammars/logos.peg#L1719-L1720`, `tools/peg_gen_cpp/grammars/logos.peg#L1776-L1780`


## Types · `never-return`

### `type.never-return.void-operand` — `return <e>` in a `!`-returning function emits an operand-less return

A function whose declared return type is the never type `!` has a zero-result signature. A `return <e>;` inside such a function still evaluates `<e>` (for its side effects — `<e>` may itself diverge and terminate the block), but if control survives, the emitted return carries no operand.

**Source:** `src/compiler/mlir_gen_stmt.cpp#L2169-L2179`


## Types · `dst`

### `type.dst.effective-after-instantiation` — A struct is custom-DST directly or after generic instantiation

A struct type is effectively custom-DST if the template was declared DST, or if generic instantiation bound the template's `?Sized` last-field type-var to an unsized type; effective-DST status governs `&S` -> fat DstRef canonicalisation at borrow/pointer-resolve time.

**Source:** `src/compiler/sema_impl.hpp#L794-L800`

### `type.dst.self-describing-borrow-is-fat` — Borrow of a #[self_describing] DST yields a fat DstRef

Borrowing (`&`/`&mut`) a `#[self_describing]` custom-DST struct produces the fat `DstRef` type matching its `&Foo` annotation; the fat length is materialized at codegen via `dst_len`.

**Source:** `src/compiler/sema_impl.hpp#L805-L809`

### `type.dst.self-describing-fat-ref-requires-impl` — Self-describing DST borrowed as fat ref must impl SelfDescribing

Borrowing a `#[self_describing]` effective-DST struct as a fat reference `&S`/`&mut S` (which materializes by recovering the tail length via `dst_len`) requires the struct to `impl SelfDescribing`; otherwise it is an error. A self-describing DST used only via raw pointers/byte arithmetic is not subject to this requirement.

**Source:** `src/compiler/sema.cpp#L3831-L3860`


## Types · `unsized`

### `type.unsized.by-value-rejected` — Unsized type by value is an error

A bare unsized slice `[T]` in a value position (param/return/field/alias/local) is a hard error unless an explicit unsized-ok context (e.g. a turbofish arg for a `T: ?Sized` parameter) is active; it must be wrapped in `&[T]`/`*const [T]`/`*mut [T]`.

**Source:** `src/compiler/sema.cpp#L5870-L5894`, `src/compiler/sema.cpp#L5999-L6008`

### `type.unsized.value-position-forbidden` — Bare unsized type at a value position is rejected unless the context explicitly permits it

Resolving a bare unsized type-syntax node (e.g. `[T]`, `dyn Trait`) standalone at a value position is an error by default. Only contexts that genuinely permit an unsized result set the resolver's unsized-ok flag first (a turbofish type-argument bound for a `T: ?Sized` parameter, or an impl Self-type at a `?Sized` position); the flag is off by default so unsized types cannot silently slip into value positions.

**Source:** `src/compiler/sema_impl.hpp#L3666-L3671`


## Types · `recursion`

### `type.recursion.enum-finite-size` — Enum variant payload may not contain itself by value

An enum type is ill-formed if any variant payload type transitively contains the enum itself by value (through Struct/ZonedStruct/Enum/Tuple, not through indirection). Such recursion must be broken by boxing the payload behind a pointer (`*const T`).

**Source:** `src/compiler/sema_impl.hpp#L1729-L1749`, `src/compiler/sema_impl.hpp#L1742-L1746`

### `type.recursion.indirection-breaks-cycle` — Pointers/references (and arrays) break size-cycle detection

The size-cycle traversal (`walk`) descends only through inline Struct/ZonedStruct, Enum, and Tuple field/element types; it does not descend into Array element types, pointer fields, or reference fields, so self-reference occurring only through those forms is treated as finite by this check.

**Source:** `src/compiler/sema_impl.hpp#L1690-L1706`

### `type.recursion.struct-finite-size` — Struct may not contain itself by value

A struct type is ill-formed if its inline (by-value) field graph — transitively through Struct/ZonedStruct, Enum, and Tuple field types — contains itself: white/gray/black cycle detection over field types rejects an infinite-size type. Fix: indirect via a pointer or reference (`&T`).

**Source:** `src/compiler/sema_impl.hpp#L1690-L1707`, `src/compiler/sema_impl.hpp#L1708-L1728`, `src/compiler/sema_impl.hpp#L1750-L1751`


## Types · `recursive`

### `type.recursive.by-value-cycle` — Recursive by-value type cycle detection

A struct or enum that (transitively) contains itself through only by-value field/payload edges (Struct, ZonedStruct, Enum) is a forbidden recursive-value type. Pointer, `&`, and `&mut` edges break the cycle (they lower to fixed-size pointers) and are permitted.

```logos
struct S { next: S }      // error: recursive value type
struct S { next: Box<S> } // ok (pointer breaks cycle)
```

**Source:** `src/compiler/sema_impl.hpp#L1511-L1521`, `src/compiler/sema_impl.hpp#L1665-L1685`


## Types · `rec`

### `type.rec.no-by-value-cycle` — recursive by-value type cycles are rejected

A struct/enum graph that contains a by-value (non-indirected) cycle is an error; recursion through a type of statically unknown/infinite size must be broken by an indirection (e.g. a pointer/box).

**Note:** check_recursive_value_types() body is defined outside this unit; only its invocation site is evidenced here.

**Source:** `src/compiler/sema_collect.cpp#L565-L567`


## Types · `pin`

### `type.pin.non-movable-classification` — Non-movable (location-anchored) type classification

A type is non-movable iff: it is a `#[pinned]` struct; or a `#[zoned2]` struct (self-relative pointer fields anchored to their own slot); or it inlines (transitively through struct/tuple/array by-value fields, not through pointers/references) a `#[rel_ptr]` or `#[pinned]` field. A `#[rel_ptr]` type itself is movable (its value-form is the resolved absolute pointer); it counts as non-movable only when embedded as an inline field.

**Divergence:** Logos addition (zones/pin): `#[pinned]`/`#[zoned2]`/`#[rel_ptr]` anchoring has no Rust analog.

**Source:** `src/compiler/sema_impl.hpp#L2104-L2154`


## Types · `tagged`

### `type.tagged.thin-dispatch` — Tag-dispatched thin pointer type

`&tagged<SimpleType> IDENT` denotes a thin tag-dispatched pointer to a trait (IDENT names the trait): the type_code tag is stored immediately preceding the pointee object in memory; call sites read the tag, look up a dispatch table, and call indirectly.

**Source:** `tools/peg_gen_cpp/grammars/logos.peg#L1550-L1554`

### `type.tagged.thin-pointer` — tagged thin pointer type

`&tagged<T> Name` is a thin tag-dispatched pointer: a type_code tag is stored in memory before the object, and call sites read the tag, look up the dispatch table, and call indirectly.

**Divergence:** A6

**Source:** `tools/peg_gen_cpp/grammars/logos.peg#L1490-L1494`

### `type.tagged.thin-pointer-dispatch` — &`tagged<TS>` Trait: tag-dispatched thin pointer

`&tagged<TS> Trait` (TAGGED_TYPE) denotes a thin (single-word) pointer to a trait object whose concrete implementation is selected by a tag drawn from the type-set `TS`, as opposed to a fat (vtable-carrying) `&dyn Trait` pointer.

**Note:** Only the node-code comment is in this slice; dispatch mechanics (how TS maps tag→impl) are implemented elsewhere.

**Source:** `tools/peg_gen_cpp/grammars/logos.peg#L220`

### `type.tagged.thin-ptr-dispatch` — &`tagged<TS>` Trait

`&tagged<TS> Trait` resolves to a thin TaggedPtr with tag-based dispatch; Trait must be a registered trait and TS must resolve to a concrete struct type, else hard error.

**Divergence:** Logos-only tagged-dispatch pointer.

**Source:** `src/compiler/sema.cpp#L6021-L6039`


## Types · `anyval`

### `type.anyval.lowered-as-i32` — AnyVal always lowers to a scalar i32

The `AnyVal` type lowers UNIFORMLY to a scalar i32 — as a standalone value ({4,4} layout) and as a struct field — never wrapped in an aggregate (e.g. never `!llvm.struct<"AnyVal",(i32)>`), so that field loads/stores and argument-passing treat it as a plain i32 tag word rather than a 1-field struct value.

**Divergence:** A5

**Source:** `src/compiler/mlir_gen_types.cpp#L32`, `src/compiler/mlir_gen_types.cpp#L201-L212`, `src/compiler/mlir_gen_types.cpp#L447`

### `type.anyval.repr-i32` — AnyVal is represented as a 32-bit value

A value of type AnyVal is represented as a 32-bit integer in both parameter and return position.

**Note:** i32 likely encodes a handle/index into an AnyVal table; exact semantics inferred from representation only.

**Source:** `src/compiler/mlir_gen_fn.cpp#L67`, `src/compiler/mlir_gen_fn.cpp#L98-L101`, `src/compiler/mlir_gen_fn.cpp#L113-L115`


## Types · `writ`

### `type.writ.container-kinds` — Writ-view type recognition

writ_view_inner(t): t, optionally stripped of one outer Ref/MutRef layer, is a "Writ view" type iff its Kind is Struct or ZonedStruct and its struct_name ∈ {Writ, WritView, WritStatic, Rc} (`Rc<Writ>` is the writ runtime container). Any other shape yields no inner view type.

**Source:** `src/compiler/sema_impl.hpp#L4177-L4195`

### `type.writ.lit-and-array-map` — Writ literal / typed array / typed map types

`@{...}` at type position is a WritStatic value literal type (LIT_WSTATIC). `<Elem>[]` is a Writ typed-array type and `<K[,V]>{}` is a Writ typed-map type (used in `as` casts).

**Divergence:** A6

**Source:** `tools/peg_gen_cpp/grammars/logos.peg#L1451-L1473`


## Types · `writ-arr`

### `type.writ-arr.elem-set` — Writ typed array type `<Elem>[]`

`<Elem>[]` resolves to a generic struct `WritArr<elem>`; Elem must be one of I8/U8/I16/U16/I32/U32/I64/U64/F32/F64 (mapped to the Logos primitive), else hard error.

**Divergence:** Logos-only Writ container type-expression.

**Source:** `src/compiler/sema.cpp#L6234-L6266`


## Types · `writ-map`

### `type.writ-map.key-val-set` — Writ typed map type `<K,V>{}`

`<K,V>{}` resolves to `WritMap<key,val>`; key must be I32/U32/I64/U64 and value must be `AnyVal` (default), else hard error.

**Divergence:** Logos-only Writ container type-expression.

**Source:** `src/compiler/sema.cpp#L6267-L6297`


## Types · `wstatic`

### `type.wstatic.literal-arg` — WritStatic literal in type-arg position

A WritStatic literal `Foo::<@{...}>` (or a bare writ-lit value-AST in const recognition) resolves to the value's WritStatic type; a missing payload is a hard error.

**Divergence:** Logos-only WritStatic value-as-type-arg.

**Source:** `src/compiler/sema.cpp#L6370-L6386`


## Types · `writstatic`

### `type.writstatic.const-decl` — WritStatic literal at type position

A bare `@{...}` (WritStatic literal) is accepted directly at type position (e.g. `type Cfg = @{...};`), lowered via the same LIT_WSTATIC code used for the type-argument form. For naming a top-level WritStatic type-level value the dedicated spelling is `pub const X: WritStatic = @{...};`; the legacy `pub type X = @{...};` spelling no longer parses for this purpose.

**Note:** The claim that the legacy `pub type X = @{...}` form 'no longer parses' is asserted by the source comment itself (not independently verified by finding the rejecting production in this slice).

**Source:** `tools/peg_gen_cpp/grammars/logos.peg#L1476-L1480`, `tools/peg_gen_cpp/grammars/logos.peg#L1511-L1514`


## Types · `cfg-slot`

### `type.cfg-slot.assoc-projection` — Associated-type projection off a CFG-slot type

`<type:CFG.SLOT>::IDENT` is an associated-type reference (ASSOC_TYPE_REF) whose RECEIVER is the CfgSlotType projection; tried before the bare cfg_slot_type alternative so the `::Field` tail is consumed.

**Source:** `tools/peg_gen_cpp/grammars/logos.peg#L1506-L1509`

### `type.cfg-slot.const-generic-defer` — Deferred cfg-slot when base is a const type-param

When `CFG` names a const-generic type-parameter of the enclosing item, `<type:CFG.path>` is NOT resolved eagerly; it yields a deferred CfgSlotType carrying the CFG ident and an encoded path, which monomorphization resolves once the parameter is bound to a concrete WritStatic value.

**Divergence:** A6

**Note:** Logos-specific; const-generic-of-WritStatic kind.

**Source:** `src/compiler/sema.cpp#L4972-L4981`, `src/compiler/sema.cpp#L4982-L4983`, `src/compiler/sema.cpp#L5055`, `src/compiler/sema.cpp#L5101-L5105`

### `type.cfg-slot.const-param-must-be-writstatic` — cfg-slot base type-param must be const WritStatic

If `CFG` in `<type:CFG.path>` names a type-parameter, that parameter must be declared `const CFG: WritStatic`; otherwise a diagnostic is raised (the param must be a const-generic whose type is the WritStatic struct).

**Divergence:** A6

**Note:** Logos-specific WritStatic const-generic requirement.

**Source:** `src/compiler/sema.cpp#L4985-L5004`

### `type.cfg-slot.eager-alias-resolution` — Eager cfg-slot resolution against a WStaticLit alias

When `CFG` is not a type-param but resolves to a type alias bound to a WStaticLit (`pub type Cfg = @{...};`), the path is walked eagerly through that literal's registered Writ value at resolution time, producing the concrete projected type directly.

**Divergence:** A6

**Note:** Logos-specific.

**Source:** `src/compiler/sema.cpp#L4974-L4976`, `src/compiler/sema.cpp#L5055-L5099`

### `type.cfg-slot.path-extraction` — Config-slot type projection

`<type:CFG.path>` extracts a type from a WritStatic-typed binding `CFG` by walking a path of steps; each step is a struct-field access by name (on a string-keyed Writ map), an integer-field access by index (on an int-keyed Writ map), or an array index (on a Writ array). The path must be non-empty. The final reached Writ value must be a Type value; its named type is then resolved as the result.

**Divergence:** A6

**Note:** Logos-specific construct (no Rust analogue); semantics inferred from path-walk logic.

**Source:** `src/compiler/sema.cpp#L4969-L4981`, `src/compiler/sema.cpp#L5038-L5041`, `src/compiler/sema.cpp#L5067-L5096`

### `type.cfg-slot.projection` — Type-level cfg-slot projection

`<type:CFG.path>` projects, at mono-time, the type stored at a path within a WritStatic-typed type-level binding. Path steps are `.IDENT` (string key), `.INTEGER` (int key) and `.[INTEGER]` (array index). At least one path step is required. `<type:CFG.SLOT>::Assoc` projects an associated type on the slot base.

**Divergence:** A6

**Source:** `tools/peg_gen_cpp/grammars/logos.peg#L1428-L1449`


## Types · `closure`

### `type.closure.fat-fn-env-repr` — Closures represent uniformly as a 16-byte {fn,env} fat pair

Every closure value (the `Closure` kind, which also covers `dyn Fn`/`FnMut`/`FnOnce`) has a FIXED 16-byte {fn-ptr, env-ptr} storage representation — not a per-closure anonymous capture struct sized by its captures. Stored inline in aggregates/arrays exactly like a Slice; a plain closure value elsewhere is a pointer to this 16-byte storage.

**Divergence:** A10

**Source:** `src/compiler/mlir_gen_types.cpp#L111-L112`, `src/compiler/mlir_gen_types.cpp#L130`, `src/compiler/mlir_gen_types.cpp#L314-L321`, `src/compiler/mlir_gen_types.cpp#L468-L469`

### `type.closure.syntax` — Closure type `|T,...| -> R`

`|T1, T2, ...| -> R` is a closure type (parameter-type annotation position). `|| -> R` (zero-arg) is a distinct alternative because the lexer fuses `||` into one token.

**Source:** `tools/peg_gen_cpp/grammars/logos.peg#L1717-L1724`

### `type.closure.type` — Closure type

`|T1, T2| -> R` is a closure type used in parameter annotations; the zero-arg form `|| -> R` is accepted (the `||` token is split).

**Divergence:** A6: Rust spells closures via Fn-family bounds; Logos has a dedicated `|..|->R` closure type syntax.

**Source:** `tools/peg_gen_cpp/grammars/logos.peg#L1657-L1664`


## Types · `closure-arg`

### `type.closure-arg.hint-from-formal` — Closure/literal argument types hinted from method formal parameter

Each argument is lowered with a type hint derived from the corresponding method formal: a single Ref/MutRef wrapper on the formal is stripped, then a function/closure formal seeds the closure hint, a generic Enum/Struct (with type-args) seeds the enum/struct hint, and a Tuple formal seeds the tuple hint. An Fn-family-bounded bare type-parameter formal synthesizes a Closure hint from the bound's signature so an untyped closure (`|i|`) infers its parameter types.

**Source:** `src/compiler/sema_expr.cpp#L7942-L7986`, `src/compiler/sema_expr.cpp#L7958-L7979`


## Types · `closure-type`

### `type.closure-type.params-ret` — Closure type literal

A closure type literal resolves to Closure with the listed parameter types and a return type defaulting to unit/void when absent.

**Source:** `src/compiler/sema.cpp#L6067-L6082`


## Types · `impl-trait`

### `type.impl-trait.param` — impl Trait type

`impl Trait`, `impl Trait<args>`, and `impl Fn(args) [-> R]` are accepted in type position; an impl-Trait parameter desugars to a synthetic generic parameter bounded by the same trait (Fn-family args→PARAMS, return→RET_TYPE, generic args→TYPE_PARAMS).

```logos
fn f(x: impl Display) {}
```

**Source:** `tools/peg_gen_cpp/grammars/logos.peg#L1395-L1414`

### `type.impl-trait.param-desugar` — impl Trait position semantics

`impl Trait` in parameter position desugars to a fresh implicitly-Sized synthetic generic type-param bounded by Trait (a once-used generic, capturing full bound args); in return position it resolves to the dedicated ImplTrait type.

**Source:** `src/compiler/sema.cpp#L6041-L6065`

### `type.impl-trait.param-position` — `impl Trait` in parameter/type position

`impl IDENT[<Args>]` / `impl IDENT(Args) -> Ret` (mirroring trait_bound's forms, including the Fn-family paren+arrow shape) in a type position desugars to a synthetic generic parameter bounded by that trait; Fn-family arg types land in PARAMS/RET_TYPE, generic-trait arguments in TYPE_PARAMS.

```logos
f: impl Fn(i64) -> i64
x: impl Display
it: impl Iterator<Item = i64>
```

**Source:** `tools/peg_gen_cpp/grammars/logos.peg#L1456-L1474`

### `type.impl-trait.param-position-forbidden` — `impl Trait` not allowed at parameter position

`impl Trait` is not supported in parameter position; use an explicit generic `fn f<T: Trait>(x: T)` or `&dyn Trait` instead.

**Divergence:** Logos restriction: Rust supports argument-position impl Trait (APIT).

**Source:** `src/compiler/sema_decl.cpp#L309-L318`


## Types · `literal`

### `type.literal.float-default-f64` — Unresolved float literal defaults to f64

A float literal (`FloatLit`) whose concrete type was not pinned down lowers/layouts identically to `f64` (value type f64, layout {8,8}) — the default floating-point type.

**Source:** `src/compiler/mlir_gen_types.cpp#L65`, `src/compiler/mlir_gen_types.cpp#L462-L465`

### `type.literal.int-default-i32` — Unresolved integer literal defaults to i32

An integer literal (`IntLit`) whose concrete type was not pinned down lowers/layouts identically to `i32` (value type i32, layout {4,4}) — the default integer type.

**Source:** `src/compiler/mlir_gen_types.cpp#L64`, `src/compiler/mlir_gen_types.cpp#L459-L460`


## Types · `lit-int`

### `type.lit-int.const-generic-arg` — Integer literal as type

An integer literal in type position resolves to an IntLit type carrying the (optionally negated) parsed value, for use as a const-generic argument.

**Source:** `src/compiler/sema.cpp#L6127-L6138`


## Types · `intlit`

### `type.intlit.fits-range` — Integer-literal range fit per target type

A constant integer value v fits a target integer kind iff it lies within that kind's representable range: i8 [-128,127], u8 [0,255], i16 [-32768,32767], u16 [0,65535], i24 [-2^23,2^23-1], u24 [0,2^24-1], i32 [INT32_MIN,INT32_MAX], u32 [0,UINT32_MAX], i56 [-2^55,2^55-1], u56 [0,2^56-1], i64/i128/isize(64-bit) all int64 values, u64/u128/usize require v>=0. On a 32-bit target usize requires v in [0,UINT32_MAX] and isize in [INT32_MIN,INT32_MAX].

**Related:** `type.integer.bit-width`

**Source:** `src/compiler/sema_impl.hpp#L4544-L4568`


## Types · `method`

### `type.method.recv-autoderef-resolution` — Receiver dereferenced for method resolution

For method resolution and struct-type-arg extraction, a receiver of reference type (`&`/`&mut`) or raw-pointer type is dereferenced to its pointee.

**Source:** `src/compiler/sema_expr.cpp#L8742-L8749`, `src/compiler/sema_expr.cpp#L8805-L8810`, `src/compiler/sema_expr.cpp#L8988-L8989`

### `type.method.return-subst` — Method return type substitution

The type of a method-call expression is the method's declared return type with the receiver/method type-var substitution and lifetime substitution applied.

**Source:** `src/compiler/sema_expr.cpp#L9102-L9105`, `src/compiler/sema_expr.cpp#L9143`


## Types · `method-arg`

### `type.method-arg.compat-diagnostic` — Method argument type must be compatible with the (substituted) param type

After coercion, each argument's static type must satisfy `types_compatible` against the method's declared (struct/enum-substituted) param type, unless either side is already `Error`-kinded; otherwise it is an error `method '{}' arg {}: expected {}, got {}`.

**Source:** `src/compiler/sema_expr.cpp#L8935-L8943`


## Types · `method-recv`

### `type.method-recv.deref-before-lookup` — Receiver reference stripped to its pointee for nominal method lookup

For method-formal hinting and dispatch, a receiver of reference type (`&T`/`&mut T`) is reduced to its pointee T before extracting the struct/enum name and binding the receiver's nominal type-arguments into the substitution.

**Source:** `src/compiler/sema_expr.cpp#L7874-L7894`


## Types · `primitive-method`

### `type.primitive-method.mangled-lookup` — Primitive-receiver methods resolved via TypeName__method with receiver-shape variants

For a receiver with no struct name, the method is looked up as `<type-name>__<method>` matched against the actual argument signature; if no direct match, receiver-shape variants are tried in order: `&T`, `&mut T`, `*const T`, `*mut T`, and (for reference receivers) the `$ref_<...>` / `$mut_ref_<...>` mangling used to register `impl Trait for &T` / `&mut T`.

**Source:** `src/compiler/sema_expr.cpp#L8089-L8130`


## Types · `binop`

### `type.binop.arith-numeric` — Arithmetic operators require numeric operands

Arithmetic operators {+,-,*,/,%} require both operands to be numeric; the result type is the unified integer type of the operands when both are integers, otherwise unify_numeric, with a TypeVar operand propagated as the result when the other is an integer literal.

**Source:** `src/compiler/sema_expr.cpp#L2304-L2383`

### `type.binop.bitwise-integer-or-bool` — Bitwise/shift operands must be integer (or bool for bitwise-only)

Bitwise operators {&,|,^} require integer or bool operands; shift operators {`<<,>>`} require integer operands only. The result type is the unified integer type of the operands.

**Divergence:** Matches Rust `impl BitAnd/BitOr/BitXor for bool`.

**Source:** `src/compiler/sema_expr.cpp#L2384-L2416`, `src/compiler/sema_expr.cpp#L2454-L2454`

### `type.binop.comparison-bool` — Comparison operators yield bool with compatible operands

Comparison operators {==,!=,`<,<=,>,>`=} require the two operand types to be mutually compatible (in either direction) and produce type bool.

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
x * break
```

**Source:** `src/compiler/sema_expr.cpp#L2255-L2261`


## Types · `return`

### `type.return.datanode-by-value-forbidden` — DataNode eidos cannot be returned by value

A non-plain zoned-struct DataNode type (`#[data]` node) cannot be a by-value return type; the function must return `DataRef<T>` instead. The check looks through array nesting to the innermost element.

**Source:** `src/compiler/sema_decl.cpp#L479-L500`

### `type.return.non-movable-by-value-forbidden` — Location-anchored types cannot be returned by value

A type that is non-movable — containing a self-relative `#[rel_ptr]` field, or being `#[pinned]` — may not be returned by value; return a pointer (`*mut T` / `&T`) into its zone segment instead. (Crossing a function boundary by value would invalidate the self-relative anchor.)

**Divergence:** A8

**Source:** `src/compiler/sema_decl.cpp#L501-L513`


## Types · `sig`

### `type.sig.underscore-rejected` — `_` rejected in fn signature type positions

Within a fn's signature (parameter types and return type), resolution runs inside an ItemSignatureGuard (in_item_signature_) that rejects the inferred-type placeholder `_` (E0121) when it appears in those positions.

**Source:** `src/compiler/sema_collect.cpp#L4872-L4874`, `src/compiler/sema_collect.cpp#L4879-L4884`


## Types · `field`

### `type.field.placeholder-type-rejected` — Field types may not be inferred

A struct field's type is resolved under ItemSignatureGuard (in_item_signature_ = true): the `_` placeholder type is rejected in this position rather than deferred, since a struct field is part of the item's signature, not an inferable local context (E0121).

**Source:** `src/compiler/sema_collect.cpp#L4264-L4269`


## Types · `pointee`

### `type.pointee.forms` — Pointee-type alternation

A pointee type (target of `&`/`*`/etc.) is one of: ref-type, array type, tuple type, unit type, qualified associated type, associated-type reference, fn-pointer type, or a simple type reference.

**Source:** `tools/peg_gen_cpp/grammars/logos.peg#L1715`


## Types · `ref-repr`

### `type.ref-repr.thin-vs-fat-classification` — Reference SSA value is always thin; fat pair lives only in storage

For every reference-like kind, the SSA/register VALUE type is uniformly a thin pointer — the fat {data,meta} pair (for Slice/Closure/TraitObject/DstRef) exists only in memory storage (a struct field, array element, or local slot), never as a register value; a plain reference value is a pointer TO that storage.

**Source:** `src/compiler/mlir_gen_types.cpp#L33-L38`, `src/compiler/mlir_gen_types.cpp#L448-L453`


## Types · `paren`

### `type.paren.grouping` — Parenthesized type

`(T)` is a parenthesis-grouped type, distinct from unit `()`, the 1-tuple `(T,)`, and the n-tuple `(T1, T2, ...)`. Sema unwraps a PAREN_TYPE to its inner TYPE.

**Source:** `tools/peg_gen_cpp/grammars/logos.peg#L1483-L1486`

### `type.paren.transparent` — Parenthesized type is transparent

`(T)` resolves structurally identical to `T`.

**Source:** `src/compiler/sema.cpp#L5896-L5900`

### `type.paren.unwrap` — Parenthesized type

`( T )` is a parenthesized type, distinct from `()` (unit), `(T,)` (1-tuple) and `(T1,T2)` (n-tuple); sema unwraps it to its inner type T.

**Source:** `tools/peg_gen_cpp/grammars/logos.peg#L1423-L1426`

### `type.paren.unwrap-to-inner` — Paren-wrapped type unwraps to inner

A parenthesized type expression `(T)` (PAREN_TYPE) is structurally equivalent to `T`; sema unwraps it to the inner type.

**Source:** `tools/peg_gen_cpp/grammars/logos.peg#L291`


## Types · `antiquot`

### `type.antiquot.quote-only` — Antiquotation valid only inside quote_ty!

A type antiquotation `$name` or pack-splice `$name...` is a hard error outside a `quote_ty! { ... }` context.

**Source:** `src/compiler/sema.cpp#L5660-L5671`

### `type.antiquot.quote-ty` — Type-position antiquotation

`$IDENT` at a type position is an antiquotation node (ANTIQUOT_TYPE), consumed by sema's quote_ty! lowering; used outside a `quote_ty! { ... }` body it is rejected by resolve_type (per sema, outside this slice).

**Source:** `tools/peg_gen_cpp/grammars/logos.peg#L1516-L1519`

### `type.antiquot.quote-ty-only` — Type antiquotation

`$ident` in type position is a type antiquotation valid only inside `quote_ty! { ... }`; resolving it elsewhere is an error.

**Divergence:** A6

**Source:** `tools/peg_gen_cpp/grammars/logos.peg#L1456-L1459`


## Types · `pack-expand`

### `type.pack-expand.in-scope-typevar` — Pack expansion in type-arg position

`T...` in type-arg position resolves to the in-scope variadic type parameter's TypeVar; an undefined pack name is a hard error.

**Source:** `src/compiler/sema.cpp#L6299-L6310`


## Types · `self-describing`

### `type.self-describing.dst-len-required` — #[self_describing] types must implement SelfDescribing::dst_len

For a `#[self_describing]` struct, the tail length used wherever a slice/len is projected off a thin DstRef to it is recovered by calling that concrete type's monomorphized `dst_len` method on the thin header pointer. Every `#[self_describing]` struct is expected to have exactly one `SelfDescribing::dst_len` implementation (enforced elsewhere, in sema); this codegen path falls back to a defensive length of 0 only if no such symbol is found.

**Note:** The enforcement of "every #[self_describing] struct has a dst_len impl" happens in sema, not in this slice; this unit only shows the codegen-side lookup and its defensive fallback.

**Source:** `src/compiler/mlir_gen_expr.cpp#L5185-L5218`


## Types · `if`

### `expr.if.intlit-result-overflow-i64` — Integer-literal if-result widens to i64 on i32 overflow

If an `if` expression's result type is an unresolved integer literal and either branch's literal value exceeds the i32 range, the result type is i64.

**Source:** `src/compiler/sema_expr.cpp#L14038-L14052`


## Types · `let`

### `type.let.floatlit-default-f64` — Unannotated float literal binding defaults to f64

An unannotated let whose RHS is a float literal binds at type f64.

**Source:** `src/compiler/sema_stmt.cpp#L2203-L2207`

### `type.let.intlit-default-i32` — Unannotated integer literal binding defaults to i32 (i64 on overflow)

An unannotated let whose RHS is an integer literal binds at type i32, upgraded to i64 when the literal value falls outside the i32 range.

**Divergence:** Rust defaults unconstrained integer literals to i32 but never silently widens to i64 on overflow (it is a compile error); Logos auto-upgrades to i64.

**Source:** `src/compiler/sema_stmt.cpp#L2191-L2202`


---

## Layout

The physical representation of each type: size, alignment, field and variant placement, pointer and reference reprs, niche / enum encodings, and ABI.


## Layout · `abi`

### `layout.abi.aggregate-byte-size` — ABI byte size of arrays, tuples, structs, enums

Array size = N × elem size. Tuple/struct size = fields laid out sequentially, each aligned to min(field-size, 8), with the total padded to the max field alignment. Enum size = 4 (i32 tag) + the maximum total payload size across variants (void payload components contribute 0). Recursive struct fields are cycle-guarded to pointer size 8.

**Note:** Comment notes enum layout is a simplification mirroring mlir-gen.

**Source:** `src/compiler/sema.cpp#L3884-L3931`

### `layout.abi.aggregate-field-alignment` — Tuples and structs lay fields out sequentially with natural alignment, capped at 8

For a tuple/struct, fields are placed in declaration order; before each field the running offset is rounded up to that field's alignment, where alignment = min(field-size, 8) (treating zero-size as alignment 1). The aggregate's size is the final offset rounded up to the maximum field alignment encountered.

**Note:** Alignment is derived as min(size,8) rather than a separate per-type alignment; matches a same-as-size convention for scalars but may diverge for over-aligned types.

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


## Layout · `zero-size`

### `layout.zero-size.void-never` — Void/Never/unit-field are zero-sized, no SSA value

`Void` (absence of a return value) and `Never` (`!`, an uninhabited/diverging type) both have layout {0,1} and lower to no SSA value at all (a diverging expression emits its own terminator instead of a value). When either occurs as a concrete struct FIELD's type (e.g. a `!`-typed Err payload, or a unit `()` field), it is materialized as a genuine zero-size `[i8; 0]` storage slot so the aggregate's other field offsets stay correct, even though it is never read.

**Divergence:** A12

**Source:** `src/compiler/mlir_gen_types.cpp#L40-L43`, `src/compiler/mlir_gen_types.cpp#L322-L343`, `src/compiler/mlir_gen_types.cpp#L455`


## Layout · `int`

### `layout.int.fixed-widths` — Fixed-width scalar sizes/alignments

Scalar type layout is fixed and self-aligned: bool/i8/u8={1,1}; i16/u16={2,2}; i24/u24={3,1}; i32/u32/f32/char={4,4}; i56/u56={7,1}; i64/u64/f64={8,8}; i128/u128={16,16}; usize/isize={ptr-width,ptr-width}. The odd widths i24/u24/i56/u56 have byte size = ceil(bits/8) but align 1 (packed, not natively aligned).

**Divergence:** A11

**Source:** `src/compiler/mlir_gen_types.cpp#L44-L63`, `src/compiler/mlir_gen_types.cpp#L456-L466`


## Layout · `pointer`

### `layout.pointer.target-64-bit` — Pointer width is 64-bit; usize/isize follow it

The target pointer width is 64 bits. `usize` has underlying integer kind u64 and `isize` has i64 (would be u32/i32 on a 32-bit target).

**Source:** `src/compiler/sema_impl.hpp#L208-L220`


## Layout · `non-null`

### `layout.non-null.option-nullptr-niche` — #[non_null] enables Option NullPtr niche

A `#[non_null]` struct is a single 8-byte pointer wrapper whose pointer is guaranteed non-null (Box/Rc/Arc shape), letting `Option<ThisStruct>` use the NullPtr niche (None = null pointer, pointer-sized enum). It is an opt-in soundness contract asserted by the author.

**Source:** `src/compiler/sema_impl.hpp#L2481-L2487`


## Layout · `fnptr`

### `layout.fnptr.bare-call-no-env` — Bare fn-ptr call passes no environment

A bare function-pointer call `fn_ptr(args...)` (EFnPtrCall) invokes the callee with exactly the user arguments, no hidden environment operand. This is distinct from a closure call (EClosureCall), which loads {fn_ptr, env_ptr} from the closure value and prepends env_ptr to the argument list before the indirect call.

**Related:** `layout.closure.fn-env-pair`

**Source:** `src/compiler/mlir_gen_expr.cpp#L4868-L4890`, `src/compiler/mlir_gen_expr.cpp#L4830-L4845`


## Layout · `fat-ptr`

### `layout.fat-ptr.sixteen-byte` — Fat-pointer kinds are 16 bytes, 8-byte aligned

Slice (incl. `str`), Closure, TraitObject (`dyn Trait`), and DstRef (custom-DST fat pointer) each have storage layout {16,8} — a two-word {data,meta} pair. Thin pointer-like kinds (Ptr/Ref/MutRef/FnPtr/TaggedPtr/Usize/Isize) are {8,8}.

**Source:** `src/compiler/mlir_gen_types.cpp#L462-L469`, `src/compiler/mlir_gen_types.cpp#L296-L321`


## Layout · `fatptr`

### `layout.fatptr.assign-pair-copy` — Slice/Closure/TraitObject-valued assignment copies the full 16-byte fat pair

Slice (`&[T]`), Closure, and TraitObject values are represented as a 16-byte `{data, len|vtable}` pair. Assigning such a value to a place of that type copies both words (memcpy of 16 bytes); a plain pointer store would leave the second word (len/vtable) stale.

**Source:** `src/compiler/mlir_gen_stmt.cpp#L2120-L2135`

### `layout.fatptr.return-by-value` — Already-fat-typed return values are returned by value as the 16-byte aggregate

Returning a value whose static type is already a TraitObject, or whose declared return type is Slice, loads the 16-byte `{data,len|vtable}` pair from its storage pointer (if not already a loaded aggregate) and returns it by value — the function's MLIR-level return type is the 16-byte struct, not a pointer.

**Source:** `src/compiler/mlir_gen_stmt.cpp#L2219-L2256`


## Layout · `ref`

### `layout.ref.fat-lower-memcpy-16` — Fat reference lowering is a 16-byte memcpy

Lowering a fat reference value (FatDyn/FatSlice/FatCustomDst, always a 16-byte {data,meta} pair) into its storage slot is a raw 16-byte memcpy from the source pair's address to the slot, not a field-by-field store.

**Source:** `src/compiler/mlir_gen_expr.cpp#L5178-L5183`

### `layout.ref.fat-pointer-sixteen-bytes` — Fat reference layout

Every fat reference — slice `&[T]`/`str`, trait object `&dyn Tr`, closure value, custom-DST ref, and zone-mut ref — is a two-word pair of {size=16, align=8}.

**Source:** `src/compiler/mlir_gen_types.cpp#L617-L621`, `src/compiler/mlir_gen_types.cpp#L632-L636`

### `layout.ref.rel-offset-eight-bytes` — Relative-offset reference layout

A relative-offset (self-relative) reference is stored as a single i64 offset word: {size=8, align=8}.

**Divergence:** Logos self-relative pointers (zoned/Writ); no Rust equivalent.

**Source:** `src/compiler/mlir_gen_types.cpp#L622`, `src/compiler/mlir_gen_types.cpp#L637`

### `layout.ref.relptr-self-relative` — Self-relative reference representation

RefReprKind::RelOffset stores a reference as an i64 byte offset relative to its own storage slot address (the slot is the anchor). Materialize: load the offset, GEP the slot's own address by it to get an absolute thin pointer. Lower: compute offset = target_addr − slot_addr and store it at the slot. A null target is encoded as off = −slot_addr, which materializes back to address 0.

**Source:** `src/compiler/mlir_gen_expr.cpp#L5149-L5157`, `src/compiler/mlir_gen_expr.cpp#L5168-L5177`

### `layout.ref.repr-kinds` — Reference representation kinds

A reference value is lowered under one of: ThinPtr/NotARef (the value IS the data pointer, no metadata), FatDyn ({data ptr, vtable ptr} pair), or FatSlice/FatCustomDst ({data ptr, i64 len} pair) — the fat pair is materialized via an entry alloca holding fields {0:data, 1:meta}.

**Source:** `src/compiler/mlir_gen_expr.cpp#L4924-L4947`, `src/compiler/mlir_gen_expr.cpp#L5284-L5301`

### `layout.ref.self-relative-offset` — Self-relative (writ / rel_ptr) pointers store a byte offset

A self-relative pointer (the writ / `#[rel_ptr]` zoned pointer) is stored as an i64 byte offset from its own storage slot's address; materialization = slot_address + load_i64(slot); lowering a target pointer stores (target_address − slot_address). A plain thin-pointer struct field is upgraded to this self-relative storage, even without an explicit `#[rel_ptr]` tag, when its owning struct is `#[zoned2]` (the untagged zoned-reference case).

**Divergence:** Logos addition: self-relative zoned pointers, no Rust analogue.

**Source:** `src/compiler/mlir_gen_impl.hpp#L884-L887`, `src/compiler/mlir_gen_impl.hpp#L890-L895`

### `layout.ref.thin-pointer` — Plain references and fn pointers are thin 8-byte pointers

A *T, &T, &mut T, or function pointer to a Sized pointee has a thin 8-byte pointer representation with no side metadata.

**Source:** `src/compiler/mlir_gen_impl.hpp#L876`

### `layout.ref.thin-pointer-eight-bytes` — Thin reference layout

A thin reference (plain `&T`/`&mut T`/`*T`/fn-ptr to a Sized pointee) occupies one machine pointer: {size=8, align=8}.

**Source:** `src/compiler/mlir_gen_types.cpp#L616`, `src/compiler/mlir_gen_types.cpp#L631`

### `layout.ref.zone-mut-fat-pair` — &mut T to a zone_mut type carries its allocator as a fat reference

A &mut T where T is a `#[zone_mut]` type has a 16-byte {data, zone=*mut Allocator} fat representation, returned by value like a slice fat pair; the allocator rides the &mut so grow-style methods reach it from &mut self.

**Divergence:** Logos addition: zone/allocator-carrying mutable reference, no Rust analogue.

**Source:** `src/compiler/mlir_gen_impl.hpp#L881-L883`


## Layout · `refrepr`

### `layout.refrepr.classification` — Reference-representation classes by type kind

Every type classifies into exactly one reference-representation kind, by its OUTER kind: `Ptr`/`Ref`/`FnPtr`/`FnItem` -> ThinPtr; `Slice` -> FatSlice; `TraitObject` -> FatDyn; `Closure` -> FatClosure; `DstRef` -> ThinPtr if the pointee is `#[self_describing]`, else FatCustomDst; `MutRef` -> FatZoneMut if the pointee is a `Struct`/`ZonedStruct` flagged `zone_mut`, else ThinPtr; `Struct`/`ZonedStruct` -> RelOffset if flagged `rel_ptr`, else NotARef; all other kinds -> NotARef. A raw/safe pointer is always thin even when its pointee is unsized at the type level (e.g. `*const dyn` collapses to a thin ptr) -- classification is by the outer kind, not by pointee unsizedness.

**Source:** `src/compiler/mlir_gen_types.cpp#L622-L667`

### `layout.refrepr.dst-self-describing-thin` — `#[self_describing]` custom-DST reference is a thin 8-byte pointer

A `DstRef` to a `#[self_describing]` pointee is physically THIN: an 8-byte pointer straight to the header, with the tail length carried in-band (`dst_len`) rather than as a separate {data,len} metadata word. This is what allows a `&Foo` to such a type to be returned by value safely -- there is no stack-local metadata pair that could dangle. A `DstRef` to a non-self-describing pointee is FatCustomDst: a {data,meta} fat pair.

**Source:** `src/compiler/mlir_gen_types.cpp#L648-L654`

### `layout.refrepr.mut-ref-zone-fat` — `&mut T` to a zone-mut type is a fat {data,zone} pair

`&mut T` where `T` is a struct/zoned-struct flagged `zone_mut` is a FAT reference carrying {data, zone=*mut Allocator} -- the mutable reference rides its Writ allocator so grow-methods can reach it from `&mut self`. Shared `&T`, `*T`, and `&mut T` to non-`zone_mut` types stay thin (a read path never grows the allocation).

**Source:** `src/compiler/mlir_gen_types.cpp#L631-L641`

### `layout.refrepr.rel-ptr-struct` — `#[rel_ptr]` struct is a self-relative offset, not a reference

A struct/zoned-struct flagged `rel_ptr` classifies as RelOffset: it is stored as an 8-byte `i64` self-relative offset (absolute address computed on access), not as a thin/fat pointer. A struct/zoned-struct without the flag is NotARef (an ordinary by-value aggregate).

**Divergence:** A6

**Source:** `src/compiler/mlir_gen_types.cpp#L655-L662`

### `layout.refrepr.return-by-value-abi` — By-value return ABI differs between fat-materialized and pointer-only kinds

On return-by-value: FatDyn, FatSlice, and FatZoneMut are materialized as their full 16-byte storage pair in the caller's frame; FatClosure, FatCustomDst, ThinPtr, and RelOffset are returned as an 8-byte pointer/offset value (their storage, where fat, is not return-materialized).

**Source:** `src/compiler/mlir_gen_types.cpp#L718-L735`

### `layout.refrepr.storage-layout-sizes` — Storage {size,align} per reference-representation kind

The in-field/in-element storage layout by representation kind is: ThinPtr = {8,8}; FatSlice/FatDyn/FatClosure/FatCustomDst/FatZoneMut = {16,8} (a fat pair); RelOffset = {8,8} (one `i64` offset); NotARef = {0,1} (no reference footprint).

**Source:** `src/compiler/mlir_gen_types.cpp#L688-L716`

### `layout.refrepr.unsized-pointee-not-ref` — Unsized pointee kinds (bare `[T]`, bare `dyn`) are not references

The unsized-pointee kinds (an unsized slice `[T]` or unsized `dyn` used directly, not behind a pointer) classify as NotARef: they describe an unsized POINTEE, not a reference, and have no by-value footprint of their own.

**Source:** `src/compiler/mlir_gen_types.cpp#L663-L665`

### `layout.refrepr.value-type-thin` — Every reference VALUE (in a register) is a thin pointer

Regardless of representation kind, the by-value (register/operand) form of any reference is a single thin pointer to its storage; a fat {data,meta} pair, where one exists, lives in the referent's STORAGE slot, never directly in a value/register.

**Source:** `src/compiler/mlir_gen_types.cpp#L680-L686`

### `layout.refrepr.zoned2-field-self-relative` — Thin-pointer field of a `#[zoned2]` struct is stored self-relative

When computing a FIELD's reference representation (as opposed to a bare type's), a field whose representation would otherwise be ThinPtr is instead stored as RelOffset if the owning struct is flagged `zoned2`. Other representation kinds are unaffected by the owner's `zoned2` flag at this step.

**Divergence:** A6

**Source:** `src/compiler/mlir_gen_types.cpp#L669-L678`


## Layout · `dstref`

### `layout.dstref.fat-only-with-slice-tail` — Custom-DST reference is a 16-byte fat slot only with a literal slice tail

A custom-DST reference (&Foo/&mut Foo where Foo has a tail) is a 16-byte {data,len} fat pointer ONLY when the pointee has a literal `[T]` slice tail (len carried inline) and is not #[self_describing]. A `dyn`-tail DST ref or a #[self_describing] DST is physically THIN (8-byte pointer; tail length recovered in-band, e.g. sizeof(`Rc<dyn>`)==8) and is not copied as a 16-byte fat slot.

**Divergence:** Logos custom-DST representation split (slice-tail fat vs dyn-tail/self-describing thin).

**Source:** `src/compiler/mlir_gen_stmt.cpp#L1330-L1351`


## Layout · `slice`

### `layout.slice.fat-pointer-descriptor` — Slice references are a {data-pointer, length} fat descriptor

A `&[T]` / `&mut [T]` reference parameter arrives as a pointer to a fat descriptor pair `{data: *T, len}`; indexed element access dereferences the descriptor's data-pointer field first, then applies the element's layout stride (using the element's full struct layout when the element type is itself a struct, so struct-typed slice elements are laid out inline within the pointed-to buffer).

**Related:** `coerce.return.ref-by-descriptor`

**Source:** `src/compiler/mlir_gen_fn.cpp#L395-L411`

### `layout.slice.fat-pointer-pair` — Slice/str values are a 16-byte {data,len} fat pair

A slice type (and str = `Slice<u8>`) has a fat-pointer representation: a 16-byte {data_ptr, len} pair. A function returning Slice/str returns this pair BY VALUE at the LLVM fn-return level (distinct from the pointer-shorthand Slice uses at parameter/field/scope positions); the caller spills the returned value to a stack slot so downstream consumers see the usual pointer-to-{ptr,len}.

**Source:** `src/compiler/mlir_gen_impl.hpp#L695-L702`, `src/compiler/mlir_gen_impl.hpp#L877`, `src/compiler/mlir_gen_impl.hpp#L557-L578`

### `layout.slice.fat-pointer-ptr-len` — slice fat pointer is {data_ptr, len}

A slice value is laid out as a two-field fat pointer: field 0 is the data pointer, field 1 is the i64 length.

**Source:** `src/compiler/mlir_gen_stmt.cpp#L2476-L2483`

### `layout.slice.fat-ptr` — Slice reference is a fat { ptr, len } descriptor

A slice-typed value (`&[T]` / &mut [T]) is represented as a fat descriptor { data-ptr, len }. Indexed access first reads field 0 (the data pointer) then strides by sizeof(element).

**Source:** `src/compiler/mlir_gen_impl.hpp#L364-L368`

### `layout.slice.owning-box-same-as-borrow` — `Box<[T]>` shares the borrowed-slice fat layout

An owning slice `Box<[T]>` has the same 16-byte {data,len} layout as a borrowed `&[T]` slice, but is move-only and droppable (drops elements and frees the buffer); the owning kind (Borrow/Box/Rc/Arc) is carried distinctly so the four forms intern as distinct types.

**Related:** `layout.dst.owning-box-same-as-borrow`, `type.traitobject.owning-kind-distinct`

**Source:** `src/compiler/sema_impl.hpp#L638-L649`

### `layout.slice.ptr-len-pair` — Slice fat pair = {data,len}

A slice value (`&[T]`, `str`) is the pair {data: ptr, len: i64}.

**Source:** `src/compiler/mlir_gen_types.cpp#L944-L947`

### `layout.slice.repr` — Slice runtime representation is a 16-byte {ptr, i64 len} pair

A slice's fat-pair storage representation is `{ptr, i64}` -- a data pointer and an 8-byte length, 16 bytes total.

**Source:** `src/compiler/mlir_gen_types.cpp#L1017-L1020`


## Layout · `array`

### `layout.array.assign-whole-copy` — Whole-array assignment copies all elements

Arrays are represented by a pointer to their storage. Assigning one array value to another (`t = [a,b];` / `t = other_arr;`) copies the entire array's backing storage (memcpy of the array's full size), not just the source pointer.

**Source:** `src/compiler/mlir_gen_stmt.cpp#L2136-L2149`

### `layout.array.elem-inline-storage` — Struct/tuple/fat-typed elements are stored inline in array/slice/Vec buffers

Struct- and tuple-typed elements of an array, slice, or Vec buffer are stored INLINE (the full aggregate embedded contiguously in the buffer), not as pointers to separately-allocated storage; TraitObject/Closure/Slice-typed elements are likewise stored inline as their 16-byte fat pair. Element iteration strides by the element's full in-buffer footprint accordingly (`sizeof` the struct/tuple/fat-pair), not by a collapsed pointer width.

**Source:** `src/compiler/mlir_gen_stmt.cpp#L2594-L2622`, `src/compiler/mlir_gen_stmt.cpp#L2745-L2769`

### `layout.array.element-stride` — Array element stride matches the element's full by-value layout

`[T; N]`'s element stride equals T's full by-value footprint, never a collapsed pointer — for T = Struct/ZonedStruct/tagged-Enum/Tuple/Slice/Closure/TraitObject the element is embedded INLINE at its real size (matching `layout_of`), so `arr[i]` indexing and `memcpy`-based array copies use the correct stride; a collapsed 8-byte handle would corrupt indexing/overflow the copy.

**Source:** `src/compiler/mlir_gen_types.cpp#L74-L121`, `src/compiler/mlir_gen_types.cpp#L472-L476`

### `layout.array.inline-element-storage` — arrays and slice buffers store struct/tuple elements inline

Struct, zoned-struct, and tuple elements are stored inline by value in array and slice buffers (stride = sizeof(element)); iterating yields a pointer directly into the inline storage. Trait-object/closure/slice elements are stored as 16-byte fat pairs. Scalar elements are stored by their natural representation.

**Source:** `src/compiler/mlir_gen_stmt.cpp#L2451-L2468`, `src/compiler/mlir_gen_stmt.cpp#L2587-L2625`

### `layout.array.struct-elements-inline` — Arrays of structs store elements inline, not as pointers

An array type `[Struct; N]` — including when reached through a reference/pointer parameter, which peels to the array's element type before indexing — lays out its elements inline and contiguously (each element occupies exactly the struct's full layout size), rather than storing a pointer per element; indexing strides by that element layout size, not by the size of a pointer.

**Source:** `src/compiler/mlir_gen_fn.cpp#L379-L394`, `src/compiler/mlir_gen_fn.cpp#L420-L436`

### `layout.array.struct-elems-inline` — Array-literal let derives element storage type from the annotated array type

`let name: [T; N] = [...];` derives the array slot's element storage type from the let's annotated array type when it resolves to an array type (so array-of-struct elements lay out as inline aggregates, not element pointers); if the annotation doesn't resolve to an array type, the element storage type falls back to the array literal's own element type, defaulting to i32 if neither resolves.

**Source:** `src/compiler/mlir_gen_stmt.cpp#L1489-L1508`


## Layout · `tuple`

### `layout.tuple.inline-aggregate-elements` — Tuple elements of struct/enum/slice/closure/dyn/tuple kind are embedded inline at full layout

A tuple element is stored INLINE at its full by-value layout, matching the struct-field convention, for these element kinds: Struct/ZonedStruct (registered inline struct type), Enum (full {disc,payload} footprint), Slice (16-byte {ptr,len} fat pair, including `str` = `Slice<u8>`), Closure (16-byte {fn,env} pair), TraitObject/bare `&dyn` (16-byte {data,vtable} pair), and nested Tuple (its own aggregate, recursively). A `*mut dyn` (Ptr-to-TraitObject) is excluded from the dyn case and stays an 8-byte thin handle. `&(T,U)`/`&mut (T,U)`/`*(T,U)` typed as a tuple resolve through the pointee to the inner tuple's layout.

**Source:** `src/compiler/mlir_gen_types.cpp#L961-L1015`

### `layout.tuple.inline-elements` — Tuple elements stored inline by value

A tuple stores each element inline by value at its layout slot; constructing a tuple writes each element into its slot, copying inline-aggregate (struct/array) elements by value rather than storing a pointer, and a tuple value is represented as a pointer to its storage.

**Related:** `expr.tuple-index.aggregate-element-by-address`

**Source:** `src/compiler/mlir_gen_expr.cpp#L3064-L3105`, `src/compiler/mlir_gen_expr.cpp#L3084-L3094`

### `layout.tuple.ref-tuple-derefs-to-inner` — Ref-to-tuple resolves inner tuple layout

A `&(T,U)`/`&mut (T,U)`/`*(T,U)` resolves to the layout of the inner tuple `(T,U)` (default binding modes for tuple patterns over ref scrutinees).

**Source:** `src/compiler/mlir_gen_types.cpp#L888-L896`

### `layout.tuple.struct-element-inline` — Tuple element aggregate types stored inline

A tuple element whose type is a struct, enum, slice (incl. `str`), closure, trait object (`&dyn`), or nested tuple is stored inline as its full by-value layout (e.g. struct footprint, 16-byte fat pair), never collapsed to an 8-byte pointer.

**Source:** `src/compiler/mlir_gen_types.cpp#L899-L941`


## Layout · `struct`

### `layout.struct.assign-value-copy` — Struct-valued assignment copies the full struct payload

Assigning a struct (or zoned-struct) value to a struct-typed place (`acc = src;`) copies the entire struct footprint (memcpy of `sizeof(struct)`) into the destination storage — a plain pointer store would only overwrite the first machine word.

**Source:** `src/compiler/mlir_gen_stmt.cpp#L2073-L2088`

### `layout.struct.field-fat-ref-inline-storage` — Fat-ref struct/enum field stored inline

When a fat reference (slice, `dyn`/`Box<dyn>`, closure, custom-DST ref) is a payload field of an enum variant, it is stored inline as its full 16-byte fat pair, never collapsed to an 8-byte handle; thin refs keep the by-value 8-byte pointer.

**Source:** `src/compiler/mlir_gen_types.cpp#L680-L688`

### `layout.struct.pointer-field-non-owning` — Pointer/reference fields do not own their pointee

A struct field of pointer or reference type (*T / &T / &mut T) does not own the pointee; automatic Drop of the containing struct must NOT drop through such fields.

**Source:** `src/compiler/mlir_gen_impl.hpp#L52-L55`


## Layout · `aggregate`

### `layout.aggregate.field-order-padding` — Struct/tuple layout: declaration order + natural-alignment padding

A struct or tuple's fields/elements are laid out in declaration order; each field is placed at the next offset rounded up to its own alignment (inserting padding as needed), the aggregate's alignment is the max of its members' alignments, and the total size is rounded up to that alignment (matches non-packed C/LLVM layout).

**Source:** `src/compiler/mlir_gen_types.cpp#L419-L429`, `src/compiler/mlir_gen_types.cpp#L477-L481`, `src/compiler/mlir_gen_types.cpp#L513-L517`

### `layout.aggregate.inline-by-value-members` — All aggregate members stored inline by value

Every aggregate member position — struct field, array element, tuple element, or enum payload slot — stores its member INLINE by value (Rust layout): nested struct, tagged enum, tuple, slice, closure, dyn, and custom-DST members all occupy their full by-value footprint in the parent's storage, never a collapsed pointer to separately-allocated storage. A value of one of these kinds used OUTSIDE an aggregate slot (a plain local/SSA value) is instead a pointer to storage holding this layout.

**Source:** `src/compiler/mlir_gen_types.cpp#L436-L439`, `src/compiler/mlir_gen_types.cpp#L197-L352`

### `layout.aggregate.return-by-value` — Struct/array/enum return values are returned by value as the full aggregate

A function returning a Struct, fixed-size array, or Enum type returns the aggregate BY VALUE: if the computed value is a pointer to storage, the full aggregate is loaded from it; if only a scalar (e.g. a bare enum discriminant) is available, it is first written into a fresh stack slot of the aggregate's layout, then that slot is loaded and returned.

**Source:** `src/compiler/mlir_gen_stmt.cpp#L2257-L2291`


## Layout · `aggregate-member`

### `layout.aggregate-member.indirect-fat-types` — Fat-typed aggregate members are stored as an 8-byte pointer

As a struct field, tuple element, or enum-variant payload field: Slice/Closure/Tuple members are stored as an 8-byte pointer (not their by-value fat footprint); Struct/Enum/Array/bare-dyn members are stored inline (full aggregate layout); an AnyVal member is stored as i32.

**Source:** `src/compiler/mlir_gen_impl.hpp#L858-L863`


## Layout · `field`

### `layout.field.fat-ref-stored-inline` — Fat-reference struct fields are stored inline; read yields the slot address

A struct field of slice, closure, custom-DST-reference, fat-zone-mut, or relative-offset representation is stored inline within the struct (a 16-byte fat pair for the always-fat subset), and a field read yields the address of that inline storage (materializing a relative offset to an absolute pointer where applicable), not a by-value load. A tuple-typed field likewise yields its inline slot address. A trait-object field is excluded and read by value.

**Related:** `layout.index.inline-aggregate-element`

**Source:** `src/compiler/mlir_gen_expr.cpp#L2815-L2823`, `src/compiler/mlir_gen_expr.cpp#L2805-L2814`

### `layout.field.inline-struct-store-by-value` — Inline (embedded) struct field is assigned by value

When a struct field's storage is an embedded aggregate (not a pointer slot) and the assigned r-value is materialized as a pointer to the source bytes, the assignment loads the aggregate value from the source pointer and stores it by value into the field; scalar fields instead receive the value with integer coercion to the field type.

**Source:** `src/compiler/mlir_gen_stmt.cpp#L2760-L2771`, `src/compiler/mlir_gen_stmt.cpp#L2840-L2853`

### `layout.field.rel-ptr-self-relative-offset` — #[rel_ptr] field stores a self-relative i64 offset

A struct field marked #[rel_ptr] (RefRepr RelOffset) does not store an absolute pointer; on assignment the destination pointer value is lowered to a signed i64 offset relative to the field slot's own address (the slot is the anchor) and that offset is stored in the slot.

**Divergence:** Logos addition: self-relative pointer field representation (no Rust analogue).

**Source:** `src/compiler/mlir_gen_stmt.cpp#L2748-L2758`, `src/compiler/mlir_gen_stmt.cpp#L2828-L2838`

### `layout.field.scalar-loaded-by-value` — Scalar struct field read loads the value

A struct field whose type is not an inline-fat/aggregate kind is read by loading the value at the field's address.

**Source:** `src/compiler/mlir_gen_expr.cpp#L2824-L2827`


## Layout · `field-align`

### `layout.field-align.unsized-tail` — Alignment of unsized tail fields

Field alignment is min(byte_size,8) for sized fields (treating size-0 as align 1); an unsized `[T]` slice tail aligns to min(sizeof(T),8); an unsized `dyn` tail aligns to 8 (pointer width).

**Source:** `src/compiler/sema_expr.cpp#L17666-L17675`


## Layout · `field-index`

### `layout.field-index.element-stride-inline-footprint` — Indexing a pointer-typed field strides by the element's inline footprint

When indexing through a pointer-valued struct field (the stored pointer is loaded first), address computation strides by the element's inline slot footprint: the concrete struct's aggregate size for struct elements and 16 bytes for fat-pointer (dyn/closure/slice) elements, not the collapsed 8-byte pointer size.

**Source:** `src/compiler/mlir_gen_stmt.cpp#L3169-L3183`


## Layout · `enum`

### `layout.enum.aggregate-payload-inline-memcpy` — Aggregate enum payload fields are stored inline

A variant payload field whose type is an aggregate (struct, zoned struct, tuple, slice, closure, array, nested tagged enum, or trait object) is stored inline into the payload area by copying its full byte footprint; scalar payload fields are stored by value. A trait-object payload field is first coerced to a 16-byte fat (data,vtable) pair and stored inline, so it moves and drops with the enum.

**Source:** `src/compiler/mlir_gen_expr.cpp#L604-L664`

### `layout.enum.align` — Tagged enum alignment

alignof(tagged enum) = max(4, payload_align), i.e. at least the 4-byte discriminant alignment and at least the widest variant payload's alignment.

**Related:** `layout.enum.tagged-repr`

**Source:** `src/compiler/mlir_gen_impl.hpp#L64-L67`, `src/compiler/mlir_gen_impl.hpp#L72`

### `layout.enum.assign-full-repr-copy` — Enum-valued assignment copies the whole {disc,payload} representation

An enum value is represented inline as `{disc, payload}` in its storage slot. Assigning a new enum value to an enum-typed place copies the FULL footprint (via memcpy) into the slot, not merely the discriminant word. If only a bare discriminant is available at the assignment site (e.g. a payload-less variant with no inferred type args), only the discriminant word of the slot is written, leaving the rest of the slot unspecified.

**Source:** `src/compiler/mlir_gen_stmt.cpp#L2052-L2072`

### `layout.enum.clike-disc-sized` — C-like (payload-less) enum: discriminant-sized integer

An enum with no tagged-enum info (a plain C-like enum, no variant carries data) lowers as a bare integer whose width is `ceil(disc_bits/8)` bytes (minimum 1 byte), sized to the enum's variant count / declared backing type — not a fixed i32.

**Source:** `src/compiler/mlir_gen_types.cpp#L536-L538`, `src/compiler/mlir_gen_types.cpp#L66-L70`

### `layout.enum.discriminant-backing-type` — C-style enum discriminant uses its declared backing type, else i32

A C-style enum's discriminant is represented with its explicitly declared backing integer type (`enum Foo : u64 {}`); absent an explicit backing type, the discriminant defaults to i32.

**Source:** `src/compiler/mlir_gen_impl.hpp#L705-L721`

### `layout.enum.field-store-heap-promote` — Enum value stored into an enum-typed field is heap-promoted

An enum-typed field is represented by a single heap pointer slot (two-level convention). Assigning an enum r-value (held by pointer) into such a slot copies the enum's bytes into a freshly heap-allocated region of sizeof(enum) and stores that heap pointer, so the field does not dangle past the producing function's frame.

**Related:** `layout.enum.two-level-heap-ptr`

**Source:** `src/compiler/mlir_gen_stmt.cpp#L2725-L2747`

### `layout.enum.identified-llvm-type-body-deferred` — An enum's LLVM body size is finalized once, after a whole-program fixpoint

An enum's identified aggregate type is created immediately but its body (the payload byte-array size) is left unset at first registration -- a nested enum payload may still be a zero-byte stub at that point, under-sizing the outer enum. The body is set exactly once, after a fixpoint recomputes every enum's final payload size; an identified LLVM struct's body cannot be reset once set.

**Source:** `src/compiler/mlir_gen_types.cpp#L928-L937`

### `layout.enum.low-bit-niche` — Pointer-plus-small-integer two-arm enum packs into one word via a low-bit discriminant

A two-variant enum where every variant has exactly one payload field, one variant's field is a reference (`&T`/`&mut T`) whose pointee has alignment >= 2 (guaranteeing its low bit is always 0), and the other variant's field is an integer of <= 56 bits, packs into a SINGLE machine word: the pointer arm is stored raw; the integer arm is stored shifted as `(v << 1) | 1`. The discriminant is the value's low bit (0 = pointer arm, 1 = integer arm) -- no separate discriminant word.

**Divergence:** A6

**Source:** `src/compiler/mlir_gen_types.cpp#L869-L925`, `src/compiler/mlir_gen_types.cpp#L872-L890`

### `layout.enum.low-bit-niche-zoned2-raw-arms` — `#[zoned2]` enums admit a raw untagged pointer arm and a raw 64-bit integer arm in the low-bit niche

For an enum flagged `zoned2`, the low-bit-niche pointer arm additionally accepts any raw `*T`/`&T`/`&mut T` regardless of the pointee's declared alignment (the zoned2 allocator's invariant that all Writ zone objects are >= 2-aligned is trusted directly), and the integer arm additionally accepts a full 64-bit `i64`/`u64` stored RAW (no `<<1` shift) -- because the producer of such a `zoned2` value has already baked a low-bit-1 tag into the raw word itself.

**Divergence:** A6

**Source:** `src/compiler/mlir_gen_types.cpp#L899-L924`

### `layout.enum.nested-payload-is-pointer` — A nested payload-bearing enum is stored by pointer

When an enum variant's payload is itself a payload-bearing enum (e.g. `Option<Option<T>>`::Some carrying `Option<T>`), the nested enum lowers to a pointer in the outer payload rather than being inlined as a discriminant scalar.

**Note:** Inferred from the stub-registration comment; the precise inline-vs-pointer threshold lives in register_tagged_enum (another unit).

**Source:** `src/compiler/mlir_gen.cpp#L93-L107`

### `layout.enum.nested-payload-representation` — Nested enum payload: inline for tagged, scalar for C-like

An enum-variant payload field whose declared type is itself an enum is represented INLINE within the payload area (the payload GEP address is the nested enum's own storage) when the nested enum is a tagged (data-carrying) enum; a fieldless (C-like) nested enum is instead represented as a scalar integer, loaded by value.

**Source:** `src/compiler/mlir_gen_stmt.cpp#L240-L251`

### `layout.enum.niche-low-bit` — Low-bit niche packing discriminates value vs pointer arm

A LowBit-niche enum encodes its two variants in one machine word: bit0==1 selects the inline scalar ("value") arm, whose payload is `word >> 1` (arithmetic shift if the value is signed, logical otherwise); bit0==0 selects the pointer arm, whose payload is the word itself (an aligned pointer, whose low bit is therefore guaranteed 0). In raw mode (`val_raw`) both arms read the storage word verbatim with no shift/decode. The discriminant is derived at load time from the low bit (no stored disc word); constructing a value-arm payload bakes the tag into the word at construction time (no separate disc store here).

**Source:** `src/compiler/mlir_gen_expr.cpp#L4958-L4980`, `src/compiler/mlir_gen_expr.cpp#L4989-L4992`, `src/compiler/mlir_gen_expr.cpp#L5026-L5036`

### `layout.enum.niche-lowbit` — Low-bit niche for two data-arm enums

A two-data-arm enum may be packed into a single word with NO separate discriminant, disambiguated by the word's LOW BIT: the pointer arm (ptr_disc) holds a pointer to an align>=2 pointee (low bit always 0) stored raw; the value arm (val_disc) holds a value `<=63 bits stored as (value<<1)|1 (low bit 1). Read: low bit 0 -> interpret word as pointer; low bit 1 -> value = word>`>1, sign/zero-extended per the value arm's bit width and signedness.

**Related:** `layout.enum.niche-nullptr`, `layout.enum.niche-lowbit-raw`

**Source:** `src/compiler/mlir_gen_impl.hpp#L98-L111`

### `layout.enum.niche-lowbit-encoding` — LowBit niche enum payload encoding

For an enum with a LowBit niche packed into a single word: the pointer arm stores the pointer's raw integer value (low bit 0, guaranteed by >=2 alignment); the value arm stores (v<<1)|1 after sign/zero extension to the word width. In RAW mode the producer-supplied value (low-bit already set) is stored verbatim without shifting. An empty payload stores 0.

**Divergence:** A: niche-packing layout is Logos-defined; not a Rust-guaranteed representation.

**Source:** `src/compiler/mlir_gen_expr.cpp#L570-L602`

### `layout.enum.niche-lowbit-int-widths` — Low-bit niche integer arm widths

Eligible low-bit-niche value arms are Bool(1), I8/U8(8), I16/U16(16), I24/U24(24), I32/U32(32), I56(56) packed shifted; I64/U64(64) qualify only as the raw zoned variant.

**Source:** `src/compiler/mlir_gen_types.cpp#L799-L817`, `src/compiler/mlir_gen_types.cpp#L839`

### `layout.enum.niche-lowbit-ptr-int` — Low-bit niche packs pointer + small-int arms

A two single-field-arm enum where one arm is a pointer to an align>=2 pointee (low bit always 0) and the other arm is a <=56-bit integer stored shifted `(v<<1)|1` packs into one word; the discriminant is the low bit (0=ptr arm, 1=int arm).

**Divergence:** Logos low-bit pointer-tagging niche; no direct Rust analog.

**Source:** `src/compiler/mlir_gen_types.cpp#L796-L853`

### `layout.enum.niche-lowbit-raw` — Low-bit niche raw mode (zoned, 64-bit value arm)

For a #[zoned2] low-bit niche enum whose value arm is a full 64-bit word (e.g. Pod(u64)), the value-arm word is stored and read VERBATIM with no (`v<<1)|1 shift, because the producer already encodes the low-bit-1 tag in the word. The discriminant is still the low bit: low-bit-0 -> reference (pointer) arm, otherwise ->` the raw value arm.

**Related:** `layout.enum.niche-lowbit`, `layout.enum.zoned-self-relative`

**Source:** `src/compiler/mlir_gen_impl.hpp#L112-L116`

### `layout.enum.niche-null-pointer` — Null-pointer niche packing eliminates the discriminant word

For a niche-packed enum shaped like `Option<&T>` (one nullary variant + one pointer-payload variant), storage has no separate discriminant word: the payload IS the enum storage at offset 0, the nullary variant is encoded as a null pointer at that offset, and the non-null pointer of the payload variant simultaneously acts as the discriminant. Reassigning an untyped `none`-like value to a niche-packed enum slot (whose only nullary variant is this niche's `none`) is lowered as storing null at offset 0.

**Source:** `src/compiler/mlir_gen_expr.cpp#L4981-L4982`, `src/compiler/mlir_gen_expr.cpp#L4993-L5001`, `src/compiler/mlir_gen_expr.cpp#L5012-L5017`, `src/compiler/mlir_gen_expr.cpp#L5038-L5048`

### `layout.enum.niche-nullptr` — Null-pointer niche optimization for `Option<&T>`-shape enums

A two-variant enum where one variant is fieldless and the other holds a single non-null pointer field is laid out with NO separate discriminant: it is just the 8-byte pointer word. Null (0) encodes the fieldless variant (none_disc); any non-null value encodes the pointer variant (some_disc). Hence sizeof(`Option<&T>`) == sizeof(&T) == 8.

**Related:** `layout.enum.niche-lowbit`

**Source:** `src/compiler/mlir_gen_impl.hpp#L88-L98`, `src/compiler/mlir_gen_impl.hpp#L103-L107`

### `layout.enum.niche-nullptr-nonnull-wrapper` — Null-pointer niche for #[non_null] 8-byte wrapper

The null-pointer niche also applies when the single-field variant's field is a `#[non_null]` struct that is exactly an 8-byte pointer wrapper (Box/Rc/Arc-shape), whose invariant guarantees offset-0 is non-zero.

**Divergence:** Logos `#[non_null]` attribute exposes Rust's NonNull niche to user wrapper types.

**Source:** `src/compiler/mlir_gen_types.cpp#L769-L795`

### `layout.enum.niche-nullptr-ref` — Null-pointer niche for `Option<&T>`-shape

A two-variant enum with one fieldless variant and one single-field variant whose field is `&T`/`&mut T` is pointer-sized (8 bytes, no separate discriminant word): the discriminant is encoded as null vs non-null at offset 0, since references are guaranteed non-null.

```logos
enum Option<&T> { None, Some(&T) }  // sizeof == 8
```

**Source:** `src/compiler/mlir_gen_types.cpp#L761-L795`

### `layout.enum.niche-packed-no-disc` — Niche-packed tagged enum has no discriminant word

A niche-packed tagged enum (one whose variants' presence can be encoded entirely inside its payload's bit pattern) has layout EXACTLY `{payload_bytes, payload_align}` — the separate `i32` discriminant word is elided; the tag is recovered from the payload bits alone.

**Source:** `src/compiler/mlir_gen_types.cpp#L528-L530`

### `layout.enum.niche-zoned-raw-word` — Zoned (#[zoned2]) raw 64-bit low-bit niche

In a `#[zoned2]` enum, the low-bit niche additionally accepts a raw `*T` pointer arm (trusting the zone allocator's >=2 alignment even for `*u8`) and a raw 64-bit `u64`/`i64` value arm stored without a `<<1` shift (the producer bakes the low-bit-1 tag into the word).

**Divergence:** Logos zoned (Writ) niche; no Rust equivalent.

**Source:** `src/compiler/mlir_gen_types.cpp#L811-L851`

### `layout.enum.null-pointer-niche` — Two-variant enum with one fieldless + one single-non-null-pointer variant is pointer-sized

An enum with exactly two variants, one fieldless (the niche/`none` arm) and one carrying a single payload field of reference kind `&T`/`&mut T` (guaranteed non-null), is laid out with NO separate discriminant word: the discriminant is encoded as null (none arm) vs non-null (some arm) in that single pointer field at offset 0, so the whole enum is pointer-sized (e.g. `size_of::<Option<&T>>() == size_of::<&T>()`).

**Source:** `src/compiler/mlir_gen_types.cpp#L830-L868`

### `layout.enum.null-pointer-niche-nonnull-wrapper` — A `#[non_null]` single-8-byte-pointer wrapper struct also qualifies for the null-pointer niche

In the two-variant fieldless+single-field niche shape, the single payload field also qualifies for the null-pointer niche when its type is a struct/zoned-struct flagged `non_null` whose total ABI byte size is exactly 8 (a Box/Rc/Arc-style single-pointer wrapper) -- its type invariant guarantees the pointer at offset 0 is never zero, so the same null-vs-non-null discriminant encoding applies.

**Divergence:** A6

**Source:** `src/compiler/mlir_gen_types.cpp#L844-L868`

### `layout.enum.payload-by-value` — Enum payload members stored by value

Enum variant payload members are laid out by value: each member contributes its full by-value layout (e.g. `Option<&[u8]>` payload = the 16-byte slice fat pair), unlike struct/tuple fields which may store a slice/closure/tuple as an 8-byte ptr.

**Source:** `src/compiler/mlir_gen_types.cpp#L707-L720`

### `layout.enum.payload-fat-ref-inline` — A fat-reference-typed enum payload field is stored inline as its full fat pair

An enum variant payload field whose type has a fat reference representation (`&dyn`/`dyn`, slice, closure, or a fat custom-DST ref) is stored INLINE in the variant payload as its full 16-byte fat storage pair, not collapsed to an 8-byte pointer -- so the payload carries no heap handle and requires no separate free/leak-avoidance. Thin reference kinds (ptr/ref/fn) keep their ordinary by-value pointer representation in the payload.

**Source:** `src/compiler/mlir_gen_types.cpp#L737-L775`, `src/compiler/mlir_gen_types.cpp#L755-L763`

### `layout.enum.payload-inline-struct-tuple-enum` — Struct/tuple/nested-enum-typed enum payload fields embed their full ABI footprint

An enum variant payload field of Struct/ZonedStruct, Tuple, or (nested) Enum kind embeds that type's full identified/aggregate LLVM type in the payload slot, rather than the collapsed single-pointer form that a generic type lowering would otherwise produce -- the payload occupies the referent's real by-value size.

**Source:** `src/compiler/mlir_gen_types.cpp#L743-L770`

### `layout.enum.payload-size-fixpoint` — Enum payload size = max over variants, to fixpoint over nesting

An enum's payload_bytes/payload_align equal the maximum size/alignment over all its variants' payloads. Because a nested-enum payload's footprint depends on the nested enum's own payload size, sizes are computed to a fixpoint (monotonically growing) so layout is order-independent of registration.

**Related:** `layout.enum.tagged-disc-plus-payload`

**Source:** `src/compiler/mlir_gen.cpp#L111-L140`

### `layout.enum.payload-size-is-max-variant` — Enum payload size/align = max over variants

A tagged enum's payload byte size is the maximum payload size over all variants, and its payload alignment is the maximum payload alignment over all variants.

**Source:** `src/compiler/mlir_gen_types.cpp#L737-L754`

### `layout.enum.return-by-value-pair` — dyn/slice/zone-mut returned by 16B value

When returned by value, a trait object, slice, or zone-mut fat reference is materialized as its full 16-byte storage pair in the caller's frame; closure, custom-DST, thin, and rel-offset references are returned as their 8-byte value (pointer/word).

**Source:** `src/compiler/mlir_gen_types.cpp#L643-L659`

### `layout.enum.tagged-disc-i32` — Non-niche enum layout is {i32 disc, payload}

A tagged enum without niche packing is laid out as `{i32 discriminant, payload}`: the discriminant occupies field 0 (stored/loaded as a 32-bit value) and the payload begins at field 1.

**Source:** `src/compiler/mlir_gen_expr.cpp#L4949-L4953`, `src/compiler/mlir_gen_expr.cpp#L4983-L4986`, `src/compiler/mlir_gen_expr.cpp#L5003-L5008`, `src/compiler/mlir_gen_expr.cpp#L5050-L5053`

### `layout.enum.tagged-disc-payload` — Tagged-enum value layout: {i32 disc, aligned payload}

A tagged enum's by-value layout is `{ i32 discriminant, <payload> }`, where the payload sub-object is placed at the offset rounded up to the payload's own alignment (natural aggregate padding after the 4-byte disc word), and the whole is rounded to the max of (4, payload_align). The concrete instantiation is resolved so nested generic payloads (e.g. `Option<Option<i64>>`) size their full inline footprint. The SSA/value form of a tagged enum is a pointer to this storage.

**Source:** `src/compiler/mlir_gen_types.cpp#L522-L534`, `src/compiler/mlir_gen_types.cpp#L66-L69`, `src/compiler/mlir_gen_types.cpp#L409-L411`

### `layout.enum.tagged-disc-plus-payload` — Tagged-enum layout: discriminant word + aligned payload blob

A payload-bearing enum lays out as { i32 discriminant, payload }, where payload is an aligned blob of size = ceil(max_variant_payload_size / payload_align) elements each of width payload_align bytes. The whole-enum alignment is max(4, payload_align); the payload is placed after the i32 with padding so an align-8 payload begins at offset 8, not 4.

**Related:** `layout.enum.payload-size-fixpoint`, `layout.enum.niche-packed-no-disc`

**Source:** `src/compiler/mlir_gen.cpp#L141-L163`

### `layout.enum.tagged-repr` — Tagged enum layout = { discriminant, payload blob }

A tagged enum is laid out as a struct { i32 discriminant, payload-blob } where the payload-blob is sized to the widest variant's payload bytes and aligned to payload_align = max over all variants of their payload alignment. The blob is placed after the discriminant with padding so an aligned (i64/ptr/align-8) payload lands on an aligned offset.

**Related:** `layout.enum.align`

**Source:** `src/compiler/mlir_gen_impl.hpp#L64-L79`

### `layout.enum.tagged-value-is-heap-ptr` — A by-value tagged-enum parameter is one heap-pointer level

A by-value function parameter of a tagged (payload-carrying) enum type (e.g. `Option<i64>`) arrives at the callee as a single heap-pointer level (the boxed enum), not the aggregate itself and not a pointer-to-pointer; taking its address (`&x`) spills that pointer into a local slot to produce a genuine pointer-to-enum-pointer, as required by two-level-pointer enum methods (e.g. `==`). A payload-free (C-style) enum parameter is instead a plain i32 and takes the scalar-spill address-of path.

**Note:** The underlying enum-boxing/representation decision (why tagged enums are one heap-pointer level as a value) is established by code outside this slice; this unit only consumes and documents the resulting parameter-binding convention.

**Source:** `src/compiler/mlir_gen_fn.cpp#L479-L492`

### `layout.enum.unit-variant-field-omitted` — Unit `()` payload field omitted

A `()` (Void) payload field of an enum variant contributes no field and no bytes to the variant payload.

**Source:** `src/compiler/mlir_gen_types.cpp#L716-L717`, `src/compiler/mlir_gen_types.cpp#L742`

### `layout.enum.value-repr-inline` — Tagged enum value is inline {disc, payload} storage, not heap

A tagged-enum value (with or without payload) is stored inline as a {discriminant, payload} aggregate by value (the address is a one-level &Enum); it is not heap-allocated. Recursive-by-value enums are rejected, making inline storage sound. A C-style enum with no payload is just its discriminant, sized by the enum's backing type.

**Source:** `src/compiler/mlir_gen_expr.cpp#L529-L547`, `src/compiler/mlir_gen_expr.cpp#L561-L568`

### `layout.enum.variant-payload-aggregate-layout` — Variant payload {size,align} is computed as a padded aggregate, not a naive field-size sum

A variant's payload {size,align} is derived by accumulating each payload field's by-value layout as if laying out a struct/tuple of those fields (inter-field alignment padding included), matching the actual LLVM aggregate the payload is lowered to. Enum payload fields store multi-field/fat-typed members BY VALUE (their full inline representation), unlike an ordinary struct/tuple FIELD which may collapse such members to a pointer -- so payload layout is computed with the by-value accumulator, not the aggregate-member accumulator used for struct/tuple fields.

**Source:** `src/compiler/mlir_gen_types.cpp#L777-L800`

### `layout.enum.variant-payload-inline-aggregate` — A struct/tuple-typed enum-variant payload field occupies its full inline ABI size, not the collapsed pointer size

In a tagged-enum variant's payload struct, a struct- or tuple-typed field is laid out at its full inline ABI byte size (as the constructor's memcpy writes it), not the single-pointer collapsed representation otherwise used for such types elsewhere; a payload field following an aggregate field is offset according to that full inline size.

**Note:** Stated only via an anti-bug rationale comment (misaligned field after an aggregate payload member); the general variant-payload shape itself is covered elsewhere (layout.enum.tagged-repr).

**Source:** `src/compiler/mlir_gen_impl.hpp#L1022-L1030`

### `layout.enum.variant-payload-struct-layout` — Variant payload laid out as a struct

A variant's payload is laid out exactly like a struct/tuple of its fields, including inter-field alignment padding; a multi-field variant's payload size is the aligned aggregate, not the naive sum of field sizes (e.g. `Cons{head:i32, tail:*const List}` = 16, not 12).

**Source:** `src/compiler/mlir_gen_types.cpp#L702-L720`

### `layout.enum.zoned-niche-self-relative` — #[zoned2] niche enum: self-relative at rest, absolute in compute

A `#[zoned2]` niche enum's at-rest storage word `r` uses self-relative addressing for its reference arm (anchor = the slot's own address): r==0 → null; r&1==1 → Pod arm (position-independent, copied raw, identity on materialize/lower); otherwise → Ref arm, whose absolute address is `slot + r` on materialize and whose stored delta is `val − slot` on lower. The compute-side value is a fresh alloca holding the word with the Ref arm as an ABSOLUTE address, bridging storage (self-relative) and compute (absolute) representations.

**Divergence:** A6

**Note:** This is the compiler-owned generalization of writ's wa_materialize/wa_lower (per the source comment); tagged as a Writ-fabric-related Logos addition (A6) rather than a Rust behavioral divergence, since Rust has no zoned/self-relative reference concept at all.

**Source:** `src/compiler/mlir_gen_expr.cpp#L5095-L5135`

### `layout.enum.zoned-self-relative` — #[zoned2] enum reference arm stored self-relative at rest

For a #[zoned2] niche enum, the reference (low-bit-0) arm is stored SELF-RELATIVE at rest and is absolute as a value; conversion between the two representations occurs on materialize (load) and lower (store). Only meaningful together with a low-bit niche.

**Related:** `layout.enum.niche-lowbit-raw`

**Source:** `src/compiler/mlir_gen_impl.hpp#L81-L86`


## Layout · `union`

### `layout.union.common-storage` — Union layout: overlapping common storage

A `union`'s size is max(field sizes) rounded up to max(field aligns); its alignment is max(field aligns). All fields occupy the SAME storage starting at offset 0 (they overlap); the field with the maximum alignment supplies the concrete LLVM field type at that offset, with trailing padding bytes appended to reach the union's full size.

**Source:** `src/compiler/mlir_gen_types.cpp#L353-L388`, `src/compiler/mlir_gen_types.cpp#L502-L512`

### `layout.union.max-of-fields` — Union layout is max-size at max-alignment

A struct marked as a union (`#[repr(...)]` union) is laid out as the maximum field size aligned to the maximum field alignment; all fields overlap at offset 0.

**Divergence:** Logos union via #[repr]/union attribute; layout semantics match C/Rust unions.

**Source:** `src/compiler/sema_decl.cpp#L1231-L1233`


## Layout · `dyn`

### `layout.dyn.box-dyn-collapses-to-trait-object` — `Box<dyn Trait>` has the same repr as &dyn, differing only by ownership

`Box<dyn Trait>` is not a `Box<TraitObject>` struct; it is an owning bare trait object with the identical 16-byte {data, vtable} fat-pair representation as `&dyn Trait`. The two differ only in ownership: dropping an owning trait object calls vtable[0] (drop_in_place) then deallocates `data`.

**Related:** `layout.dyn.fat-pair-16-byte`, `intrinsic.drop.owning-dyn-handle`

**Source:** `src/compiler/mlir_gen_impl.hpp#L1114-L1119`

### `layout.dyn.data-vtable-pair` — Trait object fat pair = {data,vtable}

A trait object value (`dyn Tr`) is the pair {data_ptr, vtable_ptr}; a `&dyn` value is a pointer to this 16-byte storage.

**Source:** `src/compiler/mlir_gen_types.cpp#L949-L955`

### `layout.dyn.fat-pair-16-byte` — Trait object is a 16-byte {data, vtable} fat pair

A trait object (`dyn Trait`) has value representation as a 16-byte fat pair {data_ptr, vtable_ptr}. `&dyn`/`&mut dyn` are this value-fat-pair on the stack; an owning trait object (`Box<dyn>`, *const/*mut dyn) is held via an 8-byte heap handle to such a 16-byte pair.

**Related:** `layout.dyn.box-dyn-collapses-to-trait-object`

**Source:** `src/compiler/mlir_gen_impl.hpp#L1046-L1047`, `src/compiler/mlir_gen_impl.hpp#L1068-L1077`

### `layout.dyn.fat-pair-data-vtable` — `dyn Trait` handle layout: {data, vtable}

A `dyn Trait` fat handle is laid out as a 2-field struct `{data: ptr, vtable: ptr}` — field 0 is the concrete data pointer, field 1 is the vtable pointer.

**Source:** `src/compiler/mlir_gen_stmt.cpp#L524-L527`, `src/compiler/mlir_gen_stmt.cpp#L573-L576`

### `layout.dyn.fat-pointer-data-vtable-pair` — dyn trait object is a 16-byte {data, vtable} fat pair by value

`&dyn Trait`, `*dyn Trait`, and `Box<dyn Trait>` share a uniform 16-byte fat representation: a `{data_ptr, vtable_ptr}` pair stored inline. `data_ptr` is the concrete value's address (heap concrete for an owning `Box<dyn>`). The pair travels by value; escape consumers copy the 16 bytes into their own inline storage rather than holding a heap handle.

**Divergence:** B2/B3: fat-pointer model for owned dyn; `Box<dyn>` is the owning trait object.

**Source:** `src/compiler/mlir_gen_dyn.cpp#L1204-L1234`, `src/compiler/mlir_gen_dyn.cpp#L1264-L1270`

### `layout.dyn.fat-pointer-pair` — Trait-object references are a 16-byte {data,vtable} fat pair

A bare dyn / &dyn / &mut dyn trait object has a 16-byte {data_ptr, vtable_ptr} fat-pointer representation and is returned by value as that pair. A reference to such a reference (Ref/`MutRef<TraitObject>`, e.g. `Vec<&dyn T>`::index -> &T) and a raw *const/*mut dyn remain a thin 8-byte pointer.

**Source:** `src/compiler/mlir_gen_impl.hpp#L683-L694`, `src/compiler/mlir_gen_impl.hpp#L878`

### `layout.dyn.fat-pointer-two-word` — Trait-object value is a two-word {data,vtable} fat pointer

A trait-object (`dyn Trait`) value is a two-word structure {field 0 = data_ptr, field 1 = vtable_ptr}; a `&dyn`/`dyn` value is itself a pointer to this 16-byte storage (mirroring a slice value), while a struct-VALUE fat pair is spilled to storage before its fields are read.

**Source:** `src/compiler/mlir_gen_dyn.cpp#L1523`, `src/compiler/mlir_gen_dyn.cpp#L1543-L1565`

### `layout.dyn.owning-vtable-slots` — Owning-dyn vtable slot order: drop, size, align

The vtable referenced by an OWNING `dyn Trait` fat handle (`Box`/`Rc`/`Arc<dyn Trait>`) exposes at least 3 pointer-sized slots in fixed order: slot 0 = `drop_in_place(T)` function pointer, slot 1 = `size_of::<T>()` encoded as a pointer-width integer, slot 2 = `align_of::<T>()` encoded as a pointer-width integer — used by the generic release path to run the destructor and to recover the `RcInner` header offset.

**Source:** `src/compiler/mlir_gen_stmt.cpp#L590-L619`

### `layout.dyn.repr` — Trait-object runtime representation is a 16-byte {data,vtable} pair

A trait object's fat-pair storage representation is `{data_ptr, vtable_ptr}`, 16 bytes, stored inline wherever a `dyn`/`&dyn` value is held (field, element, payload); a `&dyn` reference value is a pointer to this 16-byte storage, mirroring the slice representation.

**Source:** `src/compiler/mlir_gen_types.cpp#L1022-L1028`

### `layout.dyn.uniform-fat-pair` — Every dyn value is a 16-byte {data,vtable} pair

Every trait-object value (`&dyn`, `*dyn`, `Box<dyn>`) is a 16-byte {data, vtable} pair stored inline, and a `*const/*mut dyn Trait` handle always points at such a 16-byte slot. Dereferencing such a raw dyn pointer is therefore by default a no-op reinterpret (the slot pointer IS the dyn value) — except when the pointer is known, via provenance analysis, to point INTO a container slot storing just the handle (e.g. `HashMap::get -> *const Box<dyn Trait>`), in which case the stored handle is loaded.

**Source:** `src/compiler/mlir_gen_expr.cpp#L1759-L1770`, `src/compiler/mlir_gen_expr.cpp#L1782-L1793`

### `layout.dyn.vtable-header-drop-size-align` — dyn-trait vtable layout: [drop_in_place, size, align, methods..., supers...]

Every `dyn Trait` vtable for a concrete type T is a homogeneous pointer array laid out as: slot 0 = drop_in_place(T) glue, slot 1 = size_of(T), slot 2 = align_of(T), slots 3..3+M = the M trait methods in supertrait-closure slot order, then one slot per transitive upcast supertrait (in upcast order) holding that supertrait's vtable pointer for T.

**Source:** `src/compiler/mlir_gen_dyn.cpp#L1135-L1161`, `src/compiler/mlir_gen_dyn.cpp#L976-L998`

### `layout.dyn.vtable-slot-order-supertrait-closure` — vtable method slots follow full supertrait closure order

Vtable method slots are ordered by the trait's full supertrait-closure method order (supertrait methods occupy real, dispatchable slots so they are callable through `&dyn Sub`); a trait with no supertraits uses its own declared method order.

**Source:** `src/compiler/mlir_gen_dyn.cpp#L826-L833`, `src/compiler/mlir_gen_dyn.cpp#L876-L882`


## Layout · `vtable`

### `layout.vtable.drop-size-align-prefix` — Vtable layout: [drop_in_place, size, align, method0, method1, ...]

A trait-object vtable lays out the drop glue at slot 0, size_of(T) at slot 1, align_of(T) at slot 2, followed by the trait methods in declaration order; a trait method's declared vtable index i therefore resolves to physical vtable slot i+3.

**Source:** `src/compiler/mlir_gen_dyn.cpp#L1567-L1574`

### `layout.vtable.supertrait-postorder` — dyn-Trait vtable slot order = post-order DFS over supertrait graph

For trait T's dyn-T vtable, the method-slot order is a post-order DFS over T's transitive supertrait graph, deduplicated: for each supertrait (deepest ancestors first), that trait's OWN methods are appended before continuing; T's own methods are appended last. A method's index in this order is its vtable slot (codegen adds a fixed +3-slot header on top).

**Source:** `src/compiler/sema_collect.cpp#L5105-L5114`, `src/compiler/sema_collect.cpp#L5119-L5137`

### `layout.vtable.upcast-super-slots` — upcast &dyn Sub -> &dyn Super indexes a stored per-supertrait vtable-pointer slot

After a trait's method slots, one stored super-vtable-pointer slot is emitted per transitive supertrait (every trait visited during the vtable walk except the root), in the same deepest-first DFS order as the method walk. Upcasting &dyn Sub to &dyn Super indexes this slot array by the supertrait's position in that order.

**Source:** `src/compiler/sema_collect.cpp#L5110-L5114`, `src/compiler/sema_collect.cpp#L5132`


## Layout · `dst`

### `layout.dst.dyn-tail-ref-is-thin` — Ref to a struct with a dyn tail is a thin 8-byte pointer

A reference whose pointee struct has a `dyn`-tail (e.g. `&RcInner<dyn>`) is physically thin (single 8-byte pointer); the vtable lives in the heap object rather than in the reference, distinguishing it from the 16-byte {data,len} custom-DST fat reference.

**Related:** `layout.dst.slice-tail-ref-is-fat`

**Source:** `src/compiler/mlir_gen_impl.hpp#L968-L973`

### `layout.dst.effective-dst-detection` — Effective-DST classification of a struct instance

A struct/zoned-struct type is an (effective) DST iff: it is declared unsized; or, after substituting its type-args into the template's LAST field, that field type is UnsizedSlice or UnsizedDyn; or the last field is the bare tail type-var bound to a borrow-owning TraitObject. A field reached only through a pointer is always sized (a self-referential struct is not a DST via its pointer tail).

**Source:** `src/compiler/sema.cpp#L3740-L3791`

### `layout.dst.fat-when-slice-tail` — DstRef is fat only when the pointee's tail is a literal slice

A `DstRef` is genuinely fat (16-byte {data,len}) only when the pointee struct's final field is a literal `[T]`/unsized-slice kind; a DstRef whose tail is `dyn`-typed or a generic type-variable is physically thin.

**Source:** `src/compiler/mlir_gen_expr.cpp#L5056-L5066`

### `layout.dst.owned-tail-needs-fat-dstref` — An owned dyn-tail drop only fires through a fat DST reference

A let-bound value initialized from a field read drops as an owned dyn tail only when the (substituted) receiver type is a fat custom-DST reference (DstRef) and the projected field is an unsized dyn (UnsizedDyn, or a borrow-owning TraitObject); a thin pointer/reference receiver (sized inner, genuine `Arc<&dyn>`) is NOT a DST tail and its drop is a no-op.

**Source:** `src/compiler/mono_clone.cpp#L395-L414`

### `layout.dst.owning-box-same-as-borrow` — &CustomDST / `Box<CustomDST>` is a fat {data,len} pointer

A reference or raw pointer to a custom-DST struct is a fat pointer stored as {data_ptr, tail_len} with the same ABI as a slice; `&` vs `&mut`/`*mut` is distinguished for borrow-checking. An owning `Box<Foo>` custom-DST shares this fat layout but is move-only and droppable (drops tail elements and prefix fields, then frees the heap block).

**Related:** `layout.slice.owning-box-same-as-borrow`

**Source:** `src/compiler/sema_impl.hpp#L679-L700`

### `layout.dst.prefix-field-offset` — Custom-DST prefix field offsets use sequential aligned layout

For a custom-DST struct (header fields + unsized tail), a named field's byte offset is computed by the same sequential aligned-layout walk as a normal struct (offset rounded up to min(size,8) before each field); the unsized tail field, when reached, yields its aligned offset and substituted type for fat-pair projection reusing the DstRef's carried metadata.

**Source:** `src/compiler/mono_clone.cpp#L416-L441`

### `layout.dst.self-describing-ref-is-thin` — Ref to a #[self_describing] DST is a thin 8-byte pointer

A reference to a `#[self_describing]` DST is physically thin (8-byte pointer straight to the header); the tail length is recovered in-band from the pointee header rather than carried alongside the pointer.

**Divergence:** Logos custom-DST extension (#[self_describing]); no Rust equivalent.

**Related:** `layout.dst.slice-tail-ref-is-fat`

**Source:** `src/compiler/mlir_gen_impl.hpp#L976-L980`

### `layout.dst.self-describing-thin` — #[self_describing] DstRef is physically thin

A `DstRef` whose pointee struct is `#[self_describing]` is physically THIN (8-byte pointer straight to the header), even though its tail may be slice-shaped — the tail length is recovered in-band via the type's `dst_len` rather than carried out-of-band as a {data,len} pair. This is required so that returning `&Foo` from a function is sound: a fat pair would otherwise live in the callee's stack alloca and dangle after return, whereas the thin pointer IS the (heap-resident) header address.

**Source:** `src/compiler/mlir_gen_expr.cpp#L5068-L5093`

### `layout.dst.slice-tail-ref-is-fat` — Custom-DST ref with [T] slice tail is a 16-byte {data,len} fat pointer

A reference to a custom-DST struct whose last field is a literal slice tail `[T]` (or unsized slice) is represented as a 16-byte fat pointer {data: ptr, len: i64}, the element count carried inline. The length is part of the reference value, not stored in the pointee.

**Related:** `layout.dst.dyn-tail-ref-is-thin`, `layout.dst.self-describing-ref-is-thin`

**Source:** `src/compiler/mlir_gen_impl.hpp#L967-L974`


## Layout · `customdst`

### `layout.customdst.fat-pointer-pair` — Custom-DST references are a 16-byte {data,meta} fat pair

A reference to a custom DST (&CustomDst, DstRef) has a 16-byte {data_ptr, meta} fat-pointer representation, except a `#[self_describing]` DST whose tail length is recovered in-band from the header pointer (via the struct's `dst_len(*const Self)`), so its reference stays a thin pointer.

**Source:** `src/compiler/mlir_gen_impl.hpp#L880`, `src/compiler/mlir_gen_impl.hpp#L897-L902`


## Layout · `unsized`

### `layout.unsized.no-by-value` — Unsized pointees have no by-value footprint

UnsizedSlice (`[T]`) and UnsizedDyn (bare `dyn Trait` pointee, not a reference to it) have layout {0,1} — they carry no by-value footprint; only reference/pointer/box forms of them exist as values (enforced upstream by sema/borrow-check).

**Source:** `src/compiler/mlir_gen_types.cpp#L470-L471`, `src/compiler/mlir_gen_types.cpp#L663-L665`


## Layout · `closure`

### `layout.closure.fat-pointer-pair` — Closure values are a 16-byte {fn,env} fat pair

A closure has a 16-byte {fn_ptr, env_ptr} fat-pointer representation.

**Source:** `src/compiler/mlir_gen_impl.hpp#L879`

### `layout.closure.fn-env-pair` — Closure value is a {fn_ptr, env_ptr} pair

A closure value is represented as a struct with field 0 = function pointer and field 1 = environment pointer. Calling a closure loads both fields and invokes the function indirectly with env_ptr prepended as the first argument, ahead of the user-supplied arguments.

**Divergence:** A10

**Related:** `layout.fnptr.bare-call-no-env`

**Source:** `src/compiler/mlir_gen_expr.cpp#L4819-L4845`

### `layout.closure.repr` — Closure runtime representation is a 16-byte {fn,env} pair

A closure's fat-pair storage representation is `{fn_ptr, env_ptr}`, 16 bytes total.

**Source:** `src/compiler/mlir_gen_types.cpp#L1030-L1033`


## Layout · `rc`

### `layout.rc.inner-struct-layout` — `RcInner` heap-block layout for owning `Rc`/`Arc`

The heap block backing an owning `Rc<T>`/`Arc<T>` (including the `dyn Trait` case) is laid out as `RcInner = { strong: i32 (or atomic i32), weak: i32 (or atomic i32), val: T }`, with `val` at byte offset `round_up(8, align(T)) = (align(T) + 7) & ~(align(T) − 1)` from the block start (the two i32 counters occupy the first 8 bytes). The data pointer handed to consumers points at `val`; the block start is recovered by subtracting this offset from the data pointer.

**Source:** `src/compiler/mlir_gen_stmt.cpp#L610-L629`


## Layout · `pinned`

### `layout.pinned.non-movable-type` — #[pinned] type is location-anchored and non-movable

A `#[pinned]` type's bits are anchored to its storage slot: it must not be moved by value, is accessed in place, and is materialized to a movable value form only explicitly. It is non-movable itself (unlike `#[rel_ptr]`, whose value form is the resolved absolute pointer).

**Divergence:** A8

**Source:** `src/compiler/sema_impl.hpp#L2454-L2461`


## Layout · `zoned2`

### `layout.zoned2.all-thin-fields-self-relative` — #[zoned2] stores all thin-pointer fields self-relative

A `#[zoned2]` struct (or a `#[zoned2]` enum's Ref arm) stores all of its thin-pointer fields as self-relative RelOffset i64 and materializes them to absolute pointers in compute; such a type is non-movable (it cannot be stack-allocated because the offsets are anchored to the slot).

**Source:** `src/compiler/sema_impl.hpp#L2467-L2472`, `src/compiler/sema_impl.hpp#L2609`


## Layout · `zone-mut`

### `layout.zone-mut.fat-mut-ref` — #[zone_mut] gives &mut T a zone-carrying fat ref

For a `#[zone_mut]` type, `&mut T` is a fat reference {data, zone=*mut Allocator} carrying its zone so grow methods can reach the allocator from `&mut self`; a read `&T` stays thin.

**Source:** `src/compiler/sema_impl.hpp#L2462-L2466`


## Layout · `zone-mut-ref`

### `layout.zone-mut-ref.fat-data-zone` — &mut T of a #[zone_mut] type is a fat {data, zone} pair

A `&mut T` reference to a `#[zone_mut]` (FatZoneMut) type is represented as a two-word fat pointer pair `{data, zone}`. Field/method access on the referent resolves through the `data` half of the pair (peeled off before descent); every other (thin) reference kind is unaffected (identity).

**Divergence:** Logos-specific zone/Writ memory-model addition; no Rust equivalent.

**Source:** `src/compiler/mlir_gen.cpp#L668-L681`


## Layout · `repr-transparent`

### `layout.repr-transparent.inherit-field-layout` — #[repr(transparent)] inherits the single field's layout

A single-field wrapper marked `#[repr(transparent)]` inherits its field's layout exactly (size, align, and niche). Other `#[repr(...)]` modes (`C`/`packed`/`align`) are parsed then rejected.

**Source:** `src/compiler/sema_impl.hpp#L2496-L2505`

### `layout.repr-transparent.inherits-field` — `#[repr(transparent)]` inherits its single field's layout exactly

A single-field struct annotated `#[repr(transparent)]` has EXACTLY its field's {size,align} — no aggregate wrapper or padding is added. (The single-field invariant is enforced elsewhere at collect time, so this code trusts it.)

**Source:** `src/compiler/mlir_gen_types.cpp#L494-L501`


## Layout · `anyval`

### `layout.anyval.scalar-i32` — AnyVal is a scalar i32, never an aggregate

The built-in type `AnyVal` is represented as a bare i32 scalar value at every place a value of that type occurs (local bindings, receivers, struct fields) — never as an LLVM aggregate/struct value or a pointer-to-aggregate, and never spilled to a by-value aggregate slot the way a struct receiver would be.

**Divergence:** Logos-specific built-in type; no Rust equivalent (addition).

**Note:** 32-bit width inferred from coerce_numeric(raw, i32) at L904; the language-level width contract may be defined elsewhere.

**Source:** `src/compiler/mlir_gen.cpp#L744-L757`, `src/compiler/mlir_gen.cpp#L866-L868`, `src/compiler/mlir_gen.cpp#L889-L904`


## Layout · `rel-ptr`

### `layout.rel-ptr.self-relative-offset` — #[rel_ptr] field stored as self-relative i64 offset

A `#[rel_ptr]` field is stored as an 8-byte i64 byte-offset from the field's own address and materializes to an absolute thin pointer on load; it is opaque (no field access) but transparent to `*Pointee` at the value level.

**Source:** `src/compiler/sema_impl.hpp#L2448-L2453`


## Layout · `type-code`

### `layout.type-code.auto-hash-assign` — Auto type-code from name hash for concrete zoned types

A concrete (non-generic) zoned struct/datatype with TYPE_CODE==0 receives an auto-assigned code from the 56-bit hash of its canonical name pkg::Name; codes < 128 are bumped by +128 to stay outside the reserved inline-AnyVal range 1..127. Generic templates are hashed at instantiation time.

**Source:** `src/compiler/sema.cpp#L7686-L7694`, `src/compiler/sema.cpp#L7805-L7817`


## Layout · `litstr`

### `layout.litstr.len-excludes-null` — String literal fat-pointer length excludes the NUL terminator

A string literal is represented as a fat pointer {ptr, len}: the backing storage is the decoded content plus a trailing NUL byte, but len equals the content length in bytes, excluding the NUL terminator.

**Source:** `src/compiler/mlir_gen_expr.cpp#L404-L431`


## Layout · `never`

### `layout.never.zero-size-field-skipped` — A `!`-typed struct field is skipped at construction

A struct field whose initializer expression has type `!` (never) — e.g. a `PhantomData<!>` marker produced by monomorphizing a generic over the never type — has no runtime representation: the initializer is not materialized and no store to the field slot is emitted.

**Note:** Generic instantiation over `!` is unstable/nightly in Rust; unclear whether this is Rust-conformant or a Logos-specific addition, so no divergence tag assigned.

**Source:** `src/compiler/mlir_gen.cpp#L982-L988`, `src/compiler/mlir_gen.cpp#L1040-L1046`


## Layout · `index`

### `layout.index.inline-aggregate-element` — Aggregate array/buffer elements stored inline; indexing yields slot address

Elements of arrays and contiguous buffers whose element type is a struct, a tagged enum, a closure, a slice, a trait object, or a tuple are stored inline (sizeof(elem) per slot); an index read strides by the inline footprint and yields the address of the element slot (the value of such kinds being represented by a pointer to its storage), not a by-value load of a pointer-width prefix. Scalar elements are loaded by value.

**Related:** `layout.tuple.inline-elements`

**Source:** `src/compiler/mlir_gen_expr.cpp#L2980-L3037`, `src/compiler/mlir_gen_expr.cpp#L2999-L3003`, `src/compiler/mlir_gen_expr.cpp#L3035-L3037`


## Layout · `place`

### `layout.place.element-slot-by-repr` — Place/element slot type preserves full footprint

An lvalue place slot (array/Vec element stride) uses the type's full storage footprint: the concrete aggregate type for inline Struct/ZonedStruct/Tuple, the full inline {disc,payload} footprint for a tagged Enum element, and the reference repr's storage type for any reference kind — a thin pointer is 8 bytes while every fat reference (dyn trait object {data,vtable}, closure {fn,env}, slice {ptr,len}, custom-DST ref {ptr,len}) is its 16-byte pair. A self-describing DST is a thin pointer (8 bytes).

**Source:** `src/compiler/mlir_gen_expr.cpp#L1209-L1242`


## Layout · `value`

### `layout.value.scalar-vs-aggregate-storage` — Storage representation by type kind

A scalar-typed `let` binding is represented as a scalar-sized alloca holding the initializer's value, integer- and float-coerced to the declared type before the store. Struct/zoned-struct/enum(value-repr)/tuple/slice/str/closure/array-typed bindings are instead represented as pointers to their (inline) aggregate storage.

**Source:** `src/compiler/mlir_gen_stmt.cpp#L1872-L1911`, `src/compiler/mlir_gen_stmt.cpp#L1637-L1668`


## Layout · `assign`

### `layout.assign.aggregate-rvalue-byte-copy` — Struct/tuple r-value assignment into an element slot is a full-footprint byte copy

Assigning a struct-, zoned-struct-, or tuple-typed r-value (materialized by pointer) into an indexed element slot copies sizeof(type) bytes from the source to the destination, rather than storing a pointer.

**Source:** `src/compiler/mlir_gen_stmt.cpp#L3056-L3076`, `src/compiler/mlir_gen_stmt.cpp#L3185-L3205`

### `layout.assign.fat-pointer-16-byte-copy` — Fat-pointer r-value assignment copies the full 16-byte pair

A closure, slice, or trait-object (dyn) r-value occupies a 16-byte two-word storage layout ({ptr,ptr} or {ptr,len}). Assigning such a value into an indexed element slot copies all 16 bytes, never an 8-byte single-word store, so both halves of the fat pointer are written.

**Related:** `layout.dst.fat-pointer-two-word`

**Source:** `src/compiler/mlir_gen_stmt.cpp#L3077-L3091`, `src/compiler/mlir_gen_stmt.cpp#L3206-L3217`


## Layout · `call`

### `layout.call.aggregate-return-by-value` — Aggregate call results are spilled to an alloca

When an indirect call's (closure or bare fn-ptr) LLVM return type is a struct type, the caller spills the returned aggregate value to a fresh stack alloca and uses that pointer as the expression result, so downstream codegen can uniformly treat struct/tuple/enum values as `ptr`.

**Source:** `src/compiler/mlir_gen_expr.cpp#L4861-L4865`, `src/compiler/mlir_gen_expr.cpp#L4908-L4914`


## Layout · `return`

### `layout.return.aggregate-by-value` — Struct/enum return values are the full aggregate, not a pointer-shorthand

A function returning a Struct/ZonedStruct/Enum by value returns the literal registered LLVM struct / tagged-enum aggregate type at the fn-signature/call/closure-synthesis boundary, distinct from the pointer-shorthand the same Logos type maps to at parameter/field/scope positions.

**Source:** `src/compiler/mlir_gen_impl.hpp#L659-L682`

### `layout.return.fat-ref-value-vs-pointer` — Return-position representation of a fat reference is a separate axis from its storage repr

A reference value's return-position ABI is independent of its storage type: FatSlice and FatDyn return their 16-byte fat pair BY VALUE (avoiding a dangling pointer-to-local — the slice/dyn-return-by-value leak fix); FatClosure and FatCustomDst instead return an 8-byte POINTER to their fat pair (the pair's storage is owned by the callee's escape path or the caller's slot, not materialized fresh in the return); ThinPtr returns its 8-byte value directly; NotARef falls through to the ordinary non-reference return-type mapping.

**Source:** `src/compiler/mlir_gen_impl.hpp#L914-L928`


## Layout · `visibility`

### `layout.visibility.repr-query-no-pub-check` — Struct representation queries bypass privacy

A struct lookup performed to answer a layout/representation question (e.g. an internal representation check consulted during monomorphization from a foreign package's context) uses a pub-check-free accessor, distinct from the pub-checked name-resolution accessor; visibility is not enforced for representation-only queries.

**Source:** `src/compiler/sema_impl.hpp#L3180-L3195`


---

## Coercions

Implicit conversions the compiler inserts — numeric / literal defaulting, ref / reborrow / deref adjustments, unsizing, casts, and the coercion sites (let / arg / return / …) that drive them.


## Coercions · `int`

### `coerce.int.implicit-widening` — Safe implicit integer widening

An implicit integer widening from `from` to `to` is permitted iff every value of `from` is representable in `to`: signed->signed and unsigned->unsigned require to_width >= from_width; unsigned->signed requires to_width > from_width; signed->unsigned is never permitted. usize/isize are distinct types: no implicit conversion between a pointer-sized integer and any fixed-width integer (only `psize<->`psize among themselves). Either operand having undefined rank (IntLit/Enum/non-integer) blocks widening.

**Divergence:** Rust performs NO implicit integer widening at all (requires explicit `as`). Logos permits value-preserving implicit widening here.

**Related:** `type.integer.bit-width`

**Source:** `src/compiler/sema_impl.hpp#L4482-L4495`

### `coerce.int.safe-widening` — Value-preserving integer widening is implicit; signed to unsigned never

An integer coerces to a wider integer when can_widen_int holds (e.g. u32 to i64, i32 to i64, u8 to u32); signed to unsigned widening is never implicit.

**Source:** `src/compiler/sema.cpp#L1935-L1937`

### `coerce.int.to-float-by-signedness` — Integer-to-float conversion respects source signedness

An integer-to-float coercion uses unsigned-to-float when the source Logos type is one of {U8,U16,U32,U56,U64,U128}, otherwise signed-to-float (including when the source Logos type is unavailable). Float-to-int is not an implicit numeric coercion and requires an explicit cast.

**Source:** `src/compiler/mlir_gen_impl.hpp#L627-L654`

### `coerce.int.truncate-on-narrowing` — Integer narrowing truncates

Converting an integer value to a narrower integer type truncates to the destination width (low-order bits retained), independent of signedness.

**Source:** `src/compiler/mlir_gen_impl.hpp#L612-L613`

### `coerce.int.widen-by-source-signedness` — Integer widening sign- vs zero-extends by source signedness

Widening an integer value to a wider integer type sign-extends when the source type is signed and zero-extends when the source type is unsigned. Unsigned source kinds = {U8,U16,U24,U32,U56,U64,U128} (and Bool). bool (i1) is always zero-extended. Without a known source Logos type, the coercion defaults to sign-extend.

**Source:** `src/compiler/mlir_gen_impl.hpp#L590-L615`

### `coerce.int.widen-or-literal-fits` — Implicit int widening or literal-fits cast

widen_int_expr(e, target): if e's int kind ek differs from target kind tk, e is cast to target when either (a) ek widens to tk per can_widen_int, or (b) e carries a known literal value (get_intlit_value) that fits tk (intlit_fits). Otherwise e is left unchanged (also unchanged when ek == tk already).

**Source:** `src/compiler/sema_impl.hpp#L4419-L4464`


## Coercions · `intlit`

### `coerce.intlit.dispatch-unsuffixed-fits-narrower` — Unsuffixed int literal dispatches to any param it numerically fits

In overload/dispatch argument compatibility, an arg of Kind IntLit (unsuffixed literal) is compatible with param type P iff P is an integer kind ≠ Enum and the literal's value fits P (intlit_fits). A SUFFIXED literal (`9u64`) has a concrete int type and must match by type equality/compatibility only — it does not narrow-flex to other widths that also happen to fit the value (Rust parity: `9u64` is `u64`, period). A param whose kind is Enum can never be hit by a bare integer literal (would reinterpret the int as the enum's by-pointer storage).

**Source:** `src/compiler/sema_impl.hpp#L4465-L4486`

### `coerce.intlit.to-integer-typevar-float` — Integer/float literal coercion to numeric, type-var, float

An IntLit coerces to any integer kind, to a TypeVar, and to F32/F64. A FloatLit coerces to F32/F64 and to a TypeVar.

**Source:** `src/compiler/sema.cpp#L1902-L1908`

### `coerce.intlit.unify-to-concrete` — Unsuffixed integer literal unifies to the other operand's type

When unifying two integer types, if one is the unsuffixed literal type IntLit it unifies to the other operand's type. Otherwise the narrower is widened to the wider when a safe implicit widening exists (per coerce.int.implicit-widening); if neither widens, the first operand's type is kept.

**Related:** `coerce.int.implicit-widening`, `coerce.numericlit.unify-to-concrete`

**Source:** `src/compiler/sema_impl.hpp#L4497-L4506`


## Coercions · `numeric`

### `coerce.numeric.return-value` — Scalar return values are numerically coerced to the declared return type

For a scalar (non-aggregate, non-fat-pointer) return value whose type does not already match the function's return representation, the value is coerced (widened/narrowed/signed-adjusted) to the declared return type before the return is emitted.

**Source:** `src/compiler/mlir_gen_stmt.cpp#L2292-L2293`


## Coercions · `numericlit`

### `coerce.numericlit.unify-to-concrete` — Numeric literal (int or float) unifies to the concrete operand

When unifying two numeric types where either operand may be a literal, an unsuffixed IntLit or FloatLit operand unifies to the other operand's (concrete) type; FloatLit thereby promotes to a concrete float type F32/F64.

**Related:** `coerce.intlit.unify-to-concrete`

**Source:** `src/compiler/sema_impl.hpp#L4514-L4517`


## Coercions · `float`

### `coerce.float.widen-truncate` — Float-to-float widening extends, narrowing truncates

Converting a float to a wider float type extends; to a narrower float type truncates. Equal-width is a no-op.

**Source:** `src/compiler/mlir_gen_impl.hpp#L617-L625`


## Coercions · `str`

### `coerce.str.slice-method-alias` — `str` receivers fall back to `Slice<u8>`-typed method lookup under the `str__` mangling

`str` is represented as `&[u8]` (`type_str` yields `&[u8]`), but impls written `impl Trait for str` register methods mangled as `str__<method>`. When receiver type-string is `&[u8]` and lookup under the literal `&[u8]__<method>` key fails, resolution retries under `str__<method>` (concrete, then generic).

**Source:** `src/compiler/sema_expr.cpp#L8233-L8242`


## Coercions · `array`

### `coerce.array.elementwise` — Arrays compatible iff equal size and compatible elements

`Array<T;N>` is compatible with `Array<U;M>` iff N==M and T is compatible with U (recursively).

**Source:** `src/compiler/sema.cpp#L1942-L1945`

### `coerce.array.ref-to-slice-preserves-mut` — Array-ref-to-slice unsizing preserves mutability

When `&mut [T;N]` (or `*mut [T;N]`) unsizes to a slice, the result is a mutable slice `&mut [T]`; a shared `&[T;N]` yields a shared `&[T]`. A shared array reference may not satisfy a `&mut [T]` parameter.

**Source:** `src/compiler/sema_impl.hpp#L365-L375`

### `coerce.array.ref-to-slice-unsize` — `&[T;N]`/*[T;N] unsizes to `&[T]` slice

A reference or raw pointer to an array, `&[T;N]` / `&mut [T;N]` / `*const [T;N]` / `*mut [T;N]`, coerces to a slice `&[T]` by building a fat pointer {data = the array address, len = N}. The coercion applies only when the array element type is compatible with the target slice's element type.

**Source:** `src/compiler/sema_impl.hpp#L346-L377`

### `coerce.array.to-pointer-decay` — Array and &array decay to raw pointer / reference without mutability widening

`Array<T>` coerces to *const/T-pointee Ptr when elem==pointee. A `&[T;N]`/&mut[T;N] decays to *const/*mut T or &/&mut T over a compatible element type, but a shared (&) source may not decay to a mutable (*mut/&mut) target.

**Source:** `src/compiler/sema.cpp#L1938-L1941`, `src/compiler/sema.cpp#L1999-L2019`


## Coercions · `array-lit`

### `coerce.array-lit.heterogeneous-element-unsize` — Array/slice literal elements unsize individually to an annotated dyn-Trait element type

An array/slice literal typed against an expected element type via an outer annotation (e.g. `let arr: [&dyn Trait; N] = [...]`) may contain elements of different concrete reference types (`[&Sq, &Ci]`); each element unsizes (per-element, at codegen) to the expected `&dyn Trait` element type, rather than the literal being rejected for element-type mismatch.

**Source:** `src/compiler/sema_impl.hpp#L3725-L3730`


## Coercions · `slice`

### `coerce.slice.exact-scalar-no-mut-widen` — Slice compatibility: no shared to mut widening; concrete scalar elements must match exactly

`Slice<T>` is compatible with `Slice<U>` only if it does not widen a shared (&) slice to a mutable (&mut) slice, and: two concrete scalar element types (concrete integer excl. IntLit/Enum, F32/F64/Bool/Char) must be kind-identical (slices alias raw memory at element stride); inference holes use lenient compatibility.

**Source:** `src/compiler/sema.cpp#L1946-L1968`

### `coerce.slice.to-array-ref-recovery` — Shared `&[T]` over an array variable recovers `&[T;N]`

A shared slice `&[T]` that was formed from `&array_var` may coerce back to a ref-to-array `&[T;N]` / `*const [T;N]` when the parameter wants it, provided the underlying variable is an array whose actual size equals N. The mutable case is excluded: a shared slice never satisfies a `&mut [T;N]` parameter.

**Source:** `src/compiler/sema_impl.hpp#L378-L408`


## Coercions · `tuple`

### `coerce.tuple.elementwise` — Tuples compatible iff equal arity and pairwise-compatible elements

Tuple types are compatible iff they have equal arity and each element pair is compatible.

**Source:** `src/compiler/sema.cpp#L1969-L1975`


## Coercions · `tuple-lit`

### `coerce.tuple-lit.widen-to-expected-element-type` — Tuple literal elements widen to the expected tuple type's element types

When a tuple literal appears in a position with a known expected tuple type (e.g. a parameter or `let` with a tuple-type annotation), each untyped integer-literal element widens to the corresponding expected element type at lowering, instead of defaulting to `i32` — preventing a narrower-typed literal buffer from being read back by the callee under the wider declared element type.

**Source:** `src/compiler/sema_impl.hpp#L3708-L3713`


## Coercions · `struct`

### `coerce.struct.elementwise-typeargs` — Same-named structs compatible iff type-args pairwise compatible

Two Struct types with equal struct_name and pkg_name and equal type-arg arity are compatible iff every type-arg pair is compatible (allowing inference holes like `Vec<_>` vs `Vec<i32>`).

**Divergence:** logos-core 1.3 (nested)

**Source:** `src/compiler/sema.cpp#L1846-L1857`


## Coercions · `struct-lit`

### `coerce.struct-lit.closure-to-fnptr-fallback` — closure literal field value coerces to fn-pointer field type

Before reporting a field type-mismatch, a closure-literal field-init value is attempted to coerce to the declared fn-pointer field type (try_coerce_closure_to_fnptr); success suppresses the mismatch error.

**Source:** `src/compiler/sema_expr.cpp#L10282`, `src/compiler/sema_expr.cpp#L10420`

### `coerce.struct-lit.field-numeric-coercion` — Struct-literal scalar fields coerce to the field's declared type

When a struct-literal field initializer's value has a scalar type that differs from the field's declared type (e.g. an integer or float literal), the value is numerically coerced (widen/narrow/int-float conversion) to the field's declared type before being stored.

**Source:** `src/compiler/mlir_gen.cpp#L1012-L1039`


## Coercions · `enum`

### `coerce.enum.bare-literal-retype-to-param` — Incompletely-typed enum-literal argument retyped to parameter's enum spec

An enum literal passed as an argument with missing or unresolved type-args (e.g. bare `Opt::None`, partially-inferred `Opt::Some(3)`) is retyped to the parameter's concrete enum type, pinning the missing type-args. Retype fires only when the literal's already-known (non-error) type-args match the target's, so a genuine mismatch is still rejected.

**Source:** `src/compiler/sema_impl.hpp#L500-L508`, `src/compiler/sema_impl.hpp#L582-L599`

### `coerce.enum.elementwise-typeargs-no-widen` — Same-named enums compatible by type-args, but concrete scalar args must match exactly

Two Enum types with equal enum_name, pkg_name, and non-empty equal type-arg arity are compatible iff for each arg pair: an unresolved placeholder (TypeVar/_/cfg-slot/Error) on either side unifies; otherwise two concrete scalar args (concrete integer excl. IntLit/Enum, F32/F64/Bool/Char) must be kind-identical (no by-value widening, layout is arg-width-specific); all other pairs use lenient compatibility.

**Source:** `src/compiler/sema.cpp#L1859-L1901`

### `coerce.enum.incomplete-typeargs-retype` — Incomplete enum-literal type-args inferred from expected enum type

An enum literal whose value type has empty or unresolved type-args is retyped to the expected enum type when both are the same enum (`at.enum_name() == pt.enum_name()`), the expected type-args are all resolved, and each already-resolved arg of the literal is compatible with the corresponding expected arg; retyping recurses into nested payload enum-literals.

**Note:** Compatibility predicate `types_compatible` defined elsewhere; here only the gating conditions are observable.

**Source:** `src/compiler/sema_impl.hpp#L600-L619`

### `coerce.enum.retype-nested-payload-recursive` — Enum-literal retype projects type-args through variant payloads recursively

When pinning an enum-literal expression to a concrete enum type, the concrete type-args are substituted into the matched variant's payload types, and each payload sub-expression that is itself an enum literal is recursively retyped. This prevents a nested literal (e.g. the inner `Option::None` of `Option::Some(Option::None)`) from staying a bare C-style enum while the outer slot is a heap pointer.

**Source:** `src/compiler/sema_impl.hpp#L545-L581`

### `coerce.enum.to-integer-discriminant` — C-style enum coerces to integer (discriminant) but never to another enum

An Enum coerces to a non-enum integer kind (its discriminant). Enum to Enum via this rule is forbidden, and implicit int to Enum is forbidden (requires explicit cast/variant).

**Source:** `src/compiler/sema.cpp#L1922-L1934`


## Coercions · `ref`

### `coerce.ref.permission-and-pointee` — Reference/pointer coercions: pointee compatibility, permission-dropping only

Reference and raw-pointer coercions require compatible pointees: &/&mut to *const/*mut, *T to &/&mut, &mut T to &T (exclusive to shared), &T to &T and &mut T to &mut T, and *mut T to *const T (dropping write permission). Permission is only ever dropped, never gained.

**Source:** `src/compiler/sema.cpp#L2020-L2061`

### `coerce.ref.unsized-dyn-canonicalizes-to-traitobject` — &`UnsizedDyn<Trait>` canonicalizes to `TraitObject<Trait>`

Forming a reference to an unsized-dyn pointee, `&UnsizedDyn<Trait<args...>>`, canonicalizes to the trait-object fat-pointer type `TraitObject<Trait<args...>>`, preserving the trait's type-args. Ensures `&self` and `other: &Self` for an impl-on-dyn mangle identically.

**Source:** `src/compiler/sema_impl.hpp#L243-L246`

### `coerce.ref.unsized-slice-canonicalizes-to-slice` — &`UnsizedSlice<T>` canonicalizes to `Slice<T>`

Forming a reference to an unsized-slice pointee, `&UnsizedSlice<T>`, canonicalizes to the fat-pointer slice type `Slice<T>` (= `&[T]`). The reference layer is collapsed into the slice's own fat pointer.

**Note:** Inferred from make_ref special-casing; the canonical syntactic form is also enforced at resolve_type.

**Source:** `src/compiler/sema_impl.hpp#L233-L242`

### `coerce.ref.widen-int-literal-temp-pointee` — Widening a `&int-literal` argument widens the temp's inner literal, not the pointer

Coercing an argument whose type is `&T1`/`&mut T1` and lowered form is AddrOfTemp(int-literal) to an expected `&T2`/`&mut T2` (T1,T2 both integer kinds, T1≠T2, literal fits T2) rewrites the AddrOfTemp's INNER expr — casting the literal to T2 and rebuilding the AddrOfTemp with target type `&T2` — rather than casting the outer reference. Codegen sizes the temporary's stack slot from the literal's own (unwidened) type; leaving it un-rewritten would let the callee load T2 through a pointer to an undersized/mistyped slot, reading adjacent stack memory.

**Source:** `src/compiler/sema_impl.hpp#L4423-L4454`


## Coercions · `reborrow`

### `coerce.reborrow.downgrade-mut-to-shared` — Downgrading reborrow `&mut T` → `&T` gated on allow_downgrade

When the formal is `&T` (shared), a `&mut T` argument may be implicitly reborrowed as a shared `&T` only when downgrade is permitted (fn-arg coercion). At method-receiver position downgrade is forbidden, because a formal `&Self` whose Self IS a `&mut X` (impl on a ref type) would otherwise dispatch through the wrong impl key. For a `*U` formal, dest mutability follows the formal pointer's mutability bit.

**Source:** `src/compiler/sema_expr.cpp#L12926-L12939`

### `coerce.reborrow.implicit-mut-reborrow` — Implicit &mut reborrow at coercion sites

A `&mut T` value is implicitly reborrowed when passed where a reference is expected, allowing a single `&mut` binding to be used at multiple coercion sites without an explicit reborrow.

**Source:** `src/compiler/sema_impl.hpp#L439-L440`, `src/compiler/sema_impl.hpp#L478`

### `coerce.reborrow.method-receiver-no-downgrade` — Method receiver binds without &mut→& downgrade

Binding a method receiver to its formal `self` slot performs implicit auto-reborrow but never downgrades a `&mut Self` receiver to `&Self`, so receiver mutability selects the correct impl key (e.g. for `impl X for &mut M` ref-impls). By-value `self` triggers move tracking of the receiver.

**Source:** `src/compiler/sema_impl.hpp#L442-L448`

### `coerce.reborrow.mut-place-at-coercion-site` — Implicit reborrow of `&mut T` place at call/method argument sites

At an argument coercion site, an expression of type `&mut T` that is a PLACE (VarRef, FieldRead, or IndexRead) and whose formal parameter is ref-shaped — `&mut U`, `&U`, or `*U` — is implicitly reborrowed as `AddrOfTemp(Deref(e))` rather than moved, registering a borrow on the original `&mut T` binding instead of consuming it. Reborrow is structural: the result has the SAME pointee type as the source; the genuine argument type-check runs afterward.

```logos
fn f(x: &mut T) { g(x); h(x); }  // x reborrowed, not moved
```

**Source:** `src/compiler/sema_expr.cpp#L12924-L12955`

### `coerce.reborrow.no-reborrow-of-fresh-borrow` — No reborrow of a fresh borrow expression

Implicit reborrow applies only when the `&mut T` operand is a place expression (VarRef / FieldRead / IndexRead). A fresh borrow expression (e.g. `&mut x`, `&mut p.f`) is left as-is and never wrapped in a reborrow shape, so its borrow is recorded through the normal path.

**Source:** `src/compiler/sema_expr.cpp#L12947-L12951`


## Coercions · `deref`

### `coerce.deref.box-slice-borrow` — `&Box<[T]>` borrows as `&[T]`

`&b` where `b: Box<[T]>` (an owning slice) is a Deref-coercion borrow: yields `&[T]` sharing the box's `{data,len}` representation — the same storage pointer re-typed as a borrowed slice, no copy, no move.

**Source:** `src/compiler/sema_expr.cpp#L2533-L2538`

### `coerce.deref.box-struct-borrow` — `&Box<S>` / `&Box<dyn Trait>` borrows as `&S` / `&dyn Trait`

`&b` where `b: Box<S>` for a custom-DST struct `S` (owning DstRef) or `b: Box<dyn Trait>` (owning trait object) is a Deref-coercion borrow: the box's VALUE is already the `{data,len}` DstRef pair or the `{data,vtable}` fat pair, so borrowing it reads the var's value (var_ref load) and re-types it non-owning — it never re-addresses the local slot, which would produce the wrong indirection (e.g. a thin `&&dyn Trait` where the callee expects the 16-byte fat pair by value).

**Source:** `src/compiler/sema_expr.cpp#L2539-L2565`

### `coerce.deref.ref-vec-to-slice` — &`Vec<T>` / &mut `Vec<T>` deref-coerces to slice `&[T]`

A Ref/MutRef over a stdlib `Vec<T>` struct coerces to a Slice with element compatible with Vec's first type-arg (Vec's {ptr,len,cap} has the {ptr,len} slice fat-pointer as a prefix).

**Note:** Hardcoded to the stdlib Vec struct by name; full Deref trait surface not yet covered.

**Source:** `src/compiler/sema.cpp#L2034-L2049`

### `coerce.deref.user-deref-chain` — Deref coercion through user/stdlib Deref(Mut) impls

A receiver of struct type with a Deref/DerefMut impl deref-coerces by calling `deref`/`deref_mut` to obtain `&Target`; for an unsized Target (dyn or slice) the returned fat reference is the value itself with no further place-deref. A mutable deref step may fall back to the Deref Target when only Deref is implemented (shared supertrait Target).

**Source:** `src/compiler/sema_expr.cpp#L100-L217`


## Coercions · `unsize`

### `coerce.unsize.already-fat-passthrough` — Fat-pointer source needs no rebuild

Coercing a `let` initializer to `&dyn Trait` / `*const|*mut dyn Trait` / `Box<dyn Trait>` when the source expression's type is already a trait-object (fat {data,vtable} pair — including after peeling one level of `&`/`&mut`/`*const`/`*mut` wrapping a trait object) does not rebuild the fat pair from a concrete type: if the source is itself a POINTER to an existing fat place, a fresh 16-byte slot is allocated and the fat {data,vtable} pair is memcpy'd into it (Copy-value semantics for `&dyn`/`Box<dyn>` bindings); a raw `*const dyn Trait` / `*mut dyn Trait` instead binds the alias directly (handle semantics — aliasing is the defining property of a raw pointer, so no copy is made).

**Related:** `coerce.unsize.box-concrete-to-box-dyn`

**Source:** `src/compiler/mlir_gen_stmt.cpp#L1715-L1765`

### `coerce.unsize.arg-struct-to-dyn-trait` — Implicit unsize coercion of call arguments to &dyn / `Box<dyn>`

At a call site, when the callee's (mono-resolved) parameter type is a trait object (bare, or under `&`/`&mut`/`Box`) and the argument's static type is a concrete (non-trait-object) type, the argument is implicitly unsize-coerced into a fat `{data, vtable}` trait-object value. The vtable is looked up on the peeled concrete pointee: `&T`/`&mut T` and `Box<T>` both peel to `T`; a bare struct-valued argument (not already a reference or Box) is first spilled to storage to obtain a data pointer.

**Source:** `src/compiler/mlir_gen_expr.cpp#L2449-L2510`

### `coerce.unsize.array-to-slice` — `&array` unsizes to `&[T]`

`&arr` where `arr: [T; N]` (a named array local, static or otherwise) coerces to `&[T]`: produced as a slice literal `{ addr_of(arr) as &T, len = N }` typed `&[T]`, never `&[T; N]` (`Ref<Array>`). The same coercion applies to a bare array literal `&[e0, e1, …]`: the array rvalue is spilled to a stack slot first, then wrapped as `{addr, len}` typed `&[T]`.

**Source:** `src/compiler/sema_expr.cpp#L2527-L2531`, `src/compiler/sema_expr.cpp#L2603-L2618`

### `coerce.unsize.box-array-to-box-slice` — `Box<[T;N]>` unsizes to owning `Box<[T]>`

`Box<[T;N]>` cast to an owning `Box<[T]>` (Slice with owning_slice) builds a {data,len} fat pair: data = the box's heap pointer (field 0 of the Box struct), len = N (the array's compile-time size). This is CoerceUnsized for `Box::new([..]) as Box<[T]>`.

**Related:** `coerce.unsize.thin-array-ptr-to-slice`

**Source:** `src/compiler/mlir_gen_expr.cpp#L3366-L3398`

### `coerce.unsize.box-concrete-to-box-dyn` — `Box<Concrete>` unsizes to `Box<dyn Trait>`

Coercing a concrete-typed `let` initializer (`&Concrete`, `*const|*mut Concrete`, or owning `Box<Concrete>`) to `&dyn Trait` / `Box<dyn Trait>` / `*const|*mut dyn Trait` builds a {data,vtable} fat pair: one level of `&`/`&mut`/`*const`/`*mut` wrapping is peeled to the pointee; an owning `Box<Concrete>` source is unwrapped to its single type argument; the vtable is looked up keyed on the concrete type's fully mono-mangled name (not its surface generic-angle-bracket spelling). An owning `Box<Concrete>` source coercing to `Box<dyn Trait>` yields an owning `Box<dyn Trait>` whose data handle is heap-allocated (so the scope-exit drop_in_place+free sequence frees a real heap block); a `&Concrete` borrow source instead yields a stack-resident fat pair.

**Related:** `coerce.unsize.already-fat-passthrough`

**Source:** `src/compiler/mlir_gen_stmt.cpp#L1766-L1800`, `src/compiler/mlir_gen_stmt.cpp#L1955-L1993`

### `coerce.unsize.box-consumes-source` — Unsize to owning trait object moves the source

An unsize cast to an owning trait object (e.g. `box_val as Box<dyn Trait>`) consumes/moves the operand; ownership of the heap data transfers to the result so the source's own drop does not also run (avoiding double-free).

**Related:** `coerce.unsize.struct-coerce-unsized`

**Source:** `src/compiler/sema_expr.cpp#L986-L991`

### `coerce.unsize.box-dyn-deref-then-unsize` — implicit value-to-dyn coercion unwraps refs and Box before unsizing

When a value's expected slot type is a trait object but the value's type is not, the value is coerced to dyn: references and `Box<T>` are first unwrapped to reach the concrete pointee, an already-`dyn` value is passed through unchanged, and a non-pointer value is spilled to a stack slot to obtain an address before forming the fat pair.

**Source:** `src/compiler/mlir_gen_dyn.cpp#L1236-L1271`

### `coerce.unsize.box-dyn-vtable-drops-concrete` — Owning `Box<dyn>` coercion threads the concrete destructor

When a concrete `Box<T>` argument is unsize-coerced to a `Box<dyn Trait>` parameter, the vtable is keyed on the concrete monomorphized type `T` (not the literal `Box<T>`/angle-bracket type string), so the vtable's drop-in-place slot runs `T`'s destructor. The coerced value is passed as an inline fat data/vtable pair by value (the callee drops it directly), not as a separate heap handle.

**Related:** `coerce.unsize.arg-struct-to-dyn-trait`

**Source:** `src/compiler/mlir_gen_expr.cpp#L2489-L2509`

### `coerce.unsize.concrete-to-dyn-builds-fat-pair` — unsizing a concrete to dyn stores data + vtable into the fat pair

Coercing a concrete pointer to `&dyn Trait` builds the fat pair by storing the data pointer at field 0 and the vtable pointer (the address of the static per-(trait,type) vtable) at field 1. The source type name used to select the vtable is the concrete struct mangled name for (zoned) structs, else the plain type string.

**Source:** `src/compiler/mlir_gen_dyn.cpp#L1204-L1234`, `src/compiler/mlir_gen_dyn.cpp#L1259-L1270`

### `coerce.unsize.dyn-auto-trait-bound-gate` — dyn coercion enforces + Send / + Sync auto-trait bounds

When coercing to a `dyn Trait + Send` / `+ Sync` target, the source pointee must structurally satisfy the named auto-trait bounds; failure is a coercion-site error. The general Struct→TraitObject acceptance does not waive these auto-trait constraints.

**Source:** `src/compiler/sema_impl.hpp#L417-L422`

### `coerce.unsize.dyn-field-and-element` — &dyn Trait struct field builds the fat pointer at init time

Initializing a `&dyn Trait`/`&mut dyn Trait` struct field from a value whose static type is a concrete reference/pointer to a struct performs the trait-object unsizing coercion (builds the data-pointer+vtable fat pointer) at the initialization site before storing into the field; a value that is already a trait object is stored unchanged.

**Related:** `layout.zone-mut-ref.fat-data-zone`

**Source:** `src/compiler/mlir_gen.cpp#L991-L1011`

### `coerce.unsize.dyn-place-assign` — Assignment to a `&dyn Trait`-typed place performs unsize coercion

Assigning a concrete-typed (or already-dyn) source to a place typed `&dyn Trait` coerces the source into the `{data,vtable}` fat-pointer representation (same coercion `let`/`return` apply) and copies the resulting 16-byte pair into the target slot. A raw `*const dyn T` / `*mut dyn T` place is exempt: it keeps plain pointer-store (aliasing) semantics.

**Source:** `src/compiler/mlir_gen_stmt.cpp#L2089-L2119`

### `coerce.unsize.dyn-return-value` — Returning a concrete value where `Box<dyn Trait>`/`&dyn Trait` is expected performs unsize coercion

When a function's return type is a TraitObject and the returned expression's type is a concrete (non-TraitObject) type, the concrete value is coerced to the `{data,vtable}` fat pair (vtable keyed on the underlying concrete type name, stripping one level of `&`/`&mut`/`*const`/`*mut` indirection) and returned BY VALUE as that 16-byte struct.

**Source:** `src/compiler/mlir_gen_stmt.cpp#L2182-L2218`

### `coerce.unsize.dyn-storage-stack-vs-heap` — Coercing to a dyn fat pair allocates on the stack for a borrow, on the heap for an owning handle

Building a `{data, vtable}` fat pair for a trait-object coercion places the pair on the stack (alloca) when the destination is a BORROWING handle (`&dyn`/`&mut dyn` — value-fat-pair model; the consumer copies the 16 bytes if it escapes a struct field / array / by-value return), and on the heap (malloc(16)) when the destination is an OWNING handle (`Box<dyn>`, raw `*const/*mut dyn`) whose single 8-byte handle is itself stored/escapes and is freed by that handle's drop.

**Related:** `layout.dyn.fat-pair-16-byte`, `intrinsic.drop.owning-dyn-handle`

**Source:** `src/compiler/mlir_gen_impl.hpp#L1068-L1077`

### `coerce.unsize.dyn-supertrait-upcast` — &dyn Sub coerces to &dyn Super

A trait-object reference `&dyn Sub` coerces to `&dyn Super` when `Sub` has `Super` as a supertrait (dyn upcast).

**Source:** `src/compiler/sema_impl.hpp#L432`, `src/compiler/sema_impl.hpp#L476`

### `coerce.unsize.lifetime-diff-not-unsize` — Type-arg differences that are not unsizes fall through to variance

A struct type-arg difference that is not a sized→fat unsize (e.g. lifetime-only variance `Foo<&'a>` vs `Foo<&'b>`, or multi-field structs) is NOT handled as CoerceUnsized; it must be resolved by the variance/compat machinery. CoerceUnsized requires exactly one field and that field's type to genuinely become fat.

**Related:** `coerce.unsize.struct-coerce-unsized`

**Source:** `src/compiler/sema_expr.cpp#L669-L688`

### `coerce.unsize.raw-ptr-dyn-handle-semantics` — Raw pointer-to-dyn-Trait bindings keep handle (non-copying) semantics

A `let` binding of type `*const dyn Trait` / `*mut dyn Trait` keeps handle semantics distinct from `&dyn Trait`/`Box<dyn Trait>`: it is never copied into a fresh fat slot at bind time. The bound value is either the raw fat pointer itself (default: a coerced handle, a parameter, or a field read — a later dereference is a no-op read of the handle), or, when the initializer is recognized as the return of a container-accessor method call (or a chained copy of such a value), a pointer INTO existing storage — a later dereference must instead LOAD the stored handle from that address.

**Note:** The accessor-return vs. coerced-handle distinction is determined by a syntactic heuristic (initializer is a MethodCall, or a VarRef previously so marked) rather than a type-level property; edge cases outside a direct method-call/var-chain may be misclassified.

**Source:** `src/compiler/mlir_gen_stmt.cpp#L1719-L1722`, `src/compiler/mlir_gen_stmt.cpp#L1751-L1798`

### `coerce.unsize.ref-concrete-to-dyn-trait` — &Concrete unsizes to &dyn Trait in argument position

An argument `&T` / `&mut T` coerces (unsizes) to a `&dyn Trait` / `&mut dyn Trait` parameter when the pointee implements the trait directly or via a blanket impl, or when the pointee is a type-variable whose in-scope bounds include the trait. The fat pointer is built at the call site.

**Source:** `src/compiler/sema_impl.hpp#L409-L431`

### `coerce.unsize.ref-concrete-to-trait-object` — Reference/pointer to concrete unsizes to bare trait object

`&T`/`&mut T`/`*const T`/`*mut T` (T a concrete struct or primitive) cast to a bare trait object synthesizes a {data,vtable} fat pair; the vtable keys on T's concrete struct name (or the primitive's bare type name for a blanket-impl `&i64 as &dyn`). Only fires when the source pointee is concrete; a `&dyn`→`dyn` reinterpret (pointee already a trait object) is a no-op.

**Divergence:** Uniform-fat model: `&dyn` and `*mut dyn` are both 16-byte fat pairs (Logos), unlike Rust where only references unsize.

**Source:** `src/compiler/mlir_gen_expr.cpp#L3470-L3493`

### `coerce.unsize.return-concrete-to-trait-object` — returning a concrete type where `dyn Trait` is expected unsizes to a fat pointer

When the function return type is a trait object `dyn Trait` and the returned value's type is a non-trait-object concrete type T (or a reference/pointer `&T`/`&mut T`/`*const T`/`*mut T` whose pointee is the bare struct), the value is coerced to a {data, vtable} fat pointer keyed on the bare struct name and returned by value (16-byte pair); indirection layers over the concrete struct are stripped before vtable lookup.

**Related:** `coerce.unsize.struct-to-dyn-trait`

**Source:** `src/compiler/mlir_gen_stmt.cpp#L2037-L2070`

### `coerce.unsize.smart-ptr-to-box-dyn` — Box/Rc/`Arc<T>` unsizes to owning dyn fat pair

`Box<T>`/`Rc<T>`/`Arc<T>` (T concrete) cast to a dyn smart pointer builds a value fat pair {data,vtable}: for Box, data = field 0 (the heap pointer); for Rc/Arc, data = field0 + round_up(8, align(T)) (skipping the 2×i32 RcInner strong/weak header). vtable[0..2] = drop/size/align; drop is kind-specific (Box→free; Rc/Arc→dec strong + free RcInner). A dyn-payload smart pointer is already a handle and is not re-wrapped.

**Source:** `src/compiler/mlir_gen_expr.cpp#L3502-L3552`

### `coerce.unsize.struct-coerce-unsized` — CoerceUnsized for single-field smart-pointer structs

A value of struct type `S<..A>` coerces to `S<..B>` (same struct, equal type-arg arity) when S has exactly one field whose substituted type changes from sized/thin to fat-unsized: target field kind is DstRef, or (TraitObject while source isn't), or (Slice while source isn't). The coercion reads the single field, casts it to the target field type, and repacks into the target struct.

```logos
let r: Rc<dyn Tr> = rc_a as Rc<dyn Tr>;
```

**Related:** `coerce.unsize.box-consumes-source`

**Source:** `src/compiler/sema_expr.cpp#L648-L699`

### `coerce.unsize.struct-dyn-tail-to-dstref` — Pointer to struct with concrete tail unsizes to DstRef with dyn tail

`*mut/*const/& ConcreteStruct<…, Sized>` cast to a DstRef whose tail type-arg is a trait object (`*mut Inner<dyn Tr>`) builds a {data,vtable} fat pair: data = the source thin pointer to the whole struct; vtable = the concrete tail type's vtable for the tail trait (the tail binding is the source instance's last type-arg). This is CoerceUnsized for a struct with an unsized (`dyn`) tail field.

**Source:** `src/compiler/mlir_gen_expr.cpp#L3417-L3455`

### `coerce.unsize.struct-to-dyn-trait` — Struct (or &/&mut/*Struct) unsize-coerces to a trait object

A Struct, a Ptr, or a &/&mut over a Struct coerces to a TraitObject (&dyn Trait); the impl check is deferred to codegen.

**Source:** `src/compiler/sema.cpp#L1988-L1998`

### `coerce.unsize.struct-wrapper-coerceunsized` — Single-field wrapper struct CoerceUnsized

A smart-pointer/wrapper struct with a single unsizable field coerces `Wrapper<A>` → `Wrapper<dyn Trait>` (or to a slice/DstRef target) by unsizing that field, keeping the same struct. Applies at explicit `as` casts and at implicit coercion sites (argument, let, return).

**Source:** `src/compiler/sema_impl.hpp#L433-L438`

### `coerce.unsize.thin-array-ptr-to-slice` — Thin array pointer to slice pointer synthesizes len=N

`*const [T;N]`/`*mut [T;N]` (`Ptr<Array>`) cast to `*const [T]`/`*mut [T]` (Slice) synthesizes a {ptr, len=N} fat pair on the stack, where N is the array's compile-time size; without this the cast would be a no-op leaving array contents misread as the data field.

**Related:** `coerce.unsize.box-array-to-box-slice`

**Source:** `src/compiler/mlir_gen_expr.cpp#L3653-L3677`

### `coerce.unsize.value-to-dyn-at-trait-slot` — Concrete value unsized to a fat dyn handle when the destination slot is a trait object

When a destination slot has trait-object type (`dyn`/`Box<dyn>`/`&dyn`) but the supplied value is still concrete (`Box<Concrete>`, `&Concrete`, struct value), the value is unsize-coerced into a fat {data, vtable} handle (e.g. an enum-variant payload typed `Box<dyn>` constructed from `Box<Concrete>`). It is a no-op if the value already is a trait object.

**Related:** `layout.dyn.fat-pair-16-byte`

**Source:** `src/compiler/mlir_gen_impl.hpp#L1078-L1084`


## Coercions · `dyn`

### `coerce.dyn.arg-to-trait-object` — Implicit unsize coercion of a concrete argument to a `dyn Trait` formal

When a formal is a trait object (bare or behind `&`/`&mut`) and the argument is not already compatible, the argument coerces by: (a) CoerceUnsized of a smart-pointer/wrapper struct (`Rc<A>` → `Rc<dyn Tr>`) rebuilt by unsizing the inner field; or (b) cast to the dyn type when the argument's (ref) type satisfies the target trait object.

**Source:** `src/compiler/sema_expr.cpp#L12996-L13014`

### `coerce.dyn.upcast-to-supertrait` — Implicit `dyn Sub` → `dyn Super` upcast

An argument of trait-object type `dyn Sub` (bare or behind `&`/`&mut`) implicitly coerces (via cast) to a distinct formal trait-object type `dyn Super` iff Super is a transitive supertrait of Sub (Sub != Super). Identical dyn types do not coerce.

**Source:** `src/compiler/sema_expr.cpp#L12958-L12993`


## Coercions · `cast`

### `coerce.cast.aggregate-scalar-forbidden` — as-cast forbidden between aggregates and scalars

An `as`-cast where the source is an aggregate (struct/array/tuple/enum) and the target is a scalar is rejected, EXCEPT a payload-free (C-style) enum cast to integer/bool (discriminant cast). Symmetrically, casting a scalar/pointer to an aggregate target (struct/zoned-struct/array/tuple/enum) is rejected as a non-primitive cast target.

**Source:** `src/compiler/sema_expr.cpp#L877-L956`

### `coerce.cast.as-bool-forbidden` — as bool is not a permitted cast

Casting any non-bool value (integer, float, C-style enum, etc.) to `bool` via `as` is rejected; only the reverse `bool as <int>` (true→1, false→0) is valid. Use `x != 0` / `x != 0.0` instead.

```logos
let b: bool = (i as bool);  // error
```

**Source:** `src/compiler/sema_expr.cpp#L923-L941`

### `coerce.cast.fat-to-thin-pointer` — Fat pointer to thin/void pointer extracts data half

A fat value cast to a thin pointer extracts field 0 (the data half): (a) `*mut/*const dyn` (`Ptr<TraitObject>`) → `*mut/*const ()` (void only); (b) any bare dyn/closure VALUE or `&dyn`/`&mut dyn` → void or any non-fat thin pointer; (c) `*const/*mut [T]` (Slice) → any non-fat thin pointer; (d) a non-self-describing DstRef → thin pointer; (e) a fat zone-mut `&mut T` → `*mut/*const T`. Runs before the identity check since both sides are MLIR pointer type.

**Source:** `src/compiler/mlir_gen_expr.cpp#L3554-L3652`

### `coerce.cast.float-ptr-forbidden` — Float `<->` pointer as-cast is forbidden

An `as`-cast between a float type (f32/f64) and a `Ptr` type, in either direction, is rejected — there is no meaningful direct numeric/address conversion; cast through an integer type instead (e.g. `x as usize as *const T`).

**Source:** `src/compiler/sema_expr.cpp#L968-L984`

### `coerce.cast.float-to-float` — Float to float truncates or extends by width

`E as F` (both float): truncate if width(F) < width(E), else extend.

**Source:** `src/compiler/mlir_gen_expr.cpp#L3719-L3726`

### `coerce.cast.float-to-int-by-target-signedness` — Float to integer conversion respects target signedness

`E as T` (E float, T integer) uses float-to-unsigned-int if T is unsigned (u8/u16/u24/u32/u56/u64/u128) else float-to-signed-int.

**Source:** `src/compiler/mlir_gen_expr.cpp#L3727-L3740`

### `coerce.cast.identity-noop` — Same-representation cast is identity

If the source value's representation equals the target representation, `E as T` is the identity (the value is returned unchanged).

**Source:** `src/compiler/mlir_gen_expr.cpp#L3678`

### `coerce.cast.int-null-to-trait-object` — Integer (null) cast to trait object yields zeroed fat pair

`E as T` where T is a trait object (`*mut dyn`/`&dyn`) and E has an integer type (IntLit/i32/u32/i64/u64/isize/usize) produces a 16-byte {data,vtable} fat pair with both halves null. This makes null-handle sentinels (`0 as *mut dyn`) and `… as *mut u64 == 0` null checks behave under the uniform-fat dyn model.

**Divergence:** Logos uniform-fat model: `*mut dyn`/`&dyn` are both 16-byte {data,vtable}; integer-to-dyn null cast is a Logos extension for null sentinels (no Rust analog).

**Source:** `src/compiler/mlir_gen_expr.cpp#L3236-L3253`

### `coerce.cast.int-to-float-by-signedness` — Integer to float conversion respects source signedness

`E as F` (E integer, F float) uses unsigned-to-float if the source is unsigned (u8/u16/u24/u32/u56/u64/u128) or i1 (bool), else signed-to-float. Bool must be treated as unsigned: signed conversion of i1(1) gives -1.0.

**Source:** `src/compiler/mlir_gen_expr.cpp#L3701-L3717`

### `coerce.cast.int-to-ptr` — Integer to pointer widens to 64-bit then reinterprets

`E as *T` (E integer) first widens E to 64-bit (zero-extend if the source is unsigned, else sign/value-coerce) then reinterprets the integer as an address.

**Source:** `src/compiler/mlir_gen_expr.cpp#L3743-L3758`

### `coerce.cast.int-truncate` — Integer narrowing truncates

`E as T` where both are integers and width(T) < width(E) truncates to the low width(T) bits; equal widths are the identity.

**Source:** `src/compiler/mlir_gen_expr.cpp#L3697-L3699`

### `coerce.cast.int-widen-by-signedness` — Integer widening sign- or zero-extends per source signedness

`E as T` where both are integers and width(T) > width(E): zero-extend if the source is unsigned (u8/u16/u24/u32/u56/u64/u128, or i1) else sign-extend.

**Source:** `src/compiler/mlir_gen_expr.cpp#L3682-L3696`

### `coerce.cast.ptr-to-int` — Pointer to integer reinterprets address

`E as T` (E pointer, T integer) reinterprets the address as an integer of T's width.

**Source:** `src/compiler/mlir_gen_expr.cpp#L3760-L3761`

### `coerce.cast.ref-to-scalar-autoderef` — &T as scalar auto-derefs the reference

When casting a value of type `&T`/`&mut T` (with scalar pointee T) to a scalar target (any integer/usize/isize/f32/f64/char/bool), the operand is auto-dereferenced before the cast, so the pointee value is converted, not the pointer bits. Pointer→pointer casts and `&T as *T`/`as usize` reinterpretations are unaffected.

```logos
let n: &f64 = &1.0; let x = n as i64;
```

**Source:** `src/compiler/sema_expr.cpp#L841-L875`

### `coerce.cast.str-to-mut-ptr-forbidden` — str as *mut u8 is forbidden

Casting a `str` (`Slice<u8>`) to `*mut u8` is rejected because str data is read-only (rodata); `*const u8` must be used instead.

**Source:** `src/compiler/sema_expr.cpp#L957-L967`

### `coerce.cast.supertrait-upcast` — Supertrait upcast preserves data, swaps to super vtable

`&dyn Sub`/`dyn Sub` cast to `&dyn Super` (Sub ≠ Super, Super a supertrait of Sub) keeps the SAME data pointer and replaces the vtable with Super's vtable, recovered from a stored super-vtable-pointer slot in Sub's vtable at index `3 + |methods(Sub)| + idx(Super)`. Identity dyn casts (Sub == Super) fall through to the no-op reinterpret.

**Divergence:** Rust-conformant (trait upcasting); vtable layout {drop,size,align, methods…, super-vtables…} is Logos-specific.

**Source:** `src/compiler/mlir_gen_expr.cpp#L3321-L3364`

### `coerce.cast.u8-slice-to-u8-ptr` — `&[u8]`/str to *const u8 extracts data field

`E as *const u8`/`*mut u8` where E has type `Slice<u8>` (str is `Slice<u8>`) extracts field 0 (the data pointer) of the {ptr,len} fat pair. Evaluated before the identity short-circuit because both the fat-struct alloca and `*const u8` are the same MLIR pointer type.

**Source:** `src/compiler/mlir_gen_expr.cpp#L3400-L3412`


## Coercions · `fn`

### `coerce.fn.fnitem-to-fnptr` — FnItem coerces to a matching FnPtr; not the reverse, not FnItem to FnItem

A FnItem value coerces to an FnPtr at every value-use site iff arity matches and each param and the return type are pairwise compatible. FnPtr to FnItem is rejected, and two distinct FnItems with identical signatures are not mutually compatible (distinct fn identity).

**Divergence:** logos-core 1.4: FnItem (ZST per-fn identity) auto-coerces to FnPtr; Rust models the analogous fn-item to fn-pointer coercion.

**Source:** `src/compiler/sema.cpp#L1816-L1826`


## Coercions · `closure`

### `coerce.closure.fn-ptr-requires-non-capturing` — Closure-to-fn-pointer coercion requires an empty capture set

A closure coerced to a plain function pointer is emitted as a top-level function with signature (params...) -> ret and NO env parameter; the coercion is valid only for non-capturing closures, and the resulting value is the function's address.

**Source:** `src/compiler/mlir_gen_dyn.cpp#L1701-L1772`

### `coerce.closure.hint-from-fn-bound` — Closure type hint derived from a Fn-family type-param bound

When the expected type is a type parameter bounded by an Fn-family trait (Fn/FnMut/FnOnce(params)->ret), a closure literal in that position is given the inferred closure type with parameter and return types taken from the bound (after generic substitution).

**Source:** `src/compiler/sema_expr.cpp#L14061-L14080`

### `coerce.closure.literal-to-fn-pointer` — Closure literal coerces to fn pointer

A non-capturing closure literal coerces to a function-pointer type at coercion sites.

**Source:** `src/compiler/sema_impl.hpp#L473`, `src/compiler/sema_impl.hpp#L477`

### `coerce.closure.noncapturing-to-fnptr` — Non-capturing closure coerces to fn pointer

A closure value coerces to a target `FnPtr` type iff it is a closure literal with zero captures and its parameter list and parameters are pairwise type-compatible with the target; the result type becomes `fn(params) -> ret` derived from the closure signature.

**Source:** `src/compiler/sema_impl.hpp#L620-L637`

### `coerce.closure.ref-to-closure` — &Closure / &mut Closure coerce to Closure

A Ref/MutRef over a Closure coerces to a bare Closure value (since dyn Fn* is already fat-pointer-like and the reference carries no extra meaning).

**Source:** `src/compiler/sema.cpp#L1976-L1987`


## Coercions · `method`

### `coerce.method.aggregate-arg-by-pointer` — Aggregate / tagged-enum argument passed by pointer

An argument whose value is an aggregate (struct) or a tagged (data-carrying) enum, passed where the callee parameter is pointer-represented, is materialized into storage and passed by pointer; scalar arguments are numerically coerced to the parameter type instead.

**Source:** `src/compiler/mlir_gen_expr.cpp#L2746-L2766`, `src/compiler/mlir_gen_expr.cpp#L2751-L2762`

### `coerce.method.arg-concrete-to-dyn-unsize` — Method argument concrete→trait-object unsize coercion

When a method parameter has type `dyn Trait` (a trait object, possibly after peeling one `Box<_>` layer) and the supplied argument is a non-trait-object concrete type, the argument is unsize-coerced into a fat `{data, vtable}` handle for that trait built from the argument's concrete type; this coercion applies symmetrically to free-function and method calls.

**Related:** `layout.dyn.fat-handle`

**Source:** `src/compiler/mlir_gen_expr.cpp#L2703-L2744`, `src/compiler/mlir_gen_expr.cpp#L2715-L2719`, `src/compiler/mlir_gen_expr.cpp#L2739-L2742`


## Coercions · `method-arg`

### `coerce.method-arg.pipeline` — Method-call arguments coerce through the canonical implicit-coercion pipeline

Each explicit method-call argument is coerced toward its (substituted) declared parameter type via the canonical coercion pipeline, permitting: closure → fn-pointer coercion, `&Concrete` → `&dyn Trait` unsizing, implicit reborrow, and integer widening. Widening is applied last; no other coercion in the pipeline depends on widening's output.

**Source:** `src/compiler/sema_expr.cpp#L8873-L8887`, `src/compiler/sema_expr.cpp#L8920-L8934`


## Coercions · `method-recv`

### `coerce.method-recv.auto-ref` — Method-call receiver auto-ref to match &self / &mut self

When a dispatched method's first formal (the receiver) is a reference (`&self`/`&mut self`, or otherwise ref-like `&Self`/`&mut Self`) and the actual receiver value is neither a reference nor a raw pointer, the receiver is implicitly wrapped in an address-of (`&` or `&mut` per the formal's mutability) before the call, producing a reference-typed receiver.

**Source:** `src/compiler/sema_expr.cpp#L7696-L7711`, `src/compiler/sema_expr.cpp#L8061-L8080`, `src/compiler/sema_expr.cpp#L8666-L8674`, `src/compiler/sema_expr.cpp#L9026-L9037`, `src/compiler/sema_expr.cpp#L9122-L9131`

### `coerce.method-recv.deref-bound-fallthrough` — Autoderef through a Deref/DerefMut bound when no bound provides the method

If no in-scope bound on type-parameter T provides method `m`, but T has a bound `T: Deref<C>` (or `DerefMut<C>`), the receiver is rewritten to `recv.deref()` (resp. `deref_mut()`), typed `&C` (resp. `&mut C`), and method resolution falls through to the ordinary inherent/struct-method path on C.

**Source:** `src/compiler/sema_expr.cpp#L7798-L7821`


## Coercions · `arg`

### `coerce.arg.canonical-coercion-order` — Canonical argument→parameter coercion pipeline

Implicit argument-to-parameter coercions are applied in one fixed canonical order: bare-enum retype, closure-literal→fn-pointer, array-ref↔slice unsize, dyn supertrait upcast, &Concrete→&dyn-Trait unsize, &mut auto-reborrow, then integer widening. The standard set enables all of these; a minimal set enables only auto-reborrow and integer widening.

**Source:** `src/compiler/sema_impl.hpp#L450-L487`

### `coerce.arg.canonical-flag-order` — Canonical coercion flag order for trait-method arguments

Argument-to-parameter coercion for a resolved bounded-generic trait-method call applies, via a single coercion call, the canonical pipeline in flag order: dyn-widening (arg-to-dyn), implicit reborrow, then integer widening.

**Source:** `src/compiler/sema_expr.cpp#L7572-L7577`

### `coerce.arg.method-canonical-coercions` — Method arguments coerced in canonical order

Each method argument is coerced toward its substituted parameter type in canonical order applying: unsize-to-dyn, implicit reborrow, and integer widening. After coercion, a non-Error/non-TypeVar/non-AssocType param type that is incompatible with the argument type is a type-mismatch error.

**Source:** `src/compiler/sema_expr.cpp#L7525-L7540`


## Coercions · `param`

### `coerce.param.array-by-pointer` — Array parameters are passed by pointer

A parameter whose type is an array [T; N] is passed by pointer (to the array storage), not by value.

**Source:** `src/compiler/mlir_gen_fn.cpp#L102-L104`, `src/compiler/mlir_gen_fn.cpp#L412-L420`


## Coercions · `call`

### `coerce.call.aggregate-arg-by-pointer` — Aggregate / tagged-enum arguments passed by pointer

When a callee parameter has by-pointer representation (aggregate/struct or tagged-enum) and the argument is materialized as a value (a struct value or a niche-packed enum scalar), the value is spilled to fresh storage and its pointer is passed instead of the value. When the argument is a field-access expression naming an inline (non-Box) struct-typed field of a receiver, the field's address is computed directly (GEP into the receiver's storage) and passed as the pointer, instead of loading the field and spilling a disconnected copy — so a by-pointer parameter aliases the original receiver storage and any mutation through it (e.g. passing `&mut self.field`) is visible to the caller. Scalar arguments to scalar (non-pointer) parameters are instead numerically coerced to the parameter type.

**Source:** `src/compiler/mlir_gen_expr.cpp#L2416-L2442`, `src/compiler/mlir_gen_expr.cpp#L2512-L2534`


## Coercions · `return`

### `coerce.return.aggregate-by-value` — Aggregate return types are returned by value

A function whose return type is a tuple, struct, ZonedStruct, or payload-carrying (tagged) enum returns the aggregate by value (as a value of the aggregate's storage type), never as a pointer to function-local storage; this guarantees the returned value outlives the callee frame.

**Source:** `src/compiler/mlir_gen_fn.cpp#L68-L82`, `src/compiler/mlir_gen_fn.cpp#L116-L137`

### `coerce.return.box-unsize-dyn` — `Box<Concrete>` coerces to `Box<dyn Trait>` at return

A `Box<Concrete>` return value where the declared return type is an owning trait object (`Box<dyn Trait>`) is implicitly unsize-cast: the source Box is marked moved and the value is wrapped in the same `as`-unsize cast the explicit form uses, producing the {data,vtable} fat pair with Box drop-glue. Applies only to a bare `Box<Concrete>` source, not an already-unsized trait object or a non-Box return.

**Source:** `src/compiler/sema_stmt.cpp#L2839-L2853`

### `coerce.return.c-enum-as-integer` — Payload-free enum returns as i32

A function returning a C-style enum (an enum with no payload variants, so no tagged-enum layout) returns its discriminant as a 32-bit integer rather than an aggregate.

**Source:** `src/compiler/mlir_gen_fn.cpp#L78-L82`, `src/compiler/mlir_gen_fn.cpp#L129-L137`

### `coerce.return.closure-to-fnptr` — Non-capturing closure coerces to fn-ptr at return

A non-capturing closure literal returned where a fn-value type is expected coerces to that fn-pointer type (same coercion as let-annotation and call-arg sites).

**Source:** `src/compiler/sema_stmt.cpp#L2832-L2834`

### `coerce.return.enum-discriminant-to-aggregate` — returning an enum discriminant where an aggregate is expected wraps it

When the function return type is an aggregate (struct/enum representation) but the returned value is a scalar enum discriminant, the discriminant is materialized into the aggregate's discriminant slot and the whole aggregate is returned by value.

**Source:** `src/compiler/mlir_gen_stmt.cpp#L2109-L2143`

### `coerce.return.float-lit` — Float-literal return retyped to return type

A float-literal return value is retyped to the concrete f32/f64 return type when the return type is a float; otherwise it defaults to f64.

**Source:** `src/compiler/sema_stmt.cpp#L2876-L2881`

### `coerce.return.numeric-to-ret-type` — scalar return value is coerced to the declared numeric return type

A scalar returned value is coerced (widened/narrowed/sign-adjusted) to the function's declared numeric return type before being returned.

**Source:** `src/compiler/mlir_gen_stmt.cpp#L2144-L2145`

### `coerce.return.ref-by-descriptor` — Reference-kind return ABI determined by RefRepr descriptor

When the return type is a reference kind, its by-value return representation is the reference's RefRepr: dyn-trait and slice references return their 16-byte fat (pointer,metadata) pair by value; closure / custom-DST / thin references return their 8-byte value pointer.

**Related:** `coerce.return.aggregate-by-value`

**Source:** `src/compiler/mlir_gen_fn.cpp#L83-L89`, `src/compiler/mlir_gen_fn.cpp#L138-L142`

### `coerce.return.slice-by-value` — slice/str return is the {ptr,len} fat pair by value

When the function return type is a slice (`&[T]`/`str`), the returned value is the 16-byte {data_ptr, len} fat pair returned by value; a value that is a pointer to slice storage is dereferenced to the pair first. No heap copy is allocated for the returned slice.

**Source:** `src/compiler/mlir_gen_stmt.cpp#L2092-L2108`

### `coerce.return.trait-object-by-value` — returning an existing trait object passes the fat pair by value

When both the function return type and the returned value's type are trait objects `dyn Trait`, the value is returned by value as the 16-byte {data, vtable} pair; a value that is a pointer to fat-pair storage is dereferenced to the pair first.

**Source:** `src/compiler/mlir_gen_stmt.cpp#L2071-L2088`

### `coerce.return.unsize-struct` — CoerceUnsized applied implicitly at return

A return value whose type can be unsized to the return type (e.g. `Rc<T>` → `Rc<dyn Tr>`) is implicitly coerced by rebuilding the smart-pointer struct, without an explicit `as`.

**Source:** `src/compiler/sema_stmt.cpp#L2835-L2838`


## Coercions · `let`

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


## Coercions · `binop`

### `coerce.binop.autoderef-numeric-ref` — Auto-deref reference operand to primitive in scalar binops

For binary operators in {+,-,*,/,%,<,<=,>,>=,==,!=,&,|,^,<<,>>}, an operand of type &T or &mut T whose pointee T is an integer, f32, f64, bool, or char is implicitly dereferenced to T before operator resolution; struct pointees are not peeled.

```logos
fn f(r: &i32) -> i32 { r + 1 }
```

**Divergence:** Models Rust's `impl Add<i32> for &i32` family via auto-deref rather than blanket ref impls.

**Source:** `src/compiler/sema_expr.cpp#L1718-L1742`

### `coerce.binop.bitwise-ref-scalar-deref` — Auto-deref &T for bitwise/shift when T is integer (or bool for bitwise-only)

For bitwise/shift operators {&,|,^,`<<,>>`}, an operand of type &T is implicitly dereferenced to T when T is an integer type, or when the operator is one of {&,|,^} and T is bool; shift operators never deref a bool pointee.

**Source:** `src/compiler/sema_expr.cpp#L2389-L2402`


## Coercions · `infer`

### `coerce.infer.placeholder-unifies` — Inference placeholder _ unifies in either direction

If either side is the InferredType placeholder (_), the pair is compatible; actual resolution is deferred to the surrounding annotation/RHS unifier.

**Divergence:** logos-core 1.3

**Source:** `src/compiler/sema.cpp#L1836-L1840`


## Coercions · `never`

### `coerce.never.subtype-of-all` — Never (!) is a subtype of every type; T to ! rejected

Never coerces to any type T (Never to T accepted unconditionally). The reverse T to Never is rejected.

**Divergence:** logos-core 1.1: T to ! previously accepted, now rejected to match Rust.

**Source:** `src/compiler/sema.cpp#L1827-L1835`


## Coercions · `variance`

### `coerce.variance.gate-on-compatible` — Variance check gates a type-compatible coercion under the fn's outlives graph

A coercion `from -> to` that already passed the base type-compatibility check must additionally satisfy the variance/subtype relation (`subtype(from,to)`) computed against the current fn's outlives graph and the program's fixed-point def-variance table, else diagnostic `variance mismatch — ... lifetime structure incompatible`. `permissive=true` (call-site arg-passing) forwards unresolved lifetime relations to the caller's region inference; `permissive=false` (return / let-init, fn-scope-fixed lifetimes) requires the relation to already hold.

**Source:** `src/compiler/sema_impl.hpp#L3368-L3375`, `src/compiler/sema_impl.hpp#L3559-L3571`


## Coercions · `anyval`

### `coerce.anyval.let-binds-i32` — AnyVal-typed let binds an i32

`let name: AnyVal = expr;` numerically coerces expr's value to a 32-bit integer before storing; the binding's storage is a single i32-sized scalar slot.

**Divergence:** AnyVal itself is a Logos addition with no Rust equivalent (not a tracked DIVERGENCES.md tag).

**Note:** AnyVal is a Logos-specific type (no Rust equivalent); the i32 coercion is this call site's implementation choice, not independently cross-checked here.

**Source:** `src/compiler/mlir_gen_stmt.cpp#L1465-L1474`


## Coercions · `writ`

### `coerce.writ.mapslice-to-typed-map` — MapSlice as `<K,AnyVal>{}` builds a typed Writ map

`src as <K,V>{}` (target struct WritMap) is permitted only for V = AnyVal and K in {I32,U32,I64,U64}, with source the matching `MapSlice<K>` struct; it lowers to a stdlib `writ_build_map_<k>`_anyval call returning `Rc<Writ>`. Any other key/value combination, a mismatched source, or a missing builder is an error.

**Divergence:** A6

**Source:** `src/compiler/sema_expr.cpp#L774-L838`

### `coerce.writ.slice-to-typed-array` — `&[T]` as `<T>[]` builds a typed Writ array

`src as <T>[]` (target struct WritArr) requires `src: &[T]` (a Slice) whose element kind equals the target element kind; element T must be one of i8/u8/i16/u16/i32/u32/i64/u64/f32/f64. It lowers to a stdlib `writ_build_array_<T>` call returning the builder's `Rc<Writ>` type; missing builder (no `use logos.lang.writ.typed_arr`) or non-slice source or element mismatch or unsupported element is an error.

**Divergence:** A6

**Source:** `src/compiler/sema_expr.cpp#L716-L773`


## Coercions · `writ-anyval`

### `coerce.writ-anyval.scalar-helpers` — Implicit coercion of comprehension element to AnyVal

Inside a Writ comprehension element/value, the value is coerced to AnyVal: WAny and legacy AnyVal struct values pass through unchanged; bool/i8/i16/i32/IntLit/u8/u16/u32 are wrapped via the matching `writ_coerce_<ty>` helper; `str` (`&[u8]`) is wrapped via `writ_coerce_str` (taking `&ctr` first). Any other type is rejected with a message to cast explicitly or wrap with AnyVal::embed_*.

**Divergence:** Logos-specific Writ value model.

**Source:** `src/compiler/sema_expr.cpp#L11382-L11458`

### `coerce.writ-anyval.wide-int-no-implicit` — Wide integers not implicitly coerced to AnyVal

i64/u64/i24/u24/i56/u56/i128/u128 are intentionally NOT auto-coerced to AnyVal (implicit i32 embedding would silently truncate); the user must cast explicitly (`x as i32`) or wrap with WAny::from.

**Divergence:** Logos-specific anti-truncation rule.

**Source:** `src/compiler/sema_expr.cpp#L11418-L11427`, `src/compiler/sema_expr.cpp#L11430-L11436`


## Coercions · `taggedptr`

### `coerce.taggedptr.from-raw-ptr` — Raw pointer coerces to a tagged trait pointer

Any *T (Ptr) coerces to a TaggedPtr (&`tagged<TS>` Trait, a thin pointer to a tagged object); the tag is read at dispatch time.

**Source:** `src/compiler/sema.cpp#L2062-L2066`


## Coercions · `relptr`

### `coerce.relptr.transparent-to-thin-ptr` — #[rel_ptr] struct is value-transparent to a thin pointer

A `#[rel_ptr]` struct `RP<T>` is value-transparent to `*T`/`&T`/`&mut T`: its computed form is an absolute thin pointer (only storage is a self-relative offset), so the coercion is accepted in both directions at value-flow sites.

**Source:** `src/compiler/sema_impl.hpp#L801-L804`


## Coercions · `rel-ptr`

### `coerce.rel-ptr.pointer-compatibility` — #[rel_ptr] struct / GAT pointer ↔ raw pointer compatibility

A concrete `#[rel_ptr]` struct `RP<U>` is pointer-compatible with `*U`/`&U`/`&mut U` (pointee type-equal to its type-arg); a type-erased rel_ptr (no type-arg) is compatible only with a thin `*u8` pointer. An abstract GAT projection `Z::Ptr<U>` (assoc base a type-var) is compatible with a raw pointer iff its GAT arg equals the pointee. Compatibility is symmetric.

**Source:** `src/compiler/sema.cpp#L3793-L3829`


## Coercions · `cfgslot`

### `coerce.cfgslot.numeric-bidirectional` — Cfg-slot types coerce bidirectionally with any numeric / literal

A CfgSlotType (deferred WritStatic-bound primitive) behaves like a TypeVar for coercion: IntLit/FloatLit to CfgSlot accepted, and any integer or float on either side is compatible with a CfgSlot in both directions; mono enforces the resolved-type compatibility.

**Source:** `src/compiler/sema.cpp#L1909-L1921`


## Coercions · `compatible`

### `coerce.compatible.equal-implies-compatible` — Equal types are compatible; compatibility is the implicit-coercion relation

types_compatible(from,to) is the directed implicit-coercion relation. It holds whenever types_equal(from,to); otherwise a fixed set of one-directional coercion rules apply. Either null operand is incompatible.

**Source:** `src/compiler/sema.cpp#L1797-L1799`


## Coercions · `if`

### `expr.if.fnitem-branches-lub-to-fnptr` — Two fn-item branches join to a fn pointer

When both `if` branches are distinct fn-item values with the same signature, the result type is the corresponding fn-pointer type `fn(params)->ret` (fn-item-to-fn-pointer coercion at the join), since two distinct fn-items are not directly type-compatible.

**Source:** `src/compiler/sema_expr.cpp#L14007-L14027`

