# Working on Logos

This file is the entry point for anyone — human or automated agent — about to make
a change here. It is deliberately short and vendor-neutral: it says what you must
know before the first command, and points at the documents that hold the detail.

## Before the first command

**The compiler must be clang 20.** The generated parser does not build with GCC,
and CMake picks `c++` (usually GCC) unless told otherwise. Everything else follows
from [README.md](README.md), whose commands are verified end to end:

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_CXX_COMPILER=clang++-20
cmake --build build
```

**`logosc` emits an object file, not an executable.** Compile, then link against
the stdlib archives, then run — the exact line is in the README and in
`tests/logos/run_test.sh`. Running `logosc`'s output directly gives exit 126;
that is the shell refusing a relocatable, not your program failing.

**Do not run the bare full suite to check yourself.** It is 11,000+ tests and
`ctest` defaults to one job. Use the tiers from `build/`:
`../tests/logos/test-levels.sh L1` (one test per group) or `L2` (ten per group).
`L4` is the whole suite and requires `LOGOS_L4_BG=1` — without it the harness
refuses and reports zero tests run, which reads like a result but is not one.

## Where work comes from

Open work lives in **GitHub issues**, not in files. Labels carry the structure:

- `ledger:squeue` / `ledger:bc-admits` / `ledger:backlog` — which counted list a
  row belongs to. The label is the filter; there is no separate place to look.
- `tier:1`…`tier:4` and `sev:*` — the measured tier and the measured effect.
- `needs-measurement` — **not a task yet.** It names the one measurement that
  turns it into a row or discards it. It may turn out to be zero defects.
- `group:atomic` — a parent whose children are the same bug seen more than once.
  **Take the whole group**; closing one member alone leaves the defect standing.
- `group:member` — a sub-issue of such a group. Do not pick it on its own; issue
  listings are flat, so a member looks like an independent task and is not one.

## The ledgers under `tests/logos/` are generated

`soundness_queue.ledger`, `bc_admits.ledger` and `unrowed_backlog.ledger` are
**generated from the issues** by `scripts/issues-to-ledger.py` and committed like a
lockfile. They stay in the tree because `tests/logos/CMakeLists.txt` reads them at
configure time to generate per-row tests, and several gates read them offline.

Do not edit them by hand. Each carries a `# SYNC-HASH` over its row region and the
`logos_00_ledger_sync` gate recomputes it; a hand edit turns the build red. Change
the issue instead, then regenerate:

```bash
python3 scripts/issues-to-ledger.py --list <backlog|bc-admits|squeue> --check   # drift?
python3 scripts/issues-to-ledger.py --list <backlog|bc-admits|squeue> --write   # pull
```

Closing an issue **deletes a counted row**, so the puller refuses a removal that no
commit accounts for. That refusal is the point; do not force past it casually.

## Rules that are not negotiable

- **Never weaken a test, a gate, a fixture or a pinned number to make something
  green.** If a gate is red, the gate is the measurement and the change is the
  hypothesis.
- **A gate's exit code is a measurement with a timestamp.** Do not restate a
  verdict you did not just run, and do not quote one tier's result as another's.
- **Ship a regression test with every functional change.** The bar is: revert the
  fix and the test fails.
- **Sign off your commits** (`git commit -s`) — see [CONTRIBUTING.md](CONTRIBUTING.md)
  for the DCO, licensing and patch workflow.

## Before opening a PR

Walk [docs/dev-checklist.md](docs/dev-checklist.md). It exists because a systematic
bug hunt found ~122 defects falling into ~12 recurring patterns, and each item
prevents one of them.
