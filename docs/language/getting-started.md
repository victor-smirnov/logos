# Getting Started

This page walks through building the Logos compiler, running an example program, and running the test suite.

## Prerequisites

- Linux (Ubuntu LTS is the supported platform).
- A C++23-capable compiler (recent Clang).
- CMake and Ninja.
- VCPKG, with the manifest in this repository (third-party dependencies resolve automatically on first configure).
- LLVM/MLIR development packages. The compiler target is gated on `LLVM_PACKAGE_VERSION` being detectable.

## Building

From the repository root:

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
```

The compiler binary is `build/src/compiler/logosc`.

## Hello, Logos

A minimal program lives at [examples/hermes_round_trip.logos](../../examples/hermes_round_trip.logos). The simplest possible source looks like:

```logos
package hello;

use std.io;

fn main() -> i32 {
    let s: String = String::from_str("hello, logos\n");
    print_string(&s);
    return 0;
}
```

Compile and run:

```bash
build/src/compiler/logosc hello.logos -o hello
./hello
```

`logosc --help` lists current flags (output path, dump options for AST/MLIR/LLVM IR, optimization level).

## Examples

- [examples/hermes_round_trip.logos](../../examples/hermes_round_trip.logos) — parse a Hermes document and stringify it.
- [examples/hermes_showcase.logos](../../examples/hermes_showcase.logos) — broader tour of Hermes features (capture, view types, typed arrays).

## Running the Test Suite

The language tests live in `tests/logos`:

```
tests/logos/pass/   — programs that must compile and run with matching .expected output
tests/logos/fail/   — programs that must produce a specific diagnostic
```

A typical run is driven by CTest:

```bash
cd build && ctest --output-on-failure
```

To run a single test directly:

```bash
tests/logos/run_test.sh tests/logos/pass/arith_i64
```

The full suite is sizeable (~660 pass tests, ~245 fail tests). CI gates merges on it being green.

## Where to Go Next

- [Syntax](syntax.md) — the language reference.
- [Ownership](ownership.md) — how `&`, `&mut`, and lifetimes interact.
- [Hermes in Logos](hermes.md) — the data substrate.
