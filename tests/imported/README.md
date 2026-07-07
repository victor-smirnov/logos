# Imported Tests

This directory tree holds test files **adapted from external open-
source projects**, primarily the [Rust compiler test suite
(`rust-lang/rust`)](https://github.com/rust-lang/rust). Logos Lang is
syntactically Rust-adjacent; large parts of Rust's `tests/ui/` cover
language constructs we share, so adopting them gives us broad
coverage cheaply and keeps us honest about Rust-parity intent.

## Provenance and licensing

Adapted files retain their original copyright. Imported test files
are dual-licensed under Apache 2.0 and MIT — the same licences Logos
Lang itself uses (see [COPYRIGHT](../../COPYRIGHT) at the repo root).
Compatibility is exact; no additional licence terms apply.

* Source list with original paths and the pinned upstream commit:
  [RUSTC-PROVENANCE.md](RUSTC-PROVENANCE.md).
* Attribution and the Rust Project Developers copyright notice:
  [RUST-COPYRIGHT.md](RUST-COPYRIGHT.md).
* Project-wide attribution string preserved in derivative works:
  [NOTICE](../../NOTICE) at the repo root.

## Per-file provenance header

Every imported test file MUST start with a provenance comment block.
This is the **only** exception to the project-wide "no per-file
headers" policy (per [CONTRIBUTING.md](../../CONTRIBUTING.md)) — it
is required by Apache 2.0 § 4(b) ("carry prominent notices stating
that You changed the files") and by basic ethical attribution.

Format:

```logos
// Imported from rust-lang/rust@<COMMIT-SHA>
// Original path: tests/ui/<original-path>.rs
// Modifications: <one-line summary of porting changes>
```

The `<COMMIT-SHA>` must match the pinned commit in
[RUSTC-PROVENANCE.md](RUSTC-PROVENANCE.md); imports from different
upstream commits should land in separate batches with the manifest
updated accordingly.

## Layout

```
tests/imported/
├── README.md                  — this file
├── RUSTC-PROVENANCE.md        — pinned commit + per-file manifest
├── RUST-COPYRIGHT.md          — attribution / licence pointer
└── ui/                        — imported test categories
    ├── parser/
    ├── traits/
    ├── ownership/
    ├── macros/
    └── ...
```

Sub-directory structure under `ui/` mirrors the rustc layout where
practical, so cross-referencing upstream is straightforward.

## What we do NOT import

* `tests/ui/async/` — Logos's concurrency model is fiber-based, no
  async colouring; the tests don't translate.
* `tests/ui/nightly-features/` — by definition not Rust 1.0
  language; we only port stabilised behaviour.
* Tests gated on target-specific or compiler-version flags.
* Tests that exercise `macro_rules!` — Logos uses `#[fn_macro]` /
  `#[token_macro]` (see [docs/spec/metaprogramming.md](../../docs/spec/metaprogramming.md));
  feature parity tracked in other suites.

## Workflow for adding a batch

1. Decide a category (e.g. `traits/object-safety`).
2. Pin the rustc commit if a fresh batch (update RUSTC-PROVENANCE.md).
3. For each file:
   * Copy the `.rs` source into the corresponding `ui/<category>/`
     directory.
   * Rename to `.logos`.
   * Add the provenance header block (see above).
   * Mechanical port: `use std::` → `use std.`, `fn main() -> ()` →
     `fn main() -> i32`, etc. Where mechanical port fails, file an
     issue + skip the test for now.
   * Add a `.expected` file matching our test-runner format.
4. Append a row to the manifest table in RUSTC-PROVENANCE.md.
5. Run `ctest` — green expected.

When upstream rustc changes a ported test, our copy diverges
silently (our manifest pins the old commit). A periodic sweep with
the upstream-diff tool (TBD) reconciles drift.
