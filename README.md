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

Build the compiler. The compiler **must** be clang 20 — the generated parser does
not build with GCC, and CMake picks `c++` (often GCC) unless you say otherwise:

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_CXX_COMPILER=clang++-20
cmake --build build
```

Compile and run a program. `logosc` emits a **native object file**, not an
executable — you link it against the stdlib archives yourself:

```bash
build/bin/logosc examples/writ_round_trip.logos -o round_trip.o
cc round_trip.o -Wl,--start-group build/lib/logos/*.a -Wl,--end-group \
   -lpthread -lm -lstdc++ -Wl,--gc-sections -Wl,--allow-multiple-definition \
   -o round_trip
./round_trip
```

(That is the same link line `tests/logos/run_test.sh` uses. Skipping it and
running `logosc`'s output directly gives "Permission denied" — the file is an
ELF relocatable, and the 126 you see is the shell's, not the program's.)

Run the tests with `scripts/lt`. The full suite is over 11,000 tests, so `lt`
runs the part a task touches first: L0 is the task's own tests, L1 the groups
they belong to (`tests/groups.rules`), L2 everything else.

```bash
scripts/lt task new mytask /bc_/ tests_name   # L0: names or /regexes/
scripts/lt run --level 1 --upto               # L0, then L1
scripts/lt run --all                          # everything
scripts/lt run --group borrow_checker         # one group
scripts/lt show TEST --output                 # a test's history and output
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
