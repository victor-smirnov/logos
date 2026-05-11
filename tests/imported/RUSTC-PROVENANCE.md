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
| B1 | `4b0c9d76ae7d387229caea55cfa73c280b08b8a7` | 2026-05-11 | Victor Smirnov | parser top-level (11 tests) |
| B2 | `4b0c9d76ae7d387229caea55cfa73c280b08b8a7` | 2026-05-11 | Victor Smirnov | borrowck — move/copy/reference (11 tests) |

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
| `pass/parser/as-precedence.logos` | `tests/ui/parser/as-precedence.rs` | B1 | suffixed integer literals; ports as-is via `assert_eq!` |
| `pass/parser/doc-comment-parsing.logos` | `tests/ui/parser/doc-comment-parsing.rs` | B1 | `pub fn main()` → `fn main() -> i32`; bare `5;` → `let _: i64 = 5;` |
| `pass/parser/generics-rangle-eq-15043.logos` | `tests/ui/parser/generics-rangle-eq-15043.rs` | B1 | tuple-struct `S<T>(T)` → named-field `S<T> { v: T }` (Logos has no tuple structs) |
| `pass/parser/integer-literal-method-call-underscore.logos` | `tests/ui/parser/integer-literal-method-call-underscore.rs` | B1 | trait method signature carries explicit `self: Self` |
| `pass/parser/multiline-comments-basic.logos` | `tests/ui/parser/multiline-comments-basic.rs` | B1 | `pub fn main()` → `fn main() -> i32` |
| `pass/parser/nested-block-comments.logos` | `tests/ui/parser/nested-block-comments.rs` | B1 | `pub fn main()` → `fn main() -> i32` |
| `pass/parser/operator-associativity.logos` | `tests/ui/parser/operator-associativity.rs` | B1 | suffixed literals; uses `assert_eq!` |
| `pass/parser/operator-precedence-braces-exprs.logos` | `tests/ui/parser/operator-precedence-braces-exprs.rs` | B1 | suffixed literals; relies on Logos block-as-expression |
| `pass/parser/parse-panic.logos` | `tests/ui/parser/parse-panic.rs` | B1 | `panic!()` / `println!()` → `panic("")` / `let _ = 1;` (function is never called) |
| `pass/parser/reference-whitespace-parsing.logos` | `tests/ui/parser/reference-whitespace-parsing.rs` | B1 | trimmed to `&T` depth 1 (Logos `&&T` / whitespace-tolerant `&` stacking at type position is a tracked grammar gap, see `docs/track3-gaps/parser-gaps.md`) |
| `pass/parser/super-fast-paren-parsing.logos` | `tests/ui/parser/super-fast-paren-parsing.rs` | B1 | `static a: isize = (...)` → `const A: isize = (...)` |
| `pass/borrowck/borrowck-assign-to-subfield.logos` | `tests/ui/borrowck/borrowck-assign-to-subfield.rs` | B2 | nested struct decls hoisted to top level (Logos doesn't permit struct decls inside fn bodies) |
| `pass/borrowck/borrowck-borrow-of-mut-base-ptr-safe.logos` | `tests/ui/borrowck/borrowck-borrow-of-mut-base-ptr-safe.rs` | B2 | trimmed: `let t2: &&mut isize = &t0;` step crashes mlir-gen at runtime (`&&mut T` codegen partial) — tracked gap |
| `pass/borrowck/borrowck-closures-two-imm.logos` | `tests/ui/borrowck/borrowck-closures-two-imm.rs` | B2 | trimmed to fn `a()`: cases `b/c` use `&x` inside closure body (capture-by-ref / addr-of-captured-local) — mlir-gen "& undefined 'x'", tracked gap |
| `pass/borrowck/borrowck-fixed-length-vecs.logos` | `tests/ui/borrowck/borrowck-fixed-length-vecs.rs` | B2 | explicit array type `[i64; 1]` |
| `pass/borrowck/borrowck-mut-vec-as-imm-slice.logos` | `tests/ui/borrowck/borrowck-mut-vec-as-imm-slice.rs` | B2 | `&[isize]` slice arg → `&Vec<i64>` (no implicit Vec→slice coercion); for-loop unrolled to index loop (tracked gap on borrowed for-iter pattern) |
| `pass/borrowck/borrowck-pat-reassign-no-binding.logos` | `tests/ui/borrowck/borrowck-pat-reassign-no-binding.rs` | B2 | `Option::Some` / `Option::None` qualified |
| `pass/borrowck/borrowck-rvalues-mutable.logos` | `tests/ui/borrowck/borrowck-rvalues-mutable.rs` | B2 | explicit lifetime `<'a>` dropped from `inc` — Logos elides |
| `pass/borrowck/borrowck-scope-of-deref-issue-4666.logos` | `tests/ui/borrowck/borrowck-scope-of-deref-issue-4666.rs` | B2 | rust user-struct `Box` renamed `MyBox`; `fun1` (declare-then-init for immut binding) dropped — grammar gap |
| `pass/borrowck/lazy-init.logos` | `tests/ui/borrowck/lazy-init.rs` | B2 | declare-without-init `let mut x: isize;` not accepted — pre-init to 0 (grammar gap tracked) |
| `pass/borrowck/pointer-reassignment-after-deref-78192.logos` | `tests/ui/borrowck/pointer-reassignment-after-deref-78192.rs` | B2 | explicit `as *const u32` cast on `c = d` (no implicit `&T → *const T` coercion) |
| `pass/borrowck/two-phase-baseline.logos` | `tests/ui/borrowck/two-phase-baseline.rs` | B2 | `Vec` via `vec_new::<i64>` + `push`; `assert_eq!` per-element since Logos `Vec` doesn't implement equality vs array literal |

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
