# `tests/exhaustive` — the enumerator

**Status: STILL RED, and every red case left is ONE root — see the ledger below.
Not wired into `ctest`: a gate that is red is not a gate.**

## The ledger — what it found, what closed, what is left

Measured on the same 246 programs / 13 508 cases at each point. The "before"
column is the run that produced this directory; the "after" column is HEAD.

| family  | cases  | before | after | what closed |
|---|---|---|---|---|
| cast    | 2 048  | 0   | 0   | (control) |
| cmp     | 3 456  | 0   | 0   | (control) |
| arith   | 4 320  | 216 | 0   | a shift's result type is its LEFT operand |
| pattern | 1 216  | 211 | 112 | the range predicate + the unsigned bounds |
| deem    | 2 466  | 686 | 162 | the value's byte size; float total order; `!=`; the f32 literal; the 24-bit lattice rows |
| poison  | 2      | 1   | 0   | a subnormal float literal no longer kills the compiler |

**Silent wrong answers: 484 → 0.** Every remaining finding is a COMPILE
REFUSAL, and all 274 of them are the same root:

> **An integer literal's value is carried in 64 SIGNED bits.**
> `parse_int_literal` returns the raw 64-bit pattern for a magnitude above
> `INT64_MAX`, and the readers that treat that pattern as a signed value refuse
> what they should accept. What is left:
>
> * **deem, 162** — a `where` literal above `i64::MAX` over a `u64` / `usize` /
>   `u128` column. The WQL surface tokenizer POISONS the token and `codegen`
>   reports "integer literal out of range — exceeds i64::MAX". The value is
>   carried in a 64-bit signed `WAny` from the token to the emitter.
> * **pattern, 88 + 24** — an `i128` / `u128` range-pattern bound. The L-IR
>   mirror stores a pattern's bounds as `int64` (`PatRangeView::lo()`), so a
>   bound outside 64 bits is truncated: 88 of these are refusals and 24 are
>   WRONG ANSWERS (the truncated bound still compiles and tests the wrong
>   range). These 24 are the only wrong answers left in the whole product.
>
> Fixing it is one arc — widen the literal's value end-to-end — not five
> patches. It is reported rather than half-done.

One more finding is recorded in
`tests/logos/pass/range_pattern_predicate_is_the_scrutinee_type.logos` rather
than here, because it is outside the axes: an unsuffixed literal above
`INT64_MAX` passed to a `usize` PARAMETER is rejected while the same literal to
a `u64` parameter is accepted — `intlit_fits` reads the identical 64-bit
pattern as `v >= 0` for `Usize` and as `true` for `U64`.

## What the six defects it found were, in one line each

1. `mlir::DataLayout` on a module with no `dlti.dl_spec` gives i64 an ABI
   ALIGNMENT of 4, so `{i32,i64}` sized 12 and every value copy of such a
   struct dropped four bytes — a struct whose first field is narrower than 64
   bits returned addresses instead of ids.
2. The odd widths were written at their STORE size (i24 = 3, i56 = 7) while the
   emitted GEP steps their ALLOC size — `Vec<i56>::push` overran the heap.
3. `let <range> = x else {…}` emitted a SIGNED range test for an unsigned
   scrutinee; `pat_test` did the same for u24/u56/u128.
4. `order by <f64>` sorted with a PARTIAL comparator, so one NaN left every row
   ahead of it in collection order.
5. `!=` on floats was ORDERED, so `NaN != x` was false and a `where k != v`
   filter dropped every NaN row.
6. A subnormal float literal terminated the compiler through an uncaught
   `std::out_of_range` from `std::stod`.

None of the six was visible to `tests/logos`. Two of them had a fixture that
CLAIMED the axis: `wql_join_order_key_fidelity_e2e` checked `order by <f64>`
against a sort written beside it with the same partial comparator, and pinned
the unsorted answer as its expectation.

## Why it is here and not in `tests/logos`

`tests/logos/{pass,fail}` holds AUTHORED fixtures: a person chose the case and
wrote the expectation. This directory holds a GENERATOR: nobody chose the cases,
and no expectation is written by hand — the product of the axes is enumerated and
every answer is checked against `model.py`. The two are different kinds of
artifact with different failure modes, so they get different directories. The
generator's output is never checked in: it is regenerated from the axes, and a
generated file that could be edited would stop being a spec.

## The rule that makes it worth running

> The expected answer must be computed by something that does not share code,
> algorithm or assumption with what it checks.

