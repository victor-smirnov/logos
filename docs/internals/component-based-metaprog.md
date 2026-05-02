# Component-Based Metaprogramming as a Third Abstraction Mechanism

> **Status: design draft.** This document records the current thinking on
> generalising Logos's metaprogramming layer (`metafn`, `quote_*!`, `meta_*`
> queries) into a coherent, first-class abstraction mechanism, peer to
> structured programming and object-oriented / data-abstraction programming.
> It is not a specification: open questions are flagged in §6. Memoria
> (legacy C++ codebase, see [legacy-memoria-container-assembly.md](legacy-memoria-container-assembly.md))
> is the forcing function — every claim about features and ergonomics is
> grounded in what Memoria-class container assembly actually demands.

## 1. Frame

Three abstraction mechanisms, each adding a different axis of composition:

| Mechanism | Abstracts over | Unit | Composition |
|---|---|---|---|
| **Structured programming** | execution sequencing | procedure | call |
| **Object / data-abstraction programming** | data + behaviour bundles | type (with methods, traits) | dispatch |
| **Component-based / metaprogramming** | configuration → behaviour synthesis | component (predicate-gated behaviour) | assembly (orchestration of components) |

Memoria already practises component-based programming, painfully expressed
in C++ TMP. The legacy pattern (`BTTypes` specialisation → `CtrTF` →
chain-inherited `Ctr<Types>`) is the C++ projection: tag classes and
partial-specialisation are TMP costumes for **plain configuration values**
and **predicate-gated component metafunctions**.

In Logos the same paradigm gets first-class language support — not as "we
have macros and reflection", but as **Configuration / Component / Assembly
are kinds of language entities**, peer to `struct`, `trait`, `impl`.

## 2. The trinity

### 2.1 Configurations are Hermes documents

The first major design decision: configurations are **not** Logos structs.
They are Hermes documents — recursive polymorphic dictionaries, which is
exactly the data class Hermes was built for. A Logos struct schema for a
configuration would force structuring decisions onto the type level, where
they become rigid and "swampy" (more on this below). Hermes is the right
substrate.

```logos
const PMAP_CFG: HermesStatic = @{
    "name": "PMap",
    "streams": [
        @{"key_ty": <type:Varchar>, "val_ty": <type:Hermes>, "ordered": true}
    ],
    "cow": true,
    "leaf_kind": "Variable",
    "branch_kind": "Variable",
};
```

Why Hermes-as-config is structurally right:

- **Hermes is already the cross-stage fabric** (compile / runtime / wire /
  disk; see [memory: feat_hermes_compile_runtime_fabric]). Configurations
  written in Hermes participate in this fabric automatically: serialisable,
  hashable, comparable, persistent across builds and language versions.
- **Identity comes from content-hash for free.** Memoria's
  `TypeHash<bt::LeafNode<Types>> = HashHelper(Base, VERSION, true, Name)`
  collapses to `hermes_hash(serialize(cfg))`. Stable, cross-build,
  cross-language.
- **Configuration evolution = Hermes schema evolution.** TinyObjectMap
  already carries a `schema_type_code`; adding a field to a configuration
  is a Hermes schema change, not a separate problem.
- **Inspectability is Hermes-native.** Predicates over configurations are
  ordinary Hermes-document walks, no special "config inspection" intrinsics.
- **Rich types as values are already Hermes's job.** A configuration
  carrying a `<type:Foo<i64>>` is just one more value alongside ints,
  strings, maps, arrays.

The **enabling feature** for this — which is not yet a first-class part of
the language — is *Type as a first-class Hermes value*. Today our metaprog
`Type` handle lives in compiler-side data structures, not in the Hermes
universe. This needs to be lifted: see §5.

### 2.2 Components

A component is a metafunction with three pieces of declarative metadata:

- **Target** — what the synthesised items attach to (a named assembly slot).
- **Predicate** — a closure over the configuration that decides whether the
  component applies.
- **Ordering / conflicts** — `#[after(...)]`, `#[conflicts_with(...)]`,
  optionally `#[before(...)]`.

