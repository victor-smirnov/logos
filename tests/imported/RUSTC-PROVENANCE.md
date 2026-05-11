# rustc Test Import Provenance

This file is the **authoritative manifest** for every test under
[tests/imported/ui/](ui/) — the upstream commit it was sourced from,
the original path in rust-lang/rust, and a one-line summary of any
adaptations made.

When this file is empty (no rows in the manifest table), no tests
have been imported yet — the infrastructure is in place for the
first batch.

## Pinned upstream commit

Imports happen in **batches**; each batch pins a single rustc commit
that all files in that batch were sourced from. When a new batch
starts, a new commit row appears here and is referenced from the
per-file rows below.

| Batch | rustc commit (SHA) | Date | Imported by | Scope |
|---|---|---|---|---|
| B1 (pin only — actual imports land in later batches) | `4b0c9d76ae7d387229caea55cfa73c280b08b8a7` | 2026-05-10 | Victor Smirnov | parser top-level (planned) |

To kick off a batch:

1. Pick a stable, recent rustc commit
   (`git -C ~/src/rust rev-parse HEAD` on a fresh `master` clone, or
   a tagged release like `1.83.0`).
2. Add a row above with the SHA, date, and importer's git
   `user.name`.
3. Land all files in that batch in one commit (or a small commit
   series); each per-file row below references the SHA.

## Per-file manifest

Columns:

* **Our path** — path under `tests/imported/ui/`.
* **rustc path** — original path under `tests/ui/` in rust-lang/rust.
* **Commit** — the batch SHA from the table above (or `(direct)` if
  the import was a one-off outside a numbered batch).
* **Modifications** — one-line summary; matches the per-file
  provenance header in the test itself.

| Our path | rustc path | Commit | Modifications |
|---|---|---|---|
| _no imports yet_ |  |  |  |

## When upstream changes

The manifest pins to a commit, not to `master`. If rust-lang/rust
later changes a test we imported, our copy diverges silently. A
periodic reconciliation sweep:

1. Run a diff tool (TBD) comparing each imported file against the
   current upstream version at the same path.
2. For each non-trivial drift, decide:
   * **Re-import** the new version (replace, bump the manifest row's
     commit).
   * **Keep** our version (rustc tightened a Rust-specific check
     that doesn't apply to Logos).
   * **Retire** our copy if upstream removed the test.

This file is the source of truth for what came from where; keep it
honest.
