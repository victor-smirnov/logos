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

The compiler binary is `build/bin/logosc`.

## Hello, Logos

A minimal program lives at [examples/writ_round_trip.logos](../../examples/writ_round_trip.logos). The simplest possible source looks like:

```logos
package hello;

use logos.std.io;
use logos.mem.string;

fn main() -> i32 {
    let s: String = String::from("hello, logos\n");
    print_string(&s);
    return 0;
}
```

There are two ways to build a Logos program: drive `logosc` directly (good for testing the compiler, single files), or use `lforge`, the Logos-level build system (what real projects use).

### With `lforge` (recommended)

Create a project layout:

```
hello/
  lforge.writ
  src/main.logos
```

with `lforge.writ`:

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
cc hello.o -Wl,--start-group build/lib/logos/lib*.a -Wl,--end-group \
   -lpthread -lm -lstdc++ -o hello
./hello
```

Useful flags: `--emit-mlir`, `--emit-llvm`, `-O0`/`-O1`/`-O2`/`-O3`, `--diag-format=json` (NDJSON diagnostics for tooling), `--print-system-libdir`.

## Examples

- [examples/writ_round_trip.logos](../../examples/writ_round_trip.logos) — parse a Writ document and stringify it.
- [examples/writ_showcase.logos](../../examples/writ_showcase.logos) — broader tour of Writ features (capture, view types, typed arrays).

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

To run a single test by name:

```bash
cd build && ctest -R arith_i64 --output-on-failure
```

The full suite is sizeable (~3100 pass tests, ~900 fail tests). CI gates merges on it being green.

## Where to Go Next

- [Syntax](syntax.md) — the language reference.
- [Ownership](ownership.md) — how `&`, `&mut`, and lifetimes interact.
- [Writ in Logos](writ.md) — the data substrate.
