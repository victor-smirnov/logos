# Metaprogramming

Logos's compile-time programming model is "ordinary Logos programs that have access to a compiler API". There are no procedural macros, no template metaprogramming in the C++ sense, and no separate macro language. Reflection over types and members is provided through the same hash-and-registry machinery the runtime uses.

This page summarizes the **current implementation state**, not the final design.

## What Exists Today

### Type Hashes and Type Codes

Every Logos type has a content-addressed identity:

- 23 bytes from SHA-256 of the resolved type expression for type identity.
- 8 bytes for member identity.
- A 56-bit user-space dispatch type code derived from the same hash.

This identity is shared between the language type system and the Hermes runtime, which gives metaprogramming and serialization a single source of truth. Metadata lookup is O(1) given the hash.

### Hermes Schema Type Code in TinyObjectMap

Phase −1 added a `u64 schema_type_code` to `TinyObjectMap` — both in the C++ implementation and in Logos, including the binary codec, hbs, and clone paths. The 904/904 test baseline covers it. This is the foundation the metaprogramming work builds on.

### Explicit Instantiation Declarations

```logos
#[type_code = N] datatype Array<i32>;
```

Without a body, this binds metadata to a generic instantiation. It is the chosen mechanism — over a `well_known` annotation form and a centralized registry block — because it keeps the binding co-located with the instantiation itself.

### Reflection Emission

[src/compiler/reflection_emit.cpp](../../src/compiler/reflection_emit.cpp) is the compiler-side path that emits per-type metadata. The compile-time programming surface is being grown on top of this rather than as a separate facility.

## Phase 2c.4e (In Progress)

The current refactoring effort migrates the compiler's internals from raw `Type *` access to a `TypeRef` accessor abstraction, on a per-site basis. The ordering is:

1. Small files first — done. Four files (33 of 255 sites) migrated.
2. Larger files next — `sema_expr.cpp`, `sema_stmt.cpp`, `mlir_gen_*.cpp`, `mono_impl.hpp`. Not yet started.

The recent commit history reflects this: `2c.4e.2c` switched `types_equal` to take `TypeRef`; `2c.4e.2d.0`–`2c.4e.2d.3` moved pointer-field reads to accessor calls in borrow_check, sema, mono_clone, and sema_impl.hpp.

Why this matters for metaprogramming: the `TypeRef` accessor surface is the same surface a compile-time Logos program will eventually see. Stabilizing it inside the compiler is a prerequisite for exposing it to user metaprograms.

The master plan for this work lives at `~/.claude/plans/snappy-knitting-kay.md` (out of tree).

## What Is Planned

- **Compile-time Logos programs.** Code that runs at compile time with access to a compiler API. Replaces the templating/macro layer of conventional systems languages. The first concrete users will be the standard library's container traits.
- **Vec<Class> over C++ type lists.** Once compile-time programs run, generic-over-shape utilities (serializers, hashers, equality) are written as ordinary loops over a `Vec<Class>` rather than as recursive type-list templates.
- **Datalog/Rete on Logos.** A native Datalog engine in Logos itself, using Hermes as the fact base. Long-term, this is a candidate for the compiler's trait resolution and borrow analysis. It is also intentional dogfooding — the engine and the compiler exercise each other.
- **Constraint solving via Z3.** Embedding Z3 cleanly behind a small solver layer is a near-term priority. Used for trait resolution, reward signals (for AI-generated code), and verification.

## Shape and Scope

Calling Logos metaprogramming "no macros, no templates, no separate macro language" is technically defensible but misleading — every claim of that form depends on what one means by *macro* or *template*. The accurate framing is positive, not negative: Logos pursues **metaprogramming in the large**.