```logos
#[component(target = "btree_container")]
#[applies_when(|cfg| cfg["leaf_kind"].as_str() == "Fixed")]
#[after(base_part)]
#[conflicts_with(leaf_variable_part)]
metafn leaf_fixed_part(cfg: HermesView, target: Type) -> Vec<Item> {
    let key_ty: Type = cfg["streams"][0]["key_ty"].as_type();
    quote_item! { ... }
}
```

Mapping from Memoria/C++ TMP:

| C++ TMP idiom | Component-based equivalent |
|---|---|
| empty tag class (`bt::FindName`) | the `target = "..."` slot + the predicate |
| partial specialisation body | the metafunction body |
| chain-inheritance composition | assembly merges items from all applicable components |
| `BTTypes::CommonContainerPartsList` | components registered against the same `target` |
| `CtrExtensionsList` | components from any module are discovered by target |
| `IfThenElse<...>` axis | predicate over the configuration document |

Properties:

- Components are **discoverable**: the compiler indexes all
  `#[component]`-attributed metafunctions and groups them by target.
- Components are **named**: ordering and conflict declarations refer to
  components by name; the compiler topologically sorts before invoking.
- Components carry **diagnostic context**: a non-applying predicate is
  reported as "component `X` did not apply because `cfg.leaf_kind ==
  Variable`, expected `Fixed`" — readable, not 200 lines of typelist
  mismatch.

### 2.3 Assemblies

An assembly is a top-level metafunction that takes a configuration,
discovers applicable components, and merges their item lists into a complete
program fragment (a struct, its impls, a trait, ...).

```logos
#[assembly("btree_container")]
metafn assemble_btree(cfg: HermesView) -> Vec<Item> {
    let target = synthesize_target_struct(cfg);
    let mut items = vec![target.clone()];
    for c in components_for("btree_container", &cfg) {  // topo-sorted by after/conflicts
        items.extend(c.invoke(&cfg, &target));
    }
    items
}
```

End-user surface:

```logos
#[memoria_container]
const PMAP_CFG: HermesStatic = @{ ... };
// at compile time: assemble_btree(PMAP_CFG) runs, emits PMap struct + impls
```

Properties:

- Assemblies **chain**: assembly A produces a configuration that drives
  assembly B. Memoria's prototype/profile/container layering falls out
  naturally.
- `(assembly, cfg)` pairs have **stable identity** via Hermes content-hash —
  cache key, dedup key, cross-version compatibility key, all the same hash.
- Assemblies declare **stages** (struct definitions before impls, impls
  before trait checks); ordering inside a stage is by `#[after]`/`#[before]`.
- Phase 2 `Pass<Rewrites, Diagnostics>` (whole-program transformative
  passes) is a **special case** of assembly: configuration = "the whole
  program", components = rewrites and lints. Unification, not a separate
  feature.

## 3. What this gives us, structurally

1. **Names the unit.** "Component" and "assembly" are concrete things, as
   tangible as "struct" or "trait impl". Stops being "we have macros".
2. **Localises the cleverness.** Each component is a small, predicate-gated
   chunk of behaviour. The configuration is a value. The assembly is the
   orchestrator. There is no "all logic in a 500-line typelist expression"
   pattern.
3. **Composes diagnostics.** Failures attach to components: "did not
   apply because X", "conflicts with Y". Not "expected typelist<...> got
   typelist<...>" 200 lines deep.
4. **Closes Memoria's MBT loop inside the language.** Metadata extraction =
   walking configurations. Code generation = assembly. Invariant checks =
   `#[validates_cfg]` metafunctions running before assembly. No external
   tool, no parser, no separate codegen pipeline.

## 4. Memoria-class feature checklist

Going through what Memoria-class container assembly actually demands and
checking against the current Logos metaprog stack.

### 4.1 Already in place

- First-class typelevel handles: `Type`, `Ident`, `ExprBlob`,
  `QuoteItemBlob`.
- `quote_item!` / `quote_expr!` / `quote_ty!` / struct-lit antiquot.
- `Vec<T>` repeat groups in quote (variadic synthesis).
- `meta_*` reflection: `meta_field_iter`, `meta_has_trait`, `meta_typelist`,
  `meta_kind_preds`, `meta_template_handle`, `meta_variant_intrinsics`,
  `meta_type_align`.
