# Types, Layout & Coercions

Scope: the Logos type system — type kinds and identity, physical layout/representation (ABI), and the implicit coercion/conversion rules. Rules are extracted from the compiler source layers `grammar`, `sema`, `mono`, and `codegen` (see each rule's source evidence). Every rule carries a stable, linkable `id`; cite rules by id.

Total rules: 470 (type: 224, layout: 120, coerce: 126).

## Type system — kinds, identity, inference

### Group: `primitive`

### `type.primitive.set` — Built-in primitive scalar types

The language has primitive scalar types: void, bool, char, the floats f32/f64, and the integers i8/u8, i16/u16, i24/u24, i32/u32, i56/u56, i64/u64, i128/u128, isize/usize. Each is a distinct type identified by its keyword name.

**Divergence (from Rust):** A: extra fixed-width widths i24/u24/i56/u56 and 128-bit i128/u128 beyond Rust's standard set.

**Source evidence:** `src/compiler/sema.cpp#L2077-L2097`, `src/compiler/sema.cpp#L2530-L2551`

### Group: `integer`

### `type.integer.bit-width` — Integer bit-width and signedness

Each concrete integer kind has a fixed bit width and signedness: i8/u8=8, i16/u16=16, i24/u24=24, i32/u32=32, i56/u56=56, i64/u64=64, i128/u128=128; signed forms are signed, unsigned forms unsigned. usize/isize have width equal to the target pointer width (isize signed, usize unsigned). IntLit, Enum, and non-integers have no defined rank (width 0).

**Divergence (from Rust):** usize/isize width is target-dependent (pointer bits) as in Rust; the exotic 24/56-bit widths are a Logos addition.

**Source evidence:** `src/compiler/sema_impl.hpp#L4453-L4474`

### `type.integer.kind-set` — Integer-class type kinds

The integer type class comprises the fixed-width signed/unsigned kinds {i8,u8,i16,u16,i24,u24,i32,u32,i56,u56,i64,u64,i128,u128}, the pointer-sized {usize,isize}, the unsuffixed-literal type IntLit, and Enum. An enum type is treated as an integer kind for these classifications.

**Divergence (from Rust):** Logos adds non-power-of-two integer widths i24/u24/i56/u56 (not in Rust); also classifies Enum as an integer kind.

**Uncertainty:** Whether Enum membership here reflects a general language rule or only this classifier's use sites is not determinable from this unit alone.

**Source evidence:** `src/compiler/sema_impl.hpp#L4439-L4449`

### Group: `numeric`

### `type.numeric.classification` — Numeric type classification

A type is `numeric` iff it is f64/f32, an unresolved float literal, an integer kind, a type variable, or a cfg-slot type (the latter two deferred to monomorphization, trusted to resolve to a numeric primitive). A type is `integer` iff it is an integer kind.

**Source evidence:** `src/compiler/sema_impl.hpp#L3740-L3756`

### Group: `char`

### `type.char.is-u32` — char is a 32-bit value

char is a 32-bit (4-byte, 4-align) value, representing a Unicode scalar.

**Source evidence:** `src/compiler/mlir_gen_types.cpp#L63`, `src/compiler/mlir_gen_types.cpp#L457-L458`

### Group: `str`

### `type.str.default-fat-slice` — str defaults to &[u8] fat-slice shape

The `str` keyword resolves to the fat-pointer slice form `Slice<u8>` (the &[u8] shape) by default; in a context that explicitly permits an unsized result (e.g. a `T: ?Sized` turbofish position) it resolves to the unsized `[u8]` form so `&T` routes to the same Slice<u8> ABI without double-wrapping.

**Uncertainty:** str modeled as u8 slice rather than a distinct str primitive; unsized vs fat-slice choice is context-driven via unsized_ok_.

**Source evidence:** `src/compiler/sema.cpp#L2552-L2561`

### `type.str.slice-alias` — str is an alias for Slice<u8>; impls aliased to &[u8]

`str` is a built-in that resolves to Slice<u8> (printed `&[u8]`); a trait impl whose target is `str` is also registered under target `&[u8]` so trait-satisfaction checks keyed on the printed slice type find the impl.

**Divergence (from Rust):** Logos models `str` as Slice<u8>; Rust `str` is a distinct DST.

**Source evidence:** `src/compiler/sema_collect.cpp#L3777-L3787`

### Group: `tuple`

### `coerce.tuple.elementwise` — Tuples compatible iff equal arity and pairwise-compatible elements

Tuple types are compatible iff they have equal arity and each element pair is compatible.

**Source evidence:** `src/compiler/sema.cpp#L1969-L1975`

### `layout.tuple.inline-elements` — Tuple elements stored inline by value

A tuple stores each element inline by value at its layout slot; constructing a tuple writes each element into its slot, copying inline-aggregate (struct/array) elements by value rather than storing a pointer, and a tuple value is represented as a pointer to its storage.

**Related:** `expr.tuple-index.aggregate-element-by-address`

**Source evidence:** `src/compiler/mlir_gen_expr.cpp#L3064-L3105`, `src/compiler/mlir_gen_expr.cpp#L3084-L3094`

### `layout.tuple.ref-tuple-derefs-to-inner` — Ref-to-tuple resolves inner tuple layout

A `&(T,U)`/`&mut (T,U)`/`*(T,U)` resolves to the layout of the inner tuple `(T,U)` (default binding modes for tuple patterns over ref scrutinees).

**Source evidence:** `src/compiler/mlir_gen_types.cpp#L888-L896`

### `layout.tuple.struct-element-inline` — Tuple element aggregate types stored inline

A tuple element whose type is a struct, enum, slice (incl. `str`), closure, trait object (`&dyn`), or nested tuple is stored inline as its full by-value layout (e.g. struct footprint, 16-byte fat pair), never collapsed to an 8-byte pointer.

**Source evidence:** `src/compiler/mlir_gen_types.cpp#L899-L941`

### `type.tuple.multi` — Tuple type

A tuple type is `(T1, T2, ...)` with ≥2 comma-separated element types (optional trailing comma), or a 1-element tuple `(T,)` requiring the trailing comma.

**Source evidence:** `tools/peg_gen/grammars/logos.peg#L1732-L1735`

### `type.tuple.unit` — Unit type `()`

`()` denotes the unit type, the empty tuple type.

**Source evidence:** `tools/peg_gen/grammars/logos.peg#L1722-L1723`, `tools/peg_gen/grammars/logos.peg#L1816-L1817`

### `type.tuple.unit-and-elements` — Tuple type, unit, and variadic pack

`()` (or an empty tuple) resolves to the unit/void type; `(T1,...,Tn)` resolves to a tuple of the element types; `(A...)` resolves to a Tuple of one TypeVar naming the variadic pack.

**Source evidence:** `src/compiler/sema.cpp#L5902-L5926`

### `type.tuple.variadic-arity` — Variadic-arity tuple target `(A...)`

`(A...)` is a variadic-arity tuple type naming pack-typevar A; used as an impl target `impl<A...> Trait for (A...)`. Resolves to a Tuple type with one variadic element naming A.

**Divergence (from Rust):** Logos addition: variadic tuple impls (no direct Rust equivalent).

**Source evidence:** `tools/peg_gen/grammars/logos.peg#L1726-L1731`

### Group: `array`

### `coerce.array.elementwise` — Arrays compatible iff equal size and compatible elements

Array<T;N> is compatible with Array<U;M> iff N==M and T is compatible with U (recursively).

**Source evidence:** `src/compiler/sema.cpp#L1942-L1945`

### `coerce.array.ref-to-slice-preserves-mut` — Array-ref-to-slice unsizing preserves mutability

When `&mut [T;N]` (or `*mut [T;N]`) unsizes to a slice, the result is a mutable slice `&mut [T]`; a shared `&[T;N]` yields a shared `&[T]`. A shared array reference may not satisfy a `&mut [T]` parameter.

**Source evidence:** `src/compiler/sema_impl.hpp#L365-L375`

### `coerce.array.ref-to-slice-unsize` — &[T;N]/*[T;N] unsizes to &[T] slice

A reference or raw pointer to an array, `&[T;N]` / `&mut [T;N]` / `*const [T;N]` / `*mut [T;N]`, coerces to a slice `&[T]` by building a fat pointer {data = the array address, len = N}. The coercion applies only when the array element type is compatible with the target slice's element type.

**Source evidence:** `src/compiler/sema_impl.hpp#L346-L377`

### `coerce.array.to-pointer-decay` — Array and &array decay to raw pointer / reference without mutability widening

Array<T> coerces to *const/T-pointee Ptr when elem==pointee. A &[T;N]/&mut[T;N] decays to *const/*mut T or &/&mut T over a compatible element type, but a shared (&) source may not decay to a mutable (*mut/&mut) target.

**Source evidence:** `src/compiler/sema.cpp#L1938-L1941`, `src/compiler/sema.cpp#L1999-L2019`

### `layout.array.element-stride` — Array layout = N x element

An array [T; N] has size = N * size_of(T) and align = align_of(T), where T's element representation is its full inline by-value footprint (struct/enum/slice/closure/dyn/tuple elements are embedded inline, not collapsed to a pointer).

**Source evidence:** `src/compiler/mlir_gen_types.cpp#L470-L474`, `src/compiler/mlir_gen_types.cpp#L74-L119`, `src/compiler/mlir_gen_types.cpp#L430-L438`

### `layout.array.inline-element-storage` — arrays and slice buffers store struct/tuple elements inline

Struct, zoned-struct, and tuple elements are stored inline by value in array and slice buffers (stride = sizeof(element)); iterating yields a pointer directly into the inline storage. Trait-object/closure/slice elements are stored as 16-byte fat pairs. Scalar elements are stored by their natural representation.

**Source evidence:** `src/compiler/mlir_gen_stmt.cpp#L2451-L2468`, `src/compiler/mlir_gen_stmt.cpp#L2587-L2625`

### `type.array.length-forms` — Array type length forms

`[T; N]` length is determined by: a `metacall { expr }` block whose tail integer is CTFE-evaluated; `sizeof...(P)` over an in-scope type-param pack (symbolic `__sizeof_pack:P`); a literal integer; or a symbolic const parameter name. A missing/empty metacall tail or an unknown pack/op is a hard error.

**Divergence (from Rust):** Array length via `metacall {..}` replaces Rust const-eval at this position (MP-mc-01).

**Source evidence:** `src/compiler/sema.cpp#L6140-L6226`

### `type.array.size-from-metacall` — Array size from metacall

`[T; metacall { ... }]` permits a compile-time metacall block as the array size expression.

**Divergence (from Rust):** Logos: comptime sizing via explicit metacall (see explicit-metacall design).

**Source evidence:** `tools/peg_gen/grammars/logos.peg#L1769-L1770`

### `type.array.size-from-pack` — Array size from variadic pack length

`[T; P...(P)]` sizes the array from a variadic pack length; lowered to symbolic array-size-var `__sizeof_pack:P` and resolved at monomorphization.

**Divergence (from Rust):** Logos addition: pack-length array sizing.

**Source evidence:** `tools/peg_gen/grammars/logos.peg#L1762-L1768`

### `type.array.sized` — Fixed-size array type `[T; N]`

`[T; N]` is a fixed-size array type where N is an integer literal or an identifier (const generic). Size-bearing forms are matched before the unsized fallback.

**Source evidence:** `tools/peg_gen/grammars/logos.peg#L1761-L1772`

### Group: `slice`

### `coerce.slice.exact-scalar-no-mut-widen` — Slice compatibility: no shared to mut widening; concrete scalar elements must match exactly

Slice<T> is compatible with Slice<U> only if it does not widen a shared (&) slice to a mutable (&mut) slice, and: two concrete scalar element types (concrete integer excl. IntLit/Enum, F32/F64/Bool/Char) must be kind-identical (slices alias raw memory at element stride); inference holes use lenient compatibility.

**Source evidence:** `src/compiler/sema.cpp#L1946-L1968`

### `coerce.slice.to-array-ref-recovery` — Shared &[T] over an array variable recovers &[T;N]

A shared slice `&[T]` that was formed from `&array_var` may coerce back to a ref-to-array `&[T;N]` / `*const [T;N]` when the parameter wants it, provided the underlying variable is an array whose actual size equals N. The mutable case is excluded: a shared slice never satisfies a `&mut [T;N]` parameter.

**Source evidence:** `src/compiler/sema_impl.hpp#L378-L408`

### `layout.slice.fat-pointer-pair` — Slice/str values are a 16-byte {data,len} fat pair

A slice type (and str = Slice<u8>) has a fat-pointer representation: a 16-byte {data_ptr, len} pair. Functions returning a slice return this pair by value.

**Source evidence:** `src/compiler/mlir_gen_impl.hpp#L496-L501`, `src/compiler/mlir_gen_impl.hpp#L639-L645`, `src/compiler/mlir_gen_impl.hpp#L784-L785`

### `layout.slice.fat-pointer-ptr-len` — slice fat pointer is {data_ptr, len}

A slice value is laid out as a two-field fat pointer: field 0 is the data pointer, field 1 is the i64 length.

**Source evidence:** `src/compiler/mlir_gen_stmt.cpp#L2476-L2483`

### `layout.slice.fat-ptr` — Slice reference is a fat { ptr, len } descriptor

A slice-typed value (&[T] / &mut [T]) is represented as a fat descriptor { data-ptr, len }. Indexed access first reads field 0 (the data pointer) then strides by sizeof(element).

**Source evidence:** `src/compiler/mlir_gen_impl.hpp#L346-L350`

### `layout.slice.owning-box-same-as-borrow` — Box<[T]> shares the borrowed-slice fat layout

An owning slice `Box<[T]>` has the same 16-byte {data,len} layout as a borrowed `&[T]` slice, but is move-only and droppable (drops elements and frees the buffer); the owning kind (Borrow/Box/Rc/Arc) is carried distinctly so the four forms intern as distinct types.

**Related:** `layout.dst.owning-box-same-as-borrow`, `type.traitobject.owning-kind-distinct`

**Source evidence:** `src/compiler/sema_impl.hpp#L638-L649`

### `layout.slice.ptr-len-pair` — Slice fat pair = {data,len}

A slice value (`&[T]`, `str`) is the pair {data: ptr, len: i64}.

**Source evidence:** `src/compiler/mlir_gen_types.cpp#L944-L947`

### `type.slice.ref` — Slice type

`&[T]` and `&mut [T]` are slice types (fat pointer: ptr + len); an explicit lifetime `&'a [T]` / `&'a mut [T]` is accepted (captured but not distinctly enforced).

**Source evidence:** `tools/peg_gen/grammars/logos.peg#L1618-L1629`

### `type.slice.sized-vs-unsized` — Sized slice vs bare unsized slice

A `[T]` written under a reference/pointer (SLICE_TYPE) resolves to a sized fat Slice (mut bit tracked); a bare `[T]` by value (UNSIZED_SLICE_TYPE) resolves to an unsized slice.

**Source evidence:** `src/compiler/sema.cpp#L5863-L5894`

### `type.slice.unsized` — Unsized slice type `[T]`

Bare `[T]` (no size) is the unsized slice type. The size-bearing array forms are tried first, so `[T; N]` always wins over the unsized fallback.

**Source evidence:** `tools/peg_gen/grammars/logos.peg#L1764-L1774`

### `type.slice.unsized-not-a-value-type` — Bare [T] is unsized and not a value type

The bare slice type `[T]` (UnsizedSlice) is distinct from `&[T]`; it cannot appear as a value type and may occur only behind a reference (where it canonicalises to the borrowed slice form) or as a `T: ?Sized` substitution.

**Source evidence:** `src/compiler/sema_impl.hpp#L650-L657`

### `type.slice.unsized-only-behind-pointer` — Unsized slice [T] cannot appear by value

The bare unsized slice type `[T]` (Kind::UnsizedSlice) may not appear by value; it is only legal behind `&`/`*const`/`*mut` (canonicalised to a sized Slice) or as a `T: ?Sized` substitution.

**Source evidence:** `tools/peg_gen/grammars/logos.peg#L304`

### Group: `struct`

### `coerce.struct.elementwise-typeargs` — Same-named structs compatible iff type-args pairwise compatible

Two Struct types with equal struct_name and pkg_name and equal type-arg arity are compatible iff every type-arg pair is compatible (allowing inference holes like Vec<_> vs Vec<i32>).

**Divergence (from Rust):** logos-core 1.3 (nested)

**Source evidence:** `src/compiler/sema.cpp#L1846-L1857`

### `layout.struct.field-fat-ref-inline-storage` — Fat-ref struct/enum field stored inline

When a fat reference (slice, `dyn`/`Box<dyn>`, closure, custom-DST ref) is a payload field of an enum variant, it is stored inline as its full 16-byte fat pair, never collapsed to an 8-byte handle; thin refs keep the by-value 8-byte pointer.

**Source evidence:** `src/compiler/mlir_gen_types.cpp#L680-L688`

### `layout.struct.pointer-field-non-owning` — Pointer/reference fields do not own their pointee

A struct field of pointer or reference type (*T / &T / &mut T) does not own the pointee; automatic Drop of the containing struct must NOT drop through such fields.

**Source evidence:** `src/compiler/mlir_gen_impl.hpp#L52-L55`

### `type.struct.bare-all-default-inst` — Bare generic struct name with all-default params instantiates defaults

A bare struct name N (written without `<...>`) referring to a generic struct whose every type parameter has a default resolves to the defaulted instantiation `N<d0, d1, ...>`, where each default may reference earlier defaults via substitution. If any parameter lacks a default, the bare name resolves to the uninstantiated struct type.

**Source evidence:** `src/compiler/sema.cpp#L2583-L2610`

### `type.struct.dst-tail-slice-last-field` — Custom-DST slice tail only at last field

A struct field may have an unsized slice type `[T]` only at the last field position; such a struct is marked dynamically-sized (DST). Unsized field types are otherwise rejected.

**Divergence (from Rust):** B2: custom-DST tail-slice (DONE) — Logos supports `struct Foo { hdr: H, tail: [T] }`.

**Related:** `item.struct.tuple-struct-fields`

**Source evidence:** `src/compiler/sema_collect.cpp#L4023-L4029`, `src/compiler/sema_collect.cpp#L4055-L4069`

### `type.struct.non-null-niche` — non_null single-pointer wrapper yields Option niche

A struct annotated `#[non_null]` wrapping a single non-null pointer makes `Option<T>` use the null-pointer value as the None niche (no discriminant overhead).

**Divergence (from Rust):** A: #[non_null] attribute is a Logos addition mirroring Rust NonNull niche.

**Source evidence:** `src/compiler/sema_decl.cpp#L1200-L1201`

### `type.struct.package-qualified-identity` — Struct types are identified by package-qualified name

A struct type's identity carries its package prefix (pkg.Name); same-named structs in different packages are distinct types and do not alias. Method-symbol mangling uses the package-agnostic bare struct name.

**Source evidence:** `src/compiler/mlir_gen_impl.hpp#L667-L691`

### `type.struct.rel-ptr-offset-storage` — rel_ptr struct is a self-relative pointer

A struct annotated `#[rel_ptr]` is classified as a self-relative pointer using 8-byte offset storage.

**Divergence (from Rust):** A: RefRepr RelOffset Logos addition, no Rust analog.

**Source evidence:** `src/compiler/sema_decl.cpp#L1194-L1196`

### `type.struct.self-describing-thin-ptr` — self_describing keeps *Self thin

A struct annotated `#[self_describing]` keeps `*Self` a thin pointer (no DstRef fattening) under Ptr→DstRef canonicalization.

**Divergence (from Rust):** A: Writ/RefRepr Logos addition, no Rust analog.

**Source evidence:** `src/compiler/sema_decl.cpp#L1186-L1189`

### `type.struct.zone-mut-fat-ref` — zone_mut makes &mut T fat carrying its allocator

For a struct annotated `#[zone_mut]`, a `&mut T` reference is a fat `{data, zone}` pair carrying the value's allocator/zone.

**Divergence (from Rust):** A: Writ zone model Logos addition; Rust &mut is thin.

**Source evidence:** `src/compiler/sema_decl.cpp#L1190-L1192`

### `type.struct.zoned2-relative-fields` — zoned2 struct fields use relative pointers

A struct annotated `#[zoned2]` stores its pointer fields as self-relative offsets (RelOffset) rather than absolute addresses.

**Divergence (from Rust):** A: Writ zoned2 Logos addition, no Rust analog.

**Source evidence:** `src/compiler/sema_decl.cpp#L1193`

### Group: `ref-repr`

### `type.ref-repr.rel-ptr-self-relative` — #[rel_ptr] and #[zoned2] pointers are stored self-relative

A #[rel_ptr] struct is represented as a self-relative pointer: 8-byte i64 offset storage, resolved to an absolute thin pointer on access. Additionally, a thin-pointer field of a #[zoned2] struct is stored self-relative (offset) rather than absolute.

**Divergence (from Rust):** Logos addition: self-relative pointer storage (persistent/Writ model), no Rust equivalent.

**Source evidence:** `src/compiler/mlir_gen_types.cpp#L579-L586`, `src/compiler/mlir_gen_types.cpp#L594-L603`

### `type.ref-repr.thin-vs-fat-classification` — Reference representation classified by outer kind

A reference's thin/fat representation is determined by its outermost kind, not its pointee: raw/safe pointers (*T, &T, *mut T to sized), fn pointers, and fn items are thin (8-byte); slice/dyn/closure values are fat (16-byte); a pointer wrapping an unsized pointee (e.g. *const dyn) stays thin while a bare dyn value is the fat pair. A self-describing custom-DST ref is physically thin (8-byte) since its tail length is in-band.

**Source evidence:** `src/compiler/mlir_gen_types.cpp#L543-L592`, `src/compiler/mlir_gen_types.cpp#L574-L577`

### `type.ref-repr.zone-mut-fat` — &mut to a #[zone_mut] type is a fat zone-carrying ref

A &mut T where T is a #[zone_mut] struct is a fat reference {data, zone} carrying its allocator/zone pointer, so growth methods reach the allocator through &mut self. A shared &T or *T to the same type stays thin (reads never grow).

**Divergence (from Rust):** Logos addition: zone-carrying mutable references (Writ/zone model), no Rust equivalent.

**Source evidence:** `src/compiler/mlir_gen_types.cpp#L552-L565`

### Group: `ptr`

### `type.ptr.dst-thin-if-self-describing` — Raw pointer to a DST struct: thin iff self-describing

For a raw pointer whose pointee is an effective-DST struct: if the struct is self-describing (tail metadata recoverable in-band) the pointer stays thin (8B `Ptr<T>`); otherwise it becomes a fat DstRef carrying the tail length.

**Uncertainty:** `is_effective_dst`/`self_describing` are per-instance struct properties evaluated outside this unit.

**Source evidence:** `src/compiler/sema.cpp#L5714-L5741`

### `type.ptr.dyn-is-fat` — Raw pointer to bare dyn is a fat trait object

`*const dyn Trait` / `*mut dyn Trait` (immediate `dyn` pointee) canonicalises to the inline fat {data,vtable} TraitObject, identical to `&dyn Trait`'s representation.

**Source evidence:** `src/compiler/sema.cpp#L5703-L5713`

### `type.ptr.modifier-set` — Raw-pointer modifiers

A raw pointer type is written `*const T`, `*mut T`, or `*zoned T`/`*zoned mut T`; any other word after `*` is a hard error (`unknown raw-pointer modifier`).

**Divergence (from Rust):** `*zoned` is a Logos-only zoned-pointer modifier (F3).

**Source evidence:** `src/compiler/sema.cpp#L5685-L5699`, `src/compiler/sema.cpp#L5741`

### `type.ptr.raw` — Raw pointer type

`*const T` is an immutable raw pointer and `*mut T` is a mutable raw pointer to T.

**Source evidence:** `tools/peg_gen/grammars/logos.peg#L1746-L1749`

### `type.ptr.raw-slice` — Raw fat-pointer to slice

`*const [T]` and `*mut [T]` are raw fat pointers to a slice, sharing the `{*const T, usize}` ABI of `&[T]` but without borrow-check guarantees. These alternatives precede plain pointer/array forms so the bare `[T]` parses.

**Source evidence:** `tools/peg_gen/grammars/logos.peg#L1737-L1745`

### `type.ptr.zoned` — Zoned raw pointer `*zoned [mut] T`

`*zoned T` / `*zoned mut T` is a zoned raw pointer (Ref-arm self-relative at rest; deref/assign runs the storage↔compute bridge). `zoned` is a contextual keyword recognized only in pointer position (a bare IDENT after `*`), validated as NAME=="zoned" by sema; it is not globally reserved.

**Divergence (from Rust):** Logos addition (F3 ref-repr design): zoned pointers, no Rust equivalent.

**Source evidence:** `tools/peg_gen/grammars/logos.peg#L1750-L1759`

### `type.ptr.zoned-pointer-distinct` — *zoned T is a distinct pointer type

A zoned raw pointer `*zoned T` is a type distinct from `*T`; the zoned bit participates in type identity (interning, serialization, equality). Deref/assignment through a `*zoned T` runs the zoned storage↔compute bridge rather than a plain load/store.

**Divergence (from Rust):** Logos addition (F3 ref-repr/zoned types); no Rust equivalent.

**Source evidence:** `src/compiler/sema_impl.hpp#L222-L231`

### Group: `unsized`

### `layout.unsized.no-by-value` — Unsized pointees have no by-value footprint

Unsized types — the slice pointee [T] (UnsizedSlice) and the dyn pointee (UnsizedDyn) — have layout {size=0, align=1}; they have no by-value representation and may only appear behind a pointer/reference.

**Source evidence:** `src/compiler/mlir_gen_types.cpp#L468-L469`, `src/compiler/mlir_gen_types.cpp#L588-L590`

### `type.unsized.by-value-rejected` — Unsized type by value is an error

A bare unsized slice `[T]` in a value position (param/return/field/alias/local) is a hard error unless an explicit unsized-ok context (e.g. a turbofish arg for a `T: ?Sized` parameter) is active; it must be wrapped in `&[T]`/`*const [T]`/`*mut [T]`.

**Source evidence:** `src/compiler/sema.cpp#L5870-L5894`, `src/compiler/sema.cpp#L5999-L6008`

### `type.unsized.value-position-forbidden` — Unsized types forbidden in value positions

Resolving a type AST yields an unsized type (bare `[T]` or `dyn Trait`) only in contexts that genuinely permit one (turbofish argument for a `T: ?Sized` parameter, impl-self-type at a `?Sized` position). By default unsized results in value positions are an error, so `[T]`/`dyn Trait` cannot silently slip into a sized position.

**Source evidence:** `src/compiler/sema_impl.hpp#L3640-L3645`

### Group: `self-describing`

### `type.self-describing.dst-len-required` — self_describing struct must implement SelfDescribing::dst_len

A #[self_describing] struct must provide a SelfDescribing::dst_len method (sema-enforced) used to recover the in-band tail length from the thin header pointer; for a generic DST the method resolves to its monomorphized concrete instance.

**Source evidence:** `src/compiler/mlir_gen_expr.cpp#L5129-L5156`

### Group: `traitobject`

### `type.traitobject.owning-kind-distinct` — Trait-object owning kinds intern distinctly

A trait object has one of four owning kinds (Borrow/Box/Rc/Arc), all sharing the fat {data,vtable} layout and dispatch but differing in release semantics; the owning kind together with `+Send`/`+Sync` bits is folded into type identity so the forms intern distinctly.

**Related:** `type.dyn.unsized-bound-bits-preserved`

**Source evidence:** `src/compiler/sema_impl.hpp#L810-L835`

### Group: `closure-type`

### `type.closure-type.params-ret` — Closure type literal

A closure type literal resolves to Closure with the listed parameter types and a return type defaulting to unit/void when absent.

**Source evidence:** `src/compiler/sema.cpp#L6067-L6082`

### Group: `fn-ptr`

### `type.fn-ptr.abi-identity` — Function-pointer type and ABI identity

`fn(P...) -> R` resolves to a single-pointer FnPtr; an `extern "ABI"` prefix is part of the type identity. Accepted ABIs are `C`/`C-unwind`/`system`/`Rust`; default and `"Rust"` normalize to the same identity, a foreign ABI is tagged, any other ABI string is a hard error. Return type defaults to void.

**Source evidence:** `src/compiler/sema.cpp#L6084-L6125`

### `type.fn-ptr.type` — Function-pointer type

`fn(T1,T2) -> R` is a bare function-pointer type. Qualifiers/prefixes are accepted: `unsafe fn(...)` (IS_UNSAFE), `extern "ABI" fn(...)` (ABI threaded to the calling convention), and `for<'a> fn(...)` (HRTB binders captured for future region inference).

**Example**

```logos
extern "C" fn(i32) -> i32
```

**Source evidence:** `tools/peg_gen/grammars/logos.peg#L1666-L1715`

### Group: `impl-trait`

### `type.impl-trait.param` — impl Trait type

`impl Trait`, `impl Trait<args>`, and `impl Fn(args) [-> R]` are accepted in type position; an impl-Trait parameter desugars to a synthetic generic parameter bounded by the same trait (Fn-family args→PARAMS, return→RET_TYPE, generic args→TYPE_PARAMS).

**Example**

```logos
fn f(x: impl Display) {}
```

**Source evidence:** `tools/peg_gen/grammars/logos.peg#L1395-L1414`

### `type.impl-trait.param-desugar` — impl Trait position semantics

`impl Trait` in parameter position desugars to a fresh implicitly-Sized synthetic generic type-param bounded by Trait (a once-used generic, capturing full bound args); in return position it resolves to the dedicated ImplTrait type.

**Source evidence:** `src/compiler/sema.cpp#L6041-L6065`

### `type.impl-trait.param-position-forbidden` — `impl Trait` not allowed at parameter position

`impl Trait` is not supported in parameter position; use an explicit generic `fn f<T: Trait>(x: T)` or `&dyn Trait` instead.

**Divergence (from Rust):** Logos restriction: Rust supports argument-position impl Trait (APIT).

**Source evidence:** `src/compiler/sema_decl.cpp#L309-L318`

### Group: `never`

### `coerce.never.subtype-of-all` — Never (!) is a subtype of every type; T to ! rejected

Never coerces to any type T (Never to T accepted unconditionally). The reverse T to Never is rejected.

**Divergence (from Rust):** logos-core 1.1: T to ! previously accepted, now rejected to match Rust.

**Source evidence:** `src/compiler/sema.cpp#L1827-L1835`

### `layout.never.zero-size-field-skipped` — Never-typed fields are zero-size and uninitialized

A struct field of the never type `!` (e.g. `PhantomData<!>` after monomorphization) is zero-size and uninhabited: it carries no runtime value and its initialization is elided. Such a field has no observable storage.

**Source evidence:** `src/compiler/mlir_gen.cpp#L978-L984`, `src/compiler/mlir_gen.cpp#L1018-L1024`

### `type.never.bang` — Never type `!`

`!` is a type (the never type), parsed as a type reference named `!`.

**Source evidence:** `tools/peg_gen/grammars/logos.peg#L1719-L1720`

### `type.never.param-uninhabited` — Never type forbidden in parameter position

A function parameter typed `!` (never) is rejected: `!` is uninhabited, so no value can be supplied. `!` remains valid in return position (a diverging function).

**Example**

```logos
fn f(x: !) {}  // error
fn g() -> ! { loop {} }  // ok
```

**Source evidence:** `src/compiler/sema_decl.cpp#L589-L595`

### Group: `inhabited`

### `type.inhabited.enum` — Enum inhabitedness

An enum is uninhabited iff it has zero variants, or every variant has at least one uninhabited payload type; it is inhabited as soon as one variant is constructable (all its payload types inhabited).

**Source evidence:** `src/compiler/sema.cpp#L4363-L4376`

### `type.inhabited.never-uninhabited` — The Never type is uninhabited

The Never type `!` is uninhabited.

**Source evidence:** `src/compiler/sema.cpp#L4357-L4358`

### `type.inhabited.ref-conservative` — References to uninhabited types are treated as inhabited

A reference or pointer to an uninhabited type is conservatively treated as inhabited (only value-carrying composites are marked uninhabited).

**Divergence (from Rust):** Rust treats `&!` as uninhabited; Logos stays conservative and treats `&Never` as inhabited.

**Source evidence:** `src/compiler/sema.cpp#L4359-L4362`

### `type.inhabited.struct-tuple-array` — Composite inhabitedness

A struct is uninhabited iff any field type is uninhabited; a tuple iff any element type is uninhabited; an array `[T; N]` iff N > 0 and T is uninhabited (zero-length arrays are always inhabited).

**Source evidence:** `src/compiler/sema.cpp#L4377-L4392`

### Group: `uninhabited`

### `type.uninhabited.definition` — Uninhabited type classification

A type is uninhabited (no value can exist) if it is `Never`, an empty enum or one whose every variant has an uninhabited payload, a struct/tuple with an uninhabited field, or `[T; N]` with N>0 and uninhabited T. Match arms over an uninhabited variant are elided from exhaustiveness checking.

**Source evidence:** `src/compiler/sema_impl.hpp#L752-L757`

### Group: `pin`

### `type.pin.non-movable-classification` — Non-movable (location-anchored) type classification

A type is non-movable iff: it is a `#[pinned]` struct; or a `#[zoned2]` struct (self-relative pointer fields anchored to their own slot); or it inlines (transitively through struct/tuple/array by-value fields, not through pointers/references) a `#[rel_ptr]` or `#[pinned]` field. A `#[rel_ptr]` type itself is movable (its value-form is the resolved absolute pointer); it counts as non-movable only when embedded as an inline field.

**Divergence (from Rust):** Logos addition (zones/pin): `#[pinned]`/`#[zoned2]`/`#[rel_ptr]` anchoring has no Rust analog.

**Source evidence:** `src/compiler/sema_impl.hpp#L2096-L2146`, `src/compiler/sema_impl.hpp#L2126-L2142`

### Group: `tagged`

### `type.tagged.thin-pointer` — tagged thin pointer type

`&tagged<T> Name` is a thin tag-dispatched pointer: a type_code tag is stored in memory before the object, and call sites read the tag, look up the dispatch table, and call indirectly.

**Divergence (from Rust):** A6

**Source evidence:** `tools/peg_gen/grammars/logos.peg#L1490-L1494`

### `type.tagged.thin-ptr-dispatch` — &tagged<TS> Trait

`&tagged<TS> Trait` resolves to a thin TaggedPtr with tag-based dispatch; Trait must be a registered trait and TS must resolve to a concrete struct type, else hard error.

**Divergence (from Rust):** Logos-only tagged-dispatch pointer.

**Source evidence:** `src/compiler/sema.cpp#L6021-L6039`

### Group: `writ`

### `coerce.writ.mapslice-to-typed-map` — MapSlice as <K,AnyVal>{} builds a typed Writ map

`src as <K,V>{}` (target struct WritMap) is permitted only for V = AnyVal and K in {I32,U32,I64,U64}, with source the matching MapSlice<K> struct; it lowers to a stdlib writ_build_map_<k>_anyval call returning Rc<Writ>. Any other key/value combination, a mismatched source, or a missing builder is an error.

**Source evidence:** `src/compiler/sema_expr.cpp#L775-L838`

### `coerce.writ.slice-to-typed-array` — &[T] as <T>[] builds a typed Writ array

`src as <T>[]` (target struct WritArr) requires `src: &[T]` (a Slice) whose element kind equals the target element kind; element T must be one of i8/u8/i16/u16/i32/u32/i64/u64/f32/f64. It lowers to a stdlib writ_build_array_<T> call returning the builder's Rc<Writ> type; missing builder (no `use logos.lang.writ.typed_arr`) or non-slice source or element mismatch or unsupported element is an error.

**Source evidence:** `src/compiler/sema_expr.cpp#L716-L772`

### `type.writ.container-kinds` — Writ view container recognition

A type t (optionally behind one &/&mut) denotes a Writ value iff its underlying Struct/ZonedStruct name is one of {Writ, WritView, WritStatic, Rc} (Rc being the `Rc<Writ>` runtime container). One level of reference is peeled before matching.

**Uncertainty:** Matches by struct name only; does not check Rc's type argument is Writ.

**Source evidence:** `src/compiler/sema_impl.hpp#L4133-L4149`

### `type.writ.lit-and-array-map` — Writ literal / typed array / typed map types

`@{...}` at type position is a WritStatic value literal type (LIT_WSTATIC). `<Elem>[]` is a Writ typed-array type and `<K[,V]>{}` is a Writ typed-map type (used in `as` casts).

**Divergence (from Rust):** A6

**Source evidence:** `tools/peg_gen/grammars/logos.peg#L1451-L1473`

### Group: `writ-arr`

### `type.writ-arr.elem-set` — Writ typed array type <Elem>[]

`<Elem>[]` resolves to a generic struct `WritArr<elem>`; Elem must be one of I8/U8/I16/U16/I32/U32/I64/U64/F32/F64 (mapped to the Logos primitive), else hard error.

**Divergence (from Rust):** Logos-only Writ container type-expression.

**Source evidence:** `src/compiler/sema.cpp#L6234-L6266`

### Group: `writ-map`

### `type.writ-map.key-val-set` — Writ typed map type <K,V>{}

`<K,V>{}` resolves to `WritMap<key,val>`; key must be I32/U32/I64/U64 and value must be `AnyVal` (default), else hard error.

**Divergence (from Rust):** Logos-only Writ container type-expression.

**Source evidence:** `src/compiler/sema.cpp#L6267-L6297`

### Group: `wstatic`

### `type.wstatic.literal-arg` — WritStatic literal in type-arg position

A WritStatic literal `Foo::<@{...}>` (or a bare writ-lit value-AST in const recognition) resolves to the value's WritStatic type; a missing payload is a hard error.

**Divergence (from Rust):** Logos-only WritStatic value-as-type-arg.

**Source evidence:** `src/compiler/sema.cpp#L6370-L6386`

### Group: `datatype`

### `type.datatype.data-plain-inference` — DataPlain vs DataNode inference for datatypes

A datatype is DataPlain unless it (transitively, through array element types) embeds a datatype field that is not itself DataPlain, or a generic/unknown datatype field; such fields demote the enclosing datatype to DataNode. A by-value concrete DataPlain nested datatype does NOT demote the outer type; generic datatype fields (non-empty type args, e.g. `RelPtr<Node>`) and forward-/cross-package-referenced datatypes are treated conservatively as DataNode.

**Divergence (from Rust):** A6: Writ datatype DataPlain/DataNode classification is Logos-only.

**Source evidence:** `src/compiler/sema_collect.cpp#L3945-L3964`

### `type.datatype.pod-field-restriction` — Writ datatype fields must be POD-compatible

A field of a `datatype` (Writ fabric type) must be one of: a primitive scalar (i8..i128/u8..u128 incl. packed i24/u24/i56/u56, f32/f64, bool, integer/float literal types), an array whose element is datatype-safe, another datatype (ZonedStruct), a plain struct that is a `#[rel_ptr]` self-relative pointer, or an unresolved type variable (checked later by mono). Any other field type is rejected. Annotation types (compile-time only) are exempt and may hold non-POD fields such as `str`.

**Divergence (from Rust):** A6/A11: Writ datatype fabric is a Logos-only feature; uses extra packed int widths.

**Source evidence:** `src/compiler/sema_collect.cpp#L3892-L3933`

### Group: `anyval`

### `coerce.anyval.let-binds-i32` — AnyVal-typed let binds an i32

A binding declared with type `AnyVal` coerces its RHS to a 32-bit integer and stores it as a scalar binding.

**Divergence (from Rust):** No Rust equivalent (AnyVal is a Logos addition).

**Uncertainty:** AnyVal is a compiler-internal/metaprogramming type; the i32 coercion may be an implementation default rather than a stable language guarantee.

**Source evidence:** `src/compiler/mlir_gen_stmt.cpp#L1444-L1454`

### `layout.anyval.scalar-i32` — AnyVal is a scalar value type

The built-in type `AnyVal` has scalar value representation (a single machine word, narrowed to 32-bit), not an aggregate. It is never spilled to a by-value aggregate slot like a struct receiver.

**Divergence (from Rust):** Logos built-in type with no Rust analogue

**Uncertainty:** 32-bit width inferred from coerce_numeric(raw, i32); the language-level width may be target-defined.

**Source evidence:** `src/compiler/mlir_gen.cpp#L743-L746`, `src/compiler/mlir_gen.cpp#L888-L903`, `src/compiler/mlir_gen.cpp#L865-L867`

### `type.anyval.lowered-as-i32` — AnyVal is a 4-byte i32 everywhere

The AnyVal type is uniformly represented as a scalar i32 (size/align {4,4}) in every position — value, argument, and struct field — never as a wrapped aggregate.

**Source evidence:** `src/compiler/mlir_gen_types.cpp#L32`, `src/compiler/mlir_gen_types.cpp#L445`, `src/compiler/mlir_gen_types.cpp#L199-L210`

### `type.anyval.repr-i32` — AnyVal is represented as a 32-bit value

A value of type AnyVal is represented as a 32-bit integer in both parameter and return position.

**Uncertainty:** i32 likely encodes a handle/index into an AnyVal table; exact semantics inferred from representation only.

**Source evidence:** `src/compiler/mlir_gen_fn.cpp#L67`, `src/compiler/mlir_gen_fn.cpp#L98-L101`, `src/compiler/mlir_gen_fn.cpp#L113-L114`

### Group: `generic`

### `type.generic.instantiation` — Generic type instantiation `T<...>`

`Name<arg, ...>` (optional trailing comma) instantiates a generic type. The type name may also be a metavariable: `#Ident<...>` or `#(expr)<...>`.

**Source evidence:** `tools/peg_gen/grammars/logos.peg#L1797-L1815`

### `type.generic.type-arg-kinds` — Generic type-argument kinds

A generic type argument may be a lifetime `'a` (stored as LIFETIME_PARAM and skipped during concrete-type resolution), a pack expansion `Ident...`, an antiquote `$Ident` or `$Ident...`, an integer literal (optionally negated), a writ literal, or a type.

**Source evidence:** `tools/peg_gen/grammars/logos.peg#L1776-L1795`

### Group: `generic-inst`

### `type.generic-inst.arity-and-bounds` — Generic instantiation arity and bound checks

After default-filling, the type-argument count must match the struct/enum/datatype declared type-param count, and each argument must satisfy its param's trait bounds.

**Source evidence:** `src/compiler/sema.cpp#L5636-L5654`

### `type.generic-inst.box-slice-dst-collapse` — Box<[T]> and Box<DST-struct> collapse to owning fat references

`Box<[T]>` collapses to an owning fat slice {data, len} (same layout as `&[T]`, move-only, droppable). `Box<Foo>` where Foo is a custom-DST tail-slice struct collapses to an owning DstRef {data, len}.

**Uncertainty:** Logos custom-DST machinery; analogous to Rust CoerceUnsized.

**Source evidence:** `src/compiler/sema.cpp#L5478-L5501`

### `type.generic-inst.default-type-args` — Trailing default type arguments

When fewer type-args are supplied than the generic has params, trailing params are filled from their declared defaults (`struct S<T, U = i64>`: `S<A>` ≡ `S<A, i64>`); a default may reference an earlier param and is substituted with the already-bound args.

**Source evidence:** `src/compiler/sema.cpp#L5588-L5604`

### `type.generic-inst.generic-const` — Generic compile-time const instantiation

Applying type-args to a generic const `pub const X<T..>: WritStatic = @{...}` re-evaluates the const's value AST under the supplied type-arg bindings, yielding a fresh per-instantiation WStaticLit identity. The argument count must equal the const's type-param count.

**Uncertainty:** Logos-specific WritStatic generic const.

**Source evidence:** `src/compiler/sema.cpp#L5345-L5392`

### `type.generic-inst.generic-type-alias` — Generic type alias instantiation

A generic type alias `type Foo<T> = Bar<T>` instantiated as `Foo<A>` resolves to its RHS with type- and lifetime-args substituted; the supplied type-arg count and lifetime-arg count must equal the alias's declared type-param and lifetime-param counts respectively.

**Source evidence:** `src/compiler/sema.cpp#L5394-L5429`

### `type.generic-inst.kind-disambiguation-local-shadow` — Local declaration shadows imported same-named type

When a name resolves to multiple kinds (struct/datatype/enum) across packages, a declaration local to the current package wins over any non-local same-named declaration of another kind.

**Source evidence:** `src/compiler/sema.cpp#L5505-L5526`

### `type.generic-inst.smart-pointer-dyn-collapse` — Box<dyn Trait> collapses to an owning trait object

`Box<dyn Trait>` (FQN-gated to the stdlib Box) collapses to an owning fat-pair trait object {data, vtable} tagged Box. Rc/Arc no longer collapse and instead resolve as ordinary generic structs whose inner pointer is a fat DST reference.

**Uncertainty:** Mirrors Rust owned_box + CoerceUnsized lang item; Rc/Arc flip is Logos-specific.

**Source evidence:** `src/compiler/sema.cpp#L5432-L5477`

### `type.generic-inst.unknown-type-metaprog-defer` — Unknown generic type deferred during metaprog discovery

An unknown generic type name is an error, except during the metaprog discovery pass (before derive hooks emit items), where it silently yields error-type so a later non-metaprog pass can re-resolve once synthesized items exist.

**Source evidence:** `src/compiler/sema.cpp#L5527-L5538`

### `type.generic-inst.unsized-arg-gating` — ?Sized type-param relaxes unsized type-args

A type-argument at a generic param declared `?Sized` (implicit_sized=false) may be a bare unsized type (`[T]`, `dyn Trait`); a type-arg at a `Sized` param must not be unsized. Passing an unsized type, or a `?Sized` outer type-param, to a `Sized` param is a diagnostic.

**Source evidence:** `src/compiler/sema.cpp#L5562-L5586`, `src/compiler/sema.cpp#L5605-L5635`

### Group: `pack-expand`

### `type.pack-expand.in-scope-typevar` — Pack expansion in type-arg position

`T...` in type-arg position resolves to the in-scope variadic type parameter's TypeVar; an undefined pack name is a hard error.

**Source evidence:** `src/compiler/sema.cpp#L6299-L6310`

### Group: `assoc`

### `type.assoc.normalize-via-where-eq` — Associated-type projection normalized by where-clause equality bound

An associated-type projection `<TV as Trait>::A` (where `TV` is a generic type-param) is normalized to the concrete type `C` whenever an in-scope where-clause bound on `TV` records an associated-type equality `Trait::A = C`. When the projection records a trait, only equalities from a bound whose trait matches (or whose trait is unrecorded) apply; the first matching equality wins. If no matching equality exists, the projection is left unchanged.

**Source evidence:** `src/compiler/sema.cpp#L2689-L2704`

### `type.assoc.projection` — Associated-type projection

`T::Item` and `T::Item<A,B>` (GAT with type args) are associated-type references; the `::Name[<args>]` tail may chain one or more times. `<T as Trait>::Assoc` is the fully-qualified form, with the disambiguating trait recorded for resolution.

**Example**

```logos
<Vec<T> as IntoIterator>::IntoIter
```

**Source evidence:** `tools/peg_gen/grammars/logos.peg#L1475-L1488`

### Group: `assoc-ref`

### `type.assoc-ref.bound-and-supertrait-lookup` — Associated-type projection on a type-parameter

For `base::Item` where `base` is a type-variable, the owning trait of `Item` is found by searching the bounds of `base` (with `Self` mapped to the enclosing trait) and walking each bound trait's supertrait chain; the first trait declaring an associated type named `Item` is selected, carrying that bound's concrete trait type-args.

**Source evidence:** `src/compiler/sema.cpp#L5108-L5112`, `src/compiler/sema.cpp#L5146-L5185`

### `type.assoc-ref.concrete-impl-fallback` — Assoc projection fallback to implementing trait

If no owning trait is found from bounds or impl context, the projection's owning trait is found among traits that have an impl for the concrete base type (tried under both the full concrete type name and the bare struct name); if still none declares `Assoc`, a diagnostic 'no associated type Assoc found for <base>' is raised.

**Source evidence:** `src/compiler/sema.cpp#L5232-L5257`

### `type.assoc-ref.deferred-node` — Deferred associated-type node carries trait args

An unresolved projection yields a deferred AssocType node {base, trait, name, gat_args}; the trait name is suffixed with the concrete trait type-args so distinct `Trait<T>` instantiations produce distinct nodes (empty suffix for non-generic traits preserves the bare name). Bounds declared on the assoc type are propagated into the projection's bound context.

**Source evidence:** `src/compiler/sema.cpp#L5308-L5337`

### `type.assoc-ref.eager-concrete-projection` — Eager projection for concrete base with generic trait

When the base is a concrete type and the resolved trait is generic (has type-args), the projection is resolved immediately by looking up the trait+args-suffixed assoc-type impl and substituting the base's type-args; this disambiguates two `Trait<T>` impls on one type that would otherwise intern to a single trait-arg-less deferred node and collapse.

**Divergence (from Rust):** G156-1 disambiguation of multiple Trait<T> impls.

**Source evidence:** `src/compiler/sema.cpp#L5275-L5307`

### `type.assoc-ref.equality-bound-normalization` — Associated-type equality bound normalization

If the base type-param carries an equality bound `Trait<A = V>`, the projection `T::A` is normalized directly to `V` at resolution time.

**Source evidence:** `src/compiler/sema.cpp#L5338-L5342`

### `type.assoc-ref.gat-args` — Generic associated type arguments

An associated-type reference may carry type arguments (`T::Item<i32>`, a GAT) and lifetime arguments (`T::Item<'a>`); lifetime args are collected separately from type args. The number of supplied GAT type-args must equal the associated type's declared GAT type-param count, and those args must satisfy the GAT type-params' trait bounds.

**Source evidence:** `src/compiler/sema.cpp#L5113-L5139`, `src/compiler/sema.cpp#L5258-L5274`, `src/compiler/sema.cpp#L5320-L5325`

### `type.assoc-ref.impl-trait-context` — Assoc projection resolves against the enclosing impl trait

Inside an `impl Trait<Args> for C`, an unresolved projection `Self::Assoc` resolves to the impl's trait when that trait declares `Assoc`, binding the projection to this impl's concrete trait type-args.

**Source evidence:** `src/compiler/sema.cpp#L5212-L5231`

### Group: `alias`

### `type.alias.generic-alias-inlined` — Generic type aliases are inlined at use sites

A type alias with type parameters has no concrete standalone type; it is inlined at each use site. Only non-generic aliases resolve to a concrete type.

**Source evidence:** `src/compiler/sema_decl.cpp#L1570-L1577`

### `type.alias.impl-target-unfold` — Non-generic type aliases unfold at an impl target position

When the impl target names a non-generic type alias `type A = B;`, the impl is treated as an impl on the aliased struct/datatype B (the alias is transparent): `impl Tr for A` ≡ `impl Tr for B`, including concrete-generic mangling of B.

**Source evidence:** `src/compiler/sema_decl.cpp#L1823-L1837`

### `type.alias.name-shadowing-order` — Type-alias name resolution shadowing order

A bare type name N resolves to a 0-arg type alias by probing in order: (1) the current package's own alias `pkg::N`, (2) the bare/unqualified alias N, (3) aliases from wildcard-imported packages. The current package's alias thus shadows a same-named imported/stdlib alias (Rust scoping).

**Source evidence:** `src/compiler/sema.cpp#L2562-L2581`

### Group: `typeof`

### `type.typeof.expr` — typeof type

`typeof(expr)` is the compile-time type of expr; the expression is not evaluated.

**Divergence (from Rust):** A6

**Source evidence:** `tools/peg_gen/grammars/logos.peg#L1461-L1463`

### `type.typeof.expr-type-no-eval` — typeof(expr) yields the sema type without evaluation

`typeof(expr)` resolves to the sema-computed type of `expr`; the expression is type-checked but never evaluated at runtime.

**Divergence (from Rust):** Logos addition: Rust has no `typeof` operator.

**Source evidence:** `src/compiler/sema.cpp#L5673-L5681`

### Group: `antiquot`

### `type.antiquot.quote-only` — Antiquotation valid only inside quote_ty!

A type antiquotation `$name` or pack-splice `$name...` is a hard error outside a `quote_ty! { ... }` context.

**Source evidence:** `src/compiler/sema.cpp#L5660-L5671`

### `type.antiquot.quote-ty-only` — Type antiquotation

`$ident` in type position is a type antiquotation valid only inside `quote_ty! { ... }`; resolving it elsewhere is an error.

**Divergence (from Rust):** A6

**Source evidence:** `tools/peg_gen/grammars/logos.peg#L1456-L1459`

### Group: `paren`

### `type.paren.transparent` — Parenthesized type is transparent

`(T)` resolves structurally identical to `T`.

**Source evidence:** `src/compiler/sema.cpp#L5896-L5900`

### `type.paren.unwrap` — Parenthesized type

`( T )` is a parenthesized type, distinct from `()` (unit), `(T,)` (1-tuple) and `(T1,T2)` (n-tuple); sema unwraps it to its inner type T.

**Source evidence:** `tools/peg_gen/grammars/logos.peg#L1423-L1426`

### `type.paren.unwrap-to-inner` — Parenthesized type is structurally its inner type

A parenthesized type `(T)` is unwrapped to its inner type `T`; `(T)` and `T` are structurally identical.

**Source evidence:** `tools/peg_gen/grammars/logos.peg#L290`

### Group: `lit-int`

### `type.lit-int.const-generic-arg` — Integer literal as type

An integer literal in type position resolves to an IntLit type carrying the (optionally negated) parsed value, for use as a const-generic argument.

**Source evidence:** `src/compiler/sema.cpp#L6127-L6138`

### Group: `self`

### `type.self.impl-binding-precedence` — `Self` resolves to the enclosing impl's type

Within a method body, `Self` denotes the impl's target type; when the impl fixed `Self = Foo<T>` with type-args, a bare same-named `Self` from an unrelated impl is treated as stale and replaced. A datatype binding for `Self` takes precedence over a struct binding when both names exist; a primitive target binds `Self` to that primitive.

**Uncertainty:** Precedence/staleness handling inferred from impl-context heuristics; observable effect is Self resolution.

**Source evidence:** `src/compiler/sema_decl.cpp#L207-L242`

### `type.self.implicit-self-param-ref` — Bare `self` parameter is `&Self` / `&mut Self`

A method parameter written as `self` (no explicit type) has type `&Self` by default, or `&mut Self` when marked mutable.

**Source evidence:** `src/compiler/sema_decl.cpp#L321-L328`

### Group: `name`

### `type.name.inference-placeholder` — `_` placeholder type

`_` resolves to an inferred-type placeholder, but is a hard error (E0121 analog) in item-signature position (fn params/return, const item type) where no inference context exists.

**Source evidence:** `src/compiler/sema.cpp#L6326-L6344`

### `type.name.lookup-namespaces` — Type-name lookup precedence across namespaces

An unqualified type name resolves with precedence: primitive keyword > in-scope generic type parameter > type alias > struct > datatype > enum; the first match wins. An unresolved name yields no type.

**Source evidence:** `src/compiler/sema.cpp#L2530-L2620`

### `type.name.lookup-or-error` — Named-type resolution and unknown-type diagnostics

A type name resolves via name lookup; if not found it is a hard error (`unknown type`), specialized to `generic type alias requires type arguments` when the name is a parameterized alias used without args. In metaprog discovery mode unknown names resolve silently to error_t (may be synthesised by a later hook).

**Source evidence:** `src/compiler/sema.cpp#L6349-L6366`

### `type.name.qualified-by-last-segment` — Qualified type path resolves by its last segment

A fully-qualified type `pkg.path.Type` is resolved by the final path segment alone; the package prefix is dropped.

**Source evidence:** `src/compiler/sema.cpp#L6312-L6325`

### `type.name.resolution-order` — Type-name resolution precedence

A bare type name resolves in order: (1) an in-scope type parameter wins over all global lookups; (2) built-in primitive (i8..i128/u8..u128, i24/u24/i56/u56, usize/isize, f32/f64, bool, char, void); (3) a non-generic type alias (generic aliases are deferred to use-site); (4) a struct, then datatype, then enum of that name. Unresolved names yield no type.

**Source evidence:** `src/compiler/sema_collect.cpp#L4132-L4178`

### `type.name.self-typevar` — Self resolves to the bound Self type parameter

`Self` resolves to the current `Self` type-param binding when one is in scope.

**Source evidence:** `src/compiler/sema.cpp#L6345-L6348`

### Group: `identity`

### `type.identity.array-size` — Array identity includes the length

Array `[T; N]` identity = (length N, symbolic-length variable name, element T). Arrays with different lengths (concrete or symbolic) are distinct types.

**Source evidence:** `src/compiler/sema.cpp#L822-L826`, `src/compiler/sema.cpp#L980-L983`

### `type.identity.array-size-significant` — Array length is part of structural identity; tuple arity too

[T; N] structural identity mixes the element type identity AND N. (T1,...,Tn) mixes the arity n and each element identity in order. Arrays/tuples differing only in length/arity have distinct identity.

**Source evidence:** `src/compiler/mono_clone.cpp#L110-L122`

### `type.identity.assoc-type` — Associated-type identity = (trait, assoc name, base, GAT args)

An associated/projection type identity = (trait name, associated-type name, base type, generic-associated-type arguments). GATs differing in their args are distinct projection types.

**Source evidence:** `src/compiler/sema.cpp#L903-L908`, `src/compiler/sema.cpp#L1044-L1049`

### `type.identity.cfg-slot` — Config-slot type identity = (cfg-typevar name, slot key)

A config-slot type is identified by the pair (config type-variable name, slot key); distinct slots intern to distinct types.

**Divergence (from Rust):** Logos addition (zone/config slots)

**Source evidence:** `src/compiler/sema.cpp#L923-L929`, `src/compiler/sema.cpp#L1050-L1052`

### `type.identity.dstref` — Custom-DST reference identity = (package, name, mutability, owning kind, type-args)

A custom-DST reference type's identity = (package, struct name, mutability, owning kind {Borrow/Box}, type-args); an owning `Box<Foo>` custom-DST is distinct from a borrowed `&Foo`.

**Divergence (from Rust):** A3 (custom-DST)

**Source evidence:** `src/compiler/sema.cpp#L855-L863`, `src/compiler/sema.cpp#L1009-L1014`

### `type.identity.dyn-trait` — Trait-object identity = (owning kind, auto-traits, trait, type-args)

A trait-object `dyn Trait<..>` identity = (owning kind {Borrow/Box/Rc/Arc} in const_val low byte, `+Send` bit 8, `+Sync` bit 9, trait name, trait type-args). `&dyn T` and `&dyn T + Send` are distinct types; the same trait behind Box vs Rc vs Arc are distinct.

**Source evidence:** `src/compiler/sema.cpp#L884-L892`, `src/compiler/sema.cpp#L1032-L1035`

### `type.identity.fnitem-distinct` — Each function item is a distinct zero-sized type

A function-item type's identity = (function symbol name, turbofish type-args, signature params, return). Two distinct functions with identical signatures get distinct fn-item types, and distinct instantiations of one generic function (even when the resulting fn-ptr signature collapses, e.g. unused type param) get distinct fn-item types.

**Source evidence:** `src/compiler/sema.cpp#L874-L883`, `src/compiler/sema.cpp#L1023-L1031`

### `type.identity.fnptr-abi` — Function-pointer identity = (ABI tag, params, return)

A function-pointer type identity = (extern-ABI tag where empty = default Rust ABI, ordered parameter types, return type). Function pointers differing only in ABI are distinct types.

**Source evidence:** `src/compiler/sema.cpp#L864-L869`, `src/compiler/sema.cpp#L1015-L1019`

### `type.identity.int-lit-value` — Integer-literal placeholder identity carries its value

An inferred integer-literal type `{integer}` carrying a const value is identified by that value (const_val); two literal placeholders with different values do not collapse to one type.

**Source evidence:** `src/compiler/sema.cpp#L909-L916`

### `type.identity.intern-canonical` — Types are interned by canonical structural identity

Every type has a canonical identity: two types constructed structurally identically (per the per-kind identity fields below, computed bottom-up over already-canonical sub-types) denote the same type and intern to one shared representative; structurally distinct types intern to distinct representatives.

**Related:** `type.identity.ref-vs-typeuid`, `type.identity.lifetime-ignored`

**Source evidence:** `src/compiler/sema.cpp#L801-L940`, `src/compiler/sema.cpp#L1099-L1109`, `src/compiler/sema.cpp#L1345-L1354`

### `type.identity.lifetime-ignored` — Lifetimes excluded from type identity for & / &mut

Reference types `&'a T` and `&mut 'a T` have identity determined solely by mutability and pointee `T`; the lifetime `'a` is NOT part of type identity (matches types_equal). Lifetime args on struct/enum/assoc types likewise do not affect type equality.

**Divergence (from Rust):** Rust treats lifetimes as part of the type but as a separate region-check phase; identity-collapse of lifetimes here matches Rust's type-equality-modulo-regions.

**Source evidence:** `src/compiler/sema.cpp#L817-L821`, `src/compiler/sema.cpp#L954-L959`

### `type.identity.nominal-args` — Struct/enum identity = (package, name, type-args)

A nominal struct or enum type's identity = (package name, type/enum name, ordered type arguments). Two instantiations of a generic nominal type with different type arguments are distinct types; zoned structs share this scheme.

**Source evidence:** `src/compiler/sema.cpp#L827-L837`, `src/compiler/sema.cpp#L984-L994`

### `type.identity.primitive-kind` — Primitive types identified by kind alone

Primitive types carry no structural fields; their kind tag alone identifies them, so all occurrences of a given primitive are the same interned type.

**Source evidence:** `src/compiler/sema.cpp#L930-L932`, `src/compiler/sema.cpp#L1053-L1055`

### `type.identity.ptr-distinct-by-mut` — Raw pointer identity = (mutability, pointee, zoned-flag)

Raw pointer `*const T`, `*mut T`, and `*zoned T` are mutually distinct types: identity = (mut flag, zoned flag carried in const_val bit 0, pointee T). `*zoned T` interns distinctly from a plain `*T`.

**Source evidence:** `src/compiler/sema.cpp#L808-L816`, `src/compiler/sema.cpp#L974-L975`

### `type.identity.recursive-cycle-guard` — Recursive struct types terminate identity computation with a cycle marker

When structural identity recursion re-enters a struct type already on the current walk path (recursive/self-referential types), the recursion is cut with a fixed marker rather than diverging; identity computation always terminates.

**Source evidence:** `src/compiler/mono_clone.cpp#L143-L147`, `src/compiler/mono_clone.cpp#L176-L177`

### `type.identity.ref-vs-typeuid` — Post-interning, type equality = pointer/UID equality

After interning, every type has a unique representative, so type equality reduces to representative-identity: equal hash/UID implies type-equal, and identical reference trivially implies type-equal (lifetime, package, lifetime-args, const_val being the only fields that may share a hash bucket while differing).

**Source evidence:** `src/compiler/sema.cpp#L1345-L1354`, `src/compiler/sema.cpp#L1099-L1104`

### `type.identity.slice-mut-owning` — Slice identity = (mutability, owning kind, element)

Slice types are distinguished by element T, mutability, and owning kind (const_val): `&[T]`, `&mut [T]`, and owning `Box<[T]>` are mutually distinct types.

**Divergence (from Rust):** A3 (custom-DST / Box<[T]> as owning slice kind)

**Source evidence:** `src/compiler/sema.cpp#L841-L847`, `src/compiler/sema.cpp#L997-L1003`

### `type.identity.struct-field-recursion` — Struct identity recurses through substituted field types

Structural identity of a struct type S<A...> mixes the struct shape tag, the field count, and the identity of each field type after substituting S's type-params by the concrete type-args A.... Generic struct instances thus get distinct identity per instantiation by their concrete field layouts.

**Source evidence:** `src/compiler/mono_clone.cpp#L141-L178`

### `type.identity.structural-hash-layout-stable` — Structural type identity is layout-stable, name-independent

A type's structural identity (used for wire/persistent identity) is computed by a tag-prefixed structural walk that bears no struct/field NAME: two types with identical physical layout (same primitive leaves, same field types in order, same array sizes) have equal identity regardless of struct/field renames. Each primitive kind has a distinct code; aggregate shapes carry distinct shape tags (struct/tuple/array/ptr/&/&mut/slice/enum/fnptr/void/wstatic).

**Uncertainty:** Concrete code values are an implementation detail; the normative content is name-independence + per-shape distinctness.

**Source evidence:** `src/compiler/mono_clone.cpp#L14-L21`, `src/compiler/mono_clone.cpp#L56-L78`, `src/compiler/mono_clone.cpp#L80-L185`

### `type.identity.tuple` — Tuple identity = ordered element types

A tuple type's identity is the ordered sequence of its element types; tuples are equal iff same arity and pairwise-equal elements.

**Source evidence:** `src/compiler/sema.cpp#L838-L840`, `src/compiler/sema.cpp#L995-L996`

### `type.identity.typevar-name` — Type/const variable identity = name

A type variable or const variable is identified by its name (plus const_val); two type parameters with the same name denote the same type variable.

**Source evidence:** `src/compiler/sema.cpp#L899-L902`, `src/compiler/sema.cpp#L1040-L1043`

### `type.identity.wstatic-config` — WritStatic-literal type identity = its byte-hash

A type parameterized by a WritStatic literal config (`Foo::<@{...}>`) is identified by the byte-hash of that literal; distinct configurations instantiate to distinct types and do not dedupe.

**Divergence (from Rust):** Logos addition (WritStatic const-config type parameters)

**Source evidence:** `src/compiler/sema.cpp#L917-L922`

### Group: `subtype`

### `type.subtype.assoc-gat-lifetime-invariant` — Associated/GAT types: covariant args, invariant GAT lifetime args

For an associated type projection (same trait_name and assoc_type_name): the base is covariant, each GAT type-arg is covariant, and each GAT lifetime-arg is invariant (must be equal; GAT lifetime variance is not user-controllable). Differing trait/name or arity is a shape difference.

**Source evidence:** `include/logos/compiler/subtype.hpp#L310-L329`, `include/logos/compiler/subtype.hpp#L93-L113`

### `type.subtype.depth-cap-accept` — Recursion depth cap conservatively accepts the subtype relation

If subtype recursion exceeds depth 64, or either operand is null, the relation is accepted (returns true), deferring soundness to the caller's separate compatibility check.

**Uncertainty:** Conservative termination guard, not a language-design choice.

**Source evidence:** `include/logos/compiler/subtype.hpp#L203-L204`

### `type.subtype.enum-covariant` — Enums covariant in all type-arg and lifetime-arg positions

For same enum (matching pkg_name+enum_name), every type-arg and lifetime-arg position is treated as covariant (Co); there is no per-enum variance table. Matches the covariant shape of Option/Result/Box. Differing name, package, or arity is a shape difference.

**Uncertainty:** Per-enum variance table not yet wired; Co is a conservative fallback (B81 compiler tag).

**Source evidence:** `include/logos/compiler/subtype.hpp#L279-L298`

### `type.subtype.fn-contra-params-co-ret` — Function pointers and closures: contravariant params, covariant return

FnPtr and Closure subtype identically: sub <: sup iff each param position is contravariant (sup_param <: sub_param) and the return type is covariant (sub_ret <: sup_ret), with matching param arity. Arity mismatch is deferred to the compatibility check.

**Source evidence:** `include/logos/compiler/subtype.hpp#L299-L309`, `include/logos/compiler/subtype.hpp#L10`

### `type.subtype.inferred-wildcard` — Inferred-type placeholder `_` is variance-compatible with any type

An InferredType (`_`) on either side of a structural-equality-with-lifetimes comparison is treated as a wildcard matching any type at any nesting depth (e.g. Vec<_> compares equal to Vec<i32>), letting region/type inference resolve it later.

**Source evidence:** `include/logos/compiler/subtype.hpp#L62-L63`, `include/logos/compiler/subtype.hpp#L57-L61`

### `type.subtype.rawptr-variance` — *const covariant, *mut invariant; mut/const mismatch is shape diff

Raw pointers carry no lifetime. *const T is covariant in pointee (*const T <: *const U iff T <: U); *mut T is invariant in pointee (*mut T <: *mut U iff T == U with lifetimes). A const-vs-mut pointer-kind mismatch is a shape difference, deferred to the compatibility check (subtype returns true).

**Source evidence:** `include/logos/compiler/subtype.hpp#L226-L235`

### `type.subtype.relation-purpose` — Subtyping refines kind-equality with lifetime variance

sub <: sup holds when a value of type sub may be used where sup is expected. Subtyping augments lifetime-erased kind/structural compatibility with lifetime-aware variance constraints; it returns true for any cross-kind pair (leaving legitimate cross-kind coercions, e.g. IntLit→i32, &mut→&, Vec→slice, to the separate compatibility check) and only fails when sub and sup share a kind that has a variance rule and their lifetime-aware structure disagrees.

**Related:** `coerce.compatible.equal-implies-compatible`

**Source evidence:** `include/logos/compiler/subtype.hpp#L37-L41`, `include/logos/compiler/subtype.hpp#L197-L211`

### `type.subtype.struct-variance-table` — Struct variance from per-def table keyed by package+name, default covariant

For same struct (matching pkg_name+struct_name), each type-arg position i and lifetime-arg position i is checked at its variance looked up from the per-definition variance table (key 'pkg.Name', subkeys '#i' for type args, '@i' for lifetime args). Absent table or absent entry defaults to covariant (Co). Differing struct name, package, or arg-list arity is a shape difference (subtype returns true).

**Uncertainty:** Variance table is user/compiler-supplied; this unit only consumes it.

**Source evidence:** `include/logos/compiler/subtype.hpp#L247-L278`, `include/logos/compiler/subtype.hpp#L11-L13`

### `type.subtype.tuple-array-slice-covariant` — Tuples covariant per element; arrays and slices covariant in element

(S0,..,Sn) <: (P0,..,Pn) iff each Si <: Pi (same arity). [T; N] and [T] are covariant in element type: sub <: sup iff elem(sub) <: elem(sup). Arity/shape mismatch is deferred to the compatibility check.

**Source evidence:** `include/logos/compiler/subtype.hpp#L236-L246`

### Group: `equal`

### `type.equal.lifetime-aware-structural` — Lifetime-aware structural equality is stronger than lifetime-erased TypeUID

Structural type equality used at invariant positions includes lifetime fragments and recurses through Ref/MutRef/Ptr (pointee + lifetime), Tuple/Array/Slice (elements), FnPtr/Closure (params + return), Struct/ZonedStruct/Enum (name, package, type-args, lifetime-args), and AssocType (trait, name, base, GAT type-args + lifetime-args). Two types differing only by an inner lifetime are unequal here even though TypeUID erases the lifetime. Primitives, TypeVars, and other kinds use TypeUID identity.

**Source evidence:** `include/logos/compiler/subtype.hpp#L54-L151`

### `type.equal.uid-identity` — Type equality is canonical-UID identity within one pool

types_equal(a,b) holds iff both are non-null, drawn from the same type pool, and have equal canonical UIDs (uid_of(a)==uid_of(b)); types from different pools are never equal. Pointer-identical refs are trivially equal.

**Source evidence:** `src/compiler/sema.cpp#L1355-L1362`

### Group: `recursion`

### `type.recursion.enum-finite-size` — Enum variant payload may not contain itself by value

An enum type is ill-formed if any variant payload type transitively contains the enum itself by value (through struct/enum/tuple/array, not through indirection). Such recursion must be broken by boxing the payload behind a pointer (`*const T`).

**Source evidence:** `src/compiler/sema_impl.hpp#L1729-L1749`, `src/compiler/sema_impl.hpp#L1742-L1746`

### `type.recursion.indirection-breaks-cycle` — Pointers/references break size-cycle detection

Size-cycle traversal descends only through inline by-value containers (Struct, ZonedStruct, Enum, Tuple, Array element types); it does not descend through pointer or reference fields, so indirected self-reference is finite-size and legal.

**Source evidence:** `src/compiler/sema_impl.hpp#L1693-L1706`

### `type.recursion.struct-finite-size` — Struct may not contain itself by value

A struct type is ill-formed if its by-value field graph (transitively through struct/enum/tuple/array fields, but not through pointers or references) contains itself: cycle detection (white/gray/black) over field types rejects an infinite-size type. The fix is to indirect via a pointer or reference (`&T`).

**Source evidence:** `src/compiler/sema_impl.hpp#L1690-L1707`, `src/compiler/sema_impl.hpp#L1708-L1728`, `src/compiler/sema_impl.hpp#L1750-L1751`

### Group: `recursive`

### `type.recursive.by-value-cycle` — Recursive by-value type cycle detection

A struct or enum that (transitively) contains itself through only by-value field/payload edges (Struct, ZonedStruct, Enum) is a forbidden recursive-value type. Pointer, `&`, and `&mut` edges break the cycle (they lower to fixed-size pointers) and are permitted.

**Example**

```logos
struct S { next: S }      // error: recursive value type
struct S { next: Box<S> } // ok (pointer breaks cycle)
```

**Source evidence:** `src/compiler/sema_impl.hpp#L1511-L1521`, `src/compiler/sema_impl.hpp#L1665-L1685`

### Group: `rec`

### `type.rec.no-by-value-cycle` — recursive by-value type cycles are rejected

A struct/enum graph that contains a by-value (non-indirected) cycle is an error; recursion through a type of statically unknown/infinite size must be broken by an indirection (e.g. a pointer/box).

**Source evidence:** `src/compiler/sema_collect.cpp#L544-L546`

### Group: `infer`

### `coerce.infer.placeholder-unifies` — Inference placeholder _ unifies in either direction

If either side is the InferredType placeholder (_), the pair is compatible; actual resolution is deferred to the surrounding annotation/RHS unifier.

**Divergence (from Rust):** logos-core 1.3

**Source evidence:** `src/compiler/sema.cpp#L1836-L1840`

### `type.infer.fill-annotation-from-rhs` — Inferred holes in let-annotation filled from RHS type

An `_` (inferred) hole in a let annotation is filled from the structurally-matching concrete RHS type (e.g. `let v: Vec<_> = vec![1]` binds as `Vec<i32>`); the annotation's concrete parts win, holes take the RHS side. Mismatched shapes leave the annotation unchanged. A bare `_` against an integer-literal RHS defaults to i32 and against a float-literal RHS to f64.

**Example**

```logos
let v: Vec<_> = vec![1];  // Vec<i32>
```

**Source evidence:** `src/compiler/sema.cpp#L4395-L4402`, `src/compiler/sema.cpp#L4310-L4318`

### `type.infer.hole-detection` — A type contains an inferred hole transitively

A type is considered to contain an inferred hole if it is `_` or if any of its type arguments, tuple elements, element type, or pointee transitively contains one.

**Source evidence:** `src/compiler/sema.cpp#L4343-L4351`

### `type.infer.let-hole-from-rhs` — `_` holes in a let annotation are filled from the RHS type

An inference hole `_` at any depth in a `let` type annotation is filled from the corresponding position of the initializer's inferred type.

**Source evidence:** `src/compiler/sema_impl.hpp#L739-L742`

### `type.infer.never-fallback-on-divergent-body` — ! fallback for unbound type-param of always-diverging callee

If a callee's body always diverges (panic-tail or `loop {}`-tail) and a type-parameter is otherwise unbound at the call site, the inference variable falls back to `!` (Never). A non-diverging body leaves an unbound type-param as an ambiguity error: `fn f<T>()->T{panic();}` infers T=! while `fn f<T>()->T{return 0;}` is ambiguous.

**Divergence (from Rust):** Rust-2024 `!`-fallback semantics (logos-core 1.1).

**Source evidence:** `src/compiler/sema_impl.hpp#L2550-L2560`

### `type.infer.no-underscore-in-item-signature` — `_` placeholder type forbidden in item signatures

The inferred-type placeholder `_` is not permitted within types in item signature positions (function parameter types, return type, const item type), including nested occurrences (`Vec<_>`, `&_`, `[_; N]`); such occurrences are an error rather than an inference hole.

**Source evidence:** `src/compiler/sema_impl.hpp#L1957-L1968`

### Group: `canonicalize`

### `type.canonicalize.global-substitution` — Global type simplification pass

After collection, every declared type position is canonicalized by an identity substitution: struct field types, enum variant payload types, free + generic function parameter/return types, type-alias bodies, module-const types, associated-const impl types, and associated-type impl bodies. Non-generic forms (e.g. `type Inner<T> = i32`) resolve to their concrete type; forms still mentioning a TypeVar are left unchanged for later substitution.

**Source evidence:** `src/compiler/sema_collect.cpp#L703-L729`

### Group: `cfg-slot`

### `type.cfg-slot.const-generic-defer` — Deferred cfg-slot when base is a const type-param

When `CFG` names a const-generic type-parameter of the enclosing item, `<type:CFG.path>` is NOT resolved eagerly; it yields a deferred CfgSlotType carrying the CFG ident and an encoded path, which monomorphization resolves once the parameter is bound to a concrete WritStatic value.

**Uncertainty:** Logos-specific; const-generic-of-WritStatic kind.

**Source evidence:** `src/compiler/sema.cpp#L4972-L4981`, `src/compiler/sema.cpp#L4982-L4983`, `src/compiler/sema.cpp#L5055`, `src/compiler/sema.cpp#L5101-L5105`

### `type.cfg-slot.const-param-must-be-writstatic` — cfg-slot base type-param must be const WritStatic

If `CFG` in `<type:CFG.path>` names a type-parameter, that parameter must be declared `const CFG: WritStatic`; otherwise a diagnostic is raised (the param must be a const-generic whose type is the WritStatic struct).

**Uncertainty:** Logos-specific WritStatic const-generic requirement.

**Source evidence:** `src/compiler/sema.cpp#L4985-L5004`

### `type.cfg-slot.eager-alias-resolution` — Eager cfg-slot resolution against a WStaticLit alias

When `CFG` is not a type-param but resolves to a type alias bound to a WStaticLit (`pub type Cfg = @{...};`), the path is walked eagerly through that literal's registered Writ value at resolution time, producing the concrete projected type directly.

**Uncertainty:** Logos-specific.

**Source evidence:** `src/compiler/sema.cpp#L4974-L4976`, `src/compiler/sema.cpp#L5055-L5099`

### `type.cfg-slot.path-extraction` — Config-slot type projection

`<type:CFG.path>` extracts a type from a WritStatic-typed binding `CFG` by walking a path of steps; each step is a struct-field access by name (on a string-keyed Writ map), an integer-field access by index (on an int-keyed Writ map), or an array index (on a Writ array). The path must be non-empty. The final reached Writ value must be a Type value; its named type is then resolved as the result.

**Uncertainty:** Logos-specific construct (no Rust analogue); semantics inferred from path-walk logic.

**Source evidence:** `src/compiler/sema.cpp#L4969-L4981`, `src/compiler/sema.cpp#L5038-L5041`, `src/compiler/sema.cpp#L5067-L5096`

### `type.cfg-slot.projection` — Type-level cfg-slot projection

`<type:CFG.path>` projects, at mono-time, the type stored at a path within a WritStatic-typed type-level binding. Path steps are `.IDENT` (string key), `.INTEGER` (int key) and `.[INTEGER]` (array index). At least one path step is required. `<type:CFG.SLOT>::Assoc` projects an associated type on the slot base.

**Divergence (from Rust):** A6

**Source evidence:** `tools/peg_gen/grammars/logos.peg#L1428-L1449`

### Group: `binop`

### `coerce.binop.autoderef-numeric-ref` — Auto-deref reference operand to primitive in scalar binops

For binary operators in {+,-,*,/,%,<,<=,>,>=,==,!=,&,|,^,<<,>>}, an operand of type &T or &mut T whose pointee T is an integer, f32, f64, bool, or char is implicitly dereferenced to T before operator resolution; struct pointees are not peeled.

**Example**

```logos
fn f(r: &i32) -> i32 { r + 1 }
```

**Divergence (from Rust):** Models Rust's `impl Add<i32> for &i32` family via auto-deref rather than blanket ref impls.

**Source evidence:** `src/compiler/sema_expr.cpp#L1718-L1742`

### `coerce.binop.bitwise-ref-scalar-deref` — Auto-deref &T for bitwise/shift when T is integer (or bool for bitwise-only)

For bitwise/shift operators {&,|,^,<<,>>}, an operand of type &T is implicitly dereferenced to T when T is an integer type, or when the operator is one of {&,|,^} and T is bool; shift operators never deref a bool pointee.

**Source evidence:** `src/compiler/sema_expr.cpp#L2389-L2402`

### `type.binop.arith-numeric` — Arithmetic operators require numeric operands

Arithmetic operators {+,-,*,/,%} require both operands to be numeric; the result type is the unified integer type of the operands when both are integers, otherwise unify_numeric, with a TypeVar operand propagated as the result when the other is an integer literal.

**Source evidence:** `src/compiler/sema_expr.cpp#L2304-L2383`

### `type.binop.bitwise-integer-or-bool` — Bitwise/shift operands must be integer (or bool for bitwise-only)

Bitwise operators {&,|,^} require integer or bool operands; shift operators {<<,>>} require integer operands only. The result type is the unified integer type of the operands.

**Divergence (from Rust):** Matches Rust `impl BitAnd/BitOr/BitXor for bool`.

**Source evidence:** `src/compiler/sema_expr.cpp#L2384-L2416`, `src/compiler/sema_expr.cpp#L2454-L2454`

### `type.binop.comparison-bool` — Comparison operators yield bool with compatible operands

Comparison operators {==,!=,<,<=,>,>=} require the two operand types to be mutually compatible (in either direction) and produce type bool.

**Source evidence:** `src/compiler/sema_expr.cpp#L2272-L2303`

### `type.binop.enum-lit-rehint` — Bare enum-literal operand re-lowered with peer's concrete type

In an enum == / != where one operand is a bare enum literal (no type-args, e.g. Option::None) and the other carries concrete type-args, the bare operand is re-lowered with the peer's enum type as the hint so both sides share the same concrete layout for the eq impl.

**Source evidence:** `src/compiler/sema_expr.cpp#L2124-L2150`

### `type.binop.error-propagation` — Error operand yields error type

If either operand has the error type, the binary expression's result type is the error type (error already reported upstream; no cascade).

**Source evidence:** `src/compiler/sema_expr.cpp#L2253-L2254`

### `type.binop.intlit-fit-arith` — Arithmetic literal operand must fit the peer integer type

In integer arithmetic where one operand is an integer literal and the other a concrete integer type, the literal value must fit in that concrete type's range.

**Source evidence:** `src/compiler/sema_expr.cpp#L2367-L2382`

### `type.binop.intlit-fit-comparison` — Comparison literal must fit the peer integer type

In a comparison where one operand is an integer literal and the other a concrete integer type, the literal value must fit in that type's range; otherwise the comparison is rejected (it could never hold).

**Example**

```logos
let x: i32; x == 10000000000
```

**Source evidence:** `src/compiler/sema_expr.cpp#L2290-L2302`

### `type.binop.logical-bool` — && and || require bool operands, yield bool

Operators && and || require each operand to be bool or the never type !; the result type is bool.

**Source evidence:** `src/compiler/sema_expr.cpp#L2262-L2271`

### `type.binop.never-operand` — Diverging operand makes binop type !

If either operand has the never type !, the binary expression type-checks against any operator (no numeric/bool requirement) and its result type is !.

**Example**

```logos
1 + return 7
x * break
```

**Source evidence:** `src/compiler/sema_expr.cpp#L2255-L2261`

### Group: `closure-arg`

### `type.closure-arg.hint-from-formal` — Closure/literal argument types hinted from method formal parameter

Each argument is lowered with a type hint derived from the corresponding method formal: a single Ref/MutRef wrapper on the formal is stripped, then a function/closure formal seeds the closure hint, a generic Enum/Struct (with type-args) seeds the enum/struct hint, and a Tuple formal seeds the tuple hint. An Fn-family-bounded bare type-parameter formal synthesizes a Closure hint from the bound's signature so an untyped closure (`|i|`) infers its parameter types.

**Source evidence:** `src/compiler/sema_expr.cpp#L7942-L7986`, `src/compiler/sema_expr.cpp#L7958-L7979`

### Group: `copy`

### `type.copy.drop-mutually-exclusive` — Copy and Drop are mutually exclusive (E0184)

A type may not both implement Copy and Drop. An `impl Drop for X` blocks X from auto-Copy; an explicit `impl Copy for X` coexisting with `impl Drop for X` is a compile error (E0184), since bitwise duplication of a Copy value would re-run the destructor on each copy (double-free).

**Source evidence:** `src/compiler/sema.cpp#L2878-L2879`, `src/compiler/sema.cpp#L2971`, `src/compiler/sema.cpp#L2983-L3000`

### `type.copy.field-kinds` — Copy field-type classification

For auto-Copy, a field type counts as Copy iff it is: a primitive integer/float/bool/char/usize/isize; a raw pointer (`*const`/`*mut`); a shared reference (`&T`); a function pointer or fn-item; a payload-less enum (no variant carries a payload and the enum has no `impl Drop`); a non-owning slice (`&[T]`); a struct already classified Copy; or a tuple all of whose elements are Copy. A `&mut T` exclusive reference is NOT Copy (move-only). Owning slices `Box<[T]>`, arrays, closures, type-vars, trait-objects, and payload-bearing enums are not Copy.

**Source evidence:** `src/compiler/sema.cpp#L2883-L2953`

### `type.copy.struct-structural-auto` — Structural auto-Copy for plain-data structs

A plain-data `struct` with no `impl Drop` and at least one field, whose every field type is Copy, is itself Copy — no `#[derive(Copy)]` opt-in is required. Determined by fixpoint over the struct dependency graph (a struct may become Copy once all its struct-typed fields are known Copy). Zero-field structs are not auto-promoted.

**Divergence (from Rust):** Logos auto-derives Copy structurally; Rust requires explicit `#[derive(Copy)]`. Capability-equivalent (a Copy type stays usable after by-value use).

**Source evidence:** `src/compiler/sema.cpp#L2867-L2880`, `src/compiler/sema.cpp#L2955-L2981`

### `type.copy.structural-auto` — non-Drop struct of all-Copy fields is automatically Copy

A struct that does not implement Drop and whose every field type is Copy is automatically Copy, without an explicit `impl Copy`.

**Divergence (from Rust):** A: diverges from Rust, which requires an explicit `#[derive(Copy)]`/`impl Copy`.

**Uncertainty:** Exact DIVERGENCES.md tag not confirmed; promotion logic lives in compute_auto_copy_types outside this unit.

**Source evidence:** `src/compiler/sema_collect.cpp#L674-L678`

### Group: `default`

### `type.default.array-elementwise-default` — Default for [E;N] is elementwise

The default value of an array type `[E; N]` is `[E::default(); N]`, recursing on the element type. A type has a default only if it (and every element) has a `Default` impl in scope.

**Source evidence:** `src/compiler/sema_impl.hpp#L494-L498`

### Group: `drop`

### `type.drop.aggregate-recursive` — Struct/enum/tuple/array droppability is recursive plus explicit Drop

A (zoned) struct or enum needs drop iff it has a user `drop` method or any field/variant-payload type needs drop. A tuple needs drop iff any element needs drop. An array `[T; N]` needs drop iff `T` needs drop.

**Source evidence:** `src/compiler/mlir_gen_stmt.cpp#L482-L502`

### `type.drop.closure-value-not-auto-dropped` — A closure value is not auto-reported as needs-drop

A closure value (e.g. stored in a struct field or iterator adapter, held by pointer) is not classified as needs-drop by recursive aggregate scanning; closure drop is driven narrowly only via the owning `Box<Closure>` path.

**Divergence (from Rust):** Narrows automatic Drop coverage relative to Rust for indirectly-stored closures; intentional to avoid misreading a pointer slot as a {fn,env} pair.

**Source evidence:** `src/compiler/mlir_gen_stmt.cpp#L503-L511`

### `type.drop.copy-bounded-typevar-not-droppable` — Copy-bounded type-param is non-droppable

A generic type-param `T` with an explicit `Copy` bound is provably non-droppable (Copy and Drop are mutually exclusive), so it contributes no drop glue when it appears as a tuple element, array element, or enum payload — even though a bare type-param otherwise defers its drop decision to monomorphization.

**Source evidence:** `src/compiler/sema.cpp#L2784-L2802`

### `type.drop.move-closure-captures` — Captures moved into a move-closure still drop, at the closure binding's slot

A variable moved into a `move` closure remains use-after-move-checked, but its destructor still runs (the closure only borrows its storage): such captures drop at their owning closure binding's slot, in capture order, even if the binding's own drop was skipped — same-frame owners only. A `return` inside a closure body drops only the closure's own frames, never the enclosing function's captured locals.

**Source evidence:** `src/compiler/sema.cpp#L3205-L3240`, `src/compiler/sema.cpp#L3254-L3258`

### `type.drop.moved-out-fields-skipped` — Partially-moved fields are excluded from a value's drop

When a local is dropped, fields (at any depth) that were moved out of it are excluded from its destructor: an exact field-path match skips that field, while a deeper moved path recurses and still drops the field's non-moved siblings.

**Source evidence:** `src/compiler/sema.cpp#L3181-L3202`

### `type.drop.no-auto-drop-suppresses-fields` — #[no_auto_drop] suppresses field destructors

A struct marked `#[no_auto_drop]` (the `ManuallyDrop<T>` lang-item shape) is treated as having no droppable fields: the compiler does not run its inner field destructors at scope exit.

**Source evidence:** `src/compiler/sema.cpp#L2856-L2859`

### `type.drop.no-self-recursion` — self of a Drop body is not auto-dropped

The `self` parameter of a `Drop::drop` method is not auto-dropped at the end of that method's body — calling drop on `self` from inside its own drop body would be infinite recursion. Detected when the resolved drop fn equals the function currently being lowered (modulo package prefix and overload-disambiguation suffix).

**Source evidence:** `src/compiler/sema.cpp#L3157-L3180`

### `type.drop.order-reverse-declaration` — Locals drop in reverse declaration order at scope exit

At scope exit, a frame's live (non-moved) locals are dropped in reverse of declaration order. Drops respect early-exit edges: `return` collects drops across enclosing frames up to (and not across) a closure boundary; `break`/`continue` collects drops up to and including the loop-body frame, stopping at a loop or closure boundary.

**Source evidence:** `src/compiler/sema.cpp#L3213-L3273`

### `type.drop.owning-dst-droppable` — Drop-need of dyn/slice/DST is decided by the owning bit

An unsized handle is droppable iff it owns its heap data: `Box<dyn Trait>` (owning trait object), `Box<[T]>` (owning slice), and owning custom-DST `Box<Foo>` require drop; their borrowed counterparts `&dyn Trait`, `&[T]`, `&Foo` do not.

**Source evidence:** `src/compiler/mlir_gen_stmt.cpp#L474-L481`

### `type.drop.receiver-shapes` — Drop method accepted by-value or by-reference receiver

The drop method for type `T` is matched whether its single parameter is `T` by value, `&T`, or `&mut T` (`fn drop(&mut self)` / `fn drop(&self)` are the canonical stdlib shapes); the by-reference forms are accepted by peeling one reference level. A generic `impl<T> Drop for Foo<T>` is matched against a concrete `Foo<C>` by struct base-name (re-mangled to the concrete name at monomorphization).

**Source evidence:** `src/compiler/sema.cpp#L2742-L2780`

### `type.drop.references-never-drop` — References and raw pointers are never droppable

A value of type `&T`, `&mut T`, or `*T` (raw pointer) does not require drop.

**Source evidence:** `src/compiler/mlir_gen_stmt.cpp#L469-L473`

### `type.drop.same-package-impl` — Drop impl must belong to the same package as the type

A candidate `Drop` impl is selected for type `t` only if its target type belongs to the same package as `t` (an empty package on either side acts as a wildcard). Two distinct types sharing a bare concrete name across packages do not borrow each other's Drop impl.

**Source evidence:** `src/compiler/sema.cpp#L2720-L2731`, `src/compiler/sema.cpp#L2778`

### `type.drop.transitive-aggregate-droppable` — Aggregate types are droppable if any owned member is

A type owns drop responsibility for its members: an array `[T;N]` is droppable iff `T` is; a tuple is droppable iff any element is; an enum is droppable iff any variant's payload field is (generic payloads concretized through the enum's type-params); a struct is droppable iff it has a drop fn or any field is (transitively) droppable. Owning `Box<dyn Trait>`, owning `Box<[T]>`, and owning `Box<Foo>` custom-DST are always droppable; their borrowed (`&dyn`, `&[T]`) counterparts are not.

**Source evidence:** `src/compiler/sema.cpp#L2804-L2864`

### Group: `enum-lit`

### `type.enum-lit.type-bounds-checked` — Generic enum type args are bound-checked

The inferred type arguments of a generic enum literal are checked against the enum's type-parameter bounds.

**Source evidence:** `src/compiler/sema_expr.cpp#L12145`

### Group: `impl-method`

### `trait.impl-method.unsized-self-seed` — Self seeded for unsized and str impl targets

For an impl whose target is unsized (slice/dyn) or `str`, `Self` is seeded before method lowering: an unsized-slice/unsized-dyn target binds `Self` to that type, and `impl ... for str` binds `Self` to an unsized slice of `u8`, so `self: &Self` / `&Self` / `Self::...` in method bodies resolve.

**Source evidence:** `src/compiler/sema_decl.cpp#L2142-L2145`, `src/compiler/sema_decl.cpp#L2160-L2173`

### Group: `move`

### `type.move.enum-droppable-payload` — Enum is a move type iff droppable

An enum is a move type iff it has a user `impl Drop` or carries a droppable payload field; a C-like enum or one whose payloads are all Copy is non-move.

**Source evidence:** `src/compiler/sema.cpp#L2676-L2680`

### `type.move.owning-heap-pointers` — Owning heap pointers are move types

Owning heap-backed types are move types: an owning `Box<dyn Trait>`, an owning `Box<[T]>` slice, and an owning `Box<Foo>` custom-DST each own heap data and are non-Copy, hence move. The corresponding borrowed forms (`&dyn`, `&[T]`) are Copy-like and not move types.

**Source evidence:** `src/compiler/sema.cpp#L2646-L2656`

### `type.move.struct-non-copy` — Struct is a move type unless Copy

A struct-typed value is a move type (its source slot is invalidated on by-value use and dropped on scope exit) unless the struct implements Copy. Copy holds either unconditionally or conditionally (e.g. `impl<P: Copy> Copy for Pin<P>`), the latter requiring every recorded copy-relevant type-argument position to hold a non-move (Copy) type.

**Source evidence:** `src/compiler/sema.cpp#L2630-L2641`, `src/compiler/sema.cpp#L2681-L2685`

### `type.move.typevar-conservative` — Generic type parameter is move unless bounded Copy

A type parameter T is treated as a move type within a generic body unless its bounds include `Copy`, in which case T is provably Copy (Copy and Drop are mutually exclusive) and by-value use of `x: T` does not move. Only an explicit Copy bound makes T non-move; otherwise the conservative move classification holds.

**Source evidence:** `src/compiler/sema.cpp#L2663-L2673`

### Group: `primitive-method`

### `type.primitive-method.mangled-lookup` — Primitive-receiver methods resolved via TypeName__method with receiver-shape variants

For a receiver with no struct name, the method is looked up as `<type-name>__<method>` matched against the actual argument signature; if no direct match, receiver-shape variants are tried in order: `&T`, `&mut T`, `*const T`, `*mut T`, and (for reference receivers) the `$ref_<...>` / `$mut_ref_<...>` mangling used to register `impl Trait for &T` / `&mut T`.

**Source evidence:** `src/compiler/sema_expr.cpp#L8089-L8130`

## Layout & representation

### Group: `abi`

### `layout.abi.aggregate-byte-size` — ABI byte size of arrays, tuples, structs, enums

Array size = N × elem size. Tuple/struct size = fields laid out sequentially, each aligned to min(field-size, 8), with the total padded to the max field alignment. Enum size = 4 (i32 tag) + the maximum total payload size across variants (void payload components contribute 0). Recursive struct fields are cycle-guarded to pointer size 8.

**Uncertainty:** Comment notes enum layout is a simplification mirroring mlir-gen.

**Source evidence:** `src/compiler/sema.cpp#L3884-L3931`

### `layout.abi.aggregate-field-alignment` — Tuples and structs lay fields out sequentially with natural alignment, capped at 8

For a tuple/struct, fields are placed in declaration order; before each field the running offset is rounded up to that field's alignment, where alignment = min(field-size, 8) (treating zero-size as alignment 1). The aggregate's size is the final offset rounded up to the maximum field alignment encountered.

**Uncertainty:** Alignment is derived as min(size,8) rather than a separate per-type alignment; matches a same-as-size convention for scalars but may diverge for over-aligned types.

**Source evidence:** `src/compiler/mono_clone.cpp#L365-L390`

### `layout.abi.array-size` — Array ABI size is element-size times length

sizeof([T; N]) = N * sizeof(T) (no per-element padding beyond the element's own size).

**Source evidence:** `src/compiler/mono_clone.cpp#L363-L364`

### `layout.abi.fat-pointer-16` — Slices, closures, trait objects, and DST refs are 16-byte fat values

A slice value, a closure, a trait object, and a DST reference each occupy 16 bytes (a two-word fat representation: data/pointer + metadata such as length, environment, or vtable).

**Source evidence:** `src/compiler/mono_clone.cpp#L362`

### `layout.abi.scalar-byte-sizes` — ABI byte sizes of scalar and pointer types

ABI byte sizes: void=0; bool/u8/i8=1; i16/u16=2; i24/u24=3; i32/u32/f32/char/int-literal=4; i56/u56=7; i64/u64/f64/float-literal/usize/isize and all thin pointers (raw/ref/fn-ptr/fn-item/tagged-ptr)=8; i128/u128=16; fat values (slice/closure/trait-object/dst-ref)=16; unsized slice/dyn=0; unknown types default to pointer size 8.

**Source evidence:** `src/compiler/sema.cpp#L3862-L3883`, `src/compiler/sema.cpp#L3932`

### `layout.abi.scalar-sizes` — Scalar ABI byte sizes

ABI size: void/never = 0; bool/u8/i8 = 1; i16/u16 = 2; i24/u24 = 3; i32/u32/f32/char = 4; i56/u56 = 7; i64/u64/f64/usize/isize/pointer/&/&mut/fnptr/fn-item/tagged-ptr = 8; i128/u128 = 16. The Writ-fabric widths I24/U24/I56/U56 occupy their narrow byte sizes (3 and 7).

**Divergence (from Rust):** A11 (I24/U24/I56/U56 are Logos-only widths)

**Source evidence:** `src/compiler/mono_clone.cpp#L348-L361`

### Group: `value`

### `layout.value.scalar-vs-aggregate-storage` — Storage representation by type kind

A binding's storage representation is fixed by type kind: scalars/fn-pointers/fn-items hold the value inline; structs, zoned-structs, arrays, and value-repr enums hold inline storage addressed by a pointer; tuples, closures, and slices/str are pointer-to-aggregate ({ptr,len} for slices); FnItem (per-instantiation ZST) lowers identically to a function pointer.

**Related:** `layout.fatptr.slice-dyn-16-bytes`

**Source evidence:** `src/compiler/mlir_gen_stmt.cpp#L1457-L1557`, `src/compiler/mlir_gen_stmt.cpp#L1528-L1541`, `src/compiler/mlir_gen_stmt.cpp#L1599-L1630`

### Group: `aggregate`

### `layout.aggregate.field-order-padding` — Struct/tuple layout = ordered fields with natural padding

A struct or tuple lays out fields in declaration order: each field starts at the next offset rounded up to its alignment, the aggregate's align = max(field aligns), and total size = accumulated offset rounded up to the aggregate align. All aggregate members are embedded inline by value.

**Source evidence:** `src/compiler/mlir_gen_types.cpp#L416-L427`, `src/compiler/mlir_gen_types.cpp#L475-L479`, `src/compiler/mlir_gen_types.cpp#L507-L511`, `src/compiler/mlir_gen_types.cpp#L430-L438`

### `layout.aggregate.inline-by-value-members` — Aggregate fields are embedded by value, references inline as fat pairs

A struct field whose type is a struct, tuple, tagged enum, slice, closure, custom-DST ref, or bare dyn trait object is embedded inline by value (the struct/enum's aggregate type, or its 16-byte fat pair, occupies the field). A field that is a pointer/reference to such a type (e.g. *Struct, &Struct, *const dyn) stores an 8-byte thin pointer instead.

**Source evidence:** `src/compiler/mlir_gen_types.cpp#L196-L319`, `src/compiler/mlir_gen_types.cpp#L244-L283`

### Group: `union`

### `layout.union.common-storage` — Union layout = max size / max align

A union's size = round_up(max(field sizes), align) and align = max(field aligns); all fields overlap at offset 0 (share field index 0). The physical body is {<largest-aligned field type>, [pad x i8]} so the aggregate's own align = max-align and raw size = max-size.

**Source evidence:** `src/compiler/mlir_gen_types.cpp#L351-L382`, `src/compiler/mlir_gen_types.cpp#L496-L506`

### `layout.union.max-of-fields` — Union layout is max-size at max-alignment

A struct marked as a union (`#[repr(...)]` union) is laid out as the maximum field size aligned to the maximum field alignment; all fields overlap at offset 0.

**Divergence (from Rust):** Logos union via #[repr]/union attribute; layout semantics match C/Rust unions.

**Source evidence:** `src/compiler/sema_decl.cpp#L1202-L1204`

### Group: `field`

### `layout.field.fat-ref-stored-inline` — Fat-reference struct fields are stored inline; read yields the slot address

A struct field of slice, closure, custom-DST-reference, fat-zone-mut, or relative-offset representation is stored inline within the struct (a 16-byte fat pair for the always-fat subset), and a field read yields the address of that inline storage (materializing a relative offset to an absolute pointer where applicable), not a by-value load. A tuple-typed field likewise yields its inline slot address. A trait-object field is excluded and read by value.

**Related:** `layout.index.inline-aggregate-element`

**Source evidence:** `src/compiler/mlir_gen_expr.cpp#L2815-L2823`, `src/compiler/mlir_gen_expr.cpp#L2805-L2814`

### `layout.field.inline-struct-store-by-value` — Inline (embedded) struct field is assigned by value

When a struct field's storage is an embedded aggregate (not a pointer slot) and the assigned r-value is materialized as a pointer to the source bytes, the assignment loads the aggregate value from the source pointer and stores it by value into the field; scalar fields instead receive the value with integer coercion to the field type.

**Source evidence:** `src/compiler/mlir_gen_stmt.cpp#L2760-L2771`, `src/compiler/mlir_gen_stmt.cpp#L2840-L2853`

### `layout.field.rel-ptr-self-relative-offset` — #[rel_ptr] field stores a self-relative i64 offset

A struct field marked #[rel_ptr] (RefRepr RelOffset) does not store an absolute pointer; on assignment the destination pointer value is lowered to a signed i64 offset relative to the field slot's own address (the slot is the anchor) and that offset is stored in the slot.

**Divergence (from Rust):** Logos addition: self-relative pointer field representation (no Rust analogue).

**Source evidence:** `src/compiler/mlir_gen_stmt.cpp#L2748-L2758`, `src/compiler/mlir_gen_stmt.cpp#L2828-L2838`

### `layout.field.scalar-loaded-by-value` — Scalar struct field read loads the value

A struct field whose type is not an inline-fat/aggregate kind is read by loading the value at the field's address.

**Source evidence:** `src/compiler/mlir_gen_expr.cpp#L2824-L2827`

### `type.field.placeholder-type-rejected` — Field types may not be inferred

Struct/datatype field types are item signatures: the inference placeholder `_` is rejected at field-type resolution (E0121).

**Source evidence:** `src/compiler/sema_collect.cpp#L4062-L4067`

### Group: `field-align`

### `layout.field-align.unsized-tail` — Alignment of unsized tail fields

Field alignment is min(byte_size,8) for sized fields (treating size-0 as align 1); an unsized `[T]` slice tail aligns to min(sizeof(T),8); an unsized `dyn` tail aligns to 8 (pointer width).

**Source evidence:** `src/compiler/sema_expr.cpp#L17666-L17675`

### Group: `field-index`

### `layout.field-index.element-stride-inline-footprint` — Indexing a pointer-typed field strides by the element's inline footprint

When indexing through a pointer-valued struct field (the stored pointer is loaded first), address computation strides by the element's inline slot footprint: the concrete struct's aggregate size for struct elements and 16 bytes for fat-pointer (dyn/closure/slice) elements, not the collapsed 8-byte pointer size.

**Source evidence:** `src/compiler/mlir_gen_stmt.cpp#L3169-L3183`

### Group: `place`

### `layout.place.element-slot-by-repr` — Place/element slot type preserves full footprint

An lvalue place slot (array/Vec element stride) uses the type's full storage footprint: the concrete aggregate type for inline Struct/ZonedStruct/Tuple, the full inline {disc,payload} footprint for a tagged Enum element, and the reference repr's storage type for any reference kind — a thin pointer is 8 bytes while every fat reference (dyn trait object {data,vtable}, closure {fn,env}, slice {ptr,len}, custom-DST ref {ptr,len}) is its 16-byte pair. A self-describing DST is a thin pointer (8 bytes).

**Source evidence:** `src/compiler/mlir_gen_expr.cpp#L1199-L1232`

### Group: `index`

### `layout.index.inline-aggregate-element` — Aggregate array/buffer elements stored inline; indexing yields slot address

Elements of arrays and contiguous buffers whose element type is a struct, a tagged enum, a closure, a slice, a trait object, or a tuple are stored inline (sizeof(elem) per slot); an index read strides by the inline footprint and yields the address of the element slot (the value of such kinds being represented by a pointer to its storage), not a by-value load of a pointer-width prefix. Scalar elements are loaded by value.

**Related:** `layout.tuple.inline-elements`

**Source evidence:** `src/compiler/mlir_gen_expr.cpp#L2980-L3037`, `src/compiler/mlir_gen_expr.cpp#L2999-L3003`, `src/compiler/mlir_gen_expr.cpp#L3035-L3037`

### Group: `enum`

### `coerce.enum.bare-literal-retype-to-param` — Incompletely-typed enum-literal argument retyped to parameter's enum spec

An enum literal passed as an argument with missing or unresolved type-args (e.g. bare `Opt::None`, partially-inferred `Opt::Some(3)`) is retyped to the parameter's concrete enum type, pinning the missing type-args. Retype fires only when the literal's already-known (non-error) type-args match the target's, so a genuine mismatch is still rejected.

**Source evidence:** `src/compiler/sema_impl.hpp#L500-L508`, `src/compiler/sema_impl.hpp#L582-L592`

### `coerce.enum.elementwise-typeargs-no-widen` — Same-named enums compatible by type-args, but concrete scalar args must match exactly

Two Enum types with equal enum_name, pkg_name, and non-empty equal type-arg arity are compatible iff for each arg pair: an unresolved placeholder (TypeVar/_/cfg-slot/Error) on either side unifies; otherwise two concrete scalar args (concrete integer excl. IntLit/Enum, F32/F64/Bool/Char) must be kind-identical (no by-value widening, layout is arg-width-specific); all other pairs use lenient compatibility.

**Source evidence:** `src/compiler/sema.cpp#L1859-L1901`

### `coerce.enum.incomplete-typeargs-retype` — Incomplete enum-literal type-args inferred from expected enum type

An enum literal whose value type has empty or unresolved type-args is retyped to the expected enum type when both are the same enum (`at.enum_name() == pt.enum_name()`), the expected type-args are all resolved, and each already-resolved arg of the literal is compatible with the corresponding expected arg; retyping recurses into nested payload enum-literals.

**Uncertainty:** Compatibility predicate `types_compatible` defined elsewhere; here only the gating conditions are observable.

**Source evidence:** `src/compiler/sema_impl.hpp#L593-L618`

### `coerce.enum.retype-nested-payload-recursive` — Enum-literal retype projects type-args through variant payloads recursively

When pinning an enum-literal expression to a concrete enum type, the concrete type-args are substituted into the matched variant's payload types, and each payload sub-expression that is itself an enum literal is recursively retyped. This prevents a nested literal (e.g. the inner `Option::None` of `Option::Some(Option::None)`) from staying a bare C-style enum while the outer slot is a heap pointer.

**Source evidence:** `src/compiler/sema_impl.hpp#L545-L581`

### `coerce.enum.to-integer-discriminant` — C-style enum coerces to integer (discriminant) but never to another enum

An Enum coerces to a non-enum integer kind (its discriminant). Enum to Enum via this rule is forbidden, and implicit int to Enum is forbidden (requires explicit cast/variant).

**Source evidence:** `src/compiler/sema.cpp#L1922-L1934`

### `layout.enum.aggregate-payload-inline-memcpy` — Aggregate enum payload fields are stored inline

A variant payload field whose type is an aggregate (struct, zoned struct, tuple, slice, closure, array, nested tagged enum, or trait object) is stored inline into the payload area by copying its full byte footprint; scalar payload fields are stored by value. A trait-object payload field is first coerced to a 16-byte fat (data,vtable) pair and stored inline, so it moves and drops with the enum.

**Source evidence:** `src/compiler/mlir_gen_expr.cpp#L604-L664`

### `layout.enum.align` — Tagged enum alignment

alignof(tagged enum) = max(4, payload_align), i.e. at least the 4-byte discriminant alignment and at least the widest variant payload's alignment.

**Related:** `layout.enum.tagged-repr`

**Source evidence:** `src/compiler/mlir_gen_impl.hpp#L65-L67`, `src/compiler/mlir_gen_impl.hpp#L72`

### `layout.enum.clike-disc-sized` — C-like enum is a backing-type-sized discriminant

A fieldless (C-like) enum has no payload; its layout is a single discriminant whose size = ceil(disc_bits/8) bytes (min 1) with equal alignment, where disc_bits is the enum's backing integer width.

**Source evidence:** `src/compiler/mlir_gen_types.cpp#L530-L533`, `src/compiler/mlir_gen_types.cpp#L66-L69`

### `layout.enum.discriminant-backing-type` — C-style enum discriminant uses its declared backing type, else i32

A C-style enum's discriminant is represented with its explicitly declared backing integer type (e.g. `enum Foo : u64 {}`); absent an explicit backing type the discriminant defaults to i32.

**Source evidence:** `src/compiler/mlir_gen_impl.hpp#L649-L665`

### `layout.enum.field-store-heap-promote` — Enum value stored into an enum-typed field is heap-promoted

An enum-typed field is represented by a single heap pointer slot (two-level convention). Assigning an enum r-value (held by pointer) into such a slot copies the enum's bytes into a freshly heap-allocated region of sizeof(enum) and stores that heap pointer, so the field does not dangle past the producing function's frame.

**Related:** `layout.enum.two-level-heap-ptr`

**Source evidence:** `src/compiler/mlir_gen_stmt.cpp#L2725-L2747`

### `layout.enum.nested-payload-is-pointer` — A nested payload-bearing enum is stored by pointer

When an enum variant's payload is itself a payload-bearing enum (e.g. Option<Option<T>>::Some carrying Option<T>), the nested enum lowers to a pointer in the outer payload rather than being inlined as a discriminant scalar.

**Uncertainty:** Inferred from the stub-registration comment; the precise inline-vs-pointer threshold lives in register_tagged_enum (another unit).

**Source evidence:** `src/compiler/mlir_gen.cpp#L93-L107`

### `layout.enum.niche-low-bit` — Low-bit niche enum packs tag into the payload word's low bit

A LowBit-niche enum packs a 64-bit word where the low bit distinguishes arms: low bit 0 → pointer arm (the aligned word IS the pointer, ptr_disc), low bit 1 → value arm. The value-arm payload is encoded as (v<<1)|1 and decoded as word>>1 (arithmetic shift if signed, logical otherwise), yielding val_disc. In raw mode (WAny Pod(u64)) both arms read the word verbatim with no decode.

**Divergence (from Rust):** Niche layout is a Logos-defined packing not specified by Rust.

**Source evidence:** `src/compiler/mlir_gen_expr.cpp#L4897-L4919`, `src/compiler/mlir_gen_expr.cpp#L4926-L4931`, `src/compiler/mlir_gen_expr.cpp#L4963-L4976`

### `layout.enum.niche-lowbit` — Low-bit niche for two data-arm enums

A two-data-arm enum may be packed into a single word with NO separate discriminant, disambiguated by the word's LOW BIT: the pointer arm (ptr_disc) holds a pointer to an align>=2 pointee (low bit always 0) stored raw; the value arm (val_disc) holds a value <=63 bits stored as (value<<1)|1 (low bit 1). Read: low bit 0 -> interpret word as pointer; low bit 1 -> value = word>>1, sign/zero-extended per the value arm's bit width and signedness.

**Related:** `layout.enum.niche-nullptr`, `layout.enum.niche-lowbit-raw`

**Source evidence:** `src/compiler/mlir_gen_impl.hpp#L98-L111`

### `layout.enum.niche-lowbit-encoding` — LowBit niche enum payload encoding

For an enum with a LowBit niche packed into a single word: the pointer arm stores the pointer's raw integer value (low bit 0, guaranteed by >=2 alignment); the value arm stores (v<<1)|1 after sign/zero extension to the word width. In RAW mode the producer-supplied value (low-bit already set) is stored verbatim without shifting. An empty payload stores 0.

**Divergence (from Rust):** A: niche-packing layout is Logos-defined; not a Rust-guaranteed representation.

**Source evidence:** `src/compiler/mlir_gen_expr.cpp#L570-L602`

### `layout.enum.niche-lowbit-int-widths` — Low-bit niche integer arm widths

Eligible low-bit-niche value arms are Bool(1), I8/U8(8), I16/U16(16), I24/U24(24), I32/U32(32), I56(56) packed shifted; I64/U64(64) qualify only as the raw zoned variant.

**Source evidence:** `src/compiler/mlir_gen_types.cpp#L799-L817`, `src/compiler/mlir_gen_types.cpp#L839`

### `layout.enum.niche-lowbit-ptr-int` — Low-bit niche packs pointer + small-int arms

A two single-field-arm enum where one arm is a pointer to an align>=2 pointee (low bit always 0) and the other arm is a <=56-bit integer stored shifted `(v<<1)|1` packs into one word; the discriminant is the low bit (0=ptr arm, 1=int arm).

**Divergence (from Rust):** Logos low-bit pointer-tagging niche; no direct Rust analog.

**Source evidence:** `src/compiler/mlir_gen_types.cpp#L796-L853`

### `layout.enum.niche-lowbit-raw` — Low-bit niche raw mode (zoned, 64-bit value arm)

For a #[zoned2] low-bit niche enum whose value arm is a full 64-bit word (e.g. Pod(u64)), the value-arm word is stored and read VERBATIM with no (v<<1)|1 shift, because the producer already encodes the low-bit-1 tag in the word. The discriminant is still the low bit: low-bit-0 -> reference (pointer) arm, otherwise -> the raw value arm.

**Related:** `layout.enum.niche-lowbit`, `layout.enum.zoned-self-relative`

**Source evidence:** `src/compiler/mlir_gen_impl.hpp#L112-L116`

### `layout.enum.niche-null-pointer` — Null-pointer niche enum has no discriminant word

A null-pointer-niche enum (Option<&T> shape) has no separate discriminant word: the payload (a non-null pointer) occupies offset 0; the `none` variant is encoded as a null pointer at offset 0, and the `some` variant's non-null payload pointer is itself the discriminant. Decoding: null → none_disc, non-null → some_disc.

**Divergence (from Rust):** Niche layout is an unspecified Rust optimization; here it is observable/normative for Option<&T>-shaped enums.

**Source evidence:** `src/compiler/mlir_gen_expr.cpp#L4920-L4921`, `src/compiler/mlir_gen_expr.cpp#L4932-L4941`, `src/compiler/mlir_gen_expr.cpp#L4977-L4988`

### `layout.enum.niche-nullptr` — Null-pointer niche optimization for Option<&T>-shape enums

A two-variant enum where one variant is fieldless and the other holds a single non-null pointer field is laid out with NO separate discriminant: it is just the 8-byte pointer word. Null (0) encodes the fieldless variant (none_disc); any non-null value encodes the pointer variant (some_disc). Hence sizeof(Option<&T>) == sizeof(&T) == 8.

**Related:** `layout.enum.niche-lowbit`

**Source evidence:** `src/compiler/mlir_gen_impl.hpp#L88-L98`, `src/compiler/mlir_gen_impl.hpp#L103-L107`

### `layout.enum.niche-nullptr-nonnull-wrapper` — Null-pointer niche for #[non_null] 8-byte wrapper

The null-pointer niche also applies when the single-field variant's field is a `#[non_null]` struct that is exactly an 8-byte pointer wrapper (Box/Rc/Arc-shape), whose invariant guarantees offset-0 is non-zero.

**Divergence (from Rust):** Logos `#[non_null]` attribute exposes Rust's NonNull niche to user wrapper types.

**Source evidence:** `src/compiler/mlir_gen_types.cpp#L769-L795`

### `layout.enum.niche-nullptr-ref` — Null-pointer niche for Option<&T>-shape

A two-variant enum with one fieldless variant and one single-field variant whose field is `&T`/`&mut T` is pointer-sized (8 bytes, no separate discriminant word): the discriminant is encoded as null vs non-null at offset 0, since references are guaranteed non-null.

**Example**

```logos
enum Option<&T> { None, Some(&T) }  // sizeof == 8
```

**Source evidence:** `src/compiler/mlir_gen_types.cpp#L761-L795`

### `layout.enum.niche-packed-no-disc` — Niche-packed enum carries no discriminant word

> ⚠ **ID COLLISION**: `layout.enum.niche-packed-no-disc` is emitted more than once below. Each block reflects a distinct source location with a complementary (not identical) statement; both are normative and surfaced verbatim. Resolve by reconciling the extractors that produced the shared id.

A niche-packed enum has no separate discriminant word: its representation is just the niche-bearing payload (a pointer), making it pointer-sized, with the variant tag encoded in a niche of the payload.

**Related:** `layout.enum.tagged-disc-plus-payload`

**Source evidence:** `src/compiler/mlir_gen.cpp#L157-L162`

### `layout.enum.niche-packed-no-disc` — Niche-packed enum drops the discriminant word

> ⚠ **ID COLLISION**: `layout.enum.niche-packed-no-disc` is emitted more than once below. Each block reflects a distinct source location with a complementary (not identical) statement; both are normative and surfaced verbatim. Resolve by reconciling the extractors that produced the shared id.

A niche-optimized enum is represented by its payload alone, with layout {payload_bytes, payload_align} and no separate discriminant word; the variant is distinguished by an in-payload niche value.

**Divergence (from Rust):** Rust-conformant in intent (niche optimization); see ref_enum_niche.

**Source evidence:** `src/compiler/mlir_gen_types.cpp#L522-L524`

### `layout.enum.niche-zoned-raw-word` — Zoned (#[zoned2]) raw 64-bit low-bit niche

In a `#[zoned2]` enum, the low-bit niche additionally accepts a raw `*T` pointer arm (trusting the zone allocator's >=2 alignment even for `*u8`) and a raw 64-bit `u64`/`i64` value arm stored without a `<<1` shift (the producer bakes the low-bit-1 tag into the word).

**Divergence (from Rust):** Logos zoned (Writ) niche; no Rust equivalent.

**Source evidence:** `src/compiler/mlir_gen_types.cpp#L811-L851`

### `layout.enum.payload-by-value` — Enum payload members stored by value

Enum variant payload members are laid out by value: each member contributes its full by-value layout (e.g. `Option<&[u8]>` payload = the 16-byte slice fat pair), unlike struct/tuple fields which may store a slice/closure/tuple as an 8-byte ptr.

**Source evidence:** `src/compiler/mlir_gen_types.cpp#L707-L720`

### `layout.enum.payload-size-fixpoint` — Enum payload size = max over variants, to fixpoint over nesting

An enum's payload_bytes/payload_align equal the maximum size/alignment over all its variants' payloads. Because a nested-enum payload's footprint depends on the nested enum's own payload size, sizes are computed to a fixpoint (monotonically growing) so layout is order-independent of registration.

**Related:** `layout.enum.tagged-disc-plus-payload`

**Source evidence:** `src/compiler/mlir_gen.cpp#L111-L140`

### `layout.enum.payload-size-is-max-variant` — Enum payload size/align = max over variants

A tagged enum's payload byte size is the maximum payload size over all variants, and its payload alignment is the maximum payload alignment over all variants.

**Source evidence:** `src/compiler/mlir_gen_types.cpp#L737-L754`

### `layout.enum.return-by-value-pair` — dyn/slice/zone-mut returned by 16B value

When returned by value, a trait object, slice, or zone-mut fat reference is materialized as its full 16-byte storage pair in the caller's frame; closure, custom-DST, thin, and rel-offset references are returned as their 8-byte value (pointer/word).

**Source evidence:** `src/compiler/mlir_gen_types.cpp#L643-L659`

### `layout.enum.tagged-disc-i32` — Tagged enum layout {i32 disc, payload}

A non-niche enum is laid out as {i32 discriminant at offset 0, payload at field 1}. The discriminant is read/written as an i32 at field 0; the payload is accessed at field 1.

**Source evidence:** `src/compiler/mlir_gen_expr.cpp#L4922-L4925`, `src/compiler/mlir_gen_expr.cpp#L4942-L4946`, `src/compiler/mlir_gen_expr.cpp#L4989-L4992`

### `layout.enum.tagged-disc-payload` — Tagged enum layout = {i32 disc, aligned payload}

A tagged (data-carrying) enum has layout = aggregate of a 4-byte/4-align discriminant followed by the payload blob {payload_bytes, payload_align}: the payload starts at round_up(4, payload_align) and total size rounds up to the enum align. The payload is sized from the concrete instantiation so nested generics (e.g. Option<Option<i64>>) carry their full inline footprint.

**Source evidence:** `src/compiler/mlir_gen_types.cpp#L406-L409`, `src/compiler/mlir_gen_types.cpp#L516-L529`

### `layout.enum.tagged-disc-plus-payload` — Tagged-enum layout: discriminant word + aligned payload blob

A payload-bearing enum lays out as { i32 discriminant, payload }, where payload is an aligned blob of size = ceil(max_variant_payload_size / payload_align) elements each of width payload_align bytes. The whole-enum alignment is max(4, payload_align); the payload is placed after the i32 with padding so an align-8 payload begins at offset 8, not 4.

**Related:** `layout.enum.payload-size-fixpoint`, `layout.enum.niche-packed-no-disc`

**Source evidence:** `src/compiler/mlir_gen.cpp#L141-L163`

### `layout.enum.tagged-repr` — Tagged enum layout = { discriminant, payload blob }

A tagged enum is laid out as a struct { i32 discriminant, payload-blob } where the payload-blob is sized to the widest variant's payload bytes and aligned to payload_align = max over all variants of their payload alignment. The blob is placed after the discriminant with padding so an aligned (i64/ptr/align-8) payload lands on an aligned offset.

**Related:** `layout.enum.align`

**Source evidence:** `src/compiler/mlir_gen_impl.hpp#L64-L79`

### `layout.enum.unit-variant-field-omitted` — Unit `()` payload field omitted

A `()` (Void) payload field of an enum variant contributes no field and no bytes to the variant payload.

**Source evidence:** `src/compiler/mlir_gen_types.cpp#L716-L717`, `src/compiler/mlir_gen_types.cpp#L742`

### `layout.enum.value-repr-inline` — Tagged enum value is inline {disc, payload} storage, not heap

A tagged-enum value (with or without payload) is stored inline as a {discriminant, payload} aggregate by value (the address is a one-level &Enum); it is not heap-allocated. Recursive-by-value enums are rejected, making inline storage sound. A C-style enum with no payload is just its discriminant, sized by the enum's backing type.

**Source evidence:** `src/compiler/mlir_gen_expr.cpp#L529-L547`, `src/compiler/mlir_gen_expr.cpp#L561-L568`

### `layout.enum.variant-payload-struct-layout` — Variant payload laid out as a struct

A variant's payload is laid out exactly like a struct/tuple of its fields, including inter-field alignment padding; a multi-field variant's payload size is the aligned aggregate, not the naive sum of field sizes (e.g. `Cons{head:i32, tail:*const List}` = 16, not 12).

**Source evidence:** `src/compiler/mlir_gen_types.cpp#L702-L720`

### `layout.enum.zoned-niche-self-relative` — Zoned niche enum stores Ref arm self-relative

A #[zoned2] niche enum's at-rest 8-byte word encodes: r==0 → null; r&1==1 → Pod (position-independent, copied raw); else Ref → self-relative offset (anchor = slot address). Materialize: Ref → absolute = slot + r (null/Pod identity). Lower: Ref → delta = val − slot (null/Pod identity).

**Divergence (from Rust):** Logos-only zoned representation; no Rust analogue.

**Uncertainty:** Exact bit-encoding of Pod vs Ref inferred from comments and the AND/shift sequence.

**Source evidence:** `src/compiler/mlir_gen_expr.cpp#L5034-L5074`, `src/compiler/mlir_gen_expr.cpp#L5076-L5081`

### `layout.enum.zoned-self-relative` — #[zoned2] enum reference arm stored self-relative at rest

For a #[zoned2] niche enum, the reference (low-bit-0) arm is stored SELF-RELATIVE at rest and is absolute as a value; conversion between the two representations occurs on materialize (load) and lower (store). Only meaningful together with a low-bit niche.

**Related:** `layout.enum.niche-lowbit-raw`

**Source evidence:** `src/compiler/mlir_gen_impl.hpp#L81-L86`

### `type.enum.backing-integer` — enum backing type must be integer

An explicit enum backing type `enum Foo : T { … }` must be an integer kind; a non-integer T is rejected.

**Example**

```logos
enum E : u64 { A }
```

**Source evidence:** `src/compiler/sema_collect.cpp#L1917-L1925`

### `type.enum.unresolved-when-fewer-args-or-nested-typevar` — Enum/struct type is unresolved if under-applied or nests an unresolved type

A type is treated as incompletely resolved when it is `Error` or a type-variable, when an enum/struct carries fewer type-args than its declared params (notably zero, e.g. a bare `Option`), or when any nested type-arg, tuple element, or reference pointee is itself unresolved. The check is recursive, not shallow.

**Source evidence:** `src/compiler/sema_impl.hpp#L509-L544`

### Group: `dyn`

### `coerce.dyn.arg-to-trait-object` — Implicit unsize coercion of a concrete argument to a `dyn Trait` formal

When a formal is a trait object (bare or behind `&`/`&mut`) and the argument is not already compatible, the argument coerces by: (a) CoerceUnsized of a smart-pointer/wrapper struct (`Rc<A>` → `Rc<dyn Tr>`) rebuilt by unsizing the inner field; or (b) cast to the dyn type when the argument's (ref) type satisfies the target trait object.

**Source evidence:** `src/compiler/sema_expr.cpp#L12996-L13014`

### `coerce.dyn.upcast-to-supertrait` — Implicit `dyn Sub` → `dyn Super` upcast

An argument of trait-object type `dyn Sub` (bare or behind `&`/`&mut`) implicitly coerces (via cast) to a distinct formal trait-object type `dyn Super` iff Super is a transitive supertrait of Sub (Sub != Super). Identical dyn types do not coerce.

**Source evidence:** `src/compiler/sema_expr.cpp#L12958-L12993`

### `layout.dyn.box-dyn-collapses-to-trait-object` — Box<dyn Trait> has the same repr as &dyn, differing only by ownership

`Box<dyn Trait>` is not a Box<TraitObject> struct; it is an owning bare trait object with the identical 16-byte {data, vtable} fat-pair representation as `&dyn Trait`. The two differ only in ownership: dropping an owning trait object calls vtable[0] (drop_in_place) then deallocates `data`.

**Related:** `layout.dyn.fat-pair-16-byte`, `intrinsic.drop.owning-dyn-handle`

**Source evidence:** `src/compiler/mlir_gen_impl.hpp#L1022-L1027`

### `layout.dyn.data-vtable-pair` — Trait object fat pair = {data,vtable}

A trait object value (`dyn Tr`) is the pair {data_ptr, vtable_ptr}; a `&dyn` value is a pointer to this 16-byte storage.

**Source evidence:** `src/compiler/mlir_gen_types.cpp#L949-L955`

### `layout.dyn.fat-pair-16-byte` — Trait object is a 16-byte {data, vtable} fat pair

A trait object (`dyn Trait`) has value representation as a 16-byte fat pair {data_ptr, vtable_ptr}. `&dyn`/`&mut dyn` are this value-fat-pair on the stack; an owning trait object (Box<dyn>, *const/*mut dyn) is held via an 8-byte heap handle to such a 16-byte pair.

**Related:** `layout.dyn.box-dyn-collapses-to-trait-object`

**Source evidence:** `src/compiler/mlir_gen_impl.hpp#L954-L955`, `src/compiler/mlir_gen_impl.hpp#L976-L985`

### `layout.dyn.fat-pair-data-vtable` — A trait-object handle is a {data, vtable} fat pair

A trait-object value is a two-pointer fat pair: field 0 is the data pointer, field 1 is the vtable pointer; an owning smart-pointer `dyn` stores this pair inline.

**Source evidence:** `src/compiler/mlir_gen_stmt.cpp#L520-L524`, `src/compiler/mlir_gen_stmt.cpp#L555-L573`

### `layout.dyn.fat-pointer-data-vtable-pair` — dyn trait object is a 16-byte {data, vtable} fat pair by value

`&dyn Trait`, `*dyn Trait`, and `Box<dyn Trait>` share a uniform 16-byte fat representation: a `{data_ptr, vtable_ptr}` pair stored inline. `data_ptr` is the concrete value's address (heap concrete for an owning `Box<dyn>`). The pair travels by value; escape consumers copy the 16 bytes into their own inline storage rather than holding a heap handle.

**Divergence (from Rust):** B2/B3: fat-pointer model for owned dyn; Box<dyn> is the owning trait object.

**Source evidence:** `src/compiler/mlir_gen_dyn.cpp#L1204-L1234`, `src/compiler/mlir_gen_dyn.cpp#L1264-L1270`

### `layout.dyn.fat-pointer-pair` — Trait-object references are a 16-byte {data,vtable} fat pair

A bare dyn / &dyn / &mut dyn trait object has a 16-byte {data_ptr, vtable_ptr} fat-pointer representation and is returned by value as that pair. A reference to such a reference (Ref/MutRef<TraitObject>) or a raw *const/*mut dyn remains a thin 8-byte pointer.

**Source evidence:** `src/compiler/mlir_gen_impl.hpp#L627-L638`, `src/compiler/mlir_gen_impl.hpp#L786`

### `layout.dyn.fat-pointer-two-word` — Trait-object value is a two-word {data,vtable} fat pointer

A trait-object (`dyn Trait`) value is a two-word structure {field 0 = data_ptr, field 1 = vtable_ptr}; a `&dyn`/`dyn` value is itself a pointer to this 16-byte storage (mirroring a slice value), while a struct-VALUE fat pair is spilled to storage before its fields are read.

**Source evidence:** `src/compiler/mlir_gen_dyn.cpp#L1523`, `src/compiler/mlir_gen_dyn.cpp#L1543-L1565`

### `layout.dyn.uniform-fat-pair` — Every dyn value is a 16-byte {data,vtable} pair

Every trait-object value (`&dyn`, `*dyn`, `Box<dyn>`) is a 16-byte {data, vtable} pair stored inline, and a `*const/*mut dyn` always points at such a 16-byte slot; dereferencing such a raw dyn pointer is therefore a no-op reinterpret (the slot pointer is the dyn value).

**Source evidence:** `src/compiler/mlir_gen_expr.cpp#L1749-L1760`, `src/compiler/mlir_gen_expr.cpp#L1922-L1941`

### `layout.dyn.vtable-header-drop-size-align` — dyn-trait vtable layout: [drop_in_place, size, align, methods..., supers...]

Every `dyn Trait` vtable for a concrete type T is a homogeneous pointer array laid out as: slot 0 = drop_in_place(T) glue, slot 1 = size_of(T), slot 2 = align_of(T), slots 3..3+M = the M trait methods in supertrait-closure slot order, then one slot per transitive upcast supertrait (in upcast order) holding that supertrait's vtable pointer for T.

**Source evidence:** `src/compiler/mlir_gen_dyn.cpp#L1135-L1161`, `src/compiler/mlir_gen_dyn.cpp#L976-L998`

### `layout.dyn.vtable-slot-order-supertrait-closure` — vtable method slots follow full supertrait closure order

Vtable method slots are ordered by the trait's full supertrait-closure method order (supertrait methods occupy real, dispatchable slots so they are callable through `&dyn Sub`); a trait with no supertraits uses its own declared method order.

**Source evidence:** `src/compiler/mlir_gen_dyn.cpp#L826-L833`, `src/compiler/mlir_gen_dyn.cpp#L876-L882`

### `type.dyn.auto-trait-and-lifetime-bounds` — dyn auto-trait and lifetime bounds

In `dyn Trait<T,...> + Send + Sync + 'a`, type-args drive the TraitObject's type_args; `+ Send`/`+ Sync` set marker bits on the object; lifetime bounds (`+ 'a`, LIFETIME_PARAM) are recorded but not enforced and are excluded from the trait dispatch identity.

**Source evidence:** `src/compiler/sema.cpp#L5966-L5998`, `src/compiler/sema.cpp#L6013-L6018`

### `type.dyn.fn-family-is-closure` — dyn Fn/FnMut/FnOnce resolves to Closure

`dyn Fn(P...) -> R`, `dyn FnMut(...)`, `dyn FnOnce(...)` resolve directly to the Closure type {fn_ptr, env_ptr}; there is no distinct Fn-trait-object vtable layer.

**Divergence (from Rust):** A10

**Source evidence:** `src/compiler/sema.cpp#L5928-L5952`

### `type.dyn.object-safety-required` — Forming &dyn Trait requires object safety

Forming a fat `&dyn Trait` (non-unsized-ok context) requires Trait to be object-safe (dyn-compatible); a non-object-safe trait is rejected at type resolution.

**Source evidence:** `src/compiler/sema.cpp#L6009-L6012`

### `type.dyn.trait-object` — dyn trait-object type

`dyn Trait`, `&dyn Trait`, `&mut dyn Trait` (optionally lifetime-annotated and HRTB-quantified `for<'a>`) form trait objects (fat pointer: data + vtable). Type args use `<...>`; Fn-family `dyn Fn(args)[-> R]` puts args in PARAMS/return in RET_TYPE. Trailing `+ Ident` (auto-trait, e.g. Send/Sync) and `+ 'lt` (lifetime) bounds are accepted; auto-trait bounds are enforced at unsize coercion, lifetime bounds are recorded but not enforced.

**Example**

```logos
&dyn Display + Send
Box<dyn for<'a> Fn(&'a i32) -> i32>
```

**Source evidence:** `tools/peg_gen/grammars/logos.peg#L1496-L1616`

### `type.dyn.unknown-trait-error` — dyn over an unknown trait is an error

`dyn Trait` where Trait is not a registered trait (and not an Fn-family name) is a hard error (`unknown trait '...' in &dyn type`).

**Uncertainty:** Bare-name lookup: a package-local trait shadowed by a prelude trait of the same name resolves to the prelude trait (known gap, dyn-local-trait-shadowing).

**Source evidence:** `src/compiler/sema.cpp#L5964-L5965`

### `type.dyn.unsized-bound-bits-preserved` — dyn Trait + Send/Sync auto-bounds preserved and folded into identity

The bare `dyn Trait` type may carry `+ Send` and/or `+ Sync` auto-trait bounds (bit 8 = Send, bit 9 = Sync); these bits are preserved through type construction and folded into type identity so e.g. `Box<dyn T + Send>` interns distinctly from `Box<dyn T>` and the unsize coercion can enforce the bound.

**Related:** `type.traitobject.owning-kind-distinct`

**Source evidence:** `src/compiler/sema_impl.hpp#L658-L678`, `src/compiler/sema_impl.hpp#L819-L820`

### `type.dyn.unsized-vs-fat` — Bare dyn unsized vs fat trait object

In an unsized-ok context (turbofish for `T: ?Sized`), bare `dyn Trait` resolves to the unsized-dyn form; otherwise it resolves to the fat-pointer TraitObject.

**Source evidence:** `src/compiler/sema.cpp#L5999-L6018`

### Group: `vtable`

### `layout.vtable.drop-size-align-prefix` — Vtable layout: [drop_in_place, size, align, method0, method1, ...]

A trait-object vtable lays out the drop glue at slot 0, size_of(T) at slot 1, align_of(T) at slot 2, followed by the trait methods in declaration order; a trait method's declared vtable index i therefore resolves to physical vtable slot i+3.

**Source evidence:** `src/compiler/mlir_gen_dyn.cpp#L1567-L1574`

### Group: `ref`

### `coerce.ref.permission-and-pointee` — Reference/pointer coercions: pointee compatibility, permission-dropping only

Reference and raw-pointer coercions require compatible pointees: &/&mut to *const/*mut, *T to &/&mut, &mut T to &T (exclusive to shared), &T to &T and &mut T to &mut T, and *mut T to *const T (dropping write permission). Permission is only ever dropped, never gained.

**Source evidence:** `src/compiler/sema.cpp#L2020-L2061`

### `coerce.ref.unsized-dyn-canonicalizes-to-traitobject` — &UnsizedDyn<Trait> canonicalizes to TraitObject<Trait>

Forming a reference to an unsized-dyn pointee, `&UnsizedDyn<Trait<args...>>`, canonicalizes to the trait-object fat-pointer type `TraitObject<Trait<args...>>`, preserving the trait's type-args. Ensures `&self` and `other: &Self` for an impl-on-dyn mangle identically.

**Source evidence:** `src/compiler/sema_impl.hpp#L243-L246`

### `coerce.ref.unsized-slice-canonicalizes-to-slice` — &UnsizedSlice<T> canonicalizes to Slice<T>

Forming a reference to an unsized-slice pointee, `&UnsizedSlice<T>`, canonicalizes to the fat-pointer slice type `Slice<T>` (= `&[T]`). The reference layer is collapsed into the slice's own fat pointer.

**Uncertainty:** Inferred from make_ref special-casing; the canonical syntactic form is also enforced at resolve_type.

**Source evidence:** `src/compiler/sema_impl.hpp#L233-L242`

### `coerce.ref.widen-int-literal-temp-pointee` — &<int-literal> sizes its temporary to the expected pointee width

When `&L` (an address-of an integer literal materialized as a temporary) is passed where `&T`/`&mut T` is expected and T is a wider integer type that L can widen to or fits, the inner literal is cast to T before being addressed, so the temporary's storage slot is sized to T (preventing a narrow temp read as the wider pointee).

**Source evidence:** `src/compiler/sema_impl.hpp#L4365-L4400`

### `layout.ref.fat-lower-memcpy-16` — Lowering a fat reference copies the 16-byte pair

Lowering a fat reference (FatDyn/FatSlice/FatCustomDst) to its storage slot copies the full 16-byte {data, meta} pair; thin/non-ref reprs store the single pointer value.

**Source evidence:** `src/compiler/mlir_gen_expr.cpp#L5102-L5122`

### `layout.ref.fat-pointer-sixteen-bytes` — Fat reference layout

Every fat reference — slice `&[T]`/`str`, trait object `&dyn Tr`, closure value, custom-DST ref, and zone-mut ref — is a two-word pair of {size=16, align=8}.

**Source evidence:** `src/compiler/mlir_gen_types.cpp#L617-L621`, `src/compiler/mlir_gen_types.cpp#L632-L636`

### `layout.ref.rel-offset-eight-bytes` — Relative-offset reference layout

A relative-offset (self-relative) reference is stored as a single i64 offset word: {size=8, align=8}.

**Divergence (from Rust):** Logos self-relative pointers (zoned/Writ); no Rust equivalent.

**Source evidence:** `src/compiler/mlir_gen_types.cpp#L622`, `src/compiler/mlir_gen_types.cpp#L637`

### `layout.ref.relptr-self-relative` — RelOffset reference stores a self-relative byte offset

A RelOffset reference stores at its slot an i64 byte offset = target_addr − slot_addr, where the slot's own address is the anchor. Materializing computes target = slot + offset (GEP by bytes); a null target is encoded as offset = −slot and materializes back to address 0.

**Source evidence:** `src/compiler/mlir_gen_expr.cpp#L5088-L5096`, `src/compiler/mlir_gen_expr.cpp#L5107-L5116`

### `layout.ref.repr-kinds` — Reference representation kinds

Every reference type has a representation kind: ThinPtr / NotARef hold a single data pointer (8B); FatDyn holds a {data, vtable} pair (16B); FatSlice / FatCustomDst hold a {data, len:i64} pair (16B). For all fat reprs field 0 (offset 0) is the data pointer and field 1 is the metadata (vtable for dyn, length otherwise).

**Source evidence:** `src/compiler/mlir_gen_expr.cpp#L4863-L4886`, `src/compiler/mlir_gen_expr.cpp#L5244-L5261`

### `layout.ref.self-relative-offset` — Self-relative (writ / rel_ptr) pointers store a byte offset

A self-relative pointer (the writ / #[rel_ptr] zoned pointer) is stored as an i64 byte offset from the slot's own address; its compute/absolute form is slot_address + load_i64(slot), and lowering stores (target_address - slot_address). A thin-pointer field inside a #[zoned2] struct stores self-relative.

**Divergence (from Rust):** Logos addition: self-relative zoned pointers, no Rust analogue.

**Source evidence:** `src/compiler/mlir_gen_impl.hpp#L792-L795`, `src/compiler/mlir_gen_impl.hpp#L799-L803`

### `layout.ref.thin-pointer` — Plain references and fn pointers are thin 8-byte pointers

A *T, &T, &mut T, or function pointer to a Sized pointee has a thin 8-byte pointer representation.

**Source evidence:** `src/compiler/mlir_gen_impl.hpp#L784`

### `layout.ref.thin-pointer-eight-bytes` — Thin reference layout

A thin reference (plain `&T`/`&mut T`/`*T`/fn-ptr to a Sized pointee) occupies one machine pointer: {size=8, align=8}.

**Source evidence:** `src/compiler/mlir_gen_types.cpp#L616`, `src/compiler/mlir_gen_types.cpp#L631`

### `layout.ref.zone-mut-fat-pair` — &mut T to a zone_mut type carries its allocator as a fat reference

A &mut T where T is a #[zone_mut] type has a 16-byte {data, zone=*mut Allocator} fat representation, returned by value; the allocator rides the &mut so grow-style methods reach it from &mut self.

**Divergence (from Rust):** Logos addition: zone/allocator-carrying mutable reference, no Rust analogue.

**Source evidence:** `src/compiler/mlir_gen_impl.hpp#L789-L791`, `src/compiler/mlir_gen_impl.hpp#L507-L508`

### `type.ref.borrow` — Reference types

`&T`, `&mut T`, `&'a T`, `&'a mut T` are safe borrow-checked reference types. `&&T` / `&&mut T` (no whitespace, tokenized as AND) denote double-references; arbitrary-depth `& & … T` stacks are accepted at type position.

**Source evidence:** `tools/peg_gen/grammars/logos.peg#L1631-L1655`

### `type.ref.canonicalize-unsized-pointee` — Reference to bare unsized pointee folds to the fat-pointer form

`&T`/`&mut T` whose immediate pointee is bare `[U]` folds to `Slice<U>` (mut-tracked); bare `dyn Tr` folds to TraitObject; an effective-DST struct folds to DstRef. `&str` (`str` pointee) is treated as `&[u8]` and folds to `Slice<u8>`.

**Related:** `type.ptr.dyn-is-fat`, `type.slice.str-is-byte-slice`

**Source evidence:** `src/compiler/sema.cpp#L5744-L5847`

### `type.ref.dotted-path` — Fully-qualified non-generic type path

A fully-qualified non-generic type in type position is written `pkg.path.Type` (dotted); the last path segment is the type. Matched before bare-IDENT alternatives so the whole dotted form is claimed. The generic dotted form `pkg.path.Type<A>` is not supported (use a `use` import + short name).

**Divergence (from Rust):** Logos path model: `.` for package/module path, `::` for items.

**Source evidence:** `tools/peg_gen/grammars/logos.peg#L1805-L1813`

### `type.ref.double-ref` — Double-reference types

`&&T` resolves to `&(&T)` and `&&mut T` to `&(&mut T)` (lexer collapses `&&`).

**Source evidence:** `src/compiler/sema.cpp#L5849-L5861`

### `type.ref.double-ref-nesting` — Double reference types desugar to nested references

`&&T` resolves to a nested reference `&(&T)`; `&&mut T` resolves to `&(&mut T)`.

**Source evidence:** `tools/peg_gen/grammars/logos.peg#L286-L287`

### `type.ref.metavar` — Metavariable type reference

`#Ident` and `#(expr)` are type references whose name is supplied by a metaprogram variable/expression rather than a literal identifier.

**Divergence (from Rust):** Logos metaprogramming addition.

**Source evidence:** `tools/peg_gen/grammars/logos.peg#L1801-L1804`

### `type.ref.ordered-choice` — Type-reference ordered choice

A type reference resolves by ordered choice: antiquot, typeof, cfg-slot-assoc, cfg-slot, writ-array, writ-map, pointer, array, slice, tagged, dyn, reference, impl-Trait, unit, never, closure, fn-pointer, tuple, paren, qualified-assoc, assoc-type-ref, then simple type. Associated-type forms precede simple_type so `T::Item` matches before `T`.

**Source evidence:** `tools/peg_gen/grammars/logos.peg#L1421`

### `type.ref.unsized-pointee-gated` — Unsized reference pointees allowed only at the immediate position

A bare unsized pointee (`[T]`, `dyn`, `str`) is permitted directly under `&`/`&mut` but the unsized-ok relaxation does not leak into nested type-arg resolution (e.g. `dyn` inside `&Box<dyn>` is still subject to the Box Sized bound).

**Source evidence:** `src/compiler/sema.cpp#L5744-L5777`, `src/compiler/sema.cpp#L5810-L5822`

### Group: `fat-ptr`

### `layout.fat-ptr.sixteen-byte` — Fat pointers are 16 bytes, pointer-aligned

Slice (&[T], str), closure value, trait object (dyn), and custom-DST reference each have layout {size=16, align=8}: a two-word fat pair (data + metadata). The metadata word is length for slices/custom-DST and a vtable pointer for trait objects; closures pair {fn, env}.

**Source evidence:** `src/compiler/mlir_gen_types.cpp#L465-L467`, `src/compiler/mlir_gen_types.cpp#L294-L319`

### Group: `dst`

### `layout.dst.dyn-tail-ref-is-thin` — Ref to a struct with a dyn tail is a thin 8-byte pointer

A reference whose pointee struct has a `dyn`-tail (e.g. `&RcInner<dyn>`) is physically thin (single 8-byte pointer); the vtable lives in the heap object rather than in the reference, distinguishing it from the 16-byte {data,len} custom-DST fat reference.

**Related:** `layout.dst.slice-tail-ref-is-fat`

**Source evidence:** `src/compiler/mlir_gen_impl.hpp#L876-L882`

### `layout.dst.effective-dst-detection` — Effective-DST classification of a struct instance

A struct/zoned-struct type is an (effective) DST iff: it is declared unsized; or, after substituting its type-args into the template's LAST field, that field type is UnsizedSlice or UnsizedDyn; or the last field is the bare tail type-var bound to a borrow-owning TraitObject. A field reached only through a pointer is always sized (a self-referential struct is not a DST via its pointer tail).

**Source evidence:** `src/compiler/sema.cpp#L3740-L3791`

### `layout.dst.fat-when-slice-tail` — DstRef is fat only with a literal slice tail

A DstRef whose pointee struct's last field is a literal slice ([T] or unsized [T]) is genuinely fat (16B {data,len}); a DstRef with a dyn-tail or type-variable tail is physically thin (8B pointer).

**Source evidence:** `src/compiler/mlir_gen_expr.cpp#L4995-L5005`

### `layout.dst.owned-tail-needs-fat-dstref` — An owned dyn-tail drop only fires through a fat DST reference

A let-bound value initialized from a field read drops as an owned dyn tail only when the (substituted) receiver type is a fat custom-DST reference (DstRef) and the projected field is an unsized dyn (UnsizedDyn, or a borrow-owning TraitObject); a thin pointer/reference receiver (sized inner, genuine Arc<&dyn>) is NOT a DST tail and its drop is a no-op.

**Source evidence:** `src/compiler/mono_clone.cpp#L395-L414`

### `layout.dst.owning-box-same-as-borrow` — &CustomDST / Box<CustomDST> is a fat {data,len} pointer

A reference or raw pointer to a custom-DST struct is a fat pointer stored as {data_ptr, tail_len} with the same ABI as a slice; `&` vs `&mut`/`*mut` is distinguished for borrow-checking. An owning `Box<Foo>` custom-DST shares this fat layout but is move-only and droppable (drops tail elements and prefix fields, then frees the heap block).

**Related:** `layout.slice.owning-box-same-as-borrow`

**Source evidence:** `src/compiler/sema_impl.hpp#L679-L700`

### `layout.dst.prefix-field-offset` — Custom-DST prefix field offsets use sequential aligned layout

For a custom-DST struct (header fields + unsized tail), a named field's byte offset is computed by the same sequential aligned-layout walk as a normal struct (offset rounded up to min(size,8) before each field); the unsized tail field, when reached, yields its aligned offset and substituted type for fat-pair projection reusing the DstRef's carried metadata.

**Source evidence:** `src/compiler/mono_clone.cpp#L416-L441`

### `layout.dst.self-describing-ref-is-thin` — Ref to a #[self_describing] DST is a thin 8-byte pointer

A reference to a `#[self_describing]` DST is physically thin (8-byte pointer straight to the header); the tail length is recovered in-band from the pointee header rather than carried alongside the pointer.

**Divergence (from Rust):** Logos custom-DST extension (#[self_describing]); no Rust equivalent.

**Related:** `layout.dst.slice-tail-ref-is-fat`

**Source evidence:** `src/compiler/mlir_gen_impl.hpp#L884-L888`

### `layout.dst.self-describing-thin` — self_describing DST reference is thin

A DstRef whose pointee struct is #[self_describing] is physically THIN (8B pointer straight to the header); its tail length is not carried out-of-band but recovered in-band by calling SelfDescribing::dst_len(header_ptr). This contrasts with a plain [T]-tail DstRef which is an 8B pointer to a 16-byte {data,len} pair.

**Divergence (from Rust):** Logos-only self_describing DST model (Rust's DST metadata is always out-of-band).

**Source evidence:** `src/compiler/mlir_gen_expr.cpp#L5007-L5032`, `src/compiler/mlir_gen_expr.cpp#L5124-L5157`

### `layout.dst.slice-tail-ref-is-fat` — Custom-DST ref with [T] slice tail is a 16-byte {data,len} fat pointer

A reference to a custom-DST struct whose last field is a literal slice tail `[T]` (or unsized slice) is represented as a 16-byte fat pointer {data: ptr, len: i64}, the element count carried inline. The length is part of the reference value, not stored in the pointee.

**Related:** `layout.dst.dyn-tail-ref-is-thin`, `layout.dst.self-describing-ref-is-thin`

**Source evidence:** `src/compiler/mlir_gen_impl.hpp#L875-L882`

### `type.dst.effective-after-instantiation` — A struct is custom-DST directly or after generic instantiation

A struct type is effectively custom-DST if the template was declared DST, or if generic instantiation bound the template's `?Sized` last-field type-var to an unsized type; effective-DST status governs `&S` -> fat DstRef canonicalisation at borrow/pointer-resolve time.

**Source evidence:** `src/compiler/sema_impl.hpp#L794-L800`

### `type.dst.self-describing-borrow-is-fat` — Borrow of a #[self_describing] DST yields a fat DstRef

Borrowing (`&`/`&mut`) a `#[self_describing]` custom-DST struct produces the fat `DstRef` type matching its `&Foo` annotation; the fat length is materialized at codegen via `dst_len`.

**Source evidence:** `src/compiler/sema_impl.hpp#L805-L809`

### `type.dst.self-describing-fat-ref-requires-impl` — Self-describing DST borrowed as fat ref must impl SelfDescribing

Borrowing a `#[self_describing]` effective-DST struct as a fat reference `&S`/`&mut S` (which materializes by recovering the tail length via `dst_len`) requires the struct to `impl SelfDescribing`; otherwise it is an error. A self-describing DST used only via raw pointers/byte arithmetic is not subject to this requirement.

**Source evidence:** `src/compiler/sema.cpp#L3831-L3860`

### Group: `customdst`

### `layout.customdst.fat-pointer-pair` — Custom-DST references are a 16-byte {data,meta} fat pair

A reference to a custom DST (DstRef) has a 16-byte {data_ptr, meta} fat-pointer representation, except a #[self_describing] DST whose metadata is recovered in-band from the header pointer, which is carried as a thin pointer.

**Source evidence:** `src/compiler/mlir_gen_impl.hpp#L788`, `src/compiler/mlir_gen_impl.hpp#L799-L810`

### Group: `rel-ptr`

### `coerce.rel-ptr.pointer-compatibility` — #[rel_ptr] struct / GAT pointer ↔ raw pointer compatibility

A concrete `#[rel_ptr]` struct `RP<U>` is pointer-compatible with `*U`/`&U`/`&mut U` (pointee type-equal to its type-arg); a type-erased rel_ptr (no type-arg) is compatible only with a thin `*u8` pointer. An abstract GAT projection `Z::Ptr<U>` (assoc base a type-var) is compatible with a raw pointer iff its GAT arg equals the pointee. Compatibility is symmetric.

**Source evidence:** `src/compiler/sema.cpp#L3793-L3829`

### `layout.rel-ptr.self-relative-offset` — #[rel_ptr] field stored as self-relative i64 offset

A `#[rel_ptr]` field is stored as an 8-byte i64 byte-offset from the field's own address and materializes to an absolute thin pointer on load; it is opaque (no field access) but transparent to `*Pointee` at the value level.

**Source evidence:** `src/compiler/sema_impl.hpp#L2440-L2444`

### Group: `repr-transparent`

### `layout.repr-transparent.inherit-field-layout` — #[repr(transparent)] inherits the single field's layout

A single-field wrapper marked `#[repr(transparent)]` inherits its field's layout exactly (size, align, and niche). Other `#[repr(...)]` modes (`C`/`packed`/`align`) are parsed then rejected.

**Source evidence:** `src/compiler/sema_impl.hpp#L2488-L2497`

### `layout.repr-transparent.inherits-field` — #[repr(transparent)] inherits the single field's layout

A #[repr(transparent)] struct with exactly one field has layout identical to that field's layout (no added aggregate padding/alignment). Multi-field #[repr(transparent)] is rejected earlier (collect time), so the single-field invariant holds.

**Source evidence:** `src/compiler/mlir_gen_types.cpp#L490-L495`

### Group: `non-null`

### `layout.non-null.option-nullptr-niche` — #[non_null] enables Option NullPtr niche

A `#[non_null]` struct is a single 8-byte pointer wrapper whose pointer is guaranteed non-null (Box/Rc/Arc shape), letting `Option<ThisStruct>` use the NullPtr niche (None = null pointer, pointer-sized enum). It is an opt-in soundness contract asserted by the author.

**Source evidence:** `src/compiler/sema_impl.hpp#L2473-L2479`

### Group: `zone-mut`

### `layout.zone-mut.fat-mut-ref` — #[zone_mut] gives &mut T a zone-carrying fat ref

For a `#[zone_mut]` type, `&mut T` is a fat reference {data, zone=*mut Allocator} carrying its zone so grow methods can reach the allocator from `&mut self`; a read `&T` stays thin.

**Source evidence:** `src/compiler/sema_impl.hpp#L2454-L2458`

### Group: `zoned2`

### `layout.zoned2.all-thin-fields-self-relative` — #[zoned2] stores all thin-pointer fields self-relative

A `#[zoned2]` struct stores all of its thin-pointer fields as self-relative RelOffset i64 and materializes them to absolute pointers in compute; such a struct is non-movable (it cannot be stack-allocated because the offsets are anchored to the slot).

**Source evidence:** `src/compiler/sema_impl.hpp#L2459-L2464`, `src/compiler/sema_impl.hpp#L2585`

### Group: `aggregate-member`

### `layout.aggregate-member.indirect-fat-types` — Fat-typed aggregate members are stored as an 8-byte pointer

As a struct field, tuple element, or enum payload field: Slice/Closure/Tuple members are stored as an 8-byte pointer (not their by-value fat footprint); struct/enum/array/bare-dyn members are stored inline; an AnyVal member is stored as i32.

**Source evidence:** `src/compiler/mlir_gen_impl.hpp#L766-L771`

### Group: `assign`

### `layout.assign.aggregate-rvalue-byte-copy` — Struct/tuple r-value assignment into an element slot is a full-footprint byte copy

Assigning a struct-, zoned-struct-, or tuple-typed r-value (materialized by pointer) into an indexed element slot copies sizeof(type) bytes from the source to the destination, rather than storing a pointer.

**Source evidence:** `src/compiler/mlir_gen_stmt.cpp#L3056-L3076`, `src/compiler/mlir_gen_stmt.cpp#L3185-L3205`

### `layout.assign.fat-pointer-16-byte-copy` — Fat-pointer r-value assignment copies the full 16-byte pair

A closure, slice, or trait-object (dyn) r-value occupies a 16-byte two-word storage layout ({ptr,ptr} or {ptr,len}). Assigning such a value into an indexed element slot copies all 16 bytes, never an 8-byte single-word store, so both halves of the fat pointer are written.

**Related:** `layout.dst.fat-pointer-two-word`

**Source evidence:** `src/compiler/mlir_gen_stmt.cpp#L3077-L3091`, `src/compiler/mlir_gen_stmt.cpp#L3206-L3217`

### Group: `call`

### `coerce.call.aggregate-arg-by-pointer` — Aggregate / tagged-enum arguments passed by pointer

When a callee parameter has aggregate (struct) or tagged-enum representation (passed by pointer) and the argument arrives as a value, the value is placed in fresh storage and the pointer is passed; scalar arguments to scalar parameters are numerically coerced to the parameter type instead.

**Source evidence:** `src/compiler/mlir_gen_expr.cpp#L2483-L2505`

### `layout.call.aggregate-return-by-value` — Indirect calls return aggregates by value

For both closure and bare-function-pointer indirect calls, the call's return type matches the callee ABI: tuple/struct/enum results are returned by aggregate value rather than as a pointer; an aggregate result is materialized into stack storage (a fresh slot) so downstream code can treat it by-address.

**Source evidence:** `src/compiler/mlir_gen_expr.cpp#L4787-L4804`, `src/compiler/mlir_gen_expr.cpp#L4831-L4853`

### Group: `dstref`

### `layout.dstref.fat-only-with-slice-tail` — Custom-DST reference is a 16-byte fat slot only with a literal slice tail

A custom-DST reference (&Foo/&mut Foo where Foo has a tail) is a 16-byte {data,len} fat pointer ONLY when the pointee has a literal `[T]` slice tail (len carried inline) and is not #[self_describing]. A `dyn`-tail DST ref or a #[self_describing] DST is physically THIN (8-byte pointer; tail length recovered in-band, e.g. sizeof(Rc<dyn>)==8) and is not copied as a 16-byte fat slot.

**Divergence (from Rust):** Logos custom-DST representation split (slice-tail fat vs dyn-tail/self-describing thin).

**Source evidence:** `src/compiler/mlir_gen_stmt.cpp#L1330-L1351`

### Group: `fnptr`

### `layout.fnptr.bare-call-no-env` — Bare function-pointer call passes no environment

A bare function-pointer value `fn_ptr` is a thin pointer; calling it `fn_ptr(a1,a2,...)` passes only the user arguments (no implicit environment), distinguishing it from a closure call.

**Related:** `layout.closure.fn-env-pair`

**Source evidence:** `src/compiler/mlir_gen_expr.cpp#L4807-L4846`

### `type.fnptr.methods-emit-non-generic` — Function-pointer impl methods emit once, non-generic

A function-pointer impl target (`$fnptr$...`) is type-erased to a uniform pointer, so its impl methods are emitted once as non-generic functions; impl-level type parameters are cleared so no never-instantiated generic template is produced.

**Source evidence:** `src/compiler/sema_decl.cpp#L2183-L2187`

### Group: `litstr`

### `layout.litstr.len-excludes-null` — String literal fat-pointer length excludes the NUL terminator

A string literal is represented as a fat pointer {ptr, len}: the backing storage is the decoded content plus a trailing NUL byte, but len equals the content length in bytes, excluding the NUL terminator.

**Source evidence:** `src/compiler/mlir_gen_expr.cpp#L404-L431`

### Group: `pinned`

### `layout.pinned.non-movable-type` — #[pinned] type is location-anchored and non-movable

A `#[pinned]` type's bits are anchored to its storage slot: it must not be moved by value, is accessed in place, and is materialized to a movable value form only explicitly. It is non-movable itself (unlike `#[rel_ptr]`, whose value form is the resolved absolute pointer).

**Divergence (from Rust):** A8

**Source evidence:** `src/compiler/sema_impl.hpp#L2446-L2453`

### Group: `pointer`

### `layout.pointer.target-64-bit` — Pointer width is 64-bit; usize/isize follow it

The target pointer width is 64 bits. `usize` has underlying integer kind u64 and `isize` has i64 (would be u32/i32 on a 32-bit target).

**Source evidence:** `src/compiler/sema_impl.hpp#L208-L220`

### Group: `type-code`

### `layout.type-code.auto-hash-assign` — Auto type-code from name hash for concrete zoned types

A concrete (non-generic) zoned struct/datatype with TYPE_CODE==0 receives an auto-assigned code from the 56-bit hash of its canonical name pkg::Name; codes < 128 are bumped by +128 to stay outside the reserved inline-AnyVal range 1..127. Generic templates are hashed at instantiation time.

**Source evidence:** `src/compiler/sema.cpp#L7649-L7657`, `src/compiler/sema.cpp#L7770-L7781`

### Group: `zero-size`

### `layout.zero-size.void-never` — Unit and never are zero-sized

The unit type () (Void) and the never type ! (Never) have layout {size=0, align=1}. As a struct/enum field each occupies a zero-size slot ([i8;0]) so sibling field offsets are unaffected; in value/result position they yield no SSA value.

**Source evidence:** `src/compiler/mlir_gen_types.cpp#L453`, `src/compiler/mlir_gen_types.cpp#L320-L340`, `src/compiler/mlir_gen_types.cpp#L40-L43`

### Group: `zone-mut-ref`

### `layout.zone-mut-ref.fat-data-zone` — &mut T of a zone_mut type is a fat {data, zone} pair

A mutable reference `&mut T` where `T` is a `#[zone_mut]` (FatZoneMut) type is a two-word fat pointer carrying {data, zone}; the address of the referent object is the data half. Field and method access on such a receiver descend through the data pointer, not the fat-pair storage.

**Divergence (from Rust):** Logos zone model; no Rust analogue

**Source evidence:** `src/compiler/mlir_gen.cpp#L667-L680`

## Coercions & conversions

### Group: `arg`

### `coerce.arg.canonical-coercion-order` — Canonical argument→parameter coercion pipeline

Implicit argument-to-parameter coercions are applied in one fixed canonical order: bare-enum retype, closure-literal→fn-pointer, array-ref↔slice unsize, dyn supertrait upcast, &Concrete→&dyn-Trait unsize, &mut auto-reborrow, then integer widening. The standard set enables all of these; a minimal set enables only auto-reborrow and integer widening.

**Source evidence:** `src/compiler/sema_impl.hpp#L450-L487`

### `coerce.arg.method-canonical-coercions` — Method arguments coerced in canonical order

Each method argument is coerced toward its substituted parameter type in canonical order applying: unsize-to-dyn, implicit reborrow, and integer widening. After coercion, a non-Error/non-TypeVar/non-AssocType param type that is incompatible with the argument type is a type-mismatch error.

**Source evidence:** `src/compiler/sema_expr.cpp#L7525-L7540`

### Group: `method-arg`

### `coerce.method-arg.pipeline` — Method argument coercions

Each method argument is coerced to its (substituted) parameter type via the canonical coercion pipeline supporting closure-to-fn-ptr, `&Concrete`-to-`&dyn Trait` unsize, implicit reborrow, and integer widening, with widening applied last.

**Source evidence:** `src/compiler/sema_expr.cpp#L8873-L8887`

### Group: `param`

### `coerce.param.array-by-pointer` — Array parameters are passed by pointer

A parameter whose type is an array [T; N] is passed by pointer (to the array storage), not by value.

**Source evidence:** `src/compiler/mlir_gen_fn.cpp#L102-L104`

### `type.param.unit-type-forbidden` — Unit-typed parameters forbidden

A function parameter may not have the unit type `()`; a unit-typed parameter carries no information and is ill-formed.

**Divergence (from Rust):** Logos restriction: Rust permits `()`-typed parameters.

**Source evidence:** `src/compiler/sema_decl.cpp#L303-L308`

### Group: `return`

### `coerce.return.aggregate-by-value` — Aggregate return types are returned by value

A function whose return type is a tuple, struct, ZonedStruct, or payload-carrying (tagged) enum returns the aggregate by value (as a value of the aggregate's storage type), never as a pointer to function-local storage; this guarantees the returned value outlives the callee frame.

**Source evidence:** `src/compiler/mlir_gen_fn.cpp#L116-L137`, `src/compiler/mlir_gen_fn.cpp#L68-L82`

### `coerce.return.c-enum-as-integer` — Payload-free enum returns as i32

A function returning a C-style enum (an enum with no payload variants, so no tagged-enum layout) returns its discriminant as a 32-bit integer rather than an aggregate.

**Source evidence:** `src/compiler/mlir_gen_fn.cpp#L129-L137`, `src/compiler/mlir_gen_fn.cpp#L78-L82`

### `coerce.return.closure-to-fnptr` — Non-capturing closure coerces to fn-ptr at return

A non-capturing closure literal returned where a fn-value type is expected coerces to that fn-pointer type (same coercion as let-annotation and call-arg sites).

**Source evidence:** `src/compiler/sema_stmt.cpp#L2819-L2825`

### `coerce.return.enum-discriminant-to-aggregate` — returning an enum discriminant where an aggregate is expected wraps it

When the function return type is an aggregate (struct/enum representation) but the returned value is a scalar enum discriminant, the discriminant is materialized into the aggregate's discriminant slot and the whole aggregate is returned by value.

**Source evidence:** `src/compiler/mlir_gen_stmt.cpp#L2109-L2143`

### `coerce.return.float-lit` — Float-literal return retyped to return type

A float-literal return value is retyped to the concrete f32/f64 return type when the return type is a float; otherwise it defaults to f64.

**Source evidence:** `src/compiler/sema_stmt.cpp#L2852-L2857`

### `coerce.return.numeric-to-ret-type` — scalar return value is coerced to the declared numeric return type

A scalar returned value is coerced (widened/narrowed/sign-adjusted) to the function's declared numeric return type before being returned.

**Source evidence:** `src/compiler/mlir_gen_stmt.cpp#L2144-L2145`

### `coerce.return.ref-by-descriptor` — Reference-kind return ABI determined by RefRepr descriptor

When the return type is a reference kind, its by-value return representation is the reference's RefRepr: dyn-trait and slice references return their 16-byte fat (pointer,metadata) pair by value; closure / custom-DST / thin references return their 8-byte value pointer.

**Divergence (from Rust):** A3/A4 fat-pointer return representation

**Related:** `coerce.return.aggregate-by-value`

**Source evidence:** `src/compiler/mlir_gen_fn.cpp#L83-L89`, `src/compiler/mlir_gen_fn.cpp#L138-L143`

### `coerce.return.slice-by-value` — slice/str return is the {ptr,len} fat pair by value

When the function return type is a slice (`&[T]`/`str`), the returned value is the 16-byte {data_ptr, len} fat pair returned by value; a value that is a pointer to slice storage is dereferenced to the pair first. No heap copy is allocated for the returned slice.

**Source evidence:** `src/compiler/mlir_gen_stmt.cpp#L2092-L2108`

### `coerce.return.trait-object-by-value` — returning an existing trait object passes the fat pair by value

When both the function return type and the returned value's type are trait objects `dyn Trait`, the value is returned by value as the 16-byte {data, vtable} pair; a value that is a pointer to fat-pair storage is dereferenced to the pair first.

**Source evidence:** `src/compiler/mlir_gen_stmt.cpp#L2071-L2088`

### `coerce.return.unsize-struct` — CoerceUnsized applied implicitly at return

A return value whose type can be unsized to the return type (e.g. `Rc<T>` → `Rc<dyn Tr>`) is implicitly coerced by rebuilding the smart-pointer struct, without an explicit `as`.

**Source evidence:** `src/compiler/sema_stmt.cpp#L2826-L2829`

### `layout.return.aggregate-by-value` — Struct/enum return values are returned by value as the full aggregate

A function returning a Struct/ZonedStruct/Enum returns the literal aggregate value by value (the registered LLVM struct/tagged-enum type), even though such types are passed by pointer at parameter/field/scope positions.

**Source evidence:** `src/compiler/mlir_gen_impl.hpp#L603-L626`

### `type.return.datanode-by-value-forbidden` — DataNode eidos cannot be returned by value

A non-plain zoned-struct DataNode type (`#[data]` node) cannot be a by-value return type; the function must return `DataRef<T>` instead. The check looks through array nesting to the innermost element.

**Source evidence:** `src/compiler/sema_decl.cpp#L479-L500`

### `type.return.non-movable-by-value-forbidden` — Location-anchored types cannot be returned by value

A type that is non-movable — containing a self-relative `#[rel_ptr]` field, or being `#[pinned]` — may not be returned by value; return a pointer (`*mut T` / `&T`) into its zone segment instead. (Crossing a function boundary by value would invalidate the self-relative anchor.)

**Divergence (from Rust):** A8

**Source evidence:** `src/compiler/sema_decl.cpp#L501-L513`

### Group: `let`

### `coerce.let.impl-trait-uses-concrete` — impl Trait let annotation adopts the concrete RHS type

When the annotation is `impl Trait`, the binding's type is the concrete RHS type (so inherent/method calls resolve), rather than the abstract impl-Trait type.

**Source evidence:** `src/compiler/sema_stmt.cpp#L2004-L2007`, `src/compiler/sema_stmt.cpp#L2179-L2183`

### `coerce.let.implicit-int-widening` — Implicit safe integer widening at let-init

At a let-init coercion site, a concrete (non-IntLit, non-enum) integer RHS whose type can safely widen to the annotated integer type is implicitly widened (e.g. u32→i64, i32→i64, u8→u32) without an explicit `as`.

**Divergence (from Rust):** Rust requires an explicit `as` cast for any integer width change; Logos performs implicit safe widening.

**Source evidence:** `src/compiler/sema_stmt.cpp#L2054-L2061`

### `coerce.let.literal-retype-to-float` — Numeric literal RHS retyped to float annotation

A FloatLit RHS is retyped to an `f32`/`f64` annotation; an IntLit RHS under a float annotation becomes a float literal (simple literal) or an `as`-cast to float (non-literal IntLit expression).

**Source evidence:** `src/compiler/sema_stmt.cpp#L2062-L2082`

### `coerce.let.reborrow-mut-at-ascription` — Type-ascription let reborrows &mut RHS

A type-ascribed let `let _: T = rhs` is a coercion site: when rhs is `&mut U` and the annotation is a reference/pointer kind (`&mut`, `&`, `*`), the RHS is implicitly reborrowed, so the original `&mut` is restored after the binding's last use (NLL).

**Source evidence:** `src/compiler/sema_stmt.cpp#L1991-L2003`

### `coerce.let.unsize-and-decays` — Implicit coercions at let-init when RHS type differs from annotation

When the RHS type is not directly compatible with the annotation, the binding applies, in order: CoerceUnsized for smart-pointer structs (`Rc<A>` → `Rc<dyn Tr>`); `&mut [T;N]` → `&mut [T]` array-ref-to-slice decay; non-capturing closure literal → `fn(..)->T`. If none apply (and not impl-Trait / not ExprBlob) a type-mismatch error is reported.

**Source evidence:** `src/compiler/sema_stmt.cpp#L1991-L2044`

### `type.let.floatlit-default-f64` — Unannotated float literal binding defaults to f64

An unannotated let whose RHS is a float literal binds at type f64.

**Source evidence:** `src/compiler/sema_stmt.cpp#L2203-L2207`

### `type.let.intlit-default-i32` — Unannotated integer literal binding defaults to i32 (i64 on overflow)

An unannotated let whose RHS is an integer literal binds at type i32, upgraded to i64 when the literal value falls outside the i32 range.

**Divergence (from Rust):** Rust defaults unconstrained integer literals to i32 but never silently widens to i64 on overflow (it is a compile error); Logos auto-upgrades to i64.

**Source evidence:** `src/compiler/sema_stmt.cpp#L2191-L2202`

### Group: `compatible`

### `coerce.compatible.equal-implies-compatible` — Equal types are compatible; compatibility is the implicit-coercion relation

types_compatible(from,to) is the directed implicit-coercion relation. It holds whenever types_equal(from,to); otherwise a fixed set of one-directional coercion rules apply. Either null operand is incompatible.

**Source evidence:** `src/compiler/sema.cpp#L1797-L1799`

### Group: `cast`

### `coerce.cast.aggregate-scalar-forbidden` — as-cast forbidden between aggregates and scalars

An `as`-cast where the source is an aggregate (struct/array/tuple/enum) and the target is a scalar is rejected, EXCEPT a payload-free (C-style) enum cast to integer/bool (discriminant cast). Symmetrically, casting a scalar/pointer to an aggregate target (struct/zoned-struct/array/tuple/enum) is rejected as a non-primitive cast target.

**Source evidence:** `src/compiler/sema_expr.cpp#L881-L956`

### `coerce.cast.as-bool-forbidden` — as bool is not a permitted cast

Casting any non-bool value (integer, float, C-style enum, etc.) to `bool` via `as` is rejected; only the reverse `bool as <int>` (true→1, false→0) is valid. Use `x != 0` / `x != 0.0` instead.

**Example**

```logos
let b: bool = (i as bool);  // error
```

**Source evidence:** `src/compiler/sema_expr.cpp#L923-L941`

### `coerce.cast.fat-to-thin-pointer` — Fat pointer to thin/void pointer extracts data half

A fat value cast to a thin pointer extracts field 0 (the data half): (a) `*mut/*const dyn` (Ptr<TraitObject>) → `*mut/*const ()` (void only); (b) any bare dyn/closure VALUE or `&dyn`/`&mut dyn` → void or any non-fat thin pointer; (c) `*const/*mut [T]` (Slice) → any non-fat thin pointer; (d) a non-self-describing DstRef → thin pointer; (e) a fat zone-mut `&mut T` → `*mut/*const T`. Runs before the identity check since both sides are MLIR pointer type.

**Source evidence:** `src/compiler/mlir_gen_expr.cpp#L3493-L3591`

### `coerce.cast.float-to-float` — Float to float truncates or extends by width

`E as F` (both float): truncate if width(F) < width(E), else extend.

**Source evidence:** `src/compiler/mlir_gen_expr.cpp#L3657-L3665`

### `coerce.cast.float-to-int-by-target-signedness` — Float to integer conversion respects target signedness

`E as T` (E float, T integer) uses float-to-unsigned-int if T is unsigned (u8/u16/u24/u32/u56/u64/u128) else float-to-signed-int.

**Source evidence:** `src/compiler/mlir_gen_expr.cpp#L3666-L3679`

### `coerce.cast.identity-noop` — Same-representation cast is identity

If the source value's representation equals the target representation, `E as T` is the identity (the value is returned unchanged).

**Source evidence:** `src/compiler/mlir_gen_expr.cpp#L3617`

### `coerce.cast.int-null-to-trait-object` — Integer (null) cast to trait object yields zeroed fat pair

`E as T` where T is a trait object (`*mut dyn`/`&dyn`) and E has an integer type (IntLit/i32/u32/i64/u64/isize/usize) produces a 16-byte {data,vtable} fat pair with both halves null. This makes null-handle sentinels (`0 as *mut dyn`) and `… as *mut u64 == 0` null checks behave under the uniform-fat dyn model.

**Divergence (from Rust):** Logos uniform-fat model: `*mut dyn`/`&dyn` are both 16-byte {data,vtable}; integer-to-dyn null cast is a Logos extension for null sentinels (no Rust analog).

**Source evidence:** `src/compiler/mlir_gen_expr.cpp#L3170-L3193`

### `coerce.cast.int-to-float-by-signedness` — Integer to float conversion respects source signedness

`E as F` (E integer, F float) uses unsigned-to-float if the source is unsigned (u8/u16/u24/u32/u56/u64/u128) or i1 (bool), else signed-to-float. Bool must be treated as unsigned: signed conversion of i1(1) gives -1.0.

**Source evidence:** `src/compiler/mlir_gen_expr.cpp#L3640-L3656`

### `coerce.cast.int-to-ptr` — Integer to pointer widens to 64-bit then reinterprets

`E as *T` (E integer) first widens E to 64-bit (zero-extend if the source is unsigned, else sign/value-coerce) then reinterprets the integer as an address.

**Source evidence:** `src/compiler/mlir_gen_expr.cpp#L3681-L3697`

### `coerce.cast.int-truncate` — Integer narrowing truncates

`E as T` where both are integers and width(T) < width(E) truncates to the low width(T) bits; equal widths are the identity.

**Source evidence:** `src/compiler/mlir_gen_expr.cpp#L3636-L3638`

### `coerce.cast.int-widen-by-signedness` — Integer widening sign- or zero-extends per source signedness

`E as T` where both are integers and width(T) > width(E): zero-extend if the source is unsigned (u8/u16/u24/u32/u56/u64/u128, or i1) else sign-extend.

**Source evidence:** `src/compiler/mlir_gen_expr.cpp#L3619-L3635`

### `coerce.cast.ptr-to-int` — Pointer to integer reinterprets address

`E as T` (E pointer, T integer) reinterprets the address as an integer of T's width.

**Source evidence:** `src/compiler/mlir_gen_expr.cpp#L3698-L3700`

### `coerce.cast.ref-to-scalar-autoderef` — &T as scalar auto-derefs the reference

When casting a value of type `&T`/`&mut T` (with scalar pointee T) to a scalar target (any integer/usize/isize/f32/f64/char/bool), the operand is auto-dereferenced before the cast, so the pointee value is converted, not the pointer bits. Pointer→pointer casts and `&T as *T`/`as usize` reinterpretations are unaffected.

**Example**

```logos
let n: &f64 = &1.0; let x = n as i64;
```

**Source evidence:** `src/compiler/sema_expr.cpp#L841-L875`

### `coerce.cast.str-to-mut-ptr-forbidden` — str as *mut u8 is forbidden

Casting a `str` (Slice<u8>) to `*mut u8` is rejected because str data is read-only (rodata); `*const u8` must be used instead.

**Source evidence:** `src/compiler/sema_expr.cpp#L957-L967`

### `coerce.cast.supertrait-upcast` — Supertrait upcast preserves data, swaps to super vtable

`&dyn Sub`/`dyn Sub` cast to `&dyn Super` (Sub ≠ Super, Super a supertrait of Sub) keeps the SAME data pointer and replaces the vtable with Super's vtable, recovered from a stored super-vtable-pointer slot in Sub's vtable at index `3 + |methods(Sub)| + idx(Super)`. Identity dyn casts (Sub == Super) fall through to the no-op reinterpret.

**Divergence (from Rust):** Rust-conformant (trait upcasting); vtable layout {drop,size,align, methods…, super-vtables…} is Logos-specific.

**Source evidence:** `src/compiler/mlir_gen_expr.cpp#L3254-L3303`

### `coerce.cast.u8-slice-to-u8-ptr` — &[u8]/str to *const u8 extracts data field

`E as *const u8`/`*mut u8` where E has type Slice<u8> (str is Slice<u8>) extracts field 0 (the data pointer) of the {ptr,len} fat pair. Evaluated before the identity short-circuit because both the fat-struct alloca and `*const u8` are the same MLIR pointer type.

**Source evidence:** `src/compiler/mlir_gen_expr.cpp#L3339-L3351`

### Group: `int`

### `coerce.int.implicit-widening` — Safe implicit integer widening

An implicit integer widening from `from` to `to` is permitted iff every value of `from` is representable in `to`: signed->signed and unsigned->unsigned require to_width >= from_width; unsigned->signed requires to_width > from_width; signed->unsigned is never permitted. usize/isize are distinct types: no implicit conversion between a pointer-sized integer and any fixed-width integer (only psize<->psize among themselves). Either operand having undefined rank (IntLit/Enum/non-integer) blocks widening.

**Divergence (from Rust):** Rust performs NO implicit integer widening at all (requires explicit `as`). Logos permits value-preserving implicit widening here.

**Related:** `type.integer.bit-width`

**Source evidence:** `src/compiler/sema_impl.hpp#L4482-L4495`

### `coerce.int.safe-widening` — Value-preserving integer widening is implicit; signed to unsigned never

An integer coerces to a wider integer when can_widen_int holds (e.g. u32 to i64, i32 to i64, u8 to u32); signed to unsigned widening is never implicit.

**Source evidence:** `src/compiler/sema.cpp#L1935-L1937`

### `coerce.int.to-float-by-signedness` — Integer-to-float conversion respects source signedness

An integer-to-float conversion uses unsigned-to-float when the source integer type is unsigned (one of {u8,u16,u32,u56,u64,u128}), otherwise signed-to-float. Float-to-int is not an implicit coercion and requires an explicit cast.

**Source evidence:** `src/compiler/mlir_gen_impl.hpp#L571-L597`

### `coerce.int.truncate-on-narrowing` — Integer narrowing truncates

Converting an integer value to a narrower integer type truncates to the destination width (low-order bits retained).

**Source evidence:** `src/compiler/mlir_gen_impl.hpp#L556-L557`

### `coerce.int.widen-by-source-signedness` — Integer widening sign- vs zero-extends by source signedness

Widening an integer value to a wider integer type sign-extends when the source type is signed and zero-extends when the source type is unsigned. Unsigned source kinds = {u8,u16,u24,u32,u56,u64,u128} (and bool). bool (i1) is always zero-extended.

**Source evidence:** `src/compiler/mlir_gen_impl.hpp#L534-L555`

### `coerce.int.widen-or-literal-fits` — Integer expression widening to target type

An integer-typed expression e is implicitly widened to a target integer type T (via an inserted cast) iff can_widen(kind(e),T) holds, OR e is an integer literal whose value fits T. Otherwise no implicit widening occurs.

**Source evidence:** `src/compiler/sema_impl.hpp#L4401-L4409`

### `layout.int.fixed-widths` — Primitive integer/float sizes and alignments

Scalar layout {size,align} in bytes: bool/i8/u8 = {1,1}; i16/u16 = {2,2}; i24/u24 = {3,1}; i32/u32/f32/char/{integer literal} = {4,4}; i56/u56 = {7,1}; i64/u64/f64/{float literal} = {8,8}; i128/u128 = {16,16}. usize/isize and all pointers are target-pointer-width-sized (8 on a 64-bit target).

**Divergence (from Rust):** A: Logos adds non-power-of-two integer widths i24/u24 (align 1) and i56/u56 (align 1) not present in Rust.

**Uncertainty:** Odd-width align=1 inferred from the literal {3,1}/{7,1} entries.

**Source evidence:** `src/compiler/mlir_gen_types.cpp#L453-L464`, `src/compiler/mlir_gen_types.cpp#L61-L62`

### Group: `intlit`

### `coerce.intlit.dispatch-unsuffixed-fits-narrower` — Unsuffixed integer literal dispatches to any integer param it fits

For overload/call dispatch an argument that is an UNSUFFIXED integer literal is compatible with an integer parameter type P iff its value fits P; a SUFFIXED literal (e.g. `9u64`) has a concrete type and must match by type, not by value. The flex path excludes Enum params (P!=Enum) so an integer never reinterprets enum by-pointer storage.

**Example**

```logos
push(7) // matches push(u8)
push(9u64) // matches push(i64)? NO — u64 only
```

**Source evidence:** `src/compiler/sema_impl.hpp#L4411-L4432`

### `coerce.intlit.to-integer-typevar-float` — Integer/float literal coercion to numeric, type-var, float

An IntLit coerces to any integer kind, to a TypeVar, and to F32/F64. A FloatLit coerces to F32/F64 and to a TypeVar.

**Source evidence:** `src/compiler/sema.cpp#L1902-L1908`

### `coerce.intlit.unify-to-concrete` — Unsuffixed integer literal unifies to the other operand's type

When unifying two integer types, if one is the unsuffixed literal type IntLit it unifies to the other operand's type. Otherwise the narrower is widened to the wider when a safe implicit widening exists (per coerce.int.implicit-widening); if neither widens, the first operand's type is kept.

**Related:** `coerce.int.implicit-widening`, `coerce.numericlit.unify-to-concrete`

**Source evidence:** `src/compiler/sema_impl.hpp#L4497-L4506`

### `type.intlit.fits-range` — Integer-literal range fit per target type

A constant integer value v fits a target integer kind iff it lies within that kind's representable range: i8 [-128,127], u8 [0,255], i16 [-32768,32767], u16 [0,65535], i24 [-2^23,2^23-1], u24 [0,2^24-1], i32 [INT32_MIN,INT32_MAX], u32 [0,UINT32_MAX], i56 [-2^55,2^55-1], u56 [0,2^56-1], i64/i128/isize(64-bit) all int64 values, u64/u128/usize require v>=0. On a 32-bit target usize requires v in [0,UINT32_MAX] and isize in [INT32_MIN,INT32_MAX].

**Related:** `type.integer.bit-width`

**Source evidence:** `src/compiler/sema_impl.hpp#L4544-L4568`

### Group: `numericlit`

### `coerce.numericlit.unify-to-concrete` — Numeric literal (int or float) unifies to the concrete operand

When unifying two numeric types where either operand may be a literal, an unsuffixed IntLit or FloatLit operand unifies to the other operand's (concrete) type; FloatLit thereby promotes to a concrete float type F32/F64.

**Related:** `coerce.intlit.unify-to-concrete`

**Source evidence:** `src/compiler/sema_impl.hpp#L4514-L4517`

### Group: `float`

### `coerce.float.widen-truncate` — Float-to-float widening extends, narrowing truncates

Converting a float to a wider float type extends; to a narrower float type truncates.

**Source evidence:** `src/compiler/mlir_gen_impl.hpp#L561-L568`

### Group: `reborrow`

### `coerce.reborrow.downgrade-mut-to-shared` — Downgrading reborrow `&mut T` → `&T` gated on allow_downgrade

When the formal is `&T` (shared), a `&mut T` argument may be implicitly reborrowed as a shared `&T` only when downgrade is permitted (fn-arg coercion). At method-receiver position downgrade is forbidden, because a formal `&Self` whose Self IS a `&mut X` (impl on a ref type) would otherwise dispatch through the wrong impl key. For a `*U` formal, dest mutability follows the formal pointer's mutability bit.

**Source evidence:** `src/compiler/sema_expr.cpp#L12926-L12939`

### `coerce.reborrow.implicit-mut-reborrow` — Implicit &mut reborrow at coercion sites

A `&mut T` value is implicitly reborrowed when passed where a reference is expected, allowing a single `&mut` binding to be used at multiple coercion sites without an explicit reborrow.

**Source evidence:** `src/compiler/sema_impl.hpp#L439-L440`, `src/compiler/sema_impl.hpp#L478`

### `coerce.reborrow.method-receiver-no-downgrade` — Method receiver binds without &mut→& downgrade

Binding a method receiver to its formal `self` slot performs implicit auto-reborrow but never downgrades a `&mut Self` receiver to `&Self`, so receiver mutability selects the correct impl key (e.g. for `impl X for &mut M` ref-impls). By-value `self` triggers move tracking of the receiver.

**Source evidence:** `src/compiler/sema_impl.hpp#L442-L448`

### `coerce.reborrow.mut-place-at-coercion-site` — Implicit reborrow of `&mut T` place at call/method argument sites

At an argument coercion site, an expression of type `&mut T` that is a PLACE (VarRef, FieldRead, or IndexRead) and whose formal parameter is ref-shaped — `&mut U`, `&U`, or `*U` — is implicitly reborrowed as `AddrOfTemp(Deref(e))` rather than moved, registering a borrow on the original `&mut T` binding instead of consuming it. Reborrow is structural: the result has the SAME pointee type as the source; the genuine argument type-check runs afterward.

**Example**

```logos
fn f(x: &mut T) { g(x); h(x); }  // x reborrowed, not moved
```

**Source evidence:** `src/compiler/sema_expr.cpp#L12924-L12955`

### `coerce.reborrow.no-reborrow-of-fresh-borrow` — No reborrow of a fresh borrow expression

Implicit reborrow applies only when the `&mut T` operand is a place expression (VarRef / FieldRead / IndexRead). A fresh borrow expression (e.g. `&mut x`, `&mut p.f`) is left as-is and never wrapped in a reborrow shape, so its borrow is recorded through the normal path.

**Source evidence:** `src/compiler/sema_expr.cpp#L12947-L12951`

### Group: `deref`

### `coerce.deref.box-slice-borrow` — &Box<[T]> deref-coerces to &[T]

`&b` where `b` is an owning slice (`Box<[T]>`) yields a borrowed `&[T]` view over the same {data,len} storage with no copy or move; mutability is inherited from the owning slice.

**Source evidence:** `src/compiler/sema_expr.cpp#L2516-L2521`

### `coerce.deref.box-struct-borrow` — &Box<Foo> deref-coerces to &Foo

`&b` where `b` is an owning DST reference (`Box<Foo>`) yields a borrowed `&Foo` with the same reference value re-typed non-owning; mutability is inherited.

**Source evidence:** `src/compiler/sema_expr.cpp#L2522-L2532`

### `coerce.deref.ref-vec-to-slice` — &Vec<T> / &mut Vec<T> deref-coerces to slice &[T]

A Ref/MutRef over a stdlib Vec<T> struct coerces to a Slice with element compatible with Vec's first type-arg (Vec's {ptr,len,cap} has the {ptr,len} slice fat-pointer as a prefix).

**Uncertainty:** Hardcoded to the stdlib Vec struct by name; full Deref trait surface not yet covered.

**Source evidence:** `src/compiler/sema.cpp#L2034-L2049`

### `coerce.deref.user-deref-chain` — Deref coercion through user/stdlib Deref(Mut) impls

A receiver of struct type with a Deref/DerefMut impl deref-coerces by calling `deref`/`deref_mut` to obtain `&Target`; for an unsized Target (dyn or slice) the returned fat reference is the value itself with no further place-deref. A mutable deref step may fall back to the Deref Target when only Deref is implemented (shared supertrait Target).

**Source evidence:** `src/compiler/sema_expr.cpp#L100-L217`

### Group: `method-recv`

### `coerce.method-recv.auto-ref` — Auto-ref of receiver to match &self / &mut self

> ⚠ **ID COLLISION**: `coerce.method-recv.auto-ref` is emitted more than once below. Each block reflects a distinct source location with a complementary (not identical) statement; both are normative and surfaced verbatim. Resolve by reconciling the extractors that produced the shared id.

If the dispatched method's first formal is a reference (`&self`/`&mut self`) and the receiver value is neither a reference nor a raw pointer, the receiver is implicitly wrapped in an address-of (`&` or `&mut` per the formal's mutability) producing a reference-typed receiver.

**Source evidence:** `src/compiler/sema_expr.cpp#L7696-L7711`, `src/compiler/sema_expr.cpp#L8061-L8080`

### `coerce.method-recv.auto-ref` — Method-call receiver auto-ref

> ⚠ **ID COLLISION**: `coerce.method-recv.auto-ref` is emitted more than once below. Each block reflects a distinct source location with a complementary (not identical) statement; both are normative and surfaced verbatim. Resolve by reconciling the extractors that produced the shared id.

When a resolved method's receiver formal is `&Self`/`&mut Self` (or otherwise ref-like) but the actual receiver value is non-reference and non-raw-pointer, the receiver is implicitly auto-referenced (`&`/`&mut` per the formal's mutability) before the call.

**Source evidence:** `src/compiler/sema_expr.cpp#L8666-L8674`, `src/compiler/sema_expr.cpp#L9026-L9037`, `src/compiler/sema_expr.cpp#L9122-L9131`

### `coerce.method-recv.deref-bound-fallthrough` — Autoderef through a Deref/DerefMut bound when no bound provides the method

If no in-scope bound on type-parameter T provides method `m`, but T has a bound `T: Deref<C>` (or `DerefMut<C>`), the receiver is rewritten to `recv.deref()` (resp. `deref_mut()`), typed `&C` (resp. `&mut C`), and method resolution falls through to the ordinary inherent/struct-method path on C.

**Source evidence:** `src/compiler/sema_expr.cpp#L7798-L7821`

### `type.method-recv.deref-before-lookup` — Receiver reference stripped to its pointee for nominal method lookup

For method-formal hinting and dispatch, a receiver of reference type (`&T`/`&mut T`) is reduced to its pointee T before extracting the struct/enum name and binding the receiver's nominal type-arguments into the substitution.

**Source evidence:** `src/compiler/sema_expr.cpp#L7874-L7894`

### Group: `unsize`

### `coerce.unsize.already-fat-passthrough` — Fat-pointer source needs no rebuild

When coercing to `dyn Trait`, if the RHS value is already a fat pointer (its type is `dyn Trait`, or `&/&mut/*` to `dyn Trait`, with Box<dyn> collapsing to the trait object), the existing {data, vtable} handle is used directly rather than rebuilt from the concrete type.

**Related:** `coerce.unsize.box-concrete-to-box-dyn`

**Source evidence:** `src/compiler/mlir_gen_stmt.cpp#L1688-L1709`

### `coerce.unsize.arg-struct-to-dyn-trait` — Implicit unsize coercion of call arguments to &dyn / Box<dyn>

At a call site, when the callee parameter type is `&dyn Trait`, `&mut dyn Trait`, or `Box<dyn Trait>` and the argument has a concrete (non-trait-object) type, the argument is implicitly unsize-coerced into a fat {data, vtable} trait object. The vtable is selected on the concrete pointee type T: `&T`/`&mut T` peel to T, `Box<T>` peels to T, and a bare struct value is spilled to storage to obtain a data pointer.

**Related:** `coerce.unsize.struct-to-dyn-trait`

**Source evidence:** `src/compiler/mlir_gen_expr.cpp#L2417-L2481`, `src/compiler/mlir_gen_expr.cpp#L2436-L2459`

### `coerce.unsize.array-to-slice` — &array → slice

`&a` where `a: [T; N]` yields a slice value `&[T]` with len = N (an unsized coercion at the point of borrow), not `&[T; N]`. This applies to array variables, array statics, and bare array literals `&[e0, .., e_{N-1}]`.

**Source evidence:** `src/compiler/sema_expr.cpp#L2510-L2514`, `src/compiler/sema_expr.cpp#L2570-L2585`

### `coerce.unsize.box-array-to-box-slice` — Box<[T;N]> unsizes to owning Box<[T]>

`Box<[T;N]>` cast to an owning `Box<[T]>` (Slice with owning_slice) builds a {data,len} fat pair: data = the box's heap pointer (field 0 of the Box struct), len = N (the array's compile-time size). This is CoerceUnsized for `Box::new([..]) as Box<[T]>`.

**Related:** `coerce.unsize.thin-array-ptr-to-slice`

**Source evidence:** `src/compiler/mlir_gen_expr.cpp#L3305-L3337`

### `coerce.unsize.box-concrete-to-box-dyn` — Box<Concrete> unsizes to Box<dyn Trait>

Binding a `Box<Concrete>` value to a `Box<dyn Trait>` (or `&Concrete`/`&dyn`/`*dyn`) target coerces the thin pointer to a fat {data, vtable} pair whose vtable is selected for the concrete type's impl of Trait. An owning `Box<dyn Trait>` is droppable: its drop runs drop_in_place on the data and frees it; a `&dyn`/`&mut dyn` borrow yields a non-owning fat pair; only raw `*const/*mut dyn` retains a separately-allocated 8-byte heap handle.

**Related:** `coerce.unsize.struct-to-dyn-trait`

**Source evidence:** `src/compiler/mlir_gen_stmt.cpp#L1665-L1748`, `src/compiler/mlir_gen_stmt.cpp#L1721-L1745`

### `coerce.unsize.box-consumes-source` — Unsize to owning trait object moves the source

An unsize cast to an owning trait object (e.g. `box_val as Box<dyn Trait>`) consumes/moves the operand; ownership of the heap data transfers to the result so the source's own drop does not also run (avoiding double-free).

**Related:** `coerce.unsize.struct-coerce-unsized`

**Source evidence:** `src/compiler/sema_expr.cpp#L969-L974`

### `coerce.unsize.box-dyn-deref-then-unsize` — implicit value-to-dyn coercion unwraps refs and Box before unsizing

When a value's expected slot type is a trait object but the value's type is not, the value is coerced to dyn: references and `Box<T>` are first unwrapped to reach the concrete pointee, an already-`dyn` value is passed through unchanged, and a non-pointer value is spilled to a stack slot to obtain an address before forming the fat pair.

**Source evidence:** `src/compiler/mlir_gen_dyn.cpp#L1236-L1271`

### `coerce.unsize.box-dyn-vtable-drops-concrete` — Owning Box<dyn> coercion threads the concrete destructor

When a concrete `Box<T>` argument is unsize-coerced to a `Box<dyn Trait>` parameter, the resulting fat trait-object value carries the vtable of the concrete T, including T's drop-in-place glue, so dropping the Box<dyn> runs T's destructor (not an empty no-op).

**Related:** `coerce.unsize.arg-struct-to-dyn-trait`

**Source evidence:** `src/compiler/mlir_gen_expr.cpp#L2446-L2480`

### `coerce.unsize.concrete-to-dyn-builds-fat-pair` — unsizing a concrete to dyn stores data + vtable into the fat pair

Coercing a concrete pointer to `&dyn Trait` builds the fat pair by storing the data pointer at field 0 and the vtable pointer (the address of the static per-(trait,type) vtable) at field 1. The source type name used to select the vtable is the concrete struct mangled name for (zoned) structs, else the plain type string.

**Source evidence:** `src/compiler/mlir_gen_dyn.cpp#L1204-L1234`, `src/compiler/mlir_gen_dyn.cpp#L1259-L1270`

### `coerce.unsize.dyn-auto-trait-bound-gate` — dyn coercion enforces + Send / + Sync auto-trait bounds

When coercing to a `dyn Trait + Send` / `+ Sync` target, the source pointee must structurally satisfy the named auto-trait bounds; failure is a coercion-site error. The general Struct→TraitObject acceptance does not waive these auto-trait constraints.

**Source evidence:** `src/compiler/sema_impl.hpp#L417-L422`

### `coerce.unsize.dyn-field-and-element` — Unsize coercion for &dyn Trait fields and array elements

Storing a concrete `&S`/`&mut S`/`*S` into a `&dyn Trait` struct field or a `[&dyn Trait; N]` array element triggers an unsize coercion that builds the fat {data, vtable} pointer (two words / 16 bytes) for the concrete struct's vtable; the thin source reference is never stored raw.

**Related:** `layout.zone-mut-ref.fat-data-zone`

**Source evidence:** `src/compiler/mlir_gen.cpp#L987-L1007`, `src/compiler/mlir_gen.cpp#L1073-L1124`

### `coerce.unsize.dyn-supertrait-upcast` — &dyn Sub coerces to &dyn Super

A trait-object reference `&dyn Sub` coerces to `&dyn Super` when `Sub` has `Super` as a supertrait (dyn upcast).

**Source evidence:** `src/compiler/sema_impl.hpp#L432`, `src/compiler/sema_impl.hpp#L476`

### `coerce.unsize.lifetime-diff-not-unsize` — Type-arg differences that are not unsizes fall through to variance

A struct type-arg difference that is not a sized→fat unsize (e.g. lifetime-only variance `Foo<&'a>` vs `Foo<&'b>`, or multi-field structs) is NOT handled as CoerceUnsized; it must be resolved by the variance/compat machinery. CoerceUnsized requires exactly one field and that field's type to genuinely become fat.

**Related:** `coerce.unsize.struct-coerce-unsized`

**Source evidence:** `src/compiler/sema_expr.cpp#L669-L688`

### `coerce.unsize.ref-concrete-to-dyn-trait` — &Concrete unsizes to &dyn Trait in argument position

An argument `&T` / `&mut T` coerces (unsizes) to a `&dyn Trait` / `&mut dyn Trait` parameter when the pointee implements the trait directly or via a blanket impl, or when the pointee is a type-variable whose in-scope bounds include the trait. The fat pointer is built at the call site.

**Source evidence:** `src/compiler/sema_impl.hpp#L409-L431`

### `coerce.unsize.ref-concrete-to-trait-object` — Reference/pointer to concrete unsizes to bare trait object

`&T`/`&mut T`/`*const T`/`*mut T` (T a concrete struct or primitive) cast to a bare trait object synthesizes a {data,vtable} fat pair; the vtable keys on T's concrete struct name (or the primitive's bare type name for a blanket-impl `&i64 as &dyn`). Only fires when the source pointee is concrete; a `&dyn`→`dyn` reinterpret (pointee already a trait object) is a no-op.

**Divergence (from Rust):** Uniform-fat model: `&dyn` and `*mut dyn` are both 16-byte fat pairs (Logos), unlike Rust where only references unsize.

**Source evidence:** `src/compiler/mlir_gen_expr.cpp#L3406-L3432`

### `coerce.unsize.return-concrete-to-trait-object` — returning a concrete type where `dyn Trait` is expected unsizes to a fat pointer

When the function return type is a trait object `dyn Trait` and the returned value's type is a non-trait-object concrete type T (or a reference/pointer `&T`/`&mut T`/`*const T`/`*mut T` whose pointee is the bare struct), the value is coerced to a {data, vtable} fat pointer keyed on the bare struct name and returned by value (16-byte pair); indirection layers over the concrete struct are stripped before vtable lookup.

**Related:** `coerce.unsize.struct-to-dyn-trait`

**Source evidence:** `src/compiler/mlir_gen_stmt.cpp#L2037-L2070`

### `coerce.unsize.smart-ptr-to-box-dyn` — Box/Rc/Arc<T> unsizes to owning dyn fat pair

`Box<T>`/`Rc<T>`/`Arc<T>` (T concrete) cast to a dyn smart pointer builds a value fat pair {data,vtable}: for Box, data = field 0 (the heap pointer); for Rc/Arc, data = field0 + round_up(8, align(T)) (skipping the 2×i32 RcInner strong/weak header). vtable[0..2] = drop/size/align; drop is kind-specific (Box→free; Rc/Arc→dec strong + free RcInner). A dyn-payload smart pointer is already a handle and is not re-wrapped.

**Source evidence:** `src/compiler/mlir_gen_expr.cpp#L3434-L3491`

### `coerce.unsize.struct-coerce-unsized` — CoerceUnsized for single-field smart-pointer structs

A value of struct type S<..A> coerces to S<..B> (same struct, equal type-arg arity) when S has exactly one field whose substituted type changes from sized/thin to fat-unsized: target field kind is DstRef, or (TraitObject while source isn't), or (Slice while source isn't). The coercion reads the single field, casts it to the target field type, and repacks into the target struct.

**Example**

```logos
let r: Rc<dyn Tr> = rc_a as Rc<dyn Tr>;
```

**Related:** `coerce.unsize.box-consumes-source`

**Source evidence:** `src/compiler/sema_expr.cpp#L651-L699`

### `coerce.unsize.struct-dyn-tail-to-dstref` — Pointer to struct with concrete tail unsizes to DstRef with dyn tail

`*mut/*const/& ConcreteStruct<…, Sized>` cast to a DstRef whose tail type-arg is a trait object (`*mut Inner<dyn Tr>`) builds a {data,vtable} fat pair: data = the source thin pointer to the whole struct; vtable = the concrete tail type's vtable for the tail trait (the tail binding is the source instance's last type-arg). This is CoerceUnsized for a struct with an unsized (`dyn`) tail field.

**Source evidence:** `src/compiler/mlir_gen_expr.cpp#L3356-L3394`

### `coerce.unsize.struct-to-dyn-trait` — Struct (or &/&mut/*Struct) unsize-coerces to a trait object

A Struct, a Ptr, or a &/&mut over a Struct coerces to a TraitObject (&dyn Trait); the impl check is deferred to codegen.

**Source evidence:** `src/compiler/sema.cpp#L1988-L1998`

### `coerce.unsize.struct-wrapper-coerceunsized` — Single-field wrapper struct CoerceUnsized

A smart-pointer/wrapper struct with a single unsizable field coerces `Wrapper<A>` → `Wrapper<dyn Trait>` (or to a slice/DstRef target) by unsizing that field, keeping the same struct. Applies at explicit `as` casts and at implicit coercion sites (argument, let, return).

**Source evidence:** `src/compiler/sema_impl.hpp#L433-L438`

### `coerce.unsize.thin-array-ptr-to-slice` — Thin array pointer to slice pointer synthesizes len=N

`*const [T;N]`/`*mut [T;N]` (Ptr<Array>) cast to `*const [T]`/`*mut [T]` (Slice) synthesizes a {ptr, len=N} fat pair on the stack, where N is the array's compile-time size; without this the cast would be a no-op leaving array contents misread as the data field.

**Related:** `coerce.unsize.box-array-to-box-slice`

**Source evidence:** `src/compiler/mlir_gen_expr.cpp#L3592-L3616`

### `coerce.unsize.value-to-dyn-at-trait-slot` — Concrete value unsized to a fat dyn handle when the destination slot is a trait object

When a destination slot has trait-object type (`dyn`/`Box<dyn>`/`&dyn`) but the supplied value is still concrete (`Box<Concrete>`, `&Concrete`, struct value), the value is unsize-coerced into a fat {data, vtable} handle (e.g. an enum-variant payload typed `Box<dyn>` constructed from `Box<Concrete>`). It is a no-op if the value already is a trait object.

**Related:** `layout.dyn.fat-pair-16-byte`

**Source evidence:** `src/compiler/mlir_gen_impl.hpp#L986-L992`

### Group: `method`

### `coerce.method.aggregate-arg-by-pointer` — Aggregate / tagged-enum argument passed by pointer

An argument whose value is an aggregate (struct) or a tagged (data-carrying) enum, passed where the callee parameter is pointer-represented, is materialized into storage and passed by pointer; scalar arguments are numerically coerced to the parameter type instead.

**Source evidence:** `src/compiler/mlir_gen_expr.cpp#L2746-L2766`, `src/compiler/mlir_gen_expr.cpp#L2751-L2762`

### `coerce.method.arg-concrete-to-dyn-unsize` — Method argument concrete→trait-object unsize coercion

When a method parameter has type `dyn Trait` (a trait object, possibly after peeling one `Box<_>` layer) and the supplied argument is a non-trait-object concrete type, the argument is unsize-coerced into a fat `{data, vtable}` handle for that trait built from the argument's concrete type; this coercion applies symmetrically to free-function and method calls.

**Related:** `layout.dyn.fat-handle`

**Source evidence:** `src/compiler/mlir_gen_expr.cpp#L2703-L2744`, `src/compiler/mlir_gen_expr.cpp#L2715-L2719`, `src/compiler/mlir_gen_expr.cpp#L2739-L2742`

### `type.method.recv-autoderef-resolution` — Receiver dereferenced for method resolution

For method resolution and struct-type-arg extraction, a receiver of reference type (`&`/`&mut`) or raw-pointer type is dereferenced to its pointee.

**Source evidence:** `src/compiler/sema_expr.cpp#L8742-L8749`, `src/compiler/sema_expr.cpp#L8805-L8810`, `src/compiler/sema_expr.cpp#L8988-L8989`

### `type.method.return-subst` — Method return type substitution

The type of a method-call expression is the method's declared return type with the receiver/method type-var substitution and lifetime substitution applied.

**Source evidence:** `src/compiler/sema_expr.cpp#L9102-L9105`, `src/compiler/sema_expr.cpp#L9143`

### Group: `fn`

### `coerce.fn.fnitem-to-fnptr` — FnItem coerces to a matching FnPtr; not the reverse, not FnItem to FnItem

A FnItem value coerces to an FnPtr at every value-use site iff arity matches and each param and the return type are pairwise compatible. FnPtr to FnItem is rejected, and two distinct FnItems with identical signatures are not mutually compatible (distinct fn identity).

**Divergence (from Rust):** logos-core 1.4: FnItem (ZST per-fn identity) auto-coerces to FnPtr; Rust models the analogous fn-item to fn-pointer coercion.

**Source evidence:** `src/compiler/sema.cpp#L1816-L1826`

### Group: `variance`

### `coerce.variance.gate-on-compatible` — Variance gate on coercion sites

When `from` is type-compatible with `to`, the coercion is additionally subjected to a variance/subtype check: &mut is invariant, &T is covariant, fn params contravariant. A variance failure (lifetime structure incompatible) is rejected with a variance-mismatch error. The check is permissive (caller region inference may fill regions) at call-site argument passing, and strict at body sites (return, let-init) where both lifetimes are fn-scope-fixed.

**Source evidence:** `src/compiler/sema_impl.hpp#L3344-L3351`, `src/compiler/sema_impl.hpp#L3531-L3547`

### Group: `cfgslot`

### `coerce.cfgslot.numeric-bidirectional` — Cfg-slot types coerce bidirectionally with any numeric / literal

A CfgSlotType (deferred WritStatic-bound primitive) behaves like a TypeVar for coercion: IntLit/FloatLit to CfgSlot accepted, and any integer or float on either side is compatible with a CfgSlot in both directions; mono enforces the resolved-type compatibility.

**Source evidence:** `src/compiler/sema.cpp#L1909-L1921`

### Group: `writ-anyval`

### `coerce.writ-anyval.scalar-helpers` — Implicit coercion of comprehension element to AnyVal

Inside a Writ comprehension element/value, the value is coerced to AnyVal: WAny and legacy AnyVal struct values pass through unchanged; bool/i8/i16/i32/IntLit/u8/u16/u32 are wrapped via the matching `writ_coerce_<ty>` helper; `str` (`&[u8]`) is wrapped via `writ_coerce_str` (taking `&ctr` first). Any other type is rejected with a message to cast explicitly or wrap with AnyVal::embed_*.

**Divergence (from Rust):** Logos-specific Writ value model.

**Source evidence:** `src/compiler/sema_expr.cpp#L11382-L11458`

### `coerce.writ-anyval.wide-int-no-implicit` — Wide integers not implicitly coerced to AnyVal

i64/u64/i24/u24/i56/u56/i128/u128 are intentionally NOT auto-coerced to AnyVal (implicit i32 embedding would silently truncate); the user must cast explicitly (`x as i32`) or wrap with WAny::from.

**Divergence (from Rust):** Logos-specific anti-truncation rule.

**Source evidence:** `src/compiler/sema_expr.cpp#L11418-L11427`, `src/compiler/sema_expr.cpp#L11430-L11436`

### Group: `if`

### `expr.if.fnitem-branches-lub-to-fnptr` — Two fn-item branches join to a fn pointer

When both `if` branches are distinct fn-item values with the same signature, the result type is the corresponding fn-pointer type `fn(params)->ret` (fn-item-to-fn-pointer coercion at the join), since two distinct fn-items are not directly type-compatible.

**Source evidence:** `src/compiler/sema_expr.cpp#L14007-L14027`

### `expr.if.intlit-result-overflow-i64` — Integer-literal if-result widens to i64 on i32 overflow

If an `if` expression's result type is an unresolved integer literal and either branch's literal value exceeds the i32 range, the result type is i64.

**Source evidence:** `src/compiler/sema_expr.cpp#L14038-L14052`

### Group: `closure`

### `coerce.closure.fn-ptr-requires-non-capturing` — Closure-to-fn-pointer coercion requires an empty capture set

A closure coerced to a plain function pointer is emitted as a top-level function with signature (params...) -> ret and NO env parameter; the coercion is valid only for non-capturing closures, and the resulting value is the function's address.

**Source evidence:** `src/compiler/mlir_gen_dyn.cpp#L1701-L1772`

### `coerce.closure.hint-from-fn-bound` — Closure type hint derived from a Fn-family type-param bound

When the expected type is a type parameter bounded by an Fn-family trait (Fn/FnMut/FnOnce(params)->ret), a closure literal in that position is given the inferred closure type with parameter and return types taken from the bound (after generic substitution).

**Source evidence:** `src/compiler/sema_expr.cpp#L14061-L14080`

### `coerce.closure.literal-to-fn-pointer` — Closure literal coerces to fn pointer

A non-capturing closure literal coerces to a function-pointer type at coercion sites.

**Source evidence:** `src/compiler/sema_impl.hpp#L473`, `src/compiler/sema_impl.hpp#L477`

### `coerce.closure.noncapturing-to-fnptr` — Non-capturing closure coerces to fn pointer

A closure value coerces to a target `FnPtr` type iff it is a closure literal with zero captures and its parameter list and parameters are pairwise type-compatible with the target; the result type becomes `fn(params) -> ret` derived from the closure signature.

**Source evidence:** `src/compiler/sema_impl.hpp#L620-L637`

### `coerce.closure.ref-to-closure` — &Closure / &mut Closure coerce to Closure

A Ref/MutRef over a Closure coerces to a bare Closure value (since dyn Fn* is already fat-pointer-like and the reference carries no extra meaning).

**Source evidence:** `src/compiler/sema.cpp#L1976-L1987`

### `layout.closure.fat-pointer-pair` — Closure values are a 16-byte {fn,env} fat pair

A closure has a 16-byte {fn_ptr, env_ptr} fat-pointer representation.

**Source evidence:** `src/compiler/mlir_gen_impl.hpp#L787`

### `layout.closure.fn-env-pair` — Closure value is a {fn_ptr, env_ptr} pair

> ⚠ **ID COLLISION**: `layout.closure.fn-env-pair` is emitted more than once below. Each block reflects a distinct source location with a complementary (not identical) statement; both are normative and surfaced verbatim. Resolve by reconciling the extractors that produced the shared id.

A closure value is represented as a two-field aggregate: field 0 = function pointer, field 1 = environment pointer. Calling a closure invokes the function pointer with env_ptr prepended as the implicit first argument, followed by the user arguments.

**Divergence (from Rust):** A10 — dyn Fn/FnMut/FnOnce collapse to this Closure pair; no separate Fn-trait vtable.

**Related:** `layout.fnptr.bare-call-no-env`

**Source evidence:** `src/compiler/mlir_gen_expr.cpp#L4759-L4799`

### `layout.closure.fn-env-pair` — Closure value = {fn,env}

> ⚠ **ID COLLISION**: `layout.closure.fn-env-pair` is emitted more than once below. Each block reflects a distinct source location with a complementary (not identical) statement; both are normative and surfaced verbatim. Resolve by reconciling the extractors that produced the shared id.

A closure value is the pair {fn_ptr, env_ptr}, 16 bytes.

**Source evidence:** `src/compiler/mlir_gen_types.cpp#L957-L960`

### `type.closure.type` — Closure type

`|T1, T2| -> R` is a closure type used in parameter annotations; the zero-arg form `|| -> R` is accepted (the `||` token is split).

**Divergence (from Rust):** A6: Rust spells closures via Fn-family bounds; Logos has a dedicated `|..|->R` closure type syntax.

**Source evidence:** `tools/peg_gen/grammars/logos.peg#L1657-L1664`

### Group: `relptr`

### `coerce.relptr.transparent-to-thin-ptr` — #[rel_ptr] struct is value-transparent to a thin pointer

A `#[rel_ptr]` struct `RP<T>` is value-transparent to `*T`/`&T`/`&mut T`: its computed form is an absolute thin pointer (only storage is a self-relative offset), so the coercion is accepted in both directions at value-flow sites.

**Source evidence:** `src/compiler/sema_impl.hpp#L801-L804`

### Group: `struct-lit`

### `coerce.struct-lit.field-numeric-coercion` — Struct-literal field initializers coerce to declared field type

A scalar initializer in a struct literal is coerced to the declared type of the target field (e.g. an integer literal to the field's integer type, a float literal to the field's float type) before being stored.

**Source evidence:** `src/compiler/mlir_gen.cpp#L1008-L1017`

### Group: `taggedptr`

### `coerce.taggedptr.from-raw-ptr` — Raw pointer coerces to a tagged trait pointer

Any *T (Ptr) coerces to a TaggedPtr (&tagged<TS> Trait, a thin pointer to a tagged object); the tag is read at dispatch time.

**Source evidence:** `src/compiler/sema.cpp#L2062-L2066`

