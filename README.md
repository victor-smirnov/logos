# Logos

Logos is a compiled, statically-typed systems programming language with its own compiler (`logosc`), standard library, and runtime. It descends from ideas explored in the [Memoria Framework](https://github.com/victor-smirnov/memoria), but is a standalone language platform — not a C++ framework layer.

## What Logos Is

- A compiled language (`.logos`) with ownership/borrowing, traits, generics, monomorphization, and pattern matching.
- A native compiler pipeline (`logosc`) covering parse, sema, borrow checking, monomorphization, MLIR generation, and LLVM lowering.
- A standard library (`stdlib/`) including a first-class **Hermes** integration — a relocatable, schema-aware, tagged data substrate.
- A large executable test suite (~800 passing tests, ~165 diagnostic tests) that gates merges.

## Relationship to Rust

The current Logos syntax and resource model are borrowed from Rust, and Rust experience transfers directly: ownership, borrowing, traits, and generics will feel familiar. This is a starting point, not a goal — Logos is built in a different paradigm (AI-first ergonomics, code/data unified through Hermes, compile-time programming as ordinary Logos code, green fibers without async coloring). Logos and Rust are expected to diverge syntactically and paradigmatically over the medium term.

## Design Direction

- **AI-first ergonomics** — syntax and semantics chosen for reliable LLM generation and verification.
- **Code + data unified** — Hermes is *built into the language*: `@{...}` / `@[...]` are literal forms in the grammar, capture (`$ident`, `${expr}`) is type-checked at sema time, view types carry lifetimes through the borrow checker, and module-scope literals fold to rodata. No DSL, no macros, no FFI between values and data.
- **Systems-level performance** — AOT native codegen, ownership, explicit memory.
- **Verification-oriented** — broad diagnostics, runtime tracing, and a strong test culture.
- **Pragmatic interop** — C/C++ FFI exists; Logos is the primary programming model.

## Getting Started

Build the compiler:

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
```

Compile and run a program:

```bash
build/src/compiler/logosc examples/hermes_round_trip.logos -o round_trip
./round_trip
```

Run the test suite:

```bash
cd build && ctest --output-on-failure
```

See [docs/language/getting-started.md](docs/language/getting-started.md) for prerequisites and details.

## Project Structure

```
logos/
  src/            Compiler, runtime, Hermes, HRPC, reactor, verification
  stdlib/         Logos standard library and language runtime
  tests/          Language test suites (pass / fail)
  examples/       Example Logos programs
  tools/          Supporting tools (PEG generator, audits, HRPC codegen)
  docs/           Documentation (start at docs/README.md)
```

## Documentation

The documentation lives in [docs/](docs/README.md) and is split into two tracks:

**For users of the language**

- [Overview](docs/language/overview.md) — what Logos is, design axes, comparisons.
- [Getting Started](docs/language/getting-started.md) — build, run, test.
- [Syntax](docs/language/syntax.md) — types, expressions, statements, patterns.
- [Ownership and Borrowing](docs/language/ownership.md) — `&`/`&mut`, lifetimes.
- [Generics and Traits](docs/language/generics-traits.md) — generic functions, trait impls.
- [Comprehensions](docs/language/comprehensions.md) — list/map comprehensions over plain values and Hermes.
- [Hermes in Logos](docs/language/hermes.md) — literals, capture, view types.
- [Language Reference](docs/language/reference/README.md) — normative reference (lexical, types, items, expressions, statements, patterns, plus cross-cutting topics).

**For contributors**

- [Compiler Architecture](docs/internals/architecture.md) — the `logosc` pipeline.
- [Hermes Runtime](docs/internals/hermes-runtime.md) — Datatype/Storage/View, zones, type registry.
- [Metaprogramming](docs/internals/metaprog.md) — current state of compile-time programming.

**Status**

- [Roadmap](docs/roadmap.md) — what is implemented, in progress, and planned.

**Essays**

- [AI Platform Era](docs/ai-platform/README.md) — how AI authorship reshapes the requirements on languages and platforms, and how Logos responds.

## Technology Stack

| Component | Technology |
|-----------|------------|
| Language Implementation | C++23 |
| Frontend / Semantics | PEG parser + sema + borrow checker |
| Codegen Backend | LLVM / MLIR |
| Data Substrate | Hermes (relocatable tagged object graphs) |
| RPC | HRPC (bidirectional streaming) |
| IO / Concurrency | io_uring reactor with green fibers |
| Build | CMake + Ninja, VCPKG |
| Platform | Linux (Ubuntu LTS) |

## Status

Active implementation. The compiler, runtime, and standard library are in daily use; the language has not stabilized and the documentation reflects the current state, not a frozen specification. See the [Roadmap](docs/roadmap.md) for what is in flight.

## License

Apache License 2.0. See [LICENSE](LICENSE).