- Type→AST bridge for bare names (`Type::ident()`).
- `gensym` for opaque-name hygiene.
- `ExprBlob` / `QuoteItemBlob` splice through the Hermes blob fabric.
- `metacall` in expression and item position.
- Tag-dispatch + per-instantiation type-codes (analogue of
  `TypeHash<bt::LeafNode<Types>>`).
- **Variadics + full const generics** — Memoria's typelist algebra and
  `IfThenElse` axes go here, *without* dropping into metafunctions.
- `@{...}` Hermes literals as compile-time constants (HermesStatic with
  cached identity).

### 4.2 Already on the roadmap, needed for full component-based

- `quote_stmt!` / `quote_pat!` / `quote_ident!`.
- Generic-instantiation Type→AST bridge (`Foo<i32>`-shaped splices).
- `metacall` captures (components closing over assembly state).
- Hybrid hygiene (full split of literal-internal vs antiquoted name scopes).
- Constant-folding through `metacall` as a first-class producer.
- Phase 2 transformative passes (which becomes "assembly over the whole
  program" under this framing).

### 4.3 New, surfaced by this framing

These are the items that would need to be added to roadmap as the
component-based dialect lands:

- **Type as first-class Hermes value.** §5 below. The single most
  load-bearing addition; without it, configurations cannot live in Hermes,
  and §2.1 collapses to "configurations are Logos structs after all".
- **`#[component]` and `#[assembly]` syntactic forms.** Today: just
  metafunctions. The component-based dialect needs the declarative facade
  (and the indexing it implies).
- **Component registry and discovery.** `components_for(target, cfg)` is
  not a runtime hashmap — it is a metaprog-phase index, populated as the
  compiler sees `#[component]`-attributed items across all linked modules.
- **`#[after]` / `#[conflicts_with]` ordering and conflict resolution.**
  Topological sort, conflict-detection diagnostics. Without this,
  plugin-style extensions become fragile.
- **Predicate combinators + compiler introspection.** A predicate is an
  ordinary closure, but the compiler must read its structure to produce
  "did not apply because" diagnostics. Reconciling these — restricted DSL,
  closure-pattern-extraction, or a second predicate-eval pass for diag — is
  open (§6).
- **`meta_diag!(span, severity, msg)`.** First-class diagnostic emission
  from inside components and assemblies.
- **Span propagation.** Items synthesised by an assembly should carry spans
  pointing back to the configuration that produced them, not to the
  assembly's own source location.
- **Caching / incrementality of assembly.** An assembly is a pure function
  of its configuration; with Hermes content-hash as the key, memoisation is
  cheap and high-value.
- **`#[validates_cfg]` slot.** Run-before-assembly validators. Today this
  could be done in the assembly body, but separating "is this configuration
  well-formed?" from "synthesise from this configuration" is good
  hygiene.
- **Cross-assembly references.** When assembly A produces a configuration
  consumed by assembly B, references between them must be stable. With
  Hermes-as-config this falls out naturally (content-hash links), but the
  language surface for declaring such links needs to exist.

### 4.4 What collapses under Hermes-as-config

The §4.3 list is shorter than it would be if configurations were Logos
structs, because Hermes-as-config absorbs several would-be features:

- Configuration evolution → Hermes schema versioning.
- Stable identity per (assembly, cfg) → Hermes content-hash.
- Cross-build / cross-language compatibility → Hermes binary format.
- Configuration serialisation / persistence → Hermes is already the disk
  format.
- `Hash` / `Eq` / `Stringify` / `Clone` for configurations → existing
  Hermes trait registry.
- Config validators based on shape → Hermes schemas
  (`#[hermes_schema]`-style annotation, when stabilised).

This is a roughly 40% reduction in the size of the metaprog extension, with
**higher** expressive power — configurations unify with the rest of the
Hermes-as-fabric story.

## 5. The enabling feature: Type as first-class Hermes value

For configurations to live in Hermes, the metaprog `Type` handle must be a
Hermes value. Today it is not — `Type` lives in compiler-side data
structures, while Hermes values are the universe of `AnyVal`-tagged
documents. The bridge is the missing piece.

Concrete shape (proposed; details in a follow-up ADR):

1. **A new Hermes type-code for Type-handle.** Likely a hybrid encoding:
   - Primitive / named types (`i64`, `Varchar`, `UID256`) — compact: type-code
     plus the type's UID256 hash (we already mint these per type, see
     [memory: feat_logos_type_hash]).
   - Generic instantiations (`Foo<i64, Bar>`) — structural: a small Hermes
     container with the head name plus a list of `Type` children. Recursive.
