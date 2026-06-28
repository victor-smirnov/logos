# Items

Scope: item-level declarations of Logos (domain `item`) - functions, structs, tuple structs, unions, enums, traits, impl blocks, consts, statics, type aliases, datatype/eidos definitions, modules, extern blocks, fields, attributes, doc comments, and visibility. Rules are extracted from the compiler implementation layers - grammar (`tools/peg_gen/grammars/logos.peg`), sema/collection (`src/compiler/sema*`), monomorphization, and MLIR codegen (`src/compiler/mlir_gen*`) - and grouped by their id middle-segment. Each `### ` heading is a stable, linkable rule id; never rename one.

## Item Kinds

### `item.kinds.set` — Module item alternatives

A module item is one of: doc-comment, annotation, template decl, const/static def, type alias, enum def, datatype def/inst, trait def/inst, struct unit/def/inst, instantiate decl, item-position metacall, fn-macro item invocation, union def, impl block, extern block, extern fn, or fn def — each in plain and `pub` forms where visibility applies.

**Evidence:** `tools/peg_gen/grammars/logos.peg#L529`

## Visibility

### `item.visibility.pub-module` — Visibility marker pub / pub(module)

Item visibility is `pub` (fully exported) or `pub(IDENT)` where IDENT is a contextual keyword validated == "module" in sema, meaning module-linkage: visible to other packages of the SAME module but not exported to consumers.

```logos
pub(module) fn helper() {}
```

**Divergence:** Logos uses `pub(module)` for module-linkage; Rust uses `pub(crate)`/path-restricted visibilities.

**Evidence:** `tools/peg_gen/grammars/logos.peg#L1273-L1284`

## Modules

### `item.module.extern-block-flatten` — extern block children flattened into item stream

An extern block's child items (extern fn declarations) are spliced in order into the flat module-item worklist; the block itself produces no item.

**Evidence:** `src/compiler/sema.cpp#L7378-L7391`

## Use Declarations

### `item.use.path-form` — use declaration path form

A use declaration is `[pub] use NAME(.part)* ;`, where path segments after the head are dot-separated.

**Divergence:** Logos paths use `.` for package/module segments rather than Rust's `::`.

**Evidence:** `src/compiler/sema_render.cpp#L1036-L1050`, `src/compiler/sema_render.cpp#L1182-L1190`

## Doc Comments

### `item.doc.comment-attached-to-next-item` — Doc comments attach as documentation

Outer doc comments (`///`, `/** */`) accumulate and attach to the next item; inner doc comments (`//!`, `/*! */`) accumulate into the module-level inner documentation. The comment markers are stripped.

**Evidence:** `tools/peg_gen/grammars/logos.peg#L305-L308`

### `item.doc.comment-strip` — doc comment accumulation and prefix stripping

`///` line docs strip the leading `///` plus one optional space; `/** */` outer block docs and `/*! */`/`//!` inner docs accumulate; outer docs attach to the next non-doc item, inner docs accumulate into a per-module inner-doc buffer joined by newlines and never attach to a specific item.

**Evidence:** `src/compiler/sema_collect.cpp#L1404-L1436`

### `item.doc.inner-module` — Inner doc-comments form the enclosing module's doc summary

An inner doc-comment — a `//!` line (with the leading `//! ` stripped) or a `/*! ... */` block (with the `/*!`/`*/` envelope and per-line `*` indent stripped) — accumulates, in source order joined by newlines, into the enclosing module's inner-doc summary and is never attached to any specific item. The buffer is committed as the module's inner documentation after all items are processed.

**Evidence:** `tools/peg_gen/grammars/logos.peg#L541-L554`, `src/compiler/sema.cpp#L7412-L7428`, `src/compiler/sema.cpp#L8022-L8030`

### `item.doc.outer-block` — Outer block doc-comment

An outer block doc-comment `/** ... */` is an item/member-stream element with the same next-item binding role as line doc-comments; the `/**` envelope and per-line leading `*` are stripped and lines joined with newline.

**Evidence:** `tools/peg_gen/grammars/logos.peg#L544-L549`

### `item.doc.outer-line` — Outer line doc-comment binds to next item

An outer line doc-comment (`///`, captured as DOC_LINE) is an item-stream element; consecutive outer doc-comments accumulate and attach to the next real item.

**Evidence:** `tools/peg_gen/grammars/logos.peg#L536-L537`

### `item.doc.outer-line-block` — Outer doc-comments (/// and /**) attach to next item

`///` line doc-comments (with leading '/// ' stripped, joined by newline) and `/** ... */` outer block doc-comments accumulate into the pending doc buffer and become the DOC of the next item.

**Evidence:** `src/compiler/sema.cpp#L7399-L7411`

## Annotations and Attributes

### `item.annotation.arg-forms` — Attribute argument forms

Within an attribute argument list, an argument is one of: `IDENT(args)` (nested call), `IDENT = lit` (key-value), a bare literal (positional), or a bare IDENT (legacy). A literal may be an enum ref `IDENT::IDENT`, raw/normal string, float, integer, `true`/`false`, or a bracketed array of literals. Lists allow a trailing comma.

```logos
#[cfg(target = "x86")]
#[align(8)]
#[list([1, 2, 3])]
```

**Evidence:** `tools/peg_gen/grammars/logos.peg#L648-L672`

### `item.annotation.attribute-forms` — annotation/attribute syntax

An annotation is `#[NAME]`, `#[NAME = literal]`, or `#[NAME(args...)]`; arguments may be positional or `key = value`, and an argument value may be an array literal `[ ... ]`.

**Evidence:** `src/compiler/sema_render.cpp#L1400-L1460`

### `item.annotation.forms` — Outer attribute forms

An attribute is `#[ NAME (args) ]`, `#[ NAME = val ]`, or `#[ NAME ]`. The `= val` form admits an enum literal `IDENT::IDENT` or an integer.

```logos
#[derive(Debug)]
#[repr = 8]
#[inline]
```

**Evidence:** `tools/peg_gen/grammars/logos.peg#L619-L641`

### `item.annotation.inner-attribute` — Inner attribute attaches to enclosing module

An inner attribute `#![ ... ]` (with the same `(args)` / `= val` / flag payload shapes as an outer attribute) attaches to the enclosing module rather than the following item. (Currently only `#![no_implicit_prelude]`.)

```logos
#![no_implicit_prelude]
```

**Evidence:** `tools/peg_gen/grammars/logos.peg#L631-L636`

## Attribute Semantics

### `item.attr.datatype-promotion` — #[datatype]/#[annotation] promote a struct into the datatype pipeline

A struct-syntax item annotated `#[datatype]` or `#[annotation]` is treated as a datatype declaration; `#[zoned]` marks self-relative fields and does NOT promote a struct to a datatype.

**Divergence:** Logos addition: datatype/annotation/zoned attributes (no Rust equivalent).

**Evidence:** `src/compiler/sema_collect.cpp#L366-L431`

### `item.attr.struct-enum-flag-set` — Struct/enum attribute flag vocabulary

The recognised struct/enum modifier attributes are exactly: `datatype`, `annotation`, `zoned`, `zone_mut`, `rel_ptr`, `self_describing`, `pinned`, `borrow_carrying`, `no_auto_drop`, `non_null`. A struct bearing `#[datatype]` or `#[annotation]` is promoted to the datatype pipeline.

**Divergence:** Logos-specific memory/zone attribute set; no Rust analogue.

**Evidence:** `src/compiler/sema_impl.hpp#L1430-L1460`

### `item.attr.target-kind-validity` — Built-in attributes restricted to declared item kinds

Each compiler-recognised attribute is valid only on a fixed set of item kinds: `type_code`→{struct,datatype,enum,trait}; `zoned`→{struct,enum}; `datatype`→{struct}; `self_describing`/`rel_ptr`/`pinned`/`zone_mut`/`no_auto_drop`/`non_null`→{struct}; `borrow_carrying`→{struct,enum}; `annotation`→{struct,datatype}; `tag_dispatch`→{trait}; `metaprog_handler`/`no_mangle`/`fn_macro`/`token_macro`/`test`/`should_panic`/`ignore`→{fn}; `cfg`/`cfg_attr`→{all item kinds}; `repr`→{struct,enum}. Applying a built-in attribute to a disallowed kind is an error; an unrecognised name is treated as a user `#[annotation]` lookup.

