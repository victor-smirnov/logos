# Logos Documentation

Entry point, organized by audience: the **language** track is for users writing Logos code; the **internals** track is for contributors working on the compiler, runtime, and Writ.

## Language

To *use* Logos:

- [Overview](language/overview.md) — what Logos is, what it is not, the design axes.
- [Getting Started](language/getting-started.md) — build the compiler, write and run a program, run the test suite.
- [Syntax](language/syntax.md) — types, expressions, statements, patterns, modules.
- [Ownership and Borrowing](language/ownership.md) — `&`/`&mut`, lifetimes, the borrow checker.
- [Generics and Traits](language/generics-traits.md) — generic functions, trait impls, monomorphization.
- [Comprehensions](language/comprehensions.md) — list/map comprehensions, plain and Writ forms.
- [Writ in Logos](language/writ.md) — Writ as a first-class language feature: literals, capture, view types.
- [Language Reference](language/reference/README.md) — normative reference by surface form (lexical / types / items / expressions / statements / patterns) plus cross-cutting topics.

## Internals

To *work on* Logos itself:

- [Compiler Architecture](internals/architecture.md) — the `logosc` pipeline from source to native code.
- [lforge — Build System](internals/lforge.md) — the Logos-level build orchestrator; MVP and roadmap toward daemon mode + package manager.
- [Package Management](internals/package-manager.md) — design (no impl yet): lforge dependency resolve/fetch/build/cache. Go-modules-shaped, Writ manifest, git-distributed, no central registry.
- [Zones](internals/zones.md) — the foundational memory model: multi-segment regions, self-relative `i64` offsets, isolation + the root zone (heap/stack glue). What makes ZTypes position-independent and portable across processes/architectures.
- [Writ Runtime](internals/writ-runtime.md) — Datatype/Storage/View, zones, the type registry.
- [Metaprogramming](internals/metaprog.md) — current state of compile-time programming and reflection.
- [HRPC](internals/hrpc.md) — bidirectional Writ-native RPC and streaming: wire format, session model, IDL, C++/Logos split.

## Target Compute Model

- [LCM — Logos Compute Model](lcm/README.md) — the abstract substrate Logos targets: many small xPUs close to the data, message-passing over hardware-accelerated HRPC, x86_64/Linux as one target among many.

## Project Status

- [Roadmap](roadmap.md) — strategic direction (MP1/MP2/MP3, build-system pivot, self-hosting), current phase, and a snapshot of implemented/in-progress/planned.
- [History Retrospective (2026-05)](retro/2026-05-history-retrospective.md) — git-history statistics: bug families by subsystem, long-running epics, workaround-vs-fundamental balance, weekly activity × topic. Snapshot at commit `0d981302`, with recompute commands.

## Essays

Long-form motivation and design rationale — broader, opinionated, slower-changing:

- [AI Platform Era](ai-platform/README.md) — how AI authorship reshapes language/platform requirements; the rationale behind Logos's metaprogramming, modular compiler, and lforge platform.

## Conventions

- Code samples are real Logos programs unless marked `// sketch` (design intent not yet implemented).
- File references use repository-relative paths (e.g. `src/compiler/sema.cpp`).
- "Compiler" means `logosc`; "runtime" means the support library and Writ substrate the compiled program links against.
