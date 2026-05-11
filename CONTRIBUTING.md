# Contributing to Logos Lang

Thanks for your interest in contributing. Logos Lang welcomes
issues, discussion, and patches. This file describes how to send
patches, the licence terms your contributions are under, and the
sign-off mechanism we use to track provenance.

## Licence

Logos Lang is dual-licensed under Apache 2.0 and MIT — see
[COPYRIGHT](COPYRIGHT). By submitting a contribution intentionally
for inclusion in the project, you agree that your contribution may
be distributed under either licence at the user's option.

## Developer Certificate of Origin

All commits must be signed off under the [Developer Certificate of
Origin](DCO) (version 1.1). The DCO is a lightweight, well-known
statement that you have the right to submit the work and that you
agree it goes out under the project's licence. It is **not** a
copyright-assignment CLA — you keep your copyright.

To sign off on a commit, append a line like

```
Signed-off-by: Your Real Name <your.email@example.com>
```

to the commit message. The easiest way is to pass `-s` to `git`:

```bash
git commit -s -m "fix: handle empty input"
```

This adds the trailing line automatically using your configured
`user.name` and `user.email`. Use the real name you'd put on legal
correspondence — pseudonyms aren't accepted (the DCO needs to be a
truthful certification).

Patches without a `Signed-off-by:` will be asked for one before merge.

If you don't already have an OSS-friendly Git identity set up:

```bash
git config --global user.name  "Your Real Name"
git config --global user.email "your.email@example.com"
```

## How to send patches

* **Issues**: open a GitHub issue describing the bug or proposal
  before sending a large change.
* **Pull requests**: small, focused commits with clear messages.
  Group unrelated changes into separate PRs. Run the full test
  suite locally (`cd build && ctest -j`) before opening.
* **Commit messages**: present-tense imperative summary line ("fix
  X"), blank line, then optional explanation. Keep summary under
  72 characters where reasonable.
* **Tests**: every functional change ships with a regression test
  under `tests/logos/{pass,fail}/`. The bar is "if I revert your
  fix, this test fails".

## Code style

* Use existing patterns in the directory you're editing. The
  codebase is intentionally not uniform across languages
  (C++ for compiler, Logos for stdlib, etc.); follow the
  conventions of the file you're in.
* No file headers / license boilerplate. Per-file SPDX identifiers
  and copyright lines were dropped project-wide; copyright is
  automatic (Berne Convention), licence terms are covered by the
  root [LICENSE-APACHE](LICENSE-APACHE) / [LICENSE-MIT](LICENSE-MIT) /
  [COPYRIGHT](COPYRIGHT) files, and authorship lives in the git
  history plus [AUTHORS.md](AUTHORS.md). Don't add file headers in
  new contributions.

  **Exception — imported tests and stdlib:** files under
  [tests/imported/](tests/imported/) and
  [stdlib/imported/](stdlib/imported/) DO start with a provenance
  comment block citing the upstream source, commit, and a one-line
  summary of modifications. This is required by Apache 2.0 § 4(b)
  for derivative works and is the only allowed per-file attribution.
  See [tests/imported/README.md](tests/imported/README.md) and
  [stdlib/imported/README.md](stdlib/imported/README.md) for the
  exact format and per-tree workflow.

## Trademarks

The Logos Lang name and project marks are subject to a separate
[trademark policy](TRADEMARKS.md). Code contributions don't affect
mark use — most descriptive references and project naming are
permitted without permission.

## Reporting security issues

Security-sensitive bug reports: please open a private channel
rather than a public issue. Until a formal security contact is
established, file as a private GitHub Security Advisory on the
repository.