```logos
#[zoned] enum E {}  // ok
#[datatype] enum E {} // error (struct only)
```

**Evidence:** `src/compiler/sema_impl.hpp#L1462-L1507`

### `item.attr.unknown-warn` — unknown attribute is warned

A top-level user `#[name]` attribute that is neither a builtin attribute, a registered metaprog-handler trigger, nor the name of an `#[annotation]` datatype is a warning (likely typo, missing import, or removed handler).

**Evidence:** `src/compiler/sema_collect.cpp#L607-L665`

## Representation Attributes

### `item.repr.recognized-modes` — `#[repr(...)]` minimal recognised modes

`#[repr(...)]` is recognised only on structs (`transparent`) and enums (integer-discriminant width). Other repr modes are parsed and then rejected (no silent acceptance).

**Divergence:** Only `transparent` (struct) and integer-width (enum) repr supported; Rust's `C`/`packed`/`align`/etc. not yet.

**Evidence:** `src/compiler/sema_impl.hpp#L1501-L1505`

## Conditional Compilation (cfg)

### `item.cfg.conditional-compilation` — cfg attributes gate item lowering

An item whose pending cfg attributes evaluate false (cfg_attrs_drop_item) is dropped before lowering; its pending annotations and doc are consumed and discarded.

```logos
#[cfg(unix)] fn f() {}
```

**Evidence:** `src/compiler/sema.cpp#L7435-L7439`

### `item.cfg.drop-disabled` — cfg-disabled items are dropped

Before collecting an item, `cfg_attr` activations are applied and `cfg(...)` predicates evaluated against pending annotations; if any predicate is false the item is dropped entirely (neither collected nor lowered) together with its pending annotations.

**Evidence:** `src/compiler/sema_collect.cpp#L1437-L1446`

### `item.cfg.gate-before-registration` — cfg-false items do not register their name

A `#[cfg(...)]` predicate is evaluated before name registration; an item whose cfg is false registers no name. This permits the same-name-under-mutually-exclusive-cfg idiom (e.g. cfg(unix)/cfg(windows) structs) without a duplicate-name error.

**Evidence:** `src/compiler/sema_collect.cpp#L358-L365`

## const Items

### `item.const.def` — Module-level constant definition

A module constant is `[pub] (const|let) NAME [<params>] : T = expr ;`. The `const` keyword admits an optional type-parameter list, making the RHS a generic compile-time factory substituted at each use site; `let` stays non-generic. Both forms require an explicit type annotation and an initializer.

```logos
pub const MAX: i32 = 100;
const PMap<K,V>: WritStatic = @{...};
let X: u8 = 1;
```

**Divergence:** `let` accepted as a const keyword at module level; generic `const NAME<...>` factory has no direct Rust analog.

**Evidence:** `tools/peg_gen/grammars/logos.peg#L688-L699`

### `item.const.generic-and-typed` — const item with optional generics and type

A const item is `[pub] const NAME [<type-params>] [: TYPE] = VALUE ;`; const items may be generic.

**Divergence:** Generic const items (const with type parameters) are a Logos extension.

**Evidence:** `src/compiler/sema_render.cpp#L1192-L1211`

### `item.const.inlined-value` — const carries an inlined value expression

A `const` item stores its initializer as a VALUE expression that downstream codegen inlines at each use site (contrasted with statics, which have one global per item).

```logos
const K: i32 = 10;
```

**Evidence:** `src/compiler/sema.cpp#L7845-L7853`

## static Items

### `item.static.address-place-machinery` — static items addressed as places

Every `static [mut]` item has link symbol `<pkg>$<NAME>` (extern-block-declared statics keep the bare name); reads lower as a dereference of the static's address and writes as a store through the same address.

**Evidence:** `src/compiler/sema_impl.hpp#L2886-L2892`, `src/compiler/sema_impl.hpp#L2905-L2910`

### `item.static.aggregate-init-by-copy` — aggregate static initialized by value-copy

If a static's type is an aggregate (struct, zoned struct, tuple, array, slice, closure, or a tagged enum) and its initializer evaluates to a pointer to the value, the static is initialized by copying the full value (size = size_of(T)) into the static's storage; scalar (non-aggregate) statics are initialized by a single store.

**Evidence:** `src/compiler/mlir_gen_dyn.cpp#L741-L756`

### `item.static.def` — Module-level static definition

`[pub] static [mut] NAME : T = expr ;` defines a true global with stable storage and address (one global symbol; `&STATIC` identity holds), distinct from `const` inline substitution. The `mut` form (matched before the immutable form) marks mutable storage; without `mut`, reads are safe and writes are rejected.

```logos
static COUNTER: u64 = 0;
static mut FLAG: bool = false;
```

**Evidence:** `tools/peg_gen/grammars/logos.peg#L705-L716`

### `item.static.extern-requires-unsafe` — Access to an extern-block static requires unsafe

A static declared in an extern block (declaration only, foreign storage) requires `unsafe` at every access.

**Evidence:** `src/compiler/sema_impl.hpp#L1931-L1933`

### `item.static.global-storage` — static gets global storage with symbol, mut/extern flags

A `static` item is lowered with IS_STATIC set, real global storage keyed by a module-qualified symbol (fallback pkg$name); `static mut` sets IS_MUT; a static lacking VALUE is extern (IS_EXTERN, no initializer emitted).

```logos
static X: i32 = 5;
static mut Y: i32 = 0;
```

**Evidence:** `src/compiler/sema.cpp#L7854-L7876`

### `item.static.global-storage-and-mut-safety` — static items have global storage; mut access is unsafe

`static [mut] NAME: T = expr;` is a true global with a stable address and `&STATIC` identity. Reads and writes of a `static mut` require `unsafe`. A static with no initializer is an extern (external-linkage) declaration.

**Evidence:** `tools/peg_gen/grammars/logos.peg#L322`

### `item.static.immutability-not-by-const-global` — immutable static stays writable storage; immutability enforced at sema

Storage for an immutable (non-`mut`) `static` is NOT a read-only constant; it is writable storage assigned once at startup. Immutability of a non-`mut` static is enforced by rejecting writes during semantic analysis, not by making the storage constant.

**Evidence:** `src/compiler/mlir_gen_dyn.cpp#L702-L708`

### `item.static.link-symbol` — static link symbol qualification

A `static` with a value is registered with a module-qualified link symbol `<module_id>.<pkg>$<name>` (bare `<pkg>$<name>` when module_id empty) so two modules declaring the same `pkg::NAME` do not collide; an extern static (no value) links against the bare name.

**Evidence:** `src/compiler/sema_collect.cpp#L1859-L1880`

### `item.static.mut-requires-unsafe` — static mut access requires unsafe

Reading or place-assigning a `static mut` item requires an enclosing `unsafe` block.

**Evidence:** `src/compiler/sema_impl.hpp#L2880-L2884`

### `item.static.runtime-initialized-storage` — static items get zero-init storage filled at program startup

A non-extern `static` has global storage that is zero-initialized at link time and assigned its declared initializer value at program startup (before `main`), via a synthesized startup initializer running every static's init expression in declaration order. A `static`'s initializer is thus an ordinary runtime-evaluated expression, not a compile-time constant.

**Divergence:** Rust requires `static` initializers to be const-evaluable; Logos evaluates them at runtime startup instead.

**Evidence:** `src/compiler/mlir_gen_dyn.cpp#L702-L714`, `src/compiler/mlir_gen_dyn.cpp#L716-L758`

### `item.static.shadowing-by-binding` — Local/param binding shadows a module static

A module static name is treated as a static reference only when not shadowed by an in-scope local binding or a type/const-generic parameter of the same name.

**Evidence:** `src/compiler/sema_impl.hpp#L2894-L2903`

### `item.static.unsafe-access` — static mut and extern static require unsafe

A `static mut` is recorded as a mutable static (its reads/writes require `unsafe`); an extern static (declared with no value) is recorded as an extern static, every access of which requires `unsafe`.

**Evidence:** `src/compiler/sema_collect.cpp#L1881-L1887`

