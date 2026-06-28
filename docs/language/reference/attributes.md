# Attributes

Attributes are `#[...]`-prefixed annotations on items. The grammar rule is `annotation` ([logos.peg:423](../../../tools/peg_gen/grammars/logos.peg#L423)). Multiple attributes stack in source order:

```logos
#[derive_clone]
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

- Writ wire-format dispatch — the registry maps `type_code → impl` for `WritStringify`, `WritEqual`, etc.
- Tag-dispatched pointers (`&tagged<TS> Trait`) — the prefix bytes carry this code.

```logos
#[type_code = 42]
#[zoned] pub struct Decimal { coef: i128, scale: i8 }

#[type_code = 100]
pub struct Array<i32>;            // body-less explicit instantiation
```

`#[type_code]` is **rejected** on an unspecialised generic template (`struct Foo<T> { ... }`); only fully-specialised forms (`struct Foo<i32>;`) or non-generic datatypes may carry it. Codes 1–128 are reserved for the stdlib runtime tag system; user codes start at 129 ([memory: feat_tag_dispatch](../../README.md)).

## `#[zoned]` and `#[datatype]`

`#[zoned]` marks a struct's fields as self-relative (zone-relative layout, no absolute heap pointers — a ZType) and sets the `zoned2` structural flag. It does **not** by itself promote a struct to a Writ datatype.

**`#[datatype]`** (or `#[annotation]`) is what promotes a struct into the datatype pipeline (`collect_datatype`) — the canonical "make a datatype" form; it is often combined with `#[zoned]`. A plain `#[zoned] struct` goes through the ordinary struct pipeline.

```logos
#[type_code = 42]
#[datatype] #[zoned] pub struct Decimal { coef: i128, scale: i8 }
```

Other structural-layout flags parsed by `parse_struct_attr_flags`: `#[non_null]`, `#[pinned]`, `#[self_describing]`, `#[rel_ptr]`, `#[zone_mut]`, `#[borrow_carrying]`, `#[no_auto_drop]`. See [Writ](writ.md) and [Types → Datatype](types.md#datatype-zoned-struct).

## `#[derive_<trait>]`

Generates a trait implementation from the struct's fields. Logos has **no** Rust-style `#[derive(Trait, ...)]` list form — that is rejected. Instead each derived trait is its own trigger annotation, `#[derive_<trait>]`, and you stack one per trait:

```logos
use logos.std.compiler.metaprog;   // brings the derive handlers into scope

#[derive_clone]
#[derive_debug]
#[derive_eq]
#[derive_hash]
pub struct Point { x: i32, y: i32 }
```

Each `#[derive_<trait>]` fires a `#[metaprog_handler("derive_<trait>")]` fn that must be in scope (the stdlib handlers live in `logos.std.compiler.metaprog`). Current set: `derive_clone`, `derive_copy`, `derive_debug`, `derive_default`, `derive_eq`, `derive_partial_eq`, `derive_ord`, `derive_partial_ord`, `derive_hash`, `derive_branch_node`. Derive expansion runs as a metaprog pass at sema time.

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
#[tag_dispatch(WritTypeTagSystem)]
pub trait WritStringify { ... }
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

An unknown attribute *name* produces **no diagnostic** today (sema silently ignores it). Mis-targeting a *known* builtin (wrong item kind) and the Rust-shape `#[derive(...)]` already do error. See Roadmap.

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

The Writ-literal payload is evaluated at sema time via `eval_static_writ_lit` ([src/compiler/sema_decl.cpp](../../../src/compiler/sema_decl.cpp)) and stored on the `LStructDef.meta_val` field. Metaprograms can read it via reflection on the type. Current uses:

- Writ type registry — `meta @{ "type_code": N, "name": "..." }` complement to `#[type_code]`.
- User schema annotations — anything stdlib-internal that wants typed metadata on a definition.

The meta block accepts the full Writ-static literal grammar (no captures `${...}`, just static values). Multiple meta blocks per item are not supported — only the last wins.

## Roadmap

The full attribute roster Logos plans to ship — many of these are *not yet* honoured by sema; check `src/compiler/sema*.cpp` before assuming an attribute does anything.

| Attribute | Status | Purpose |
|-----------|--------|---------|
| `#[type_code]` | active | Writ dispatch tag |
| `#[zoned]` | active | Writ datatype layout |
| `#[derive_<trait>]` | active (limited list) | Auto-implement a trait (one per trait; no `#[derive(...)]` list form) |
| `#[annotation]` | sketch | User-defined annotation types |
| `#[tag_dispatch(TS)]` | active | Bind trait to a tag system |
| `#[metaprog_handler("name")]` | active | Register hook for `#[name]` trigger |
| `meta @{...}` | active | Typed metadata payload on items |
| `#[datatype]`, `#[annotation]` | active | Promote a struct into the datatype pipeline |
| `#[cfg(...)]`, `#[cfg_attr(...)]` | active | Conditional compilation (drops item when predicate false; `all`/`any`/`not`) |
| `#[repr(transparent)]` (struct), `#[repr(uN)]` (enum) | active | Transparent wrapper / enum discriminant width; `#[repr(C)]`/`#[repr(packed)]` parse but are rejected |
| `#[test]`, `#[should_panic]`, `#[ignore]` | active | Test attributes (gate `cfg(test)`) |
| `#[no_mangle]` | active | Suppress pkg+sig mangling on a free fn (FFI/runtime ABI) |
| `#[fn_macro]`, `#[token_macro]` | active | Mark a free fn as a function-style / token-stream macro |
| `#[no_auto_drop]` | active | Skip auto drop glue (structural flag) |
| `#[inline]`, `#[inline(always)]`, `#[inline(never)]` | planned | Codegen hints |
| `#[cold]`, `#[hot]` | planned | Branch hints |
| `#[deprecated(...)]` | planned | Diagnostic marker |
| `#[yields_view_of(...)]` | planned | Escape-analysis annotation for view types ([memory: feat_view_ownership](../../README.md)) |

Unknown attributes today produce no diagnostic; they will become a warning, then a hard error once the supported list is frozen.
