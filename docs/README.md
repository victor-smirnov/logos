# Logos Documentation

This is the entry point for Logos documentation. The docs are organized by audience: the **language** track is for users writing Logos code, the **internals** track is for contributors working on the compiler, runtime, and Hermes.

## Language

Start here if you want to *use* Logos.

- [Overview](language/overview.md) — what Logos is, what it is not, the design axes.
- [Getting Started](language/getting-started.md) — build the compiler, write and run a program, run the test suite.
- [Syntax](language/syntax.md) — types, expressions, statements, patterns, modules.
- [Ownership and Borrowing](language/ownership.md) — `&`/`&mut`, lifetimes, the borrow checker.
- [Generics and Traits](language/generics-traits.md) — generic functions, trait impls, monomorphization.
- [Comprehensions](language/comprehensions.md) — list/map comprehensions, plain and Hermes forms.
- [Hermes in Logos](language/hermes.md) — Hermes as a first-class part of the language: literals, capture, view types.

## Internals

Start here if you want to *work on* Logos itself.

- [Compiler Architecture](internals/architecture.md) — the `logosc` pipeline from source to native code.
- [Hermes Runtime](internals/hermes-runtime.md) — Datatype/Storage/View, zones, the type registry.
- [Metaprogramming](internals/metaprog.md) — current state of compile-time programming and reflection.

## Project Status

- [Roadmap](roadmap.md) — what is implemented, what is in progress, what is planned.

## Essays

Long-form writing about motivation and design rationale, kept alongside the reference docs but distinct in tone — broader, opinionated, slower-changing.

- [AI Platform Era](ai-platform/README.md) — how AI authorship reshapes the requirements on languages and platforms; the design rationale behind Logos's metaprogramming, modular compiler, and lforge platform.

## Conventions in This Documentation

- Code samples are real Logos programs unless marked `// sketch`. Anything marked sketch is design intent that is not yet implemented.
- File references use repository-relative paths (e.g. `src/compiler/sema.cpp`).
- "Compiler" means `logosc`; "runtime" means the support library and Hermes substrate the compiled program links against.
