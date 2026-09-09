# RESULT — 2026-09-09j-capret

Full record: the `2026-09-09j-capret` entry in src/compiler/PROBES.md.
Prediction (and the pre-build correction that changed the predicate): PREDICTION.md.

LANDED. bc_admits.ledger `# TOTAL` 92 -> 91; `issue-40510-1` deleted and its
program moved to tests/imported/fail/nll/ with its sentence pinned in full.
soundness_queue `# TOTAL` 76 -> 77; one tier-4 `diag` row OPENED.

closed set, diffed both ways:  predicted {issue-40510-1} = measured {issue-40510-1}
cost:  0 damaged in every column; ONE run_oracle row moved and in the REPAIR
       direction (an over-refusal of legal Rust, cc 1 -> cc 0, runs, exit 0).
