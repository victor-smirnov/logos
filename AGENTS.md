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

**Run tests with `scripts/lt`, not `ctest`.** The suite is 11,000+ tests;
`lt` runs the part a task touches first. Describe the task's direct coverage
once, then run its levels:

    scripts/lt task new NAME TEST... /regex/...   # L0: the task's own tests
    scripts/lt run --level 0                      # L0
    scripts/lt run --level 1 --upto               # L0, then L1 (the groups of L0)
    scripts/lt run --level 2 --upto               # everything, stopping at a red level

L0, L1 and L2 are disjoint and together are the whole suite. Groups live in
`tests/groups.rules`. Every result, with its duration and output, is kept in
`build/testdb.sqlite`: `scripts/lt show TEST --output`, `scripts/lt last --failed`,
`scripts/lt run --failed`. `ctest` and `tests/logos/test-levels.sh` are
deprecated (CMake still describes the tests; `lt` imports and runs them).

**Work of one kind goes in a series** (e.g. ten borrow-checker defects from the
tracker). A series is a branch; while it is open, each task runs only its L0
and a rotating 10% of everything else; the whole suite runs when the series
closes, and the series merges only from a green whole-suite run of the commit
being merged:

    scripts/lt series new NAME [--pct 10]   # branch series/NAME off main
    scripts/lt task new TASK TEST...        # per task; joins the series
    scripts/lt run --plus                   # L0 + 10% of L1+L2; commit when green
    scripts/lt series close                 # build + whole suite; reds -> task NAME-fixN
    scripts/lt run --plus                   # fix NAME-fixN the same way, commit, close again
    scripts/lt series merge                 # into main, once a close of this HEAD is green

`close` needs a clean tree with main already merged into the branch, so what
merges is what was tested. A repeated `close` reruns the previous reds first
and stops if any is still red. A red the series did not cause is waived by
name with a reason (`lt series waive TEST --why ...`), and the merge commit
lists it. On a series branch `lt run --all` and `--level 2` refuse.

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
