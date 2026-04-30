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

Inside a `quote_*!` body, `#name` (Rust `quote!` convention) splices a value bound in the surrounding metafunction:

```logos
fn make_marker(id: Ident) -> QuoteItemBlob {
    quote_item! { struct #id { x: i32 } }
}
```

`quote_ty!` additionally accepts `$ident` and `$ts...` for pack-splicing — `$` is reused from Hermes-capture syntax and is a quote_ty-only spelling.

### Status (2026-04-30)

Implementation is partial; what works today, by form:

- **`quote_item!`** — body parses; only `#id` for the **struct name** position is recognised as antiquot (slice 5a.1). `#field` in field positions, `#expr` in method bodies, repeats — not implemented.
- **`quote_expr!`** — `#x` antiquot, `#(...)*`, `#(...),*`, `#(...)&&*` repeats all work.
- **`quote_ty!`** — `$ident` antiquot, pack-splice `$ts...`, antiquot inside tuple / array type positions.
- **`quote_stmt!`, `quote_pat!`, `quote_ident!`** — not implemented.

Hygiene is currently bare: identifiers inside `quote_*!` resolve in the same scope as the surrounding metafunction. Hybrid hygiene (literal-internal names get a fresh scope, antiquoted names resolve at the splice site) is planned (slice 10).

## Repeat Groups

```logos
quote_expr! { #(self.#fields == other.#fields)&&* }
```

`#(...)*`, `#(...),*`, `#(...)&&*` repeat the body once per element of the cursor pack referenced inside, joined by no separator / `,` / `&&`. Multiple cursors zip by length.

**Status:** repeats work in `quote_expr!` (see tests `quote_expr_repeat_comma`, `quote_expr_repeat_andand`). Repeats in `quote_item!` are **not yet implemented** — the canonical `#(self.#fields == other.#fields)&&*`-style derive that walks a struct's fields cannot be written through `quote_item!` today.

## `template`

```logos
template struct Pair<A, B> { fst: A, snd: B }
template fn map<F>(...) { ... }
```

`template <decl>` marks the declaration as **AST data, not a binding**. Sema skips templates entirely — their inner names are never registered.

### Conceptual model

Templates are a **syntactic-level** code generator (orthogonal to generics): they take template parameters (names, types, packs, consts) and produce a declaration. The result can be a concrete item, **or another generic**, which then participates in monomorphisation as usual. Template parameters and the output's generic parameters are independent axes — a template can be parameterised over a `Name` and produce `struct #Name<A, B> { ... }`, where `A`, `B` are the generic parameters of the output, not of the template.

Inside a template body (planned grammar): `#X` references a template parameter; bare `X` is an ordinary in-scope name. Triggers (planned): `apply_template<Tpl, args...>() -> Item` and `#[apply(args...)]` on the declaration.

### Status (2026-04-30)

Almost nothing of the above runs today:

- The body of a `template <decl>` is parsed, then **silently dropped** by sema. It is not persisted, not consultable, not expandable.
- `template_of::<X>()` returns a `Template` handle with `name()` and `type_param_count()` accessors only.
- There is no `apply_template`, no `#[apply(...)]`, no `apply()` method, no `#X` placeholder grammar, no repeats inside the body.
- For now, code generation goes through `#[metaprog_handler("...")]` hooks that build a `quote_item!` blob programmatically and call `logos_emit_item_blob_subst`.

End-to-end template body expansion is tracked in the planning notes `metaprog-quote-slice5.md` (quote infrastructure) and `template-body-expansion.md` (template-side triggers).

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
