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

Binds an explicit integer tag to a datatype or a generic instantiation. `N` participates in:

- Hermes wire-format dispatch — the registry maps `type_code → impl` for `HermesStringify`, `HermesEqual`, etc.
- Tag-dispatched pointers (`&tagged<TS> Trait`) — the prefix bytes carry this code.

```logos
#[type_code = 42]
#[zoned] pub struct Decimal { coef: i128, scale: i8 }

#[type_code = 100]
pub struct Array<i32>;            // body-less explicit instantiation
```

`#[type_code]` is **rejected** on an unspecialised generic template (`struct Foo<T> { ... }`); only fully-specialised forms (`struct Foo<i32>;`) or non-generic datatypes may carry it. Codes 1–127 are reserved for system use; user codes start at 128 ([memory: feat_tag_dispatch](../../README.md)).

## `#[zoned]`

Marks a `struct` as a Hermes datatype (zone-relative layout, no heap pointers). `#[zoned] struct` is the sole canonical declaration form for datatypes.

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

## `#[metaprog_handler("trigger_name")]`

Marks a free function as a metaprog hook that fires when an item carrying the matching trigger annotation is collected. The compiler invokes the hook in its JIT, passing the AST offset of the triggering item; the hook builds and emits a `QuoteItemBlob` (or `Vec<QuoteItemBlob>`) via `logos_emit_item_blob_subst`.

```logos
#[metaprog_handler("derive_clone")]
fn derive_clone_hook(target_offset: u32) -> () {
    // walk the item at target_offset, build quote_item! { ... } blob, emit
}

// User side:
#[derive_clone]
pub struct Point { pub x: i32, pub y: i32 }
```

Constraints checked at sema time ([src/compiler/sema.cpp](../../../src/compiler/sema.cpp), `#[metaprog_handler]` validator):
- Hook must be a free function (not a method or extern)
- Hook must NOT be generic
- Hook must take exactly one `target_offset: u32` parameter

Triggers form an open-vocabulary user namespace. Any `#[name]` on an item is checked against registered hooks; a match fires the hook. The complete list of triggers is stdlib- and user-defined; see [stdlib/std/compiler/metaprog/](../../../stdlib/std/compiler/metaprog/) for built-in hooks (`derive_clone`, `derive_debug`, etc.).

See [memory: feat_derive_clone_stdlib](../../../.claude/projects/-home-victor-devel-logos/memory/feat_derive_clone_stdlib.md), [feat_metacall_arch](../../../.claude/projects/-home-victor-devel-logos/memory/feat_metacall_arch.md).

## User-defined trigger attributes

The grammar's `annotation` rule is open: any `#[name]`, `#[name(args)]`, or `#[name = value]` parses successfully regardless of whether sema knows the name. There are two ways a user attribute becomes load-bearing:

1. **As a `#[metaprog_handler]` trigger** (above) — the attribute fires a hook.
2. **As an `#[annotation]`-marked struct** — the attribute parses into a typed value that metaprograms can read via reflection. (Sketch-status; reader side is partial.)

Concrete user triggers in the codebase as of writing (non-exhaustive — derived from grep):

| Trigger | Defined by | Effect |
|---|---|---|
| `#[derive_clone]` | stdlib | Generates `Clone` impl |
| `#[derive_debug_e2e]`, `#[derive_debug_enum]`, `#[derive_debug_generic]` | stdlib | Generates `Debug` impl variants (test-suite hooks) |
| `#[derive_clone_quote]` | stdlib | Quote-based `Clone` derivation |
| `#[derive_dbg_md]`, `#[derive_size_md]` | tests | Demo derive hooks |
| `#[antiquot_inject]`, `#[exprblob_splice_inject]`, `#[inject_pair]`, `#[nested_antiquot_inject]`, `#[quote_inject]`, `#[struct_lit_cursor_inject]` | tests | Quote/antiquot edge-case hooks |
| `#[template_of_probe]`, `#[template_of_typed_probe]` | tests | Template intrinsic verification |
| `#[slice]` | sema-recognised marker | (declares slice-friendly type — see Roadmap) |

Unknown `#[name]` produces **no diagnostic** today (sema silently ignores). This is a known gap; see Roadmap.

## Meta blocks (`meta @{...}`)

Distinct from attributes but lexically nearby: a `meta @{...}` block can appear inside a `struct` or `trait` definition (grammar `meta_block`, [logos.peg:821](../../../tools/peg_gen/grammars/logos.peg#L821)):

```logos
pub struct Foo {
    pub x: i32,
    pub y: i32,
    meta @{
        "doc": "A 2D point",
        "schema_version": 1,
    }
}
```

The Hermes-literal payload is evaluated at sema time via `eval_static_hermes_lit` ([src/compiler/sema_decl.cpp](../../../src/compiler/sema_decl.cpp)) and stored on the `LStructDef.meta_val` field. Metaprograms can read it via reflection on the type. Current uses:

- Hermes type registry — `meta @{ "type_code": N, "name": "..." }` complement to `#[type_code]`.
- User schema annotations — anything stdlib-internal that wants typed metadata on a definition.

The meta block accepts the full Hermes-static literal grammar (no captures `${...}`, just static values). Multiple meta blocks per item are not supported — only the last wins.

## Roadmap

The full attribute roster Logos plans to ship — many of these are *not yet* honoured by sema; check `src/compiler/sema*.cpp` before assuming an attribute does anything.

| Attribute | Status | Purpose |
|-----------|--------|---------|
| `#[type_code]` | active | Hermes dispatch tag |
| `#[zoned]` | active | Hermes datatype layout |
| `#[derive(...)]` | active (limited list) | Auto-implement traits |
| `#[annotation]` | sketch | User-defined annotation types |
| `#[tag_dispatch(TS)]` | active | Bind trait to a tag system |
| `#[metaprog_handler("name")]` | active | Register hook for `#[name]` trigger |
| `meta @{...}` | active | Typed metadata payload on items |
| `#[inline]`, `#[inline(always)]`, `#[inline(never)]` | planned | Codegen hints |
| `#[cold]`, `#[hot]` | planned | Branch hints |
| `#[test]` | planned | Testing framework |
| `#[export = "name"]`, `#[link_name = "name"]` | planned | FFI linkage |
| `#[repr(C)]`, `#[repr(packed)]`, `#[repr(transparent)]` | planned | Layout control |
| `#[deprecated(...)]` | planned | Diagnostic marker |
| `#[yields_view_of(...)]` | planned | Escape-analysis annotation for view types ([memory: feat_view_ownership](../../README.md)) |
| `#[unsafe_no_drop]` | planned | Skip drop glue |

Unknown attributes today produce no diagnostic; they will become a warning, then a hard error once the supported list is frozen.
