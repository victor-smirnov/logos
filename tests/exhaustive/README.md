# `tests/exhaustive` — the enumerator

**Status: a GATE. Two ctest tests, both green, both red the moment any of the
six defects it found comes back.**

## Why it exists

Six consecutive rounds on the Deem query compiler shipped with green gates, and
every one of them was later found to contain real defects — including silent
wrong answers that survived all six. The diagnosis was not "the tests were bad".
It was:

> **Coverage was measured against what the CORPUS contains, not against what the
> TYPE LATTICE and the QUERY SHAPES admit.**

`tests/logos/{pass,fail}` holds AUTHORED fixtures: a person chose the case and
wrote the expectation. This directory holds a GENERATOR: nobody chose the cases,
and no expectation is written by hand — the product of the axes is enumerated
and every answer is checked against `model.py`.

## The rule that makes it worth running

> The expected answer must be computed by something that does not share code,
> algorithm or assumption with what it checks.

`tests/logos/pass/wql_join_order_key_fidelity_e2e.logos` was the counter-example
this exists to replace. It checked `order by <f64>` against `stable_sort_by_f64`
— an insertion sort written in the same file, comparing with the same partial
`>` the emitter used. The oracle halted at the NaN in exactly the same place as
the implementation, so the assertion compared the implementation WITH ITSELF and
passed while the answer was wrong; its pinned 18-element sequence recorded the
unsorted answer as the specification.

That is not a hypothetical. **Measured**: with `key_ord_frag` reverted to the
partial comparator, the pre-fix fixture *as it shipped* still exits 0, while the
enumerator's smoke tier fails with

    NOT ascending at 1.0 > -inf; key sequence
      [-1.0, 0.0, -0.0, 1.0, -inf, -1.797…e+308, 0.5, 1.797…e+308, inf]

Where an independent VALUE oracle is impractical, a PROPERTY is checked instead,
never a self-comparison. `_order_check` is the example above: it asserts that
the returned ids are a permutation of the input and that the key sequence READ
OUT OF THE ANSWER is monotone. It never sorts anything, so it has nothing to
reproduce the defect with.

## The two tiers

| ctest test | programs | cases | time | runs at |
|---|---|---|---|---|
| `logos_26_exhaustive_smoke` | 105 | 12 684 | ~34 s | **every level** of `test-levels.sh` (L1/L2/L3), and L4 |
| `logos_26_exhaustive_full`  | 265 | 14 876 | ~153 s | L4 |

`smoke` is DECLARED, not sampled (`apply_tier` in `harness.py`):

* `cast`, `cmp`, `arith`, `pattern`, `layout`, `poison` **in full** — 85
  programs, 12 410 cases. They cost ~11 s together because each program batches thousands of
  cases behind ONE compile, so there is nothing to gain by cutting them.
* `deem` — a **diagonal**: one program per payload type, walking the nine
  (field-position × caller-context) combinations in order. All 20 types, all 3
  field positions and all 3 caller contexts appear, in 20 programs of 180.

**What `smoke` therefore does not run**, stated so nobody has to infer it: the
other 160 `deem` programs, i.e. the rest of the per-type field-position ×
caller-context product — 2 192 cases. Those run in `full` at L4. Nothing else
is dropped, and nothing anywhere is random.

The enumerator is not a corpus member, so the L1/L2/L3 samplers — which
enumerate `.logos` files — can never select it. `test-levels.sh` therefore names
the smoke test explicitly, ahead of the sampled corpus, and its failure is the
level's failure. `LOGOS_NO_EXHAUSTIVE=1` skips it, for BISECTING a corpus
failure, not for making a commit green.

## What each tier asserts

1. **The corpus digest matches** `corpus.<tier>.sha256`. Checked first: the
   numbers below are about a specific corpus, and a run against a drifted
   generator is a run about nothing.
2. **ZERO wrong answers** — absolutely, with no ledger and no tier exemption.
3. **The set of compile REFUSALS is EXACTLY `refusals.ledger`**, checked in
   three directions: an unlisted refusal is a new defect; a listed refusal that
   now compiles means the arc landed and the ledger is stale; a line naming no
   case at all asserts nothing. All three are red.

There is no way to make this gate green by adding a line and walking away.

## Reproducibility: the digest, not the corpus

