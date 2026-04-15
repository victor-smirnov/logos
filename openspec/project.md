# OpenSpec: Logos Language Platform

## 1. Project Overview
**Logos** is a compiled systems programming language and platform designed for AI-driven software development. AI is the primary code author; humans read, review, and steer.

Logos absorbs functionality from the historical **Memoria Framework** -- not as a C++ extension layer, but as a full rewrite into a standalone language stack (compiler, runtime, stdlib, and data model). The rewrite is feasible because AI-assisted coding makes large-scale code generation practical.

## 2. Core Philosophy
1. **AI-First**: Syntax, semantics, and tooling optimized for AI generation and verification.
2. **Code = Data**: All code is Hermes documents, stored in containers, versioned, queried, transmitted.
3. **Static Verification**: Dependent types + ownership maximize compile-time guarantees. AI emits proofs alongside code.
4. **Data-Flow + Control-Flow**: Integrated RETE forward-chaining rules alongside imperative code.
5. **Self-Applicability**: Programs observe and reason about their own execution (HOCP).

## 3. Key Architectural Decisions

### 3.1 Rewrite, Not Extend
Memoria is a 16+ year C++ codebase with accumulated complexity (heavy template metaprogramming, multiple runtime backends, historical compromises). AI can rewrite the *functionality* without inheriting this complexity. The new codebase will be cleaner, with modern C++23/26 idioms, and designed from the start for the Logos execution model.

### 3.2 C++ Interop is Secondary
Original Memoria DSL Engine design prioritized tight M-Code ↔ C++ compatibility. This is no longer a primary requirement. Programmers will eventually stop reading generated code, as they already stopped reading assembly. Compatibility exists in the scope of:
- Logos standard library: partially written in C++ initially, migrated to Logos when practical
- Logos compiler and runtime: written in C++, using LLVM/MLIR
- FFI for calling system libraries

### 3.3 Custom Clang with Green Fibers
Two-color execution model:
- **Green code** (attributed): runs on segmented stacks. Lightweight fibers, thousands per core.
- **Red code** (default): runs on system stack. Full compatibility with existing C/C++ libraries.
- **Trampolines**: transition between green and red at call boundaries.