2. **`<type:T>` syntax inside Hermes literals.** An `@-quoted` form for
   embedding a type into a Hermes document. Lowering: the compiler resolves
   `T` to a Logos type-handle and writes its Hermes form.
3. **`HermesView::as_type() -> Type`** in the metaprog API. The inverse,
   readable from any metafunction.
4. **`HermesEqual` / `HermesHash` / `HermesStringify` / `HermesClone` /
   `HermesRelease` for `Type`.** Through the existing tag-dispatch
   registry. `Stringify` produces "Foo<i64, Bar>"; `Hash` is the UID256
   equivalent; `Equal` is structural type comparison.
5. **HermesStatic literals as values for the metaprog API.** Already mostly
   true (the recent assoc-const HermesStatic identity fix is a step in this
   direction); needs to be promoted to a stable contract: "Hermes is the
   configuration language for the metaprogramming layer".

Once this is in place, everything in §2 and §4 stops being aspirational and
becomes mechanical.

## 6. Open questions

These are explicit gaps in the design, not just unwritten details.

1. **Type encoding: structural vs hash-only vs hybrid.** A structural
   encoding makes a Hermes-serialised configuration self-describing (a
   Type can be reconstructed without compiler context); a hash-only
   encoding is cheaper but requires a registry. Hybrid is the default
   answer (primitive by code+UID256, composite structural), but the
   exact split needs prototyping.
2. **Predicate language / introspection.** Closure-as-predicate is the
   user-friendly form; closure-as-opaque is the easy compiler form;
   readable diagnostics ("did not apply because X") need *some* form of
   introspection. Options: restricted predicate DSL, pattern extraction
   from closure ASTs (we already have ASTs for metaprog code), or a
   double-eval where the diagnostic pass runs the predicate with
   instrumentation. Not solved.
3. **Component registry persistence and module ordering.** When a component
   in module B contributes to an assembly invoked from module A, what
   happens if B is not linked? What if two modules contribute conflicting
   components and neither is loaded eagerly? Closely related to module
   discovery and the upcoming MP3 build-system work.
4. **The line between configuration and type.** A configuration *contains*
   types. A type can be parameterised by const-generics drawn from a
   configuration. Where exactly is the boundary? Working hypothesis:
   configurations are arbitrary Hermes documents that happen to be
   consumed by an assembly; types are the universe of typelevel handles,
   sometimes appearing inside configurations as values. They overlap
   intentionally but are not the same.
5. **Output shape: items vs types.** Most assemblies produce items
   (struct / fn / impl). Some need to produce a type expression (a tuple
   type computed from a typelist) — that is `quote_ty!`. The composition
   story for mixing both kinds of output across chained assemblies is
   sketched, not formalised.
6. **Phase 2 as whole-program assembly.** The framing "Phase 2 = assembly
   whose configuration is the whole program" is clean conceptually, but
   what *is* the whole-program configuration? A list of all modules? A
   snapshot of IR? When is the assembly initiated? Open.

## 7. Cross-references

- [legacy-memoria-container-assembly.md](legacy-memoria-container-assembly.md)
  — the C++ prior art that this generalisation responds to.
- [docs/language/reference/metaprog.md](../language/reference/metaprog.md)
  — what's currently shipping in the metaprog layer; the "vs Rust macros"
  table and the Query/Quote Asymmetry section are direct prerequisites.
- [docs/language/reference/generics-traits.md](../language/reference/generics-traits.md)
  — the C++20 baseline of generics; component-based metaprog stacks **on
  top** of this, not in place of it.
- Memory: `project_persistent_pkg`, `feat_metaprog_inversion`,
  `feat_metaprog_two_layer_hygiene`, `feat_hermes_compile_runtime_fabric`,
  `feat_logos_type_hash`, `project_persistent_abstraction_debt`.
