# tests/lattice/valgrind — the release oracle

Every other oracle in this tree reads an **exit code** or **stdout**:
`run_test.sh` compares `.expected`, `scripts/run_oracle.py` records rc + a
stdout digest, ctest matches text. **A LEAK IS INVISIBLE TO ALL OF THEM.** A
program that loses a block per iteration exits 0, prints exactly what it is
supposed to print, and is green forever.

That is what this directory is for.

## `sweep.sh [OUTDIR]`

Compiles, links and RUNS the whole pass corpus — `tests/logos/pass/**`,
`tests/imported/pass/**`, `tests/spec/pass/**` — under
`valgrind --leak-check=full`. Compile and link are native; only the run is
instrumented. Re-baselines in one command:

    LOGOSC=build/bin/logosc LOGOS_LIB_DIR=build/lib/logos \
        bash tests/lattice/valgrind/sweep.sh /some/outdir

Env: `LOGOSC`, `LOGOS_LIB_DIR`, `JOBS` (default `nproc`), `RUN_TIMEOUT`
(default 60 s per instrumented run).

Output:
  * `results.tsv` — one row per fixture:
    `path status allocs frees definite indirect reachable_blocks invalid rc`
  * `vg/<key>.vg` — the full valgrind stderr for every non-`OK` fixture, so a
    soundness-queue row can cite it.
  * `summary.txt` — the grid.

Statuses: `OK`, `LEAK`, `CORRUPT`, `LEAK+CORRUPT`, `IMBALANCE`, `CFAIL`,
`LINKFAIL`, `TIMEOUT`, `NOVG`.

### ⚠ SWEPT AND ALLOCATED ARE DIFFERENT NUMBERS

**A clean valgrind on a program that never called `malloc` is COVERAGE, NOT
EVIDENCE.** Most of this corpus allocates nothing — `hello.logos` and
`iter_terminals` report `0 allocs, 0 frees`. A sweep that reports "6527 clean"
while five thousand of them never reached the allocator has measured nothing,
so `summary.txt` prints `allocated` and `zero-alloc` separately and the clean
count is stated **of the allocating ones**.

### ⚠ `still reachable` IS NOT A LEAK

A block a pointer still reaches at exit was not lost. It is recorded and never
rowed. It is also why the balance claim is written

    allocs - frees == still-reachable blocks

rather than `allocs == frees`: a program that prints leaves the runtime's own
stdout state reachable, so the plain equality is false for every printing
fixture and true only by accident for the rest.

### Severity order for anything this finds

    invalid free / invalid read / invalid write   (corruption — one row each)
  > definitely lost                               (a leak; tier 1 where a destructor was owed)
  > indirectly lost                               (group with its owner)
  > allocs != frees with a clean leak summary     (what a destructor-count oracle sees
                                                   and leak-check may not)
    still reachable at exit                       — NOT a leak, not rowed

## `leak_gate.sh LOGOSC FIXTURE LIB_DIR [MIN_ALLOCS]`

One fixture, the same instrument, as a ctest gate. `MIN_ALLOCS` is the "the
program actually ran" floor — see the coverage-vs-evidence warning above.

Registered in `tests/logos/CMakeLists.txt` as
`logos_09_blockexpr_scope_drop_valgrind` and
`logos_09_blockexpr_scope_drop_format_ctl_valgrind`.

`tests/logos/cond_move_field_valgrind_gate.sh` is the older sibling and is
*not* superseded: it asserts the strict `allocs == frees` that is exact for a
non-printing fixture, and its three registrations rely on it.
