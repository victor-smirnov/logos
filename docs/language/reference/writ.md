# Writ Integration

Writ is Logos's structured-data fabric: a single binary format used as compile-time IR (AST stored as `TinyMapView`), runtime container (`Map`/`Array`/typed datatypes), wire protocol, and on-disk format. This page covers the language-level surface — how to write Writ literals, when types are Writ-shaped, and what the view / static / write-trait split means in practice.

For Writ wire-format internals see the Writ documentation in the project root and [memory: feat_writ_compile_runtime_fabric](../../README.md).

## Writ Literals

```logos
@{"name": "Alice", "age": 30}        // map
@[1, 2, 3]                            // array
@"hello"                              // string
@42                                   // signed integer
@-7                                   // negative integer
@1.5                                  // float
@true   @false                        // bool
@null                                 // null

@<I32>[1, 2, 3]                       // typed dense array
@<I32, AnyVal>{1: @42, 2: @"x"}       // typed map
@[x for x in xs]                      // list comprehension
@{k: v for (k, v) in pairs}           // map comprehension
```

The `@`-prefixed forms are *Writ SDN literals* — the parser builds a Writ document, the compiler stores it in rodata as a `WritStatic`, and the expression value is a non-owning view into that document.

Inner values inside a Writ literal don't repeat the `@` (`@[1, 2]`, not `@[@1, @2]`).

### Capture (splice) inside literals

```logos
let name = "Bob";
let m = @{"name": $name, "id": ${compute_id()}};
```

`$ident` substitutes the runtime value of `ident` into the literal. `${expr}` evaluates the expression and splices the result. The compiler builds the literal as a *constructor* (allocating a Writ document at runtime) when captures are present, and as a `WritStatic` rodata blob when no captures appear.

See [memory: project_writ_capture_c8](../../README.md) for the C1–C8 capture milestone.

## Writ Types

### Datatype (`#[zoned] struct`)

```logos
#[zoned] pub struct AnyVal { pub raw: u32 }
#[zoned] pub struct Decimal { pub coef: i128, pub scale: i8 }
```

A `#[zoned]` struct is a *datatype*: laid out for the Writ wire format, no heap pointers, addressable via zone-relative offsets. Internally tagged `LogosType::Kind::ZonedStruct`.

Datatypes are passed by *fat pointer* (`(zone, offset)`) when they contain relative pointers, or by-value (in registers / by reference) when they're "data plain" — see [memory: feat_datatype_passing](../../README.md). The compiler infers the passing convention; user code rarely needs to think about it.

### Generic containers

```logos
let v: Array<i32>     = Array::<i32>::new();
let m: Map<String, u64> = Map::<String, u64>::new();
let s: HashMap<K, V>  = HashMap::<K, V>::new();
```

`Array<T>` and `Map<K, V>` are blanket-implemented generic Writ containers — see [memory: project_writ_generic_containers](../../README.md).

## View Types

```logos
fn show(v: WritCtrView<'_>) -> Result<(), io::Error> { ... }
fn read_decimal(d: DecimalView<'_>) -> i128 { d.coef() }
```

A *view* is a fat borrow `(base, &doc)` over a Writ-resident value. The view does not own the document — its lifetime is tied to the document's owner.

The read/write split is reflected in two traits:

- **`WritRead`** — implemented by views and by owning containers in read context.
- **`WritWrite`** — implemented only when the document is mutable (`Zone<Mutable>`).

The `WritStatic` marker indicates a value living in rodata (`@`-literals); `WritStatic` values are read-only by construction.

See [memory: feat_writ_read_write_traits](../../README.md).

## Tag-Dispatched Pointers

```logos
fn print_each(items: &tagged<DisplayTags> Display) { ... }
```

A `&tagged<TS> Trait` is a thin pointer whose first 1–8 bytes carry a `type_code` lookup index for the trait `Trait`. Used inside Writ containers where a per-instance vtable would inflate the wire format.

`type_code` allocation:

- 1–127 — system reserved.
- 128 and up — user-allocated, currently 56-bit slice of the type-definition hash; compile / link time collision detection.

See [memory: feat_tag_dispatch](../../README.md) and [memory: feat_logos_type_hash](../../README.md).

## Schemas and `type_code`

```logos
#[type_code = 0x42] #[zoned] struct Decimal { ... }
#[type_code = 100]  struct Array<i32>;       // explicit instantiation
```

The `#[type_code = N]` attribute binds a numeric tag to a datatype or to a generic instantiation. The body-less form `struct Type;` exists specifically to attach metadata (typically `#[type_code]`) to generic instantiations without introducing new fields. See [Items](items.md#tuple-structs-and-explicit-instantiations) and [Attributes](attributes.md#type_code).

## Trait Registry

Writ maintains a global trait registry — `WritStringify`, `WritEqual`, `WritHash`, `WritClone`, `WritRelease` — keyed by `type_code`. Implementations register themselves at link time, and dispatch through the registry uses a single tag-table lookup. See [memory: project_writ_trait_registry](../../README.md).

## Typed Casts

```logos
let arr = some_value as <I32>[];           // typed dense-array view
let m   = some_value as <I32, AnyVal>{};   // typed map view
```

The `as <T>[]` / `as <K, V>{}` syntax casts a generic Writ value to a typed container view, validating the wire-level types match.

## Roadmap

- **`Set<T>`** — currently approximated via `ObjectMap<K, null>`; a real `Set` type planned ([memory: feat_writ_set_via_objectmap](../../README.md)).
- **Datatype trait derivation** — Writ trait behaviour for datatypes comes from the type-code registry / blanket impls today; dedicated `#[derive_<trait>]` handlers for Writ traits are not yet provided (the general derive set is `derive_clone` / `derive_debug` / `derive_eq` / … — see [Attributes](attributes.md)).
- **Decimal view refactor** — `to_string_value` / `to_f64` currently on `*const Decimal`; target is `DecimalView` ([memory: project_decimal_view_todo](../../README.md)).
- **Cross-language Writ** — three-impl strategy (Logos / Rust / C++) deferred until Writ API stabilises in Logos ([memory: project_writ_sync_strategy](../../README.md)).
