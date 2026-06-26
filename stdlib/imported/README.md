# stdlib Imports — Policy and Workflow

Logos Lang's standard library is partially **adapted from
[rust-lang/rust](https://github.com/rust-lang/rust)'s standard
library** — primarily the `library/core/`, `library/alloc/`, and
`library/std/` trees. Rust's stdlib is dual-licensed Apache 2.0 /
MIT (same as ours), so importing is licence-compatible. Where the
existing implementation is well-tested and the API maps cleanly,
adapting Rust's code costs less than re-deriving from scratch and
gives us inherited correctness.

This file documents the policy and per-file workflow. The
**authoritative manifest** of imported files is in
[RUSTC-PROVENANCE.md](RUSTC-PROVENANCE.md).

## When to import vs. write from scratch

Import when:

* Rust has a mature, hand-written implementation of the algorithm
  (collections, iterators, formatting, numerics, allocator-side
  data structures).
* The API maps cleanly onto Logos types (most non-async, non-trait-
  object-heavy code).
* The Rust implementation is non-trivial enough that re-deriving
  has real risk of introducing bugs.

Write from scratch when:

* Logos has different semantic primitives (Writ for tagged data;
  fibers for concurrency; const-generic-over-WritStatic).
* The Rust implementation leans on rustc-internal lang items,
  `#[lang]` attributes, or specialization.
* The code is simple enough that direct authorship is faster than
  porting + adapting.

Where these conflict, ergonomic parity with Rust matters: callers
who know `Vec::iter()` shouldn't have to learn `vec_iter(v)` if
porting gets us the same shape cheaply.

## Layout — separate subtree, mirrored module paths

Imported stdlib files live under
[`stdlib/imported/`](imported/), mirroring our logical module-path
structure (e.g. `stdlib/imported/std/collections/btree/node.logos`
declares `package std.collections.btree;`). The build system globs
every `.logos` file under `stdlib/`, so callers continue to write
`use logos.mem.collections.btree;` regardless of physical location —
import semantics is package-name-based; file placement is the
build system's concern, not the user's.

The tradeoff against an in-place layout:

* **Visibility**: `ls stdlib/imported/` enumerates every adapted
  file at a glance.
* **Audit**: drift checks against upstream and copyright sweeps
  run bulk on one subtree.
* **Sustainability**: when an imported piece is later replaced by a
  hand-written native version, the file simply moves from
  `stdlib/imported/std/.../*.logos` to `stdlib/std/.../*.logos` —
  callers see no change; the manifest row migrates to "retired" or
  is removed.

A package may have files in BOTH `stdlib/std/` and
`stdlib/imported/std/` — Logos accepts multi-file modules and
merges them transparently. This is the natural shape when an
imported algorithm gains Logos-native extensions (e.g. Writ-zone
integration): the imported file stays under `stdlib/imported/`,
the extensions live next to it under `stdlib/std/`.

## Per-file provenance header

Every imported stdlib file MUST start with a provenance comment
block — this is **required by Apache 2.0 § 4(b)** ("carry prominent
notices stating that You changed the files") and ethical attribution.
It is also the **only** exception to the project-wide "no per-file
headers" policy (per [../../CONTRIBUTING.md](../../CONTRIBUTING.md)).

Format:

```logos
// Imported from rust-lang/rust@<COMMIT-SHA>
// Original path: library/<crate>/src/<original-path>.rs
// Original copyright: The Rust Project Developers (Apache 2.0 / MIT).
// Modifications: <one-line summary of porting changes>
```

The `<COMMIT-SHA>` must match a pinned-commit row in
[RUSTC-PROVENANCE.md](RUSTC-PROVENANCE.md). Group related
files into batches that share a commit; the manifest tracks one
SHA per batch.

## Partial-file imports

stdlib imports often need only **part of a Rust file** (one struct,
one function, a couple of methods). Two patterns:

1. **Single-concept import**: the imported portion is the bulk of
   the file (≥ 70%). Use the standard provenance header above.

2. **Mixed imported + native**: a file blends an imported algorithm
   with Logos-specific additions. The provenance header gains a
   per-block scope:

   ```logos
   // Imported from rust-lang/rust@<COMMIT-SHA>
   // Original path: library/alloc/src/collections/btree/node.rs
   // Original copyright: The Rust Project Developers (Apache 2.0 / MIT).
   // Modifications: ported core `Node` layout + split logic; the
   // Writ-zone integration below this line is Logos-native.

   // ── Imported region (Rust-derived) ───────────────────────────
   pub struct Node<K, V> { ... }
   impl<K, V> Node<K, V> { ... }

   // ── End imported region ──────────────────────────────────────

   // Native Logos-specific code follows.
   pub fn node_to_writ_blob(...) { ... }
   ```

   Use `// ── Imported region (Rust-derived) ─` / `// ── End
   imported region ─` markers so reviewers can spot the boundary
   without consulting the manifest.

## Workflow for adding a batch

1. Identify a target module/function to port.
2. Pin the rustc commit (or reuse an existing batch SHA from the
   manifest if the source hasn't moved).
3. Copy the Rust source locally, port to Logos syntax:
   * Imports: `use crate::` → `use std.` or remove (rustc-internal).
   * `pub fn foo() -> Result<T, E>` etc. — Logos has `Result` in
     stdlib; check it imports.
   * Removed: `#[lang = "..."]`, `#[stable(...)]`, `#[unstable(...)]`,
     `#[rustc_const_stable(...)]` — all rustc-internal annotations.
   * `&[T]`, `Vec<T>`, `String`, `Box<T>`, `Rc<T>`, `Arc<T>` —
     check we have these (most done).
   * Trait bounds: `T: Copy + Clone + Default` — check impls exist
     for our primitives.
4. Add the per-file provenance header.
5. Append a row to the manifest in RUSTC-PROVENANCE.md.
6. Add or extend tests covering the imported behaviour (often the
   matching `tests/imported/ui/<area>` files do this).
7. ctest.

## What we cannot import wholesale

Same constraints as the test-suite import (see
[../../tests/imported/README.md](../../tests/imported/README.md)):

* `async` / `await` / `Future` machinery — fibers instead.
* `macro_rules!` definitions — convert to `#[fn_macro]` /
  `#[token_macro]` if the macro is worth keeping.
* Anything using `#[lang]`, `#[rustc_*]`, specialization,
  trait objects with `dyn Trait`, `Pin`, `Generators`,
  unstable nightly features.
* `unsafe` code that touches LLVM intrinsics directly — most must
  be re-derived against Logos's intrinsics.
* `no_std` formatting and float printing — we use libc snprintf
  (see [std/rt/fmt_native.c](rt/fmt_native.c)).

Where an import would require porting half a dozen rustc-internal
lang items first, write the stdlib piece from scratch instead.

## See also

* [../../tests/imported/README.md](../../tests/imported/README.md) — same
  policy applied to test files.
* [../../COPYRIGHT](../../COPYRIGHT) — repo-wide licensing.
* [../../NOTICE](../../NOTICE) — formal attribution preserved in derivative
  works.