The generated text is **not** checked in — it is regenerated from the axes, and
a generated file that could be edited would stop being a spec. What IS checked
in is a sha256 over `(program name, program source, case id, expected value)`
for every program in the tier. Changing an axis changes the digest, so growing
the corpus is a deliberate act with a diff:

    python3 tests/exhaustive/harness.py --all --tier smoke --digest > tests/exhaustive/corpus.smoke.sha256
    python3 tests/exhaustive/harness.py --all --tier full  --digest > tests/exhaustive/corpus.full.sha256

Observed at this commit: `smoke c30ce517…`, `full 36a89b16…`, byte-identical
across regenerations.

## Mutation proofs — the gate BITES, measured

Each: revert one fix, `cmake --build`, run `ctest -R logos_26_exhaustive_smoke`.
Every one exits **8** (ctest's failure code) and names the type, the operation
and the shape. The tree was restored and re-verified byte-identical after each.

Case ids below are quoted from the runs, not summarised from the axes.

| reverted | cases the gate named | findings |
|---|---|---|
| `attach_target_data_layout` (`8ba3c764`) | `deem.{i8,u8,i16,u16,i24,u24,i32,u32,i128}.{0,1,2}.{bare,padbefore,padafter}.{join,anti,where_*}` | 34–35 wrong answers¹ |
| `uns` in `emit_range_test` (`ac81ba99`) | `pat.u8.100.200.{100,150,200}` and `pat.u16.32718.32818.{…}`, each at all four of `{match,iflet,letelse,whilelet}` | 96 wrong answers |
| `key_ord_frag` (`519a181d`) | `deem.f64.2.padbefore.{order_asc,order_desc}`, `deem.f32.2.padafter.{order_asc,order_desc}` | 4 wrong answers |
| the 128-bit `PatRange` half | `pat.i128.{match,iflet,letelse,whilelet}` over `-2^126..=2^126-1` at scrutinees `-2^126, -1, 0, 1, 2^126-1` | 56 wrong answers |
| the array-literal element memcpy back to `mlir::DataLayout::getTypeSize` (this commit) | `layout.{i24,u24,i56,u56}.{adj,tail,nest,narrow}.{arrlit,slice,fill}.{id,n,m}` | 24 wrong answers |

¹ The layout defect reads adjacent memory, so how many cases manifest varies run
to run (34 and 35 on two consecutive runs). The gate's VERDICT does not vary.

⚠ THE LAST ROW IS THE POINT OF THE `layout` FAMILY. Under that mutation the
`iso` shape — the one the `deem` row struct has always had — reports **zero**
findings, and so does the `vec` path. Every one of the 24 is an `adj`, `tail`,
`nest` or `narrow` row read through `arrlit`, `slice` or `fill`, and every one
is an odd width. That is the residue `8ba3c764` left: a corpus of 13 508 cases
that never put two sub-64-bit fields next to each other could not see it, and it
was found by running a program by hand.

The ledger's three directions were proven the same way, without a rebuild: an
orphan line, a listed refusal that now compiles, and a real refusal removed from
the ledger — exit 1 each, each naming the offending case.

## The ledger — what is still open

**Zero wrong answers over 14 876 cases.** The 162 findings that remain are
COMPILE REFUSALS and they are all one root, written out in `refusals.ledger`:

> **An integer literal's value is carried in 64 SIGNED bits.** A `where <col>
> <op> <literal>` over an unsigned column with the literal above `i64::MAX`
> reaches `wstr_decode_i64` in the WQL surface tokenizer (duplicated in
> `el_parser.logos` and `trama_parser.logos`), which has nowhere to put it and
> POISONS the token to `i64::MIN`; `codegen.logos:985` and `check.logos:207`
> report "integer literal out of range — exceeds i64::MAX". 108 entries need a
> 64-bit UNSIGNED carriage (pivot 2^63, columns u64/usize/u128), the other 54 a
> 128-bit one (pivot 2^127, column u128). One arc, not two patches.

The refusal is LOUD — the program does not compile and the message names the
limit — which is why it is ledgerable at all. **A wrong answer never is.**

The pattern-side half of the same root CLOSED at this commit: a range-pattern
bound is 128 bits wide from `parse_int_literal_u128` through `PatRange`'s two
mirror halves to the `arith.constant` the backend materialises. That half had 88
refusals AND 24 wrong answers — and the wrong answers are exactly why it could
not have been ledgered instead.

## The ledger, historically

Measured on the same 265 programs / 14 876 cases at each point.

| family  | cases  | before | HEAD | what closed |
|---|---|---|---|---|
| cast    | 2 048  | 0   | 0   | (control) |
| cmp     | 3 456  | 0   | 0   | (control) |
| arith   | 4 320  | 216 | 0   | a shift's result type is its LEFT operand |
| pattern | 1 216  | 211 | 0   | the range predicate, the unsigned bounds, the 128-bit bound |
| deem    | 2 466  | 686 | 162 | the value's byte size; float total order; `!=`; the f32 literal; the 24-bit lattice rows |
| layout  | 1 368  | 24  | 0   | the array-literal / fill / slice element size is the BACKEND's, not `mlir::DataLayout`'s |
| poison  | 2      | 1   | 0   | a subnormal float literal no longer kills the compiler |

`cast` and `cmp` clean over 5 504 cases is the control that says the model and
the four render channels are right; without it, "0 findings" in a family would
be indistinguishable from a broken bridge.

## What the seven defects it found were, in one line each

1. `mlir::DataLayout` on a module with no `dlti.dl_spec` gives i64 an ABI
   ALIGNMENT of 4, so `{i32,i64}` sized 12 and every value copy of such a struct
   dropped four bytes — a struct whose first field is narrower than 64 bits
   returned addresses instead of ids.
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
7. An `i128`/`u128` range-pattern bound was TRUNCATED to its low 64 bits and
   still compiled, so the test covered a different range.

None of the seven was visible to `tests/logos`. Two had a fixture that CLAIMED
the axis.

## Layout

| file | what it is |
|---|---|
| `model.py` | the oracle: type lattice, boundary values, exact arithmetic, and the POISON list (values excluded from shared corpora, each naming the finding that forced it) |
| `emit.py`  | the render bridge — how each Logos type's value reaches Python, and what trust each channel costs |
| `harness.py` | the families, the runner, the bisector, the tiers, the ledger check, the digest, the CLI |
| `run_tier.sh` | one tier, as ctest invokes it |
| `refusals.ledger` | the open refusals, one cid per line, with the root named |
| `corpus.{smoke,full}.sha256` | the committed corpus digests |

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
* **STRUCT SHAPE** (`layout` family) — what SURROUNDS the payload field:
  `iso` (payload then a wide field — the shape `deem` already had, kept as the
  control), `adj` (an ADJACENT sub-64-bit field after the payload), `nnw`
  (narrow-narrow-wide, payload in the middle), `tail` (payload at the END, so
  the aggregate's trailing padding is part of the stride), `nest` (payload and
  its neighbour inside a NESTED struct) and `narrow` (no wide field anywhere).
  This axis exists because `deem`'s row struct always put an 8-aligned field
  after a narrow one, and under that shape two engines that disagree about
  `i56` still agree about the struct — the next field re-aligns the offset
  either way. It takes a SECOND narrow field for them to drift.
* **ACCESS PATH** (`layout` family) — which of the compiler's size readers is on
  the hook: `arrlit` (per-element memcpy byte count), `fill` (the `[v; N]` fill
  stride), `slice` (a callee reading at the backend's stride only), `vec` (the
  container allocates by `size_of` and writes through a GEP — the two must agree
  or the heap is overrun past the first realloc).
* **EMISSION SITE** — a range pattern is compiled at `match`, `if let`,
  `let … else` and `while let`; each is its own site and each is enumerated.

**Nothing is sampled at run time.** The only sampling in the harness is inside
the generator and it is DECLARED: `model.small_values_of` returns a fixed 6-value
subset for the two quadratic families (`cmp`, `arith`, which are all-pairs), and
its rule is written next to it — both ends of the type plus the
sign-reinterpretation boundary, never a midpoint, never random.

## Running it by hand

    python3 tests/exhaustive/harness.py --list --all
    python3 tests/exhaustive/harness.py --all --tier smoke --jobs 12 \
        --ledger tests/exhaustive/refusals.ledger \
        --digest-file tests/exhaustive/corpus.smoke.sha256
    python3 tests/exhaustive/harness.py --family deem --only deem_u32_p0 --jobs 4 \
        --workdir /tmp/x --json /tmp/x/f.json

`--workdir` keeps the generated `.logos`, `.o` and binaries, so any finding has
a standalone reproducer on disk. `LOGOSC` and `LOGOS_LIB_DIR` override the
toolchain under test; ctest passes the build it is actually testing, because a
gate that measures a DIFFERENT toolchain than the one being built reports on
nothing.

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