Logos delegates more responsibility to compile-time programs than mainstream systems languages have done. This is driven by practical need (Memoria's data structures and code generation are sizeable), not by academic interest. The level of metaprogramming Logos targets is the level Memoria requires.

Two consequences are worth stating up front:

1. **Metaprograms are ordinary Logos.** They are written in the same language, executed by the compiler's JIT, and are as safe as any other Logos code. There is no separate macro dialect, no separate evaluator, no separate type system, and no second set of safety rules to learn.

2. **Compiler + build environment is a data platform.** The compiler and `lforge` together form a full data platform: a data layer, an execution layer (data-flow graph execution engine), a query surface, monitoring, and a user-facing interface (text and web). Over time the data layer will be provided by Memoria itself.

In other words: code generation in Logos is a Logos program with a compiler API; the compiler is something you query and observe, not just something you invoke.

## Modular, Service-Oriented Compiler

The Logos compiler is being moved toward a **service-oriented architecture**: a set of cooperating modules, with the compiler proper acting as an orchestrator. Modules may run in-process, but the architecture allows them to be **separate processes**, because their communication substrate is Hermes — a Hermes document is a natural inter-process payload, no FFI required.

This is not aspirational from the data-format side: **the compiler already uses Hermes for its internal representations**. The IR-as-Hermes choice is what makes splitting the compiler into services tractable, and it is what lets metaprograms see and rewrite the same data the compiler does, without an impedance mismatch.

The orchestration story is layered:

- **The compiler** orchestrates compilation services (parsing, sema, borrow checking, monomorphization, codegen, reflection).
- **`lforge`** orchestrates compilations and build artifacts, as well as the surrounding data platform (queries, monitoring, text/web UI). The compiler is one component of `lforge`'s broader orchestration; `lforge` is the entry point for users and CI.

Concretely, individual passes can be **lifted into services**. The borrow checker is a plausible early candidate: a long-running service that ingests a Hermes-encoded program representation and returns a Hermes-encoded set of diagnostics or proofs. The same shape works for sema queries, type lookup, monomorphization, or any analysis that benefits from caching and parallelism.

### Metaprogramming as the Extension Mechanism

In a service-oriented compiler, **metaprogramming is the natural extension mechanism** — not a side feature. The same Hermes IR that services exchange is what metaprograms read and produce, so user metaprograms compose with built-in services on equal footing. Two consequences:

1. **The compiler is extensible.** A user-supplied metaprogram can introduce a new analysis, a new diagnostic, a new rewrite, or a new code-generation strategy by plugging into the same protocol the built-in services use.
2. **The platform is extensible.** Applications can customize `lforge` itself — adding domain-specific build steps, query types, monitoring hooks, or UI surfaces — through metaprograms running on the same data layer.

The intended end state: Logos's compile-time programming is not "macros that happen during compilation" but "a programmable, observable platform whose units of execution are Logos programs over a Hermes data layer." The compiler is one user of that platform; user metaprograms are another; `lforge` services are a third.

### Analogy: LLVM/Clang

The shape described above is not exotic — **LLVM/Clang already work this way internally**. LLVM is a deeply modular toolchain, and Clang ships a real data platform under the hood: data-layer abstractions, a database (PCH/modules), incremental analyses, dataflow infrastructure, AST matchers, plugin points, and so on. The architectural pattern is the right one.

What holds Clang back from realizing it cleanly is not the architecture but the **substrate**:

- **C++ inertia** — every platform feature is hand-built in C++, at C++'s development cost.
- **Plugin extension is severely limited.** The plugin surface is narrow and brittle — you cannot, for example, define a new custom attribute without rebuilding the compiler itself. Genuine extensibility lives behind a Clang fork, not behind a plugin boundary.
- **No language-level metafunctions** — extension is via plugins (separate build, separate ABI dance, no first-class language entry point). The platform is not visible inside the language.
- **No integrated data layer** — PCH and module caching are real, but they are local solutions, not a coherent data substrate the language can see and program against.
- **Strictly linear compilation pipeline.** Clang's pipeline is parse → sema → codegen, end to end, in one direction. There is no notion of an *incremental-iterative* compilation in the way Logos targets one — sema cannot ask a metafunction to materialize new code that re-enters sema, then re-enters again, on a fixed point. Adding such a loop to Clang is not a configuration change; it is a deep architectural rewrite. Even if metafunctions were desired, the pipeline shape forecloses them.

A decade ago the aggregate of these constraints — pipeline shape, no first-class metafunctions, narrow plugin surface, hand-built C++ everywhere — would have been a prohibitive multi-year rewrite on top of an already enormous codebase. With current AI assistance the cost is no longer the issue. The issue is **the destination**: even after such a rewrite you still have "C++ with metafunctions". Impressive, but still C++ — a language from which you cannot even cleanly carve out a safe subset. The platform you can build on top of it inherits that ceiling.

So the question is not "can it be done?" but "is the resulting language the one you want?" For Logos's goals, the answer is no.

Logos can do this differently because we are *not* paying those costs:

- **AI-speed coding.** A platform sized like Clang's takes years of hand-rolled C++; with AI as a primary author and Logos as a high-leverage target language, the trade-off shifts.
- **Rust-derived expressive, safe type system.** The compiler and platform are written in a language with ownership, lifetimes, and traits — the same safety floor user code stands on.
- **Memoria for data and compute.** Memoria provides the data-platform substrate the compiler needs (durable structured storage, query, dataflow execution) instead of bespoke per-feature solutions like PCH.
- **Open-source freedom.** No need to keep the architecture hidden behind a stable C++ ABI or to preserve plugin compatibility across vendor releases.

Logos is not trying to invent a paradigm; it is trying to *land* a paradigm that LLVM/Clang have demonstrated is right but cannot fully execute.

This — concretely the impossibility of bolting incremental-iterative compilation, first-class metafunctions, and a coherent data layer onto an existing Clang-shaped pipeline — is **why Logos's compiler is being built from scratch** rather than as a frontend on top of an existing toolchain. The compiler is shaped, from day one, around metaprogramming-in-the-large. That shape is non-retrofittable.

## Memoria and Logos Are Co-Developed

Logos and Memoria are intentionally entangled, in both directions:

- **Memoria will be written in Logos.** The mature Memoria codebase is a Logos project, not a C++ one. Logos's metaprogramming surface is sized to what that codebase needs.
- **Logos is Memoria's first user.** Logos exercises Memoria from day one — there is no notional "external user" the design is being held for.

This is not a future arrangement. It is already partially the case: **Hermes was originally designed inside Memoria and ported into Logos** as the data substrate. Other Memoria components are expected to follow the same path — first proven in C++ Memoria, then re-expressed in Logos as the Logos toolchain catches up. Co-development is the normal mode, not a transition.
