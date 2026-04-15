# Logos

Logos is a compiled systems programming language with its own compiler toolchain, runtime stack, standard library, and data model.

The project started as a rewrite of ideas and infrastructure from the [Memoria Framework](https://github.com/victor-smirnov/memoria), but Logos itself is not a C++ framework layer. It is a standalone language platform at the C++/Rust systems level.

## What Logos Is

- A statically-typed compiled language (`.logos`) with ownership/borrowing, traits, generics, overloads, pattern matching, and specialization.
- A native compiler pipeline (`logosc`) with semantic analysis, monomorphization, borrow checking, and LLVM/MLIR code generation.
- A language-level standard library (`stdlib/`) including Hermes integration and core runtime APIs.
- A comprehensive test suite (`tests/logos`) covering language semantics, diagnostics, integration scenarios, and runtime behavior.

## Design Direction

- **AI-First**: syntax, semantics, and tooling optimized for AI generation and verification
- **Code + Data Unification**: Hermes is a first-class data substrate used directly by language/runtime components
- **Systems-Level Performance**: ahead-of-time native code generation with explicit control over memory and ownership
- **Verification-Oriented Development**: strong diagnostics, runtime assertions/tracing, and broad language test coverage
- **Interop Pragmatism**: C/C++ interoperability exists, but Logos is the primary programming model

## Technology Stack

| Component | Technology |
|-----------|------------|
| Language Implementation | C++23/26 |
| Frontend / Semantics | Logos parser + sema + borrow checker |
| Codegen Backend | LLVM / MLIR |
| Data Substrate | Hermes (relocatable tagged object graphs) |
| Communication | HRPC (bidirectional streaming RPC) |
| IO / Runtime | io_uring reactor with green fibers |
| Build | CMake + Ninja, VCPKG |
| Platform | Linux (Ubuntu LTS) |

## Project Structure

```
logos/
  src/            Compiler, runtime, Hermes, reactor, verification
  stdlib/         Logos standard library modules
  tests/          Language test suites (smoke/core/ownership/advanced/integration/diagnostics)
  tools/          Supporting tools (including PEG generator)
  docs/           Language/runtime documentation and implementation notes
  openspec/       Formal specs and long-range architecture/design documents
```

## Specifications

- [Project Overview](openspec/project.md)
- [Architecture](openspec/architecture.md)
- [Development Plan](openspec/development-plan.md)
- [Hermes Port Spec](openspec/specs/hermes.md)
- [Hermes ABI (JSON)](openspec/specs/hermes-abi.json)
- [Hermes Wire Format](openspec/specs/hermes-wire-format.md)
- [Verification Framework](openspec/specs/verification-framework.md)
- [Datatypes Guide](docs/datatypes.md)
- [Language Feature Inventory](docs/rust_feature_inventory.md)

## Status

**Active implementation.** Logos compiler/runtime/stdlib are in use, and the repository maintains a large executable test suite.

## License

Apache License 2.0. See [LICENSE](LICENSE).
