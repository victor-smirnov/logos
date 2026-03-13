# Logos

A programming language, compiler, and execution platform designed for AI-driven software development. AI is the primary code author; humans review, steer, and verify.

Logos absorbs the functionality of the [Memoria Framework](https://github.com/victor-smirnov/memoria) through a full rewrite, reimplementing its proven algorithms and data structures in a new codebase optimized for AI generation and verification.

## Architecture

```
Phase 3: Data Stack
  Containers (B+Tree) | Storage Engines | Semantic Graphs

Phase 2: Compiler Stack
  Parser | Type Checker | LLVM/MLIR Backend | Interpreter
  RETE Engine | Datalog Engine
  M-Code (Hermes Document ASTs)

Phase 1: Runtime Foundation
  1A: Verification Framework (LOGOS_ASSERT, tracing, SQLite diagnostics)
  1B: Hermes | HRPC | IO Reactor (green fibers) | Custom Clang
```

## Key Design Decisions

- **AI-First**: syntax, semantics, and tooling optimized for AI generation and verification
- **Code = Data**: all code is Hermes documents, stored in containers, versioned, queried, transmitted
- **Dependent Types**: maximize compile-time guarantees; AI emits proofs alongside code
- **Two-Color Execution**: green fibers (segmented stacks) for lightweight concurrency, red code (system stack) for C/C++ compatibility
- **Self-Applicability (HOCP)**: programs observe and reason about their own execution
- **Optimistic Convergence**: self-checking runtime with structured diagnostics minimizes human verification effort

## Technology Stack

| Component | Technology |
|-----------|------------|
| Implementation | C++23/26 (Clang custom fork) |
| Data Substrate | Hermes (relocatable tagged object graphs) |
| Communication | HRPC (bidirectional streaming RPC) |
| IO / Runtime | io_uring reactor with green fibers |
| Compiler Backend | LLVM / MLIR |
| Build | CMake + Ninja, VCPKG |
| Platform | Linux (Ubuntu LTS) |

## Project Structure

```
logos/
  agent/          Synthea identity + cognitive architecture + development methodology
  openspec/       Specifications and design documents
    specs/        Formal specs: Hermes ABI, wire format, verification framework
  memoria/        Memoria reference (gitignored, read-only)
```

## Specifications

- [Project Overview](openspec/project.md)
- [Architecture](openspec/architecture.md)
- [Development Plan](openspec/development-plan.md)
- [Hermes Port Spec](openspec/specs/hermes.md)
- [Hermes ABI (JSON)](openspec/specs/hermes-abi.json)
- [Hermes Wire Format](openspec/specs/hermes-wire-format.md)
- [Verification Framework](openspec/specs/verification-framework.md)

## Status

**Pre-implementation.** Specification phase. Phase 1A (verification framework) is next.

## License

Apache License 2.0. See [LICENSE](LICENSE).
