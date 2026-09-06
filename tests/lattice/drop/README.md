# The ownership-and-drop lattice

Re-baseline in one command from the repo root:

    python3 tests/lattice/drop/gen.py  /tmp/droplat/progs
    bash    tests/lattice/drop/run.sh  /tmp/droplat/progs /tmp/droplat/out > /tmp/droplat/res.txt
    python3 tests/lattice/drop/score.py /tmp/droplat/res.txt

`run.sh` saturates the box (`xargs -P $(nproc)`); do not run it beside a build or a
ctest level. 459 cells take ~35 s. `LOGOSC` and `LOGOS_LIB` override the compiler and
the archive directory; `LOGOS_ROOT` overrides the tree.

`cells.json` carries, per cell: its id, the axes' values, `expect_count`,
`expect_value`, and `why` — the reason that number is Rust's answer.

THE ORACLE IS A DESTRUCTOR COUNT, NOT AN EXIT CODE. Every owner carries a distinct
power-of-ten weight and a raw `*mut i64`; its destructor ADDS its weight, so the final
number is a signature naming exactly which owners ran. Block D and the order cells use
a second counter whose destructor does `*c = *c * 10 + v`, so the number is the
SEQUENCE. Every cell also reads a value out of the construct into a variable declared
OUTSIDE it and prints it, so a cell whose bindings are entirely absent cannot pass.

A LATTICE DECAYS. Do not carry a result forward: re-run it.