`tests/logos/pass/wql_join_order_key_fidelity_e2e.logos` was the counter-example
this exists to replace: it checked `order by <f64>` against `stable_sort_by_f64`,
a sort written in the same file with the same comparator the emitter used. The
oracle reproduced the defect, so the assertion compared the implementation with
itself and passed while the answer was wrong — and its pinned sequence recorded
the unsorted answer as the specification. That fixture's oracle now compares
through `f64_total_key` and its pin is re-derived by hand; the NaN axis itself
moved to `wql_order_by_float_is_a_total_order_e2e`, whose expected sequences are
written-out constants. Here the oracle is Python's exact integer arithmetic and
the IEEE semantics of `struct` — see `model.py`, which states everything it
trusts.

Where an independent VALUE oracle is impractical, a PROPERTY is checked instead,
never a self-comparison. `_order_check` is the example: it asserts that the
returned ids are a permutation of the input and that the key sequence READ OUT OF
THE ANSWER is monotone — it never sorts anything.

## Layout

| file | what it is |
|---|---|
| `model.py` | the oracle: type lattice, boundary values, exact arithmetic, and the POISON list (values excluded from shared corpora, each naming the finding that forced it) |
| `emit.py`  | the render bridge — how each Logos type's value reaches Python, and what trust each channel costs |
| `harness.py` | the families, the runner, the bisector, the CLI |

## Axes

* **TYPE** — every integer width and signedness the language has, including this
  project's odd ones (`i24 u24 i56 u56`) and `i128 u128 isize usize`; `f32 f64
  bool str`.
* **VALUE** — boundaries only, never round numbers: `0 1 -1 MIN MIN+1 MAX MAX-1`,
  the signed ceiling `2^(b-1)-1 / 2^(b-1) / 2^(b-1)+1` for unsigned widths;
  floats get `NaN ±Inf ±0.0` and the subnormal edge; `str` gets empty, one byte,
  multi-byte UTF-8, embedded quote, embedded backslash, and a long value.
* **OPERATION** — `< <= > >= == !=` in both operand orders, `+ - * / % & | ^ <<
  >>`, and casts between every ordered pair of the 16 integer types.
* **QUERY SHAPE** — `select`, `where` with each relational operator, `order by`
  asc and desc, `limit`, `select first`, an equi-join, an anti join, a projection.
* **CONTEXT SHAPE** — the FIELD POSITION of the payload in the row struct (first,
  between two wide fields, last) and the presence of unrelated locals in the
  CALLER. This axis is not decoration: the defect that motivated it is invisible
  without it.
* **EMISSION SITE** — a range pattern is compiled at `match`, `if let`,
  `let … else` and `while let`; each is its own site and each is enumerated.

## Size, honestly

    $ python3 tests/exhaustive/harness.py --list --all
    246 programs, 13508 cases

A full run is ~9 minutes wall at `--jobs 12` — dominated by ~1.7 s of `logosc`
per program, not by the cases. **Nothing is sampled at run time.** The only
sampling in the harness is inside the generator and it is DECLARED:
`model.small_values_of` returns a fixed 6-value subset for the two quadratic
families (`cmp`, `arith`, which are all-pairs), and its rule is written next to
it — both ends of the type plus the sign-reinterpretation boundary, never a
midpoint, never random. Everything else is the full product.

Nine minutes is too long for the per-commit loop and short enough for a nightly
or a pre-merge gate. It is deliberately NOT wired into `ctest` yet: wiring a red
gate in is the thing this run is not allowed to do. At HEAD it runs in ~170 s
(the compile failures are cheap), and it stays red until the 64-bit-literal arc
lands — at which point wiring it in is one CMake entry and no new judgement.

## Running it

    python3 tests/exhaustive/harness.py --list --all
    python3 tests/exhaustive/harness.py --all --jobs 12 --json findings.json
    python3 tests/exhaustive/harness.py --family deem --only deem_u32_p0 --jobs 4 \
        --workdir /tmp/x --json /tmp/x/f.json

`--workdir` keeps the generated `.logos`, `.o` and binaries, so any finding has a
standalone reproducer on disk. Exit status is non-zero when there is any finding.

## Attribution

Cases are batched (a compile costs 1.7 s, a case costs nothing). A program that
fails to compile, or that dies before printing its last line, is **rebuilt from
half its case list** and re-run, recursively, down to the single case — so
batching buys speed without costing attribution. The text is never cut; the
generator is re-invoked with fewer cases.

## Reporting protocol

A generated program prints `#<case-id>|<value> …` per case and `DONE <n>` last.
Missing `DONE` ⇒ crash. Missing case line ⇒ `NO_ANSWER`. Row-valued answers print
their own LENGTH first and the harness reads it from the answer, so a short or
long result is a measured disagreement rather than a parse error.
