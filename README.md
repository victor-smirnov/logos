# Logos

Logos is a compiled, statically-typed systems programming language with its own compiler (`logosc`), standard library, and runtime. It descends from ideas explored in the [Memoria Framework](https://github.com/victor-smirnov/memoria), but is a standalone language platform — not a C++ framework layer.

## What Logos Is

- A compiled language (`.logos`) with ownership/borrowing, traits, generics, monomorphization, and pattern matching.
- A native compiler pipeline (`logosc`) covering parse, sema, borrow checking, monomorphization, MLIR generation, and LLVM lowering.
- A standard library (`stdlib/`) including a first-class **Writ** integration — a relocatable, schema-aware, tagged data substrate.
- A large executable test suite (~800 passing tests, ~165 diagnostic tests) that gates merges.

## Relationship to Rust

The Rust-like surface was effectively chosen by the model. The original plan was a much simpler, IR-adjacent syntax with no expressions — explicit, verbose, optimised for small and mid-sized models. In practice the language also has to be pleasant for humans to read and write, and Rust turned out to sit in a sweet spot: expressive, low-level, a good DSL host, and — importantly — models generate it more reliably than most alternatives. Since Logos is built for models first, leaning into a syntax they already handle well is the pragmatic choice.

Logos inherits surface syntax, affine types, generics, and the ownership/borrowing model from Rust, but it is *not* Rust: not source-compatible, not aiming at portability in either direction, and willing to diverge wherever AI-first ergonomics, Writ-based code/data unification, compile-time programming as ordinary Logos code, or green-fiber concurrency without async coloring point elsewhere. Substantial divergence is expected in the near future.

## Design Direction

- **AI-first ergonomics** — syntax and semantics chosen for reliable LLM generation and verification.
- **Code + data unified** — Writ is *built into the language*: `@{...}` / `@[...]` are literal forms in the grammar, capture (`$ident`, `${expr}`) is type-checked at sema time, view types carry lifetimes through the borrow checker, and module-scope literals fold to rodata. No DSL, no macros, no FFI between values and data.
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
build/src/compiler/logosc examples/writ_round_trip.logos -o round_trip
./round_trip
```

Run the test suite:

```bash
cd build && ctest --output-on-failure
```

See the [Getting Started guide](https://logos-lang.dev/docs/getting-started/) for prerequisites and details.

## Project Structure

```
logos/
  src/            Compiler, runtime, Writ, HRPC, reactor, verification
  stdlib/         Logos standard library and language runtime
  tests/          Language test suites (pass / fail)
  examples/       Example Logos programs
  tools/          Supporting tools (PEG generator, audits, HRPC codegen)
  docs/           Language spec, ADRs, and internal design notes
```

## Documentation

User-facing documentation — the guide, language spec, subsystem references
(Writ, Hest, Deem, Trama, Metacall), API reference, and essays — lives on the
project site: **[logos-lang.dev](https://logos-lang.dev)**.

This repository keeps the normative **[language spec](docs/spec/)** (the source
the site renders), the **[divergences register](docs/DIVERGENCES.md)**, and
engineering notes for contributors:

- [Compiler Architecture](docs/internals/architecture.md) — the `logosc` pipeline.
- [Writ Runtime](docs/internals/writ-runtime.md) — Datatype/Storage/View, zones, type registry.
- [Metaprogramming](docs/internals/metaprog.md) — current state of compile-time programming.
- [Architecture Decision Records](docs/adr/) — design decisions and their rationale.

## Technology Stack

| Component | Technology |
|-----------|------------|
| Language Implementation | C++23 |
| Frontend / Semantics | PEG parser + sema + borrow checker |
| Codegen Backend | LLVM / MLIR |
| Data Substrate | Writ (relocatable tagged object graphs) |
| RPC | HRPC (bidirectional streaming) |
| IO / Concurrency | io_uring reactor with green fibers |
| Build | CMake + Ninja, VCPKG |
| Platform | Linux (Ubuntu LTS) |

## Status

Active implementation. The compiler, runtime, and standard library are in daily use; the language has not stabilized and the documentation reflects the current state, not a frozen specification.

## License

Dual-licensed under Apache 2.0 and MIT — at your option. See
[LICENSE-APACHE](LICENSE-APACHE), [LICENSE-MIT](LICENSE-MIT), and
[COPYRIGHT](COPYRIGHT) for the full text and pick-your-licence rule.

The Logos Lang name and project marks are governed separately by
[TRADEMARKS.md](TRADEMARKS.md) — most descriptive uses are permitted
without permission. Forks and derivative distributions must rename.

## Contributing

Patches are welcome. Contributions are dual-licensed (Apache 2.0 /
MIT, same as the codebase) and must include a [DCO](DCO) sign-off
on each commit (`git commit -s ...`). See
[CONTRIBUTING.md](CONTRIBUTING.md) for the full workflow.
