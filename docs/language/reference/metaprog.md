# Metaprogramming

Logos metaprogramming is *not* a separate macro language — metaprograms are ordinary Logos functions that the compiler runs at compile time and whose results splice into the user program. The model is two-layer:

- **Layer A — Generics**: parameter substitution. Pure type-level, no execution at compile time. See [Generics & Traits](generics-traits.md).
- **Layer B — Metafunctions**: full Logos code that consumes / produces AST values, runs in the compiler's JIT, and returns either values or AST fragments to splice. This page is about Layer B.

See [memory: feat_metaprog_two_layer_hygiene](../../README.md) and [memory: feat_metaprog_taxonomy](../../README.md) for the architectural rationale.

## Typelevel Handles

Metafunctions manipulate compile-time values of these types:

| Handle | Source | Purpose |
|--------|--------|---------|
| `Type` | `type_of::<T>()` | Resolved type (kind + name + size). |
| `GenericType` | `generic_of::<F>()` | Unapplied generic constructor. |
| `Template` | `template_of::<X>()` | AST node of a `template`-marked declaration. |
| `TypeList` | `type_refs_of::<T...>()` | Pack of `Type`s as a list value. |
| `Item` | quoted item | Source-level item AST node (item-blob). |

These live in `std.compiler.metaprog.*` modules. They are ordinary structs whose `raw` field is an `AnyVal`; the compiler bakes the resolved value at sema time, so metafunctions can inspect them at JIT time without the compiler being involved at runtime.

## `metacall`

```logos
metacall expand_filter::<T...>()         // expression position
metacall emit_traits::<T...>();          // statement / item position
```

`metacall <call>` runs the callee at compile time and splices its return into the surrounding program. The callee:

- Is a regular Logos function compiled into the metaprog JIT module.
- Receives compile-time arguments (`Type`, `Template`, scalars, `TypeList`s).
- Returns either a normal value (spliced as a literal) or a typed AST fragment (`Item`, `Expr`, `Stmt`, `Pat`, `Ty`) that the compiler grafts into the AST.

Position rules:

- **Expression position** — return must be `Expr`-shaped or a normal value.
- **Statement position** — return is spliced as one or more statements.
- **Item position (top-level / inside `impl`)** — return is item(s); the call sites of `metacall` at item position are written without `;`.

See [memory: feat_metacall_arch](../../README.md) for full position / return-type matrix and the HERMES_BLOB splice protocol.

## Quote Forms

```logos
quote_item! { fn helper(x: i32) -> i32 { x + 1 } }
quote_expr! { x + y * 2 }
quote_ty!   { Vec<i32> }
```

`quote_*!` produces typed AST literals — the body is parsed as the corresponding form, deep-cloned into a fresh Hermes document, and emitted as a `HermesStatic` blob. Useful for building AST fragments inside metafunctions.

### Antiquotation

Inside a `quote_*!` body, `$ident` and `${expr}` splice runtime values:

```logos
fn make_getter(name: &str, ty: Ty) -> Item {
    quote_item! {
        fn $name(&self) -> $ty { self.$name }
    }
}
```

`$name` substitutes a captured value of the right typelevel handle (`Item`, `Ty`, `Expr`, `Ident`, etc.). `${expr}` allows a compile-time expression in the splice slot.

Hygiene is *hybrid*: definition-site names are fully qualified at quote time (so a `quote_item!` body can refer to `std.io::Write` without the caller importing it); call-site names introduced by the caller go through a name-collision check at the splice point. See [memory: feat_metaprog_quote](../../README.md).

## Repeat Groups

```logos
quote_item! {
    impl Display for Tup {
        fn fmt(&self, w: &mut Writer) -> Result<(), io::Error> {
            #(write!(w, "{}", self.$names);)*
            Ok(())
        }
    }
}
```

`#(... pat ...)<sep>*` repeats the body once per element of any pack referenced inside (here `$names: Pack<Ident>`). Separators are `,`, `;`, `&` — see the grammar at [logos.peg:1363](../../../tools/peg_gen/grammars/logos.peg#L1363).

## `template`

```logos
template struct Pair<A, B> { fst: A, snd: B }
template fn map<F>(...) { ... }
```

`template <decl>` marks the declaration as **AST data, not a binding**. Sema skips templates entirely — their inner names are never registered. Metafunctions consume them via `template_of::<X>()`, `apply(template, args)`, and `metacall`. Use `template` for code patterns that the metafunction layer expands into real items.

## Sema-Side Intrinsics

Three names are recognised by sema and compile down to baked-in literals:

| Form | Returns | Semantics |
|------|---------|-----------|
| `type_of::<T>()` | `Type` | Resolves `T`, emits `Type { kind, name, size }` literal. |
| `generic_of::<F>()` | `GenericType` | Resolves `F` as an unapplied generic constructor. |
| `template_of::<X>()` | `Template` | Walks the current AST for `X`, bakes its node offset. |

These intrinsics run at sema time — no JIT involvement — and the resulting struct literal flows through the regular constant-folding path.

## Two-Phase Split (Roadmap)

The metaprog system is being designed in two phases:

- **Phase 1 — Generative** (in progress): templates → items. Metafunctions produce code from compile-time inputs. Most current work targets this phase.
- **Phase 2 — Transformative** (design only): passes over whole programs as `Pass<Rewrites, Diagnostics>`. AOP, bytecode-rewriting, lints all expressed as transformative passes.

See [memory: feat_metaprog_phases](../../README.md) for the design sketch.

## Tier Split for Metafunctions

Different consumers of the metaprog API target different tiers:

- **Tier 0 — stdlib internal**: raw AST manipulation. Few consumers, careful review.
- **Tier 1 — external Datalog**: fact-base query over the program. Stable across compiler versions.
- **Tier 2 — external fragment builder**: `parse_expr` / `parse_stmt` builder API. Highest level, most stable.

External users should write in Tier 1 / Tier 2; Tier 0 is considered an unstable internal interface. See [memory: feat_metaprog_tiers](../../README.md).

## Roadmap

- **Phase 2 transformative passes** — design only.
- **Hygiene strengthening** — current call-site name resolution is a collision check; gensym is planned for opaque names.
- **`metacall` captures** — accessing local variables from the surrounding scope at expression position; not yet implemented.
- **`std::meta` module** — formal API surface for typelevel handles. Currently scattered across `std.compiler.metaprog.*`.
- **Constant-folding through `metacall`** — folder will treat `metacall` as a first-class producer; today only literal-args flows fold reliably.
