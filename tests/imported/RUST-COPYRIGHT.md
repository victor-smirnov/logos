# Rust Project Copyright (test imports)

Files under [tests/imported/ui/](ui/) are adapted from
[rust-lang/rust](https://github.com/rust-lang/rust) — the official
Rust compiler and test suite. The Rust project is dual-licensed under
Apache 2.0 and MIT.

## Attribution

Adapted files retain the Rust Project Developers copyright:

> Copyright (c) The Rust Project Developers

Modifications and additions made by Logos Lang contributors are
dual-licensed under the same Apache 2.0 / MIT pair (see
[COPYRIGHT](../../COPYRIGHT) at the repo root) and are recorded via
the Developer Certificate of Origin (see
[CONTRIBUTING.md](../../CONTRIBUTING.md)).

## Licence texts

The Apache 2.0 and MIT licence texts for the Rust project are
identical in body to ours:

* Apache 2.0: see [`LICENSE-APACHE`](../../LICENSE-APACHE) at the
  repo root. The Apache 2.0 file body is project-independent;
  attribution lives in [NOTICE](../../NOTICE) and in per-file
  provenance comments.
* MIT: see [`LICENSE-MIT`](../../LICENSE-MIT) at the repo root —
  same MIT template; the only project-specific element is the
  copyright line. For imported files the original Rust copyright
  line above applies; for our modifications the Logos Lang
  copyright line in [`LICENSE-MIT`](../../LICENSE-MIT) applies.

Both licences are simultaneously satisfied because both projects
chose the same dual-licence pair.

## Authoritative upstream files

The rust-lang/rust repository carries the authoritative copies of
its `COPYRIGHT`, `LICENSE-APACHE`, and `LICENSE-MIT` files:

* <https://github.com/rust-lang/rust/blob/master/COPYRIGHT>
* <https://github.com/rust-lang/rust/blob/master/LICENSE-APACHE>
* <https://github.com/rust-lang/rust/blob/master/LICENSE-MIT>

The commit-specific snapshot in effect for our most recent import is
recorded in [RUSTC-PROVENANCE.md](RUSTC-PROVENANCE.md).

## Modification notice (Apache 2.0 § 4(b))

Per Apache 2.0 § 4(b) requirement to "carry prominent notices stating
that You changed the files":

> Files under `tests/imported/ui/` are Rust-source tests adapted to
> Logos Lang syntax. Adaptations may include: import-path rewrites
> (`use std::` → `use std.`), entry-point signatures, stdlib API
> substitutions, and removal of Rust-only constructs. Each individual
> file carries a per-file provenance header summarising its specific
> modifications (see [README.md](README.md)).
