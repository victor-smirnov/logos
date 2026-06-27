# Core Port — Re-export Shim Policy

When a package is renamed (e.g. `std.lang.option` → `std.option`) during the core port migration, existing consumers must continue to compile during the transition. This document defines the shim policy.

## Mechanism

Logos grammar supports `pub use <pkg-path>;` for package-level re-export. After a rename, the old package path keeps a single-line shim file:

```logos
// Old path: std.lang.option — kept as a shim during core port migration.
// Remove this file when all consumers migrate to std.option.
package std.lang.option;

pub use std.option;
```

Consumers writing `use std.lang.option;` continue to compile; they see the same set of public symbols, now sourced through the shim from the renamed package.

## When to use a shim

**ALWAYS** for these widely-used types and modules:

- `Option`, `Result` — used everywhere
- `Vec`, `String`, `Box`, `Rc`, `Arc` — basic value types
- `HashMap`, `HashSet`, `BTreeMap` — collections
- `Clone`, `PartialEq`, `Eq`, `PartialOrd`, `Ord`, `Hash`, `Default` — derivable traits
- `Iterator` — pervasive
- Anything imported by ≥ 10 files internally

**OPTIONAL** for narrow-use modules:

- Writ-internal sub-packages (only stdlib internals use them)
- Recently-added test-only packages
- Single-consumer modules

For narrow-use cases, atomic rename (no shim) is cleaner — one commit changes the package decl + all consumers in lockstep.

**NEVER** for Logos-only modules — they don't move, no shim needed.

## Lifecycle

1. **At rename**: shim file added, original file renamed/relocated with new `package` decl, manifest entry recorded.
2. **During Phase 4**: shim stays in place. Consumers gradually migrate `use std.lang.option` → `use std.option`.
3. **End of Phase 4**: explicit shim-removal pass. Each shim file deleted; any straggler consumers updated atomically.
4. **No shim survives the core port milestone.** A shim that lasts beyond Phase 4 is debt — it propagates the old name to new code and defeats the purpose of the rename.

## Tracking

Add a section to `docs/core-port/shim-status.md` (created when first shim lands) listing every active shim:

| Shim path | New target | Consumers remaining | Plan to remove |
|---|---|---|---|
| _(populated as shims are introduced)_ |

Each Phase 4 batch updates this table — removing shims with zero remaining consumers, decrementing counts as consumers migrate.

## What `pub use` does NOT cover

`pub use <pkg>;` re-exports the **whole package**. It does not:

- Selectively expose individual symbols (no `pub use foo::Bar` Rust-style).
- Rename symbols during re-export.
- Cross language-version boundaries (no compat-shim for compiler ABI changes).

For our purposes — bulk renaming of stdlib packages with stable symbol sets — package-level re-export is sufficient.

## Edge cases

**MERGE cases** (multiple old packages → one new package, e.g. `std.lang.range + std.lang.arith + std.lang.drop → std.ops`):

The new target package contains the merged symbol set. Each old path gets its own shim:

```logos
// stdlib/.../old-range-path.logos
package std.lang.range;
pub use std.ops;
```

The shim re-exports more than the old package originally contained, but that's harmless — old consumers of `std.lang.range` see what they always saw, plus extra symbols they don't reference.

**Split cases** (one old package → multiple new packages, e.g. `std.lang.text → std.str + std.alloc.string`):

A single shim cannot cover this if both target packages have non-overlapping symbol sets. Either:
- Pick a primary target (e.g. `std.alloc.string`) for the shim, document the secondary path separately.
- Mass-migrate consumers atomically in a single commit — no shim.

Recommend mass-migration for splits — it's cleaner than a half-shim.
