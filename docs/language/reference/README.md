# Logos Language Reference

This is the reference for the Logos language. It is organised by surface form: lexer → types → items → expressions → statements → patterns, with cross-cutting topics (generics, ownership, metaprogramming, Hermes, attributes) grouped after.

The reference describes both **the language as it is today** (what `logos.peg` and `src/compiler/sema*.cpp` actually accept and lower) and **roadmap items** (what is planned but not yet wired through). Roadmap items are called out per-page and collected in [Roadmap](roadmap.md).

The reference is fact-and-link — for design rationale see the per-feature notes linked from individual pages.

## Pages

### Surface

- **[Lexical](lexical.md)** — source encoding, whitespace, identifiers, keywords, literals, operators.
- **[Types](types.md)** — primitives, references, raw pointers, arrays / slices, tuples, function and closure types, user-defined types, special forms.
- **[Items](items.md)** — top-level declarations: `package`, `use`, `let`, `type`, `fn`, `struct`, `enum`, `trait`, `impl`, `genos`, `template`.
- **[Expressions](expressions.md)** — operator precedence, atoms and postfix chains, primary expressions, control-flow as expression, metaprogramming forms.
- **[Statements](statements.md)** — `let` bindings, assignments, control flow, `return`, `unsafe`, expression statements.
- **[Patterns](patterns.md)** — bindings, literals, struct / enum / slice patterns, or-patterns, Hermes patterns, guards, exhaustiveness.

### Cross-cutting

- **[Modules & Visibility](modules.md)** — `package`, `use`, `pub use`, name resolution, multi-file packages, `logos.module` manifests.
- **[Generics & Traits](generics-traits.md)** — type / lifetime / const parameters, variadic packs, traits, impls, trait objects, `where`.
- **[Ownership](ownership.md)** — move vs Copy, borrows, lifetimes, mutability, `Drop`, raw pointers, view types.
- **[Metaprogramming](metaprog.md)** — typelevel handles, `metacall`, quote forms, `template`, sema-side intrinsics.
- **[Hermes](hermes.md)** — Hermes literals, datatypes, view types, tag-dispatched pointers, schemas, type-codes.
- **[Attributes](attributes.md)** — `#[...]` annotations on items.

### Status

- **[Roadmap](roadmap.md)** — known gaps and planned features collected from across the reference.

## Sources

- **Surface syntax** — `tools/peg_gen/grammars/logos.peg`. References to specific lines anchor explanations.
- **Semantic rules** — `src/compiler/sema*.cpp` and `include/logos/compiler/sema.hpp`.
- **Examples** — drawn from `tests/logos/pass/*.logos` where possible. Examples in this reference are illustrative and may simplify away details.

## Conventions

- Code blocks use the `logos` language tag; renderers without a Logos lexer fall back to plain text.
- File-and-line references use the `[file.ext:line](path/file.ext#Lline)` form.
- Cross-page links use relative paths inside `docs/language/reference/`.
- "Planned" / "Roadmap" markers indicate features that the grammar may accept but sema does not (yet) honour.
