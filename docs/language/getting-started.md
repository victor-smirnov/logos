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

There are two ways to build a Logos program: drive `logosc` directly (good for testing the compiler, single files), or use `lforge`, the Logos-level build system (what real projects use).

### With `lforge` (recommended)

Create a project layout:

```
hello/
  lforge.hermes
  src/main.logos
```

with `lforge.hermes`:

```
{
    name:    "hello",
    version: "0.1.0",
    src:     "src",
    entry:   "main"
}
```

Build and run:

```bash
cd hello
lforge build       # produces .lforge/debug/out/hello
lforge run         # build + execute
lforge clean       # remove .lforge/
```

See [lforge — Build System](../internals/lforge.md) for the manifest schema, output layout, and roadmap.

### Driving `logosc` directly

For single-file experiments and compiler testing:

```bash
build/bin/logosc hello.logos -o hello.o
cc hello.o build/lib/logos/lib*.a -lpthread -lm -o hello
./hello
```

Useful flags: `--emit-mlir`, `--emit-llvm`, `-O0`/`-O1`/`-O2`/`-O3`, `--diag-format=json` (NDJSON diagnostics for tooling), `--print-system-libdir`.

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