This gives both lightweight concurrency (essential for Memoria's thread-per-core/fiber model) and full system compatibility without the viral coroutine problem of C++20.

### 3.4 LLVM/MLIR as Compiler Backend
Logos compiler targets LLVM/MLIR for:
- JIT compilation of Logos code
- AOT compilation to native
- Optimization passes
- Future: custom MLIR dialects for RETE, Datalog, MAA

### 3.5 Not Self-Hosting (Yet)
Logos is written in C++ and will remain so for the foreseeable future. Self-hosting is not a goal, but not excluded in the long term. Standard library parts will migrate from C++ to Logos gradually, gaining type-system guarantees in the process.

## 4. Technology Stack
- **Implementation Language**: C++23 (C++26 near-term), Clang (custom fork with green fibers)
- **Data Substrate**: Hermes (ported from Memoria)
- **Communication**: HRPC (ported from Memoria)
- **IO/Runtime**: New reactor on green fibers (full reimplementation)
- **Compiler Backend**: LLVM/MLIR
- **Data Containers**: Ported from Memoria (Phase 3)
- **Storage Engines**: Ported from Memoria (Phase 3)
- **Build**: CMake + Ninja, VCPKG

## 5. Development Phases

### Phase 1: Runtime Foundation (C++)
Port and reimplement the runtime substrate.

**Phase 1A — Verification Framework:**
- `LOGOS_ASSERT` / `LOGOS_TRACE` macros with spec-linked diagnostics
- SQLite trace database + call chain capture (`-finstrument-functions`)
- Trace summarizer for LLM-consumable hierarchical feedback
- Exerciser harness, fuzz testing framework, coverage checker
- Validated on real Memoria components before Hermes port begins

**Phase 1B — Runtime Components:**
- Hermes: data format, type system, arena allocator, serialization, HermesPath, templates, schema *(implemented)*
- PEG parser generator (`tools/peg_gen`): `.peg` → C++ recursive descent parser; used for Hermes grammar and future Logos parser *(implemented)*
- HRPC: protocol, session management, streaming, TCP transport
- IO Reactor: **full reimplementation** on green fibers (segmented stacks)
- Custom Clang: green/red fiber attribute, segmented stack support, trampolines
- Build infrastructure, CI

**Depends on:** Nothing (greenfield)
**Delivers:** Working data format + networking + IO stack, with self-checking runtime verified through the optimistic convergence pipeline.

### Phase 2: Logos Compiler Stack
Build the language, compiler, and initial tooling.

**Scope:**
- Logos language design (syntax, type system, ownership, dependent types)
- M-Code intermediate representation (Hermes-document-backed)
- Parser (Logos source → M-Code AST)
- Type checker (ownership, borrowing, dependent type verification)
- LLVM/MLIR backend (M-Code → native code)
- Interpreter (for REPL, compile-time metaprogramming)
- RETE engine (forward-chaining rules)
- Datalog engine (backward-chaining queries)
- Language tools: REPL, LSP server, formatter, documentation generator

**Depends on:** Phase 1 (Hermes for code model, HRPC for tool communication, reactor for runtime)
**Delivers:** Working Logos language. Can write, compile, and run Logos programs that use Hermes data and green-fiber concurrency.

### Phase 3: Data Stack
Port Memoria's data containers and storage engines.

**Scope:**
- Container framework: B+Tree infrastructure, multistream trees, packed allocator
- Container types: Set, Map, Multimap, Vector, Sequence, hierarchical containers
- Core data structures: searchable sequences, SSRLE, LOUDS trees, wavelet trees, associative memory
- Storage engines: MemoryStore, SWMRStore (hybrid GC: tracing for hot, RC for cold)
- Future: OLTPStore, NANDStore, MAA integration

**Depends on:** Phase 1 (Hermes, IO) + Phase 2 (Logos language for higher-level container APIs)
**Delivers:** Full data platform. Logos programs can work with persistent, versioned, hardware-accelerable data structures.

## 6. Specification Documents

### Architecture
- [`architecture.md`](architecture.md) -- System architecture, component inventory

### Memoria Extraction
- [`memoria-extraction/core.md`](memoria-extraction/core.md) -- DSLEngine and M-Code foundation
- [`memoria-extraction/data-structures.md`](memoria-extraction/data-structures.md) -- Hermes format and data primitives
- [`memoria-extraction/hermes-runtime.md`](memoria-extraction/hermes-runtime.md) -- Hermes capabilities and gaps
- [`memoria-extraction/containers-and-storage.md`](memoria-extraction/containers-and-storage.md) -- Container/storage usage, memory management architecture
- [`memoria-extraction/execution-model.md`](memoria-extraction/execution-model.md) -- Execution paradigms, RETE, self-applicability

### Hermes Specifications
- [`specs/hermes.md`](specs/hermes.md) -- Hermes port specification (architecture, algorithms, API)
- [`specs/hermes-abi.json`](specs/hermes-abi.json) -- Machine-readable type registry (data types, binary layouts, type codes)
- [`specs/hermes-wire-format.md`](specs/hermes-wire-format.md) -- Formal binary wire format (bit-exact encodings, stability guarantees)

### PEG Grammar
- [`../tools/peg_gen/grammars/hermes.peg`](../tools/peg_gen/grammars/hermes.peg) -- Canonical Hermes data format grammar (machine-readable, generates the text parser)

### Verification & Methodology
- [`specs/verification-framework.md`](specs/verification-framework.md) -- Runtime observability framework specification
- [`../agent/09_development_methodology.md`](../agent/09_development_methodology.md) -- Agent-level development methodology instructions

### Language Design
- [`specs/language-design.md`](specs/language-design.md) -- Logos language syntax and semantics (Draft)

### Development
- [`development-plan.md`](development-plan.md) -- Detailed phase breakdown with deliverables
