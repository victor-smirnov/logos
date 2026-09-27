# `tests/interactions` — feature-interaction audit corpus (2026-09-26)

Evidence for `docs/audit/2026-09-26-feature-interactions.md`. NOT a test suite yet:
nothing globs this directory. The files become acceptance fixtures for the
ADR 0030 migration step named per cluster in the report (§3); a step moves the
fixtures it turns green into `tests/logos/{pass,fail}`.

- `clusters.json` — 250 clusters: key, suspected mechanism, member finding ids
  (`<probe unit>#<n>`), representatives that were independently re-run.
- `clusters/<key>/<unit>_<n>.logos|.rs` — the probe program of every member
  finding and its rustc twin (Logos half missing for 3 findings whose files
  were lost before collection).
- `min/<dir>__<file>` — the minimized repros the verifiers produced, as cited
  in the report.

Binary: `logosc 0.47.0-preview+main-g3df07251`; oracle rustc 1.98.1
(`--edition 2024`). Members inherit their representative's verdict.
