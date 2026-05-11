# stdlib Import Provenance

This file is the **authoritative manifest** for every stdlib file —
or fragment of one — adapted from
[rust-lang/rust](https://github.com/rust-lang/rust). For policy and
workflow see [README.md](README.md). For the parallel test-
suite manifest see
[../../tests/imported/RUSTC-PROVENANCE.md](../../tests/imported/RUSTC-PROVENANCE.md).

When the per-file table is empty no stdlib code has been imported
yet — the policy is in place for the first batch.

## Pinned upstream commits

Each batch pins a single rustc commit. Multiple stdlib imports done
from the same upstream snapshot share a batch SHA; new batches
appear when we re-pin to a fresher commit.

| Batch | rustc commit (SHA) | Date | Imported by | Scope |
|---|---|---|---|---|
| _none yet_ |  |  |  |  |

To kick off a batch:

1. Pick a stable rustc commit (e.g. a tagged release).
2. Add a row above with SHA, date, importer's git `user.name`, and
   a one-line scope summary (e.g. "collections::btree backbone").
3. Land all stdlib files in that batch in one commit or a small
   commit series.

## Per-file manifest

Columns:

* **Our path** — path under `stdlib/` (in-place module location).
* **rustc path** — original path under `library/` in rust-lang/rust.
* **Commit** — batch SHA from the table above.
* **Scope** — full file / partial / mixed (with markers).
* **Modifications** — one-line summary matching the per-file header.

| Our path | rustc path | Commit | Scope | Modifications |
|---|---|---|---|---|
| _no imports yet_ |  |  |  |  |

## Reconciliation with upstream

The manifest pins to a commit, not to `master`. When rust-lang/rust
later changes an imported file, our copy diverges silently. A
periodic reconciliation sweep (TBD tooling):

1. Diff each imported file against the current upstream version.
2. For each non-trivial drift, decide:
   * **Re-import** the new version (bump the batch SHA, replace
     content, refresh per-file header).
   * **Keep** our adaptation (Rust changed something inapplicable
     to Logos — record reasoning in modification line).
   * **Retire** if upstream removed the code path.

A re-import requires re-doing the Logos porting steps; the manifest
serves as a checklist of what needs review on each upstream bump.
