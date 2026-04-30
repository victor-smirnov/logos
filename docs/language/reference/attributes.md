# Attributes

Attributes are `#[...]`-prefixed annotations on items. The grammar rule is `annotation` ([logos.peg:423](../../../tools/peg_gen/grammars/logos.peg#L423)). Multiple attributes stack in source order:

```logos
#[derive(Clone, Debug)]
#[type_code = 0x42]
pub struct Foo { ... }
```

Forms:

- **Flag**: `#[name]`
- **Key/value**: `#[name = literal]` — literal is integer, string, bool, or `Type::Variant`.
- **Argument list**: `#[name(arg, arg, ...)]` — each argument is a literal or named (`key = value`).

The supported attribute names are recognised by sema (`src/compiler/sema*.cpp`). Unknown attributes are ignored today (no warning); this will become a diagnostic — see Roadmap.

## `#[type_code = N]`

Binds an explicit integer tag to a datatype, genos, or generic instantiation. `N` participates in:

- Hermes wire-format dispatch — the registry maps `type_code → impl` for `HermesStringify`, `HermesEqual`, etc.
- Tag-dispatched pointers (`&tagged<TS> Trait`) — the prefix bytes carry this code.

```logos
#[type_code = 42]
pub eidos Decimal { coef: i128, scale: i8 }

#[type_code = 100]
pub struct Array<i32>;            // body-less explicit instantiation

#[type_code = 7]
pub genos Varchar { ... }
```

`#[type_code]` is **rejected** on a template genos (`genos Foo<T> { ... }`); only fully-specialised forms or non-generic datatypes may carry it. Codes 1–127 are reserved for system use; user codes start at 128 ([memory: feat_tag_dispatch](../../README.md)).

## `#[zoned]`

Marks a `struct` as a Hermes datatype (zone-relative layout, no heap pointers). Equivalent intent to declaring `eidos Foo { ... }`.

```logos
#[zoned] pub struct AnyVal { pub raw: u32 }
```

Internally the type's `LogosType::Kind` becomes `ZonedStruct`. See [Hermes](hermes.md) and [Types → Datatype](types.md#datatype-zoned-struct).

## `#[derive(...)]`

Generates trait implementations from the struct's fields:

```logos
#[derive(Clone, Debug, Eq, Hash)]
pub struct Point { x: i32, y: i32 }
```

The list of derivable traits grows as stdlib trait derivations land — current set centres on `Clone`, `Debug`, the Hermes registry traits (`HermesStringify`, `HermesEqual`, `HermesHash`, `HermesClone`, `HermesRelease`), and a few stdlib markers. Derive expansion runs as a metaprog pass at sema time.

## `#[annotation]`

Marks a struct as itself an annotation type, allowing user-defined attribute kinds. The typed-attribute system reads back the annotation as a struct value at metaprog time.

```logos
#[annotation]
pub struct deprecated { reason: &'static str }

#[deprecated(reason = "use new_api instead")]
pub fn old_api() { ... }
```

Currently a sketch — the consumer side (reading user-defined annotations from metaprograms) is partial.

## `#[tag_dispatch(...)]`

On a `trait`, declares the tag-system that this trait dispatches through. Used together with `&tagged<TS> Trait` types.

```logos
#[tag_dispatch(HermesTypeTagSystem)]
pub trait HermesStringify { ... }
```

## Roadmap

The full attribute roster Logos plans to ship — many of these are *not yet* honoured by sema; check `src/compiler/sema*.cpp` before assuming an attribute does anything.

| Attribute | Status | Purpose |
|-----------|--------|---------|
| `#[type_code]` | active | Hermes dispatch tag |
| `#[zoned]` | active | Hermes datatype layout |
| `#[derive(...)]` | active (limited list) | Auto-implement traits |
| `#[annotation]` | sketch | User-defined annotation types |
| `#[tag_dispatch(TS)]` | active | Bind trait to a tag system |
| `#[inline]`, `#[inline(always)]`, `#[inline(never)]` | planned | Codegen hints |
| `#[cold]`, `#[hot]` | planned | Branch hints |
| `#[test]` | planned | Testing framework |
| `#[export = "name"]`, `#[link_name = "name"]` | planned | FFI linkage |
| `#[repr(C)]`, `#[repr(packed)]`, `#[repr(transparent)]` | planned | Layout control |
| `#[deprecated(...)]` | planned | Diagnostic marker |
| `#[yields_view_of(...)]` | planned | Escape-analysis annotation for view types ([memory: feat_view_ownership](../../README.md)) |
| `#[unsafe_no_drop]` | planned | Skip drop glue |

Unknown attributes today produce no diagnostic; they will become a warning, then a hard error once the supported list is frozen.