## static fn (Associated Constructors)

### `item.static-fn.def` — Static (associated) function definition

`[pub] static [unsafe] fn NAME [<params>] (params) [-> T] { ... }` defines an associated/free function with no `self` receiver; its own optional type-parameter list follows the name, matching instance/free fn generics. The name may be the `new` keyword.

```logos
static fn make<T>(x: T) -> Self { ... }
pub static fn new() -> Self { ... }
```

**Divergence:** `static fn` spelling for associated (no-self) functions; Rust uses an `fn` without a `self` parameter inside an impl.

**Evidence:** `tools/peg_gen/grammars/logos.peg#L1067-L1093`

## Type Aliases

### `item.type-alias.def` — Type alias definition

`[pub] type NAME [<params>] = <type_ref> ;` introduces a type alias, optionally generic via a type-parameter list.

```logos
type Pair = (i32, i32);
pub type Map<K,V> = HashMap<K,V>;
```

**Evidence:** `tools/peg_gen/grammars/logos.peg#L720-L727`

### `item.type-alias.duplicate` — Type alias uniqueness per package

Two type aliases with the same name in the same package are an error. A same-name alias from a different package is permitted: the incumbent (first/other-package) keeps the bare name slot and the newcomer registers only under its package-qualified key `pkg::Name`. Lookup probes `cur_package_::name` first, so user code resolves to its own alias.

**Uncertainty:** Cross-package shadowing semantics inferred from the registration logic and comment.

**Evidence:** `src/compiler/sema_collect.cpp#L2127-L2142`

### `item.type-alias.generic` — type alias with optional generics

A type alias is `[pub] type NAME [<type-params>] = TYPE ;`.

**Evidence:** `src/compiler/sema_render.cpp#L1213-L1224`

### `item.type-alias.no-inferred-rhs` — Type alias RHS may not be the inferred placeholder

A type alias RHS is resolved in item-signature context; `type T = _;` is rejected (no inference context for item signatures). (Rust E0121)

**Evidence:** `src/compiler/sema_collect.cpp#L2114-L2119`

## struct Definitions

### `item.struct.attr-flags` — structural struct attribute flags

Recognised structural struct attributes set per-struct flags: no_auto_drop, self_describing, rel_ptr, pinned, zone_mut, zoned (zoned2), borrow_carrying, non_null.

**Divergence:** Logos addition (zone/memory-model attributes).

**Evidence:** `src/compiler/sema_collect.cpp#L1557-L1573`

### `item.struct.custom-dst-last-field-unsized` — Trailing unsized field makes the struct a custom DST

A struct whose LAST field has unsized type (`[T]`, `dyn Trait`, or nested DST) is itself unsized (is_dst); such a struct may appear only behind `&`/`&mut`/`*const`/`*mut`/`Box`, and is constructed via unsafe raw-parts assembly (never by value).

**Evidence:** `src/compiler/sema_impl.hpp#L2428-L2434`

### `item.struct.explicit-inst` — Explicit struct instantiation declaration

`[pub[(vis)]] struct TYPE_REF ;` where TYPE_REF carries type arguments (e.g. `struct Foo<i64>;`) is an explicit-instantiation declaration binding annotations to a generic struct instantiation. The dedicated `instantiate Foo<T>;` form is preferred.

**Divergence:** A6: see B-item-92 — bare `struct Foo;` is the unit struct, generic form kept for the unbound-typevar diagnostic

**Evidence:** `tools/peg_gen/grammars/logos.peg#L1133-L1138`

### `item.struct.explicit-instantiation-needs-concrete-args` — Explicit struct/datatype instantiation requires concrete type args

A bodyless `struct Foo<args>;` / `datatype Foo<args>;` (NAME absent, TYPE present) is an explicit instantiation: type args must be concrete (no unbound type vars), else it is an error directing to write the body; a bare `struct Foo;` referencing an undefined name is also an error.

**Evidence:** `src/compiler/sema.cpp#L7538-L7620`, `src/compiler/sema.cpp#L7668-L7712`

### `item.struct.field-name-unique` — Struct field names must be unique

Within a struct declaration, two fields may not share a name; a duplicate is a compile error.

**Evidence:** `src/compiler/sema_decl.cpp#L1291-L1294`

### `item.struct.fields-and-inherent-methods` — struct item form with optional inherent methods

A struct is `[pub] struct NAME [<type-params>] { fields... }`, or `[pub] struct NAME [<type-params>] ;` when field-less; each field is `[pub] NAME : TYPE [...]`. Inherent methods may be declared in the struct body, which is equivalent to a separate `impl NAME { ... }` block.

**Divergence:** Legacy `struct Foo { fields, fn ... }` form (methods inside the struct body) is accepted; not a Rust form.

**Evidence:** `src/compiler/sema_render.cpp#L1140-L1150`, `src/compiler/sema_render.cpp#L1251-L1308`

### `item.struct.generic-inline-method-self` — Inline methods of a generic struct bind Self to the generic self-type

For a generic struct `Struct<T...>`, methods declared in the struct body are lowered as if inside `impl<T...> Struct<T...>`: `Self` is bound to `Struct<T...>`, the struct's type params are recorded as the method's impl type-params, and the impl target pattern is `Struct<T...>` — so `-> Self` (and other Self uses) substitute correctly at monomorphization. Non-generic structs lower body methods with their own type params directly.

```logos
struct Pair<A,B>{a:A,b:B; fn make(a:A,b:B)->Self{Self{a,b}}}  // Pair::<i32,i32>::make(..) yields Pair<i32,i32>
```

**Evidence:** `src/compiler/sema_decl.cpp#L1307-L1372`

### `item.struct.generic-method-drops-struct-params` — Generic struct body methods keep only method-level type params

When lowering a body method of a generic struct, type parameters that coincide with the struct's own type parameters are removed from the method's TYPE_PARAMS (mono re-injects them via IMPL_TYPE_PARAMS); only method-introduced type parameters remain method-level.

**Evidence:** `src/compiler/sema_decl.cpp#L1342-L1364`

### `item.struct.inline-methods-self-binding` — Inline struct-body methods get Self + struct type-params in scope

Methods declared inline in a struct body are collected with `Self` bound to the struct's (possibly generic) self-type and the struct's type parameters installed as impl type-params, identically to `impl`-block methods. For a generic struct, `Self = Name<TVs...>` and generic methods are routed so static calls (`Pair::<i32,i32>::make()`) substitute the struct's params; for a non-generic struct `Self = Name`.

**Evidence:** `src/compiler/sema_collect.cpp#L4084-L4128`

### `item.struct.lifetime-param-unique` — Struct lifetime parameters must be uniquely named

Within a struct declaration, two lifetime parameters may not share a name; a duplicate is a compile error.

**Evidence:** `src/compiler/sema_decl.cpp#L1271-L1273`

### `item.struct.named-def` — Named-field struct definition

`[pub[(vis)]] struct IDENT [<type-params>] [where-clause] { field_def_or_doc* method_def_or_doc* }` defines a struct with named fields, optional generics, an optional where-clause, and optional inline method definitions.

```logos
pub struct S<T> where T: Clone { x: T, fn get(&self) -> &T { &self.x } }
```

**Evidence:** `tools/peg_gen/grammars/logos.peg#L1149-L1150`, `tools/peg_gen/grammars/logos.peg#L1160-L1161`

### `item.struct.no-auto-drop` — #[no_auto_drop] suppresses compiler-emitted drop

A struct marked `#[no_auto_drop]` receives NO compiler-emitted automatic Drop (neither user-drop invocation nor field drop glue) — the ManuallyDrop<T> lang-item shape.

**Evidence:** `src/compiler/sema_impl.hpp#L2427`

### `item.struct.repr-other-rejected` — non-transparent struct repr modes rejected

On a struct, `#[repr]` with no argument is an error, and any repr mode other than `transparent` (e.g. `C`, `packed`, `align(...)`) is parse-then-reject (not silently accepted).

**Evidence:** `src/compiler/sema_collect.cpp#L1583-L1610`

### `item.struct.repr-transparent` — #[repr(transparent)] requires single field

`#[repr(transparent)]` on a struct sets repr_transparent (the wrapper inherits its single field's layout) and requires the struct to have exactly one field, else it is rejected.

```logos
#[repr(transparent)] struct W(i32)
```

**Evidence:** `src/compiler/sema_collect.cpp#L1581-L1604`

### `item.struct.self-describing-thin-ptr` — #[self_describing] custom-DST uses a thin raw pointer

A custom-DST struct marked `#[self_describing]` has in-band recoverable tail length/metadata, so raw `*const T`/`*mut T` to it is a THIN pointer (metadata recovered at deref) rather than a fat DstRef.

**Evidence:** `src/compiler/sema_impl.hpp#L2435-L2439`

### `item.struct.transparent-collapses-layout` — repr(transparent) collapses to the single field's layout

A struct annotated `#[repr(transparent)]` has the layout (size/alignment/ABI) of its single field.

**Uncertainty:** Single-field constraint is enforced elsewhere; this unit only propagates the flag.

**Evidence:** `src/compiler/sema_decl.cpp#L1205-L1207`

### `item.struct.tuple-def` — Tuple struct definition

`[pub[(vis)]] struct IDENT [<type-params>] ( tuple_field (, tuple_field)* ) ;` defines a tuple struct whose fields are types only; field names are synthesized as "0","1",… so `foo.0` and pattern `Foo(a,b)` work uniformly with named-field structs. Each tuple_field may carry its own `pub`.

```logos
pub struct Pair(pub i32, i32);
```

**Evidence:** `tools/peg_gen/grammars/logos.peg#L1151-L1152`, `tools/peg_gen/grammars/logos.peg#L1174-L1180`

### `item.struct.tuple-struct-fields` — Tuple-struct field shape and synthetic names

A struct whose first field definition carries no NAME is a tuple struct. Its positional fields are assigned synthetic decimal names "0", "1", … in declaration order, so member access (`foo.0`) and patterns (`Foo(a, b)`) reuse named-field machinery.

**Evidence:** `src/compiler/sema_collect.cpp#L4007-L4015`, `src/compiler/sema_collect.cpp#L4035-L4048`

### `item.struct.tuple-struct-positional` — Tuple struct: positional fields, call-form ctor and pattern

`struct Foo(T1, T2);` declares a tuple struct with positional fields; its constructor is the call form `Foo(a, b)` and its pattern is `Foo(x, y)`.

**Evidence:** `src/compiler/sema_impl.hpp#L2426`

### `item.struct.type-param-unique` — Struct type parameters must be uniquely named

Within a struct declaration, two type parameters may not share a name; a duplicate is a compile error.

**Evidence:** `src/compiler/sema_decl.cpp#L1265-L1269`

### `item.struct.unit-decl` — Unit struct declaration

`[pub] struct IDENT ;` declares a zero-field (unit) struct. A bare IDENT immediately followed by `;` is a unit struct; `struct Foo<...>;` (IDENT then `<`) is instead parsed as an explicit instantiation. This rule MUST be matched before struct_inst.

```logos
pub struct Foo;
```

**Evidence:** `tools/peg_gen/grammars/logos.peg#L1120-L1131`

### `item.struct.where-clause-named-only` — Where-clause only on IDENT-name struct alternatives

A struct/enum definition where-clause is accepted only on the IDENT-NAME alternatives, not on the antiquot (NAME_VAR / `#`-prefixed) alternatives, because WHERE and NAME_VAR share an AST slot.

**Uncertainty:** Slot-sharing is an implementation constraint surfaced as a grammar restriction.

**Evidence:** `tools/peg_gen/grammars/logos.peg#L1140-L1150`

### `item.struct.zoned-field-promotes-to-datatype` — Struct with a zoned-struct field is not plain data

A struct is plain-data (is_data_plain) unless any of its fields has zoned-struct kind, in which case it is a (non-plain) zoned datatype.

**Evidence:** `src/compiler/sema_impl.hpp#L2424`

### `item.struct.zoned-promotes-to-datatype` — #[zoned] struct lowered as a datatype (zoned struct)

A struct carrying #[zoned] (promotes_to_datatype) is lowered with IS_ZONED set, treated as a zoned struct/datatype rather than a plain struct.

**Evidence:** `src/compiler/sema.cpp#L7625-L7660`

## Tuple Structs

### `item.tuple-struct.synthetic-field-names` — Tuple-struct fields named by ordinal

Tuple-struct fields are named by their zero-based positional index rendered as a decimal string ("0", "1", ...).

**Evidence:** `src/compiler/sema_impl.hpp#L2921-L2932`

## union Definitions

### `item.union.collected-as-struct` — union shares struct collection shape

A `union NAME { … }` is collected with the same named-field/type-param shape as a struct and registered as a known type, with its `is_union` flag set.

```logos
union U { i: i32, f: f32 }
```

**Evidence:** `src/compiler/sema_collect.cpp#L1457-L1469`

### `item.union.def` — Union definition

`[pub[(vis)]] union IDENT [<type-params>] [where-clause] { field_def_or_doc* }` defines a union with named fields and optional generics. It is collected internally as a struct flagged `is_union`; no tuple shape, no methods.

```logos
union U { a: i32, b: f32 }
```

**Evidence:** `tools/peg_gen/grammars/logos.peg#L1163-L1172`

### `item.union.field-copy-restriction` — union field types restricted to non-move types

Each non-generic union field type must not be a move type (Vec/Box/String/owning trait object); allowed are Copy types, references, ManuallyDrop<T>, or aggregates thereof. A field whose type is a bare type-parameter is exempt at collection (checked at monomorphization); a field that is itself a union is allowed.

**Divergence:** B: generic-union Copy check is deferred to mono rather than enforced at use site as in Rust.

**Uncertainty:** Slice-1 uses is_move_type as the rejection oracle; full ManuallyDrop/tuple/array recursion is a follow-up.

**Evidence:** `src/compiler/sema_collect.cpp#L1502-L1530`

### `item.union.field-write-safety` — Union field write is safe, read is unsafe

Writing a union field is safe and does not require `unsafe`; only reading a union field requires an enclosing `unsafe` block.

**Evidence:** `src/compiler/sema_impl.hpp#L2912-L2919`

### `item.union.layout-and-unsafe-access` — Union layout and unsafe field access

A `union NAME { f: T, ... }` is a struct-shaped type whose size is max-of-fields aligned to max field alignment; reading or writing a union field requires an `unsafe` block.

**Evidence:** `tools/peg_gen/grammars/logos.peg#L321`

### `item.union.lowered-as-struct` — union lowered through struct path

A `union` definition is lowered through the same path as a struct (same field shape); layout/unsafe-gating is a separate concern.

**Evidence:** `src/compiler/sema.cpp#L7528-L7537`

### `item.union.max-of-fields-layout-unsafe-read` — union layout and unsafe field read

A type declared `union NAME { … }` has layout = max-of-fields size aligned to max field alignment (vs struct's sum-of-fields); only one field is active at a time (the active one is implementation-defined) and every field READ requires an enclosing `unsafe`.

**Evidence:** `src/compiler/sema_impl.hpp#L2480-L2487`

### `item.union.no-empty` — fieldless union rejected

A union with zero fields is rejected; a union must declare at least one field.

```logos
union U {} // error
```

**Evidence:** `src/compiler/sema_collect.cpp#L1474-L1480`

### `item.union.shared-namespace` — unions share the struct/enum type namespace

Union definitions occupy the same type namespace as structs and enums; a union name conflicts with a struct/enum of the same name, and `type Alias = U;` resolves U as a type.

**Evidence:** `src/compiler/sema_collect.cpp#L390-L413`

## enum Definitions

### `item.enum.def` — Enum definition

`[pub] enum NAME [<params>] [: backing_type] [where ...] { variants }` defines an enum, with optional generic params, an optional explicit backing integer type after `:`, and an optional where-clause. A metacall-named form `enum #(<expr>) ...` derives the enum name from a compile-time expression. Where-clauses are permitted only on IDENT-named (not expr-named) enums.

```logos
enum Color { Red, Green, Blue }
enum Tags : u64 { X = 0xdead }
pub enum Option<T> { Some(T), None }
```

**Evidence:** `tools/peg_gen/grammars/logos.peg#L735-L751`

### `item.enum.default-backing-i32` — Enum default discriminant backing type is i32

An enum with no explicit backing type uses i32 as its discriminant backing type.

**Evidence:** `src/compiler/sema_impl.hpp#L2583`

### `item.enum.discriminant-const-expr` — enum discriminant from const expression

An enum discriminant may be given by a general const expression (e.g. `1 << 1`), evaluated via CTFE; a `metacall { <expr> }` discriminant must contain a single integer tail expression, evaluated via CTFE to the discriminant value.

```logos
enum E { A = 1 << 1, B = metacall { 4 } }
```

**Divergence:** A1: const-eval at discriminant position is via metacall/CTFE rather than miri.

**Evidence:** `src/compiler/sema_collect.cpp#L1985-L2026`

### `item.enum.discriminant-default` — implicit enum discriminant sequencing

An enum variant without an explicit discriminant takes the value 0 for the first such variant and previous+1 thereafter; an explicit value resets the running counter to value+1.

**Evidence:** `src/compiler/sema_collect.cpp#L1926-L1942`, `src/compiler/sema_collect.cpp#L2097`

### `item.enum.discriminant-fits` — enum discriminant must fit backing type

When an enum has a backing type, each variant's discriminant value must fit in that backing integer type, else it is rejected.

**Evidence:** `src/compiler/sema_collect.cpp#L2028-L2032`

### `item.enum.discriminant-from-other-enum` — enum discriminant referencing another enum's variant

An enum discriminant may be `OtherEnum::OtherVariant` (with optional `as T` cast dropped): the referent is resolved among already-collected enums and its discriminant value is used verbatim; an unknown enum or variant is rejected.

**Evidence:** `src/compiler/sema_collect.cpp#L1943-L1984`

### `item.enum.empty-legal` — empty enum body is legal

An enum with an empty body is legal (an uninhabited / marker type); no diagnostic is emitted.

```logos
enum Void {}
```

**Evidence:** `src/compiler/sema_collect.cpp#L1900-L1902`

### `item.enum.explicit-discriminant` — Enum variants carry an explicit/assigned discriminant and optional backing type

Each enum variant has an integer discriminant value; an enum may declare an explicit backing integer type for its discriminant.

**Evidence:** `src/compiler/sema_decl.cpp#L1410`, `src/compiler/sema_decl.cpp#L1486-L1492`

### `item.enum.repr-and-variants` — enum item form

An enum is `[pub] enum NAME [<type-params>] [: TYPE] { variant, ... }` where the optional `: TYPE` gives the discriminant representation type; each variant is `NAME [(types...)] [= [-]discriminant]`.

**Evidence:** `src/compiler/sema_render.cpp#L1152-L1174`, `src/compiler/sema_render.cpp#L1226-L1249`

### `item.enum.repr-int-width` — #[repr(uN/iN)] sets enum discriminant width

`#[repr(I)]` on an enum where I is an integer type (u8/u16/u32/u64/i8/i16/i32/i64/usize/isize) sets the enum's backing (discriminant) type; it conflicts with (errors against) an already-declared `enum Foo : I'` backing type when I≠I'. `#[repr(C)]` and other non-integer modes are parse-then-reject.

```logos
#[repr(u8)] enum E { A, B }
```

**Evidence:** `src/compiler/sema_collect.cpp#L1698-L1744`

### `item.enum.struct-shape-variant` — Struct-shape enum variant carries named payload fields

An enum variant `V { x: T, y: U }` is a struct-shape variant with named payload fields (a names array parallel to payload types); user-written field names are resolved to positional indices. Tuple-shape and unit variants carry no payload field names.

**Evidence:** `src/compiler/sema_impl.hpp#L2562-L2574`

### `item.enum.type-param-unique` — Enum type parameters must be uniquely named

Within an enum declaration, two type parameters may not share a name; a duplicate is a compile error.

**Evidence:** `src/compiler/sema_decl.cpp#L1472-L1475`

### `item.enum.variant-name-unique` — Enum variant names must be unique

Within an enum declaration, two variants may not share a name; a duplicate is a compile error.

**Evidence:** `src/compiler/sema_decl.cpp#L1476-L1479`

### `item.enum.variant-payload-shapes` — enum variant payload shapes

Enum variant payloads may be tuple-style (positional types), struct-shape (named fields, in declaration order, names must be unique), or variadic (single type ref); payload type positions are item signatures in which `_` is rejected (E0121).

```logos
enum E { Tup(i32, i32), Rec { x: i32 }, Var(i32) }
```

**Evidence:** `src/compiler/sema_collect.cpp#L2033-L2091`

### `item.enum.variant-shapes` — Enum variant shapes

A variant is one of: unit `Name`; tuple `Name(T, ...)`; variadic-tuple `Name(...T)`; struct-shape `Name { f: T, ... }` (fields may be `pub`); empty struct-shape `Name {}`; or a discriminant-bearing `Name = <disc>`. Variant lists allow leading doc-comments per variant and a trailing comma.

```logos
Some(T)
Point { x: i32, y: i32 }
Empty {}
Args(...i32)
```

**Divergence:** Variadic-tuple variant `Name(...T)` has no Rust analog.

**Evidence:** `tools/peg_gen/grammars/logos.peg#L753-L786`, `tools/peg_gen/grammars/logos.peg#L757-L775`

### `item.enum.zoned-attr` — #[zoned]/#[borrow_carrying] on enum

`#[zoned]` on an enum sets its zoned2 flag (niche-enum Ref arm stored self-relative at rest, absolute as value); `#[borrow_carrying]` sets the borrow_carrying flag.

**Divergence:** Logos addition.

**Evidence:** `src/compiler/sema_collect.cpp#L1681-L1692`

## Fields

### `item.field.named` — Named field definition

A struct field is `[pub] IDENT : TYPE_REF [,]`. The contextual keywords `new` and `null` are also accepted as field names. A trailing comma is permitted.

**Evidence:** `tools/peg_gen/grammars/logos.peg#L1191-L1202`

### `item.field.repeat-group` — Repeat-group field (quote)

`#( field_def ),*` and `#( field_def )*` denote a repeat-group of field definitions (REPEAT_GROUP, OP=1 comma-separated / OP=0 plain), for use in quoted item bodies.

**Divergence:** A6

**Evidence:** `tools/peg_gen/grammars/logos.peg#L1183-L1186`

### `item.field.variadic` — Variadic field

A field of form `IDENT ... : TYPE_REF` marks a variadic field (IS_VARIADIC).

**Divergence:** A6

**Evidence:** `tools/peg_gen/grammars/logos.peg#L1203-L1204`

## datatype / eidos Definitions

### `item.datatype.def` — Writ datatype definition

A datatype item is `[pub[(vis)]] eidos NAME [<type-params>] { field_def_or_doc* }`. It declares a Writ-fabric datatype with named/repeat-group fields; the optional generic parameter list and visibility marker are accepted.

```logos
pub eidos Point<T> { x: T, y: T }
```

**Divergence:** A6

**Evidence:** `tools/peg_gen/grammars/logos.peg#L1096-L1100`

### `item.datatype.explicit-inst` — Explicit datatype instantiation declaration

`[pub[(vis)]] eidos TYPE_REF ;` (no body) is an explicit-instantiation declaration that binds metadata annotations (e.g. `#[type_code=N]`) to a concrete generic instantiation, e.g. `#[type_code=42] datatype Array<i32>;`.

**Divergence:** A6

**Evidence:** `tools/peg_gen/grammars/logos.peg#L1102-L1109`

### `item.datatype.is-zoned` — datatype/eidos is always zoned

A `datatype` (eidos) definition is always lowered with IS_ZONED set, including its specializations.

**Evidence:** `src/compiler/sema.cpp#L7757-L7761`, `src/compiler/sema.cpp#L7713-L7717`

### `item.datatype.type-code-register` — #[type_code=N] registers explicit type code

`#[type_code=N]` on a datatype registers N as the explicit type code for the datatype's fully-qualified name, making it resolvable by impl-collection in the same pass; `#[annotation]` flags the datatype as a user-annotation type.

**Divergence:** Logos addition (Writ datatype family).

**Evidence:** `src/compiler/sema_collect.cpp#L1654-L1667`

### `item.datatype.type-code-unique` — exclusive datatype annotations are unique

On a datatype, the exclusive annotations `#[type_code]` and `#[annotation]` may each appear at most once; a duplicate occurrence is rejected.

**Divergence:** Logos addition.

**Evidence:** `src/compiler/sema_collect.cpp#L1641-L1652`

## genos Definitions

### `item.genos.specialization-decl` — genos specialization decl propagates type_code to like-named eidos

A bodyless `genos Name<args>;` (trait-name TYPE, no NAME) records an instantiation annotation; its #[type_code=N] is registered under the canonical and mangled (concrete-struct) names of the like-named eidos/struct, mirrored under both the current and the template's package.

**Evidence:** `src/compiler/sema.cpp#L7887-L7994`

## Explicit Instantiation

### `item.instantiate.generic-only` — instantiate decl requires a generic target with type args

`instantiate T;` requires T to be a struct/datatype/enum with non-empty type args; `instantiate Foo;` on a non-generic type is an error ('only applies to generic templates'), and a non-struct/datatype/enum target is an error.

```logos
instantiate Foo<i32>;
```

**Evidence:** `src/compiler/sema.cpp#L7440-L7470`

## Traits

### `item.trait.explicit-inst` — Explicit genos/trait specialization declaration

`[pub[(vis)]] <trait-kw> TYPE_REF ;` (no body) binds annotations to a logical-family (genos) specialization of a concrete trait instantiation; implementing eidos inherit the metadata via impl.

**Divergence:** A6

**Evidence:** `tools/peg_gen/grammars/logos.peg#L1111-L1118`

## impl Blocks

### `item.impl.items` — Impl item kinds

An impl item is a method definition, an associated-type impl `type NAME [<params>] = T ;`, or an associated-const impl `const NAME : T = expr ;`. Doc-comments may precede impl items.

```logos
type Item = i32;
const N: usize = 4;
```

**Evidence:** `tools/peg_gen/grammars/logos.peg#L1054-L1060`, `tools/peg_gen/grammars/logos.peg#L567`

### `item.impl.method-reattach-by-package` — Impl methods are attached to their target template only within the same package

An impl method (mangled `<Struct>__<method>__[fg]__<sig>`) whose `<Struct>` names a generic template is hosted on that template only when the template's package equals the method's package; a method with no package attaches to a sole same-named candidate. A cross-package bare-name collision (e.g. user `Rc` vs stdlib `Rc<T>`) does NOT cause adoption, so the method stays with its own struct's emission.

**Uncertainty:** This is an emission/hosting invariant observable as: same-named generics in distinct packages keep their own methods; surfaced as a language-level guarantee against method mis-hosting.

**Evidence:** `src/compiler/sema.cpp#L7002-L7054`

### `item.impl.negative` — Negative impl

`impl [<params>] !Trait for <target> [where ...] {}` declares a negative impl (the body must be empty), asserting that the target does not implement Trait.

```logos
impl !Send for Foo {}
```

**Evidence:** `tools/peg_gen/grammars/logos.peg#L992-L1004`

### `item.impl.target-mangling` — Impl self-type is mangled to a canonical target key by type shape

The impl target type is reduced to a canonical string key by shape: pointer/named struct → struct name (concrete generic instantiations use the monomorphized concrete name, generic/typevar instantiations keep the base name); `[T]` and `&[T]`/`&mut [T]` → `$slice$T` (typevar elem) or `$slice$<elem>` (concrete); `dyn Tr` → `$dyn$<Trait>`; `&U`/`&mut U` → `$ref_<U>`/`$mut_ref_<U>` (typevar pointee → `$ref$T`/`$mut_ref$T`); tuple `(...)` → `$tuple$N` (typevar elems) / `$tuple$N$<t1>$<t2>...` (concrete) / `$tuple$variadic` (variadic param); fn-pointer → `$fnptr$<arity>`; unit `()` → `void`. Collection and lowering use the same mangling so they agree.

**Evidence:** `src/compiler/sema_decl.cpp#L1688-L1820`, `src/compiler/sema_decl.cpp#L1782-L1814`

### `item.impl.targets` — Impl block forms and targets

`[unsafe] impl [<impl_params>] [Trait [<args>] for] <target> [where ...] { items }` defines an impl. Trait impls use `Trait for Target`; standalone (inherent) impls omit the trait. The target may be a simple type, pointer, reference, bare slice `[T]`, `dyn Trait`, tuple, or fn-pointer type. Each form admits an optional where-clause before the body.

```logos
impl Foo { ... }
impl<T> Trait for Struct<T> { ... }
impl Debug for (A, B) { ... }
impl<T> MyTrait for [T] { ... }
```

**Evidence:** `tools/peg_gen/grammars/logos.peg#L972-L1051`, `tools/peg_gen/grammars/logos.peg#L1021-L1030`

### `item.impl.trait-and-inherent` — impl block forms

An impl block is `[unsafe] impl[<impl-type-params>] TRAIT[<type-args>] for TYPE { items }` (trait impl) or `[unsafe] impl[<type-params>] TYPE { items }` (inherent impl); negative impls are permitted.

**Evidence:** `src/compiler/sema_render.cpp#L1310-L1373`

### `item.impl.type-params-source` — Impl type parameters come from IMPL_TYPE_PARAMS or (inherent only) TYPE_PARAMS

An impl block's own generic parameters are taken from the generic-trait-impl form `impl<T> Trait for U<T>` (its dedicated parameter list). For an inherent impl `impl<T> U<T>` (no trait), the parameters are taken from the type-parameter list instead. These parameters are in scope throughout the impl's target type, trait args, and method signatures/bodies, and are recorded on each lowered method.

**Evidence:** `src/compiler/sema_decl.cpp#L1674-L1683`, `src/compiler/sema_decl.cpp#L1866-L1869`

## Functions

### `item.fn.all-paths-return` — Non-void fn must return on every path

A function whose declared return type is non-void and non-error is rejected ("not all paths return a value") unless every control-flow path through its body returns or diverges; a trailing tail expression counts as an implicit return.

**Evidence:** `src/compiler/sema_decl.cpp#L1071-L1082`, `src/compiler/sema_collect.cpp#L4524-L4528`

### `item.fn.antiquot-name` — Function with antiquoted name

`[pub] [unsafe] fn #(expr) [<type-params>] ( [params] ) [-> T] block` carries an expr-TOM name (NAME_VAR), valid only inside a quote body; these alts omit the where-clause because NAME_VAR and WHERE share a slot.

**Divergence:** A6

**Evidence:** `tools/peg_gen/grammars/logos.peg#L1286-L1293`, `tools/peg_gen/grammars/logos.peg#L1312-L1319`

### `item.fn.def` — Function definition

A function item is `[pub[(vis)]] [unsafe] fn NAME [<type-params>] ( [param_list] ) [-> T] [where-clause] block`. NAME may be IDENT or the contextual keywords `new`/`null`. The where-clause and return type are optional.

```logos
pub unsafe fn f<T>(x: T) -> T where T: Copy { x }
```

**Evidence:** `tools/peg_gen/grammars/logos.peg#L1286-L1335`

### `item.fn.empty-body-void` — Omitted return type defaults to void

A fn that declares no return type has return type `()` (void).

**Evidence:** `src/compiler/sema_collect.cpp#L4477-L4479`, `src/compiler/sema_collect.cpp#L4669-L4671`

### `item.fn.impl-trait-param-desugar` — `impl Trait` argument-position param desugars to a fresh generic param

A top-level `impl <bound>` parameter type in argument position is desugared into a fresh synthetic generic type-parameter (carrying the corresponding trait bound) appended to the fn's type-param list, so the function becomes an ordinary generic. `impl Trait` in RETURN position is not desugared this way and instead retains opaque-type handling.

**Evidence:** `src/compiler/sema_collect.cpp#L4656-L4666`, `src/compiler/sema_impl.hpp#L2703-L2710`

### `item.fn.impl-trait-return-infer` — impl Trait return inferred from body

A function declared `-> impl Trait` resolves its return type to the single concrete type inferred from the body's return expressions; if none can be inferred it is an error.

```logos
fn f() -> impl Iterator { 0..3 }
```

**Evidence:** `src/compiler/sema_decl.cpp#L1061-L1070`

### `item.fn.name-underscore-reserved` — `_` reserved as a function name

A function declaration whose name is the single underscore `_` is ill-formed; `_` is reserved for ignored bindings (so `_(...)` cannot become a valid call expression).

```logos
fn _() {}  // error: '_' is reserved for ignored bindings
```

**Evidence:** `src/compiler/sema_decl.cpp#L144-L146`

### `item.fn.nested` — Nested function statement

A `fn name(params) [-> T] { ... }` at statement position is a nested function: its body is lifted to a top-level free function and the local name binds a fn-ptr value. A nested fn captures nothing; reads of enclosing locals are rejected (use a closure instead).

**Evidence:** `tools/peg_gen/grammars/logos.peg#L1829-L1837`

### `item.fn.never-fallback-precompute` — Body-diverges flag for `!` fallback

A fn whose body always diverges is flagged so that type-argument inference can apply the Rust-2024 `!`-fallback rule.

**Evidence:** `src/compiler/sema_collect.cpp#L4673-L4679`

### `item.fn.no-mangle` — #[no_mangle] keeps bare symbol name

`#[no_mangle]` on a function causes the bare base name to be used as the symbol name (no name mangling).

**Evidence:** `src/compiler/sema_collect.cpp#L1753-L1764`

### `item.fn.param-drop-epilogue` — By-value params dropped at function epilogue

By-value droppable (move-type) parameters are dropped at the function epilogue, equivalently to a `let` binding, when the body falls off the end without an explicit terminating return/break/continue. A parameter that was moved on any branch is conservatively skipped from the static epilogue drop (to avoid double-free on the move path).

```logos
fn consume(_x: Move) {}  // _x dropped at end
```

**Evidence:** `src/compiler/sema_decl.cpp#L1083-L1117`

### `item.fn.param-list-trailing-comma` — Parameter list trailing comma

A parameter list is `param (, param)* (,)?`, but a trailing comma is forbidden when immediately followed by `...` (the variadic marker), so `, ...` separators are unambiguous.

**Evidence:** `tools/peg_gen/grammars/logos.peg#L1337-L1342`

### `item.fn.param-pattern` — Pattern-binding parameters

A parameter may bind an irrefutable pattern: a tuple-destructure `(a, b, ...) : T`, a struct pattern `Name { f, .. } : T`, or a slice pattern `[h, t] : T`. Refutable patterns at the fn boundary are rejected in sema with the same diagnostic as for `let`.

```logos
fn f(Point { x, y }: Point) {}
```

**Evidence:** `tools/peg_gen/grammars/logos.peg#L1356-L1393`

### `item.fn.param-self-shorthand` — Self-receiver / ref-binding parameter shorthand

A parameter may be `&[mut] IDENT` (reference binding, type elided), `ref IDENT : T`, or `mut IDENT : T` (mutable local binding, mutability invisible to callers).

**Evidence:** `tools/peg_gen/grammars/logos.peg#L1344-L1355`

### `item.fn.param-variadic` — Variadic parameter

`IDENT : T ...` marks a variadic parameter (IS_VARIADIC); plain `IDENT : T` is the ordinary typed parameter.

**Divergence:** A6

**Evidence:** `tools/peg_gen/grammars/logos.peg#L1379-L1382`

### `item.fn.runtime-abi-no-mangle` — main, no_mangle, metacall thunks keep bare symbol

`main`, `#[no_mangle]` functions, and `__metacall_thunk_*` functions suppress package/signature mangling and keep their bare names as link symbols.

**Evidence:** `src/compiler/sema_collect.cpp#L4858-L4868`

### `item.fn.self-ref-param-type` — self-receiver parameter type

A `&self` / `&mut self` receiver parameter takes the type `&Self` / `&mut Self`, where `Self` is the in-scope Self type; mutability follows the `mut` marker.

**Evidence:** `src/compiler/sema_collect.cpp#L4498-L4502`, `src/compiler/sema_collect.cpp#L4643-L4647`

### `item.fn.signature-form` — function item signature form

A function is `[pub] [unsafe] [extern] fn NAME [<type-params>] (params) [-> RET_TYPE] BLOCK`, or terminated with `;` when bodyless (declaration only).

**Evidence:** `src/compiler/sema_render.cpp#L1375-L1398`

### `item.fn.signature-overloading` — Functions overloadable by signature

Functions are keyed by a signature derived from base name, parameter types, and vararg-ness, allowing multiple same-named functions to coexist; only an exact symbol-name collision (same package, base, signature) is a "duplicate function" error.

**Divergence:** Rust does not permit free-function overloading by signature.

**Evidence:** `src/compiler/sema_collect.cpp#L4712-L4713`, `src/compiler/sema_collect.cpp#L4837-L4881`

### `item.fn.tail-expr-is-return` — Tail expression is implicit return

Inside a fn body, a block's tail expression acts as an implicit return value (typed against the declared return type) for both lowering and reachability analysis.

**Evidence:** `src/compiler/sema_collect.cpp#L4519-L4523`

### `item.fn.tail-match-as-return` — Tail match arms are return values

When a non-void function's body ends with a `match` expression, each EXPR arm of that tail match is treated as the function's return value.

**Evidence:** `src/compiler/sema_decl.cpp#L953-L972`

### `item.fn.test-attributes` — #[test] / #[ignore] / #[should_panic] flag functions

On a function, #[test] marks it a test, #[ignore] marks it ignored, and #[should_panic] marks expect-panic; #[should_panic(expected="msg")] records the expected panic substring (string literal, quotes stripped). These flags are reset per-function before reading.

**Evidence:** `src/compiler/sema.cpp#L7791-L7836`

### `item.fn.test-attrs` — test-harness function attributes

`#[test]`, `#[ignore]`, and `#[should_panic]` are recognised on a function; `#[should_panic(expected = "…")]` records the expected panic substring from its string-literal argument.

**Evidence:** `src/compiler/sema_collect.cpp#L1767-L1797`

### `item.fn.test-modifiers-require-test` — `#[should_panic]`/`#[ignore]` are `#[test]` modifiers

`#[test]` marks a free function as a test case; `#[should_panic]` and `#[ignore]` are modifiers valid only in combination with `#[test]`. All three apply to functions only.

**Uncertainty:** The 'only valid in combination with #[test]' constraint is enforced downstream, not in this unit (comment-stated).

**Evidence:** `src/compiler/sema_impl.hpp#L1488-L1493`

### `item.fn.unique-mangled-name` — Each mangled function symbol must have at most one body

Two distinct functions resolving to the same mangled link symbol is an error; in particular a private function in one package and a pub function of the same base name in an imported package must not collide, requiring rename to disambiguate.

**Related:** `module.symbol.function-symbol-name`

**Evidence:** `src/compiler/mlir_gen_fn.cpp#L219-L235`

### `item.fn.vararg-extern-only` — Variadic functions are extern-only C-ABI declarations

A function declared variadic (vararg) is emitted only as an external declaration with C variadic calling convention; non-vararg parameters are typed normally and a missing return type denotes void.

**Evidence:** `src/compiler/mlir_gen_fn.cpp#L169-L184`

## Function Parameters

### `item.fn-param.datanode-by-value` — DataNode eidos cannot be passed by value

A parameter whose type is (or contains) a DataNode datatype (one holding relative-pointer fields) is rejected by value; it must be passed as `DataRef<T>` because the relative pointers require a zone base pointer.

**Divergence:** Logos addition (zoned/DataNode model); no Rust analog

**Evidence:** `src/compiler/sema_decl.cpp#L700-L713`

### `item.fn-param.mut-binding` — `mut` parameter binding

A typed parameter `mut x: T` makes `x` a mutable, caller-invisible local binding: the body may reassign or take `&mut` of it. Desugared to an immutable synthetic parameter plus prologue `let mut x = synth;` (a move of the param value into the user local).

```logos
fn f(mut x: i32) { x += 1; }
```

**Evidence:** `src/compiler/sema_decl.cpp#L714-L741`, `src/compiler/sema_decl.cpp#L1046-L1060`

### `item.fn-param.owning-box-dyn` — By-value Box<dyn Trait> param owns the box

A by-value parameter of owning trait-object type (`Box<dyn Trait>`) makes the callee own the box: it is dropped at the callee epilogue (vtable drop_in_place + dealloc) and call sites coerce the argument to a heap fat handle.

```logos
fn f(b: Box<dyn Trait>) {}
```

**Evidence:** `src/compiler/sema_decl.cpp#L742-L759`

### `item.fn-param.self-reserved` — `self` reserved for impl receivers

A parameter named `self` is an error outside an impl-block; `self` is only the magic receiver inside impl methods.

```logos
fn f(self: i32) {}  // error outside impl
```

**Evidence:** `src/compiler/sema_decl.cpp#L687-L694`

### `item.fn-param.struct-pattern` — Struct-pattern function parameter

A parameter may be an irrefutable struct pattern `Name { a, b, ... }: Name`. Each named field (or its `f: binding` rename) becomes a body-visible binding of the field's type; `..` rest is ignored. The pattern is desugared to a synthetic parameter plus prologue `let bind = synth.field;`.

```logos
fn f(Point { x, y }: Point) -> i32 { x + y }
```

**Evidence:** `src/compiler/sema_decl.cpp#L604-L648`, `src/compiler/sema_decl.cpp#L999-L1045`

### `item.fn-param.tuple-pattern` — Tuple-destructure function parameter

A parameter may be an irrefutable tuple pattern `(a, b, ...): (T1, T2, ...)`. Each non-`_` element name becomes a body-visible binding of the corresponding tuple element type, desugared to a synthetic parameter plus prologue `let a = synth.0;` etc.

```logos
fn f((a, b): (i32, i32)) -> i32 { a + b }
```

**Evidence:** `src/compiler/sema_decl.cpp#L654-L684`, `src/compiler/sema_decl.cpp#L975-L994`

### `item.fn-param.unique-names` — Parameter names must be unique

All parameter names within one function signature must be distinct.

**Evidence:** `src/compiler/sema_decl.cpp#L765-L768`

## Parameters (General)

### `item.param.no-infer-placeholder` — `_` rejected in fn signature type positions

The inferred-type placeholder `_` is rejected (E0121) when it appears in a fn signature's parameter or return type positions.

**Evidence:** `src/compiler/sema_collect.cpp#L4660-L4662`, `src/compiler/sema_collect.cpp#L4667-L4672`

### `item.param.self-receiver-and-modifiers` — function parameter and self-receiver forms

A parameter is `[mut] NAME [: TYPE] [...]`; a self-receiver is rendered as `&[mut] self` (a reference parameter without an explicit type). The `...` suffix marks a variadic parameter.

**Evidence:** `src/compiler/sema_render.cpp#L1101-L1125`

## extern Blocks and ABI

### `item.extern.abi-whitelist` — extern ABI string whitelist

The ABI string of an `extern "ABI" { … }` block or an `extern "ABI" fn …` item must be one of "C", "C-unwind", "system", or "Rust" (enclosing quotes optional); any other string is rejected.

```logos
extern "C" { fn puts(s: *const u8) -> i32; }
```

**Divergence:** A7: "C-unwind" is accepted at parse but unwinding-across-FFI is moot (panic=abort).

**Evidence:** `src/compiler/sema_collect.cpp#L1334-L1344`, `src/compiler/sema_collect.cpp#L1379-L1381`

### `item.extern.block` — Extern block

`[unsafe] extern ["ABI"] { extern_block_item* }` groups same-ABI externs. The optional ABI string applies to all items in the block (inherited at splice). The Rust-2024 `unsafe extern` marker is accepted with no extra semantics.

```logos
unsafe extern "C" { fn puts(s: *const u8) -> i32; }
```

**Evidence:** `tools/peg_gen/grammars/logos.peg#L1209-L1227`

### `item.extern.block-flatten` — extern block flattening and ABI inheritance

An `extern "ABI" { extern_fn* }` block flattens to a linear item worklist; each child extern-fn that does not carry its own ABI inherits the block's ABI string, and item collection treats grouped and flat extern fns identically.

**Evidence:** `src/compiler/sema_collect.cpp#L1346-L1383`

### `item.extern.block-item` — Extern block item (fn / static)

Inside an extern block, items use bare `fn IDENT(params [, ...]) [-> T] ;` (no `extern` keyword; trailing `, ...` makes it variadic) or `static [mut] IDENT : T ;`. The produced extern fn carries no ABI of its own; an extern static with no value is marked external (no initializer).

**Evidence:** `tools/peg_gen/grammars/logos.peg#L1228-L1243`

### `item.extern.fn-def` — Standalone extern fn declaration

`extern ["ABI"] fn IDENT(params [, ...]) [-> T] ;` declares a single FFI function carrying its ABI string verbatim. A trailing `, ...` makes it variadic. Omitting the ABI string selects the default (Logos-internal) calling convention.

**Evidence:** `tools/peg_gen/grammars/logos.peg#L1209-L1216`, `tools/peg_gen/grammars/logos.peg#L1244-L1255`

## extern-block ABI Defaulting

### `item.extern-block.abi-default-to-children` — extern block applies its ABI as a default

`extern "ABI" { extern_fn* }` splices its function items into the module item stream; the block ABI applies as a default to children that do not specify their own ABI. An omitted block ABI is the default Logos-internal ABI.

**Evidence:** `tools/peg_gen/grammars/logos.peg#L317`

## extern Functions

### `item.extern-fn.implicit-pub-unsafe` — extern fn is implicitly pub + unsafe

An `extern fn` declaration is implicitly public, unsafe, and extern.

**Evidence:** `src/compiler/sema_collect.cpp#L4684-L4688`

### `item.extern-fn.no-mangle-abi-symbol` — extern fn keeps its bare ABI symbol

An extern fn keeps its raw name as the link symbol (no package/signature mangling); duplicate extern declarations of the same name+signature across modules deduplicate to a single symbol rather than erroring.

**Evidence:** `src/compiler/sema_collect.cpp#L4715-L4729`, `src/compiler/sema_collect.cpp#L4873-L4874`

## where Clauses

### `item.where.clause` — Where clause

`where where_pred (, where_pred)*`. A predicate is `<subject> : trait_bound (+ trait_bound)*` where subject is an associated-type ref, a reference type (`&T`, incl. `for<'a> &'a T`), or a plain type-param; or it is a bare type_param.

```logos
where T: Clone + Send, &T: Into<U>
```

**Evidence:** `tools/peg_gen/grammars/logos.peg#L1257-L1271`

## Name Resolution

### `item.name.forward-reference` — item names are visible before their definition (forward references)

Type names (struct, union, datatype, enum) and trait names are registered in a name-collection pass before bodies are collected, so an item may reference a type or trait declared later in the same or another module, and cross-file `impl Trait for X` resolves regardless of file order.

**Evidence:** `src/compiler/sema_collect.cpp#L313-L478`

## Names

### `item.names.duplicate-in-container` — Duplicate named member is an error

Within a named-member list of a container, any non-empty name that appears more than once is a duplicate error (`duplicate <kind> '<name>' in <container>`). The anonymous binding name `_` (and empty names) may repeat freely.

```logos
struct S { x: i32, x: i32 } // error: duplicate field 'x'
```

**Evidence:** `src/compiler/sema_impl.hpp#L1312-L1325`

## Duplicate / ODR Handling

### `item.dup.odr-dedup` — structurally identical duplicate items dedup; differing ones error

Two item definitions (struct/union/datatype/enum) sharing the same name in the same package are an error UNLESS their AST sub-trees are structurally equal, in which case the duplicate is silently dropped (ODR-style dedup). Structural equality ignores source-line metadata, so identical items emitted by metaprogramming at different source positions still dedup.

**Divergence:** Logos addition: ODR dedup of metacall-emitted items (Rust has no metacall splice model).

**Evidence:** `src/compiler/sema_collect.cpp#L25-L75`, `src/compiler/sema_collect.cpp#L267-L282`, `src/compiler/sema_collect.cpp#L378-L446`
