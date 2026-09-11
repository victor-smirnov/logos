# bufdrop-2026-09-11 — `BufReader`/`BufWriter` FREE ON DROP: PRICED AT ZERO, AND E0367 SPLIT IT IN TWO

base build `260d74a881ec778e 43` (`scripts/build_hash.py`, READ).
Armed dir `build-copy` (same HEAD compiler, rebuilt this round); base dir `build`
was never rebuilt, so the verdict store's build identity is untouched.

## STEP 1, DERIVED

```
b539451f2 / 871d1bddd / 8decdfaa5      status: clean
# TOTAL  soundness_queue 82   bc_admits 85   bc_admits_blocked 8
soundness_queue rows, by direct listing        82   (t1=18 t2=12 t3=44 t4=8)
probe-log-lint   271 records, every site symbol resolves
build_hash       260d74a881ec778e 43
queue gate       rc 0 — WITH LOGOS_LIB_DIR, as the prompt gives it
```

The queue gate's rc is **0** and it is the first number here. The STEP-1 command
as printed in this prompt carries `LOGOS_LIB_DIR` and works; that is the third
consecutive round to re-verify it against the text actually given, and it is
still fixed. **No correction to the prompt's commands is owed.**

## CORRECTIONS TO THE HANDED-DOWN FACTS (rule 17)

| handed down | measured today | how |
|---|---|---|
| "9 loss records over **7** fixtures" | 9 records over **6** fixtures | valgrind on all 8 fixtures naming the types; `adv3_generic_field_method_call` is CLEAN (it never allocates a buffer — it only exercises a generic field method call) and `bufio_basic` is clean because it calls `close` six times |
| doc comments "at lines ~50 and ~147" | **exactly 50 and 147** — the prompt's approximation is exact | `grep -n` on the pre-change file |
| "13 files use them" | **13** — confirmed (5 stdlib, 8 fixtures) | `grep -rl` |
| "adding `Drop` to two public types is an API decision with an owner" (2 rounds) | **refuted by both registries** — see below | grep by CONSTRUCT in both schemes |

## THE DIVERGENCE REGISTRIES, BY CONSTRUCT, IN BOTH SCHEMES

  * `docs/DIVERGENCES.md` — 17 LETTER-keyed rows, enumerated: `A1 A2 A3 A4 A5 A6
    A7 A8 A9 A10 A11 A12 A13 A14 A15 A16 A17`. **None is io/buffered.**
  * `docs/spec/*.md` — 3851 `### <clause.id>` clauses, 497 of them in
    `docs/spec/divergences.md`. `grep -rniE 'bufreader|bufwriter'` over all of
    `docs/spec/` returns **0 lines**. The 7 clause ids whose id mentions `drop`
    are `expr.drop.flag-uninit-conditional`, `mono.subst.assign-drop-old-preserved`,
    `mono.subst.drop-args-non-generic-impl`, `grammar.generic.hrtb-binder`,
    `type.copy.structural-auto`, `expr.assign.drop-before-replace`,
    `expr.drop.tuple-array-index-order` — **none is about a stdlib owner's
    release contract.**

So **neither registry covers `BufReader`/`BufWriter`**, and under the standing
rule the answer is Rust's: they free on drop. The previous two rounds' "API
decision with an owner" is retired by measurement, not by opinion.

⚠ **THE RUST CLAIM RESTS ON MY READING.** `/home/logos/cxx/rust` is checked out
but **has no `library/` tree** (`src/`, `tests/`, `compiler` tooling only), so
`library/std/src/io/buffered/bufwriter.rs` is NOT on this box and I could not
read the upstream `impl Drop for BufWriter`. There is no rustc binary either.
What I assert from memory, and flag as such: Rust's `BufWriter` declares
`pub struct BufWriter<W: ?Sized + Write>` — **the `Write` bound is on the STRUCT** —
and its `Drop` flushes, discarding the error; Rust's `BufReader<R: ?Sized>` has
**no `Drop` impl at all** because its buffer is a `Box<[u8]>`, i.e. it frees on
drop with nothing written.

## THE FINDING THAT SPLIT THE FIX: E0367

The obvious spelling — the one that matches Rust term for term —

```logos
impl<W: Write> Drop for BufWriter<W> { ... flush, then free ... }
impl<R: Read>  Drop for BufReader<R> { ... free ... }
```

**does not compile**, and the refusal is CORRECT:

```
error [impl Drop for BufReader]: impl Drop for BufReader<R>: `R: Read` is
required by the `Drop` impl but not by `BufReader` (E0367)
```

`sema_collect.cpp:7008`, pinned by four `tests/logos/fail/bc_dropwf_*` fixtures
and three imported `tests/imported/fail/dropck/*` ports. A `Drop` impl may not
add a bound the type does not carry — and `stdlib/std/io/buffered/buffered.logos`
declares `pub struct BufWriter<W>` / `pub struct BufReader<R>` **unbounded**,
with `W: Write` / `R: Read` living only on the inherent impls.

That is not a wall; it is the fix SPLITTING in two, along exactly the line Rust
itself draws:

  * **`BufReader` — nothing is in the way.** Its drop only frees, so an
    unbounded `impl<R> Drop for BufReader<R>` is legal and is the whole job.
  * **`BufWriter` — the Rust-canonical drop FLUSHES, and flushing needs
    `W: Write`.** Under E0367 that bound can only come from the STRUCT
    DECLARATION — which is what Rust does. So the Rust-canonical `BufWriter`
    fix is **two** changes, not one: `struct BufWriter<W: Write>` plus the
    bounded `Drop`.

## THE CANDIDATES, AND THE DIFF BUDGET

Budget declared before implementing: **≤30 added lines, one file.**

| | candidate | what it is | diff (`git diff --numstat`) |
|---|---|---|---|
| **B** | `impl<W: Write>` + `impl<R: Read>`, flush-then-free | the naive Rust transcription | 32 / 2 — **REFUSED by E0367, does not build** |
| **A** | `impl<W>` + `impl<R>`, **free only**, no struct change | the minimum that closes every record | **31 / 2** |
| **C** | A, plus `struct BufWriter<W: Write>` and a bounded `BufWriter` drop that flushes | Rust-canonical | NOT YET BUILT — see "what deserves funding" |

## THE PROBE TABLE — CANDIDATE A, ALL COST COLUMNS

| column | base | armed (A) | note |
|---|---|---|---|
| **stdlib** (`cmake --build --target stdlib_layers`) | rc 0 | **rc 0** | the hardest oracle in the tree: the stdlib asserts legality by BEING BUILT, and it contains the close-then-scope-exit site (`http/server.logos:219`) |
| **pass/cfail** on the 8 fixtures naming the types | 8/8 compile, exit codes 0/42 | **8/8, exit codes identical** | |
| **runtime** (`scripts/run_oracle.py`, ~6270 pass fixtures, compiled + linked + RUN) | (see below) | (see below) | |
| **valgrind** over the 8 fixtures | **9 definitely-lost records / 6 fixtures** | **0 records / 0 fixtures** | every record attributed to `buf_reader_wrap`/`buf_writer_wrap` by a 20-frame stack |
| **ABI** (`scripts/abi-check.sh`, rc read without a pipe) | — | (see below) | |

## THE HAND PROGRAMS — TWELVE SHAPES, NOT TWELVE COUNTS (rule 5)

The oracle is a **destructor count plus valgrind**, never rc alone: a leak reads
as a clean exit. `S5` is the count — a user `Write` sink with its own `impl Drop`
bumping a static, returned as the exit code. All programs are multi-line.

| # | shape | base | armed (A) | predicted? |
|---|---|---|---|---|
| S1 | `BufWriter` local, **no** `close`, scope exit | leak 4,096 / 1 blk | **clean** | yes |
| S2 | `BufWriter`, `close()` **then** scope exit | clean | **clean, 0 errors** | yes — the double-free case |
| S3 | `BufReader` local, no `close` | leak 4,096 / 1 | **clean** | yes |
| S4 | `BufReader`, `close()` then scope exit | clean | **clean, 0 errors** | yes — the double-free case |
| S5 | **DESTRUCTOR COUNT** — `BufWriter<CountingSink>`, sink `impl Drop` bumps a static, exit code = count | **rc 1**, leak 4,096 | **rc 1**, clean | yes — 1 is exact: too low = the field drop was replaced, too high = double drop |
| S6 | `BufWriter` moved into a **struct field**, struct dropped | leak 4,096 / 1 | **clean** | yes |
| S7 | `BufWriter` **returned from a factory**, dropped in the caller | leak 4,096 / 1 | **clean** | yes |
| S8 | `BufReader` **moved into a callee** by value | leak 4,096 / 1 | **clean** | yes |
| S9 | three `BufWriter`s born and dropped **in a loop** + one in an **untaken branch** | leak 12,288 / **3** blk (the untaken branch contributes 0 — drop elaboration is already exact) | **clean** | yes |
| S10 | **nested** `BufReader<BufReader<FdReader>>` | leak 6,144 / 2 blk (2,048 + 4,096) | **clean** | yes |
| S11 | **use after a by-value pass** — is the type auto-Copy (A16)? | `error: use of moved variable 'w'` | **identical diagnostic** | yes — see below |
| S12 | write 3 bytes, drop **without** `flush`, read them back; exit code = bytes recovered | **rc 0** (bytes discarded) | **rc 0** (still discarded) | yes — this is the Rust gap candidate A does NOT close |

**12 predictions by name before the run, 12 held, and both sets diffed both ways.**

### THE A16 INTERACTION IS A MEASURED ZERO, NOT AN ASSUMED ONE

`impl Drop` is auto-Copy's opt-out (`type.copy.structural-auto` / A16), and
after monomorphisation `BufWriter<FdWriter>` has fields `{i32, *mut u8, i64, i64}`
— every one of them Copy. So "adding `Drop` flips this public type from Copy to
move, and every by-value use site in the corpus changes meaning" is a live
hypothesis, and it is the expensive direction. **S11 refutes it on the BASE
binary**: `BufWriter<FdWriter>` is ALREADY move-only (the generic field `writer: W`
is a `TypeVar`, conservatively non-Copy, and the verdict is computed before
mono), so the diagnostic is byte-identical on both arms. The interaction is nil.

### S12 IS THE ONE THAT SAYS CANDIDATE A IS NOT THE WHOLE ANSWER

Three bytes written into a 4,096-byte buffer and dropped are **discarded** on
both arms. Rust would have written them. Candidate A frees the buffer and leaves
that divergence exactly where it was — it is a strict improvement over base and
it is not Rust. Only candidate C closes S12, and S12 is the reason C is worth its
extra change rather than a purist's flourish.

## THE ABI, MEASURED — PRESERVING, 3 ADDITIONS, NO BUMP

A public stdlib type gaining a destructor moves drop-glue symbols, so this was
priced, not assumed. `scripts/abi-check.sh` with `LOGOSC`/`LOGOS_LIB_DIR` on the
armed dir, **rc read without a pipe: rc 1** — and the rc is check 1 (FRESHNESS)
doing its job, not a BREAKING verdict: `abi/logos.abi` had not been regenerated,
which is a step the landing owes, not a defect. What check 1 printed is the whole
delta, and it is exactly the three symbols predicted BY NAME before the run:

```
+sym logos_std..logos.std.io.buffered.BufReader$G1$AsyncTcpStream__drop__g__refmut_BufReader$G1$R
+sym logos_std..logos.std.io.buffered.BufReader$G1$FdReader__drop__g__refmut_BufReader$G1$R
+sym logos_std..logos.std.io.buffered.BufWriter$G1$FdWriter__drop__g__refmut_BufWriter$G1$W
```

Because check 1 aborts before check 2, the VERDICT was then taken directly
through the same tool, both spec files sorted and diffed both ways first:

```
diff(committed, fresh):  0 removed, 3 added
logosc --abi-diff abi/logos.abi fresh.abi
  ADDED: 3 record(s)
  VERDICT: ABI-PRESERVING — additive only, patchset OK      rc 0
```

`sym 12604  type 370  vtable 123  schema 2` — the `type` records for
`logos.std.io.buffered.BufReader` / `BufWriter` are **byte-identical** (the field
lists do not change), and no vtable or schema moved. **So the landing does NOT
bump the minor version** — unlike `af13931f3`, which changed the drop RECEIVER
and therefore removed symbols. It DOES owe `cmake --build build --target
logos-abi` plus the three-line `abi/logos.abi` commit in the same change.

⚠ The canaries: checks 0 and 1 ran with theirs. Checks 2-4 and their canaries did
NOT run, because check 1 exited first. The verdict above is the differ's own
answer on the real pair, not a canaried gate pass — the landing must re-run the
whole script after regenerating the spec.

## THE RUNTIME COLUMN — 6,661 FIXTURES, COMPILED, LINKED AND RUN, BOTH ARMS

`scripts/run_oracle.py` twice **in the same build dir** (`build-copy`), one
variable between them. The control is the REVERT, and the revert was PROVEN
before the control ran: `git checkout` the file, rebuild `stdlib_layers`, and
`s1_writer_noclose` leaks 4,096 bytes again. A control on an un-restored control
proves nothing; this one was restored and the restoration was measured.

```
control (no Drop)   6661 rows   ccrc 0 x 6657, ccrc 90 x 4
armed  (cand. A)    6661 rows   ccrc 0 x 6657, ccrc 90 x 4
diff, BOTH WAYS     1 row
  cast-region-to-uint   stdout sha 5ded1140… -> 8196af38…   (rc unchanged 0/0)
names only in control: none      names only in armed: none
```

That one row is `cast-region-to-uint`, which the prompt names as the fixture to
subtract because it **prints a stack address**. Every other triple — compiler rc,
program rc, stdout sha — is identical.

**RUNTIME COST 0.** Combined with stdlib rc 0 and 8/8 fixture exit codes, the
cost of candidate A is zero in every column the tree has.

## CANDIDATE C, MEASURED TOO — AND IT IS THE ONE THAT IS RUST

`struct BufWriter<W: Write>` (the bound E0367 demands, and the one Rust's own
declaration carries) + `buf_writer_inner_mut<W: Write>` + the bounded, flushing
`Drop`. Diff **34 / 4**, one file.

| | candidate A | candidate C |
|---|---|---|
| stdlib build | rc 0 | **rc 0** |
| S1-S11 | see table | **identical, every one** |
| S5 destructor count | 1 | **1** |
| S2 / S4 close-then-scope-exit | clean | **clean** |
| the 8 fixtures | 0 leak records, exit codes unchanged | **0 leak records, exit codes unchanged** |
| **S12 — the 3 buffered bytes** | **rc 0, discarded** | **rc 3, WRITTEN** |
| ABI | 3 added, 0 removed, PRESERVING | **byte-identical spec to A** |

### ⚠ THE ABI INSTRUMENT IS BLIND TO C'S ONE REAL RISK

C's emitted spec is **byte-for-byte the same file** as A's. The `type` record
carries a FIELD LIST and nothing else, so adding `W: Write` to a public struct's
declaration — a source-level tightening that would refuse any downstream
`BufWriter<SomethingNotWrite>` — leaves **no trace in `abi/logos.abi` and cannot
be seen by any of `abi-check.sh`'s five checks.** The instrument answers
"ABI-PRESERVING" to both candidates with equal confidence, and for C that answer
is true and incomplete. The blast radius inside this tree is exactly one symbol
(`buf_writer_inner_mut`, the only unbounded mention of `BufWriter<W>` anywhere,
and it is fixed in the same 34-line diff); outside the tree the spec is silent.

## WHAT DESERVES FUNDING

**Candidate C, in one commit, and it is not an escalation.**

  1. Zero cost in every column the tree owns: stdlib rc 0, runtime oracle 0 over
     6,661 run fixtures, 8/8 fixtures' exit codes unchanged, ABI additive.
  2. It closes **all 9 `vg_leak_records` entries over 6 fixtures** — measured to
     zero, not predicted — the largest remaining group in the backlog.
  3. It is Rust's own shape, down to where the `Write` bound lives, and S12 is
     the measured difference between "frees" and "behaves like Rust": three
     buffered bytes reach the sink instead of being discarded.
  4. The double-free hazard the prompt names is closed by construction and
     measured twice on both candidates: `close` nulls `buf`, `drop` guards on it,
     and S2/S4 — plus the stdlib's own `br.close()` at
     `stdlib/std/io/http/server.logos:219` with `br` still in scope — are clean
     with 0 valgrind errors.

What the landing owes, and none of it is in this round's diff:

  * the two doc comments at `buffered.logos:50` and `:147` (already rewritten in
    both candidates — prose asserting a contract the code no longer has);
  * `cmake --build build --target logos-abi` and the three-line `abi/logos.abi`;
  * **no minor version bump** — measured PRESERVING, unlike `af13931f3`;
  * a new pass fixture that is the destructor-count pair (`close`-then-scope-exit
    and scope-exit-alone), because the 9 records were invisible to the rc oracle
    that every one of those 6 fixtures already passes: **all six exit 42 on both
    arms.** A leak is silent BY ORACLE here, and the corpus will not hold this
    fix without a valgrind-shaped or count-shaped assertion.

⚠ **AND THE COST THAT IS NOT IN THE TABLE.** This change is in stdlib SOURCE, so
the landing must rebuild `build/lib/logos` — which moves `scripts/build_hash.py`,
the verdict store's build identity, and makes the next gate re-measure everything
it already holds. That is why this round measured in `build-copy` and never once
built `build`: the base hash `260d74a881ec778e 43` is the same at the end of this
round as at the start.

## WHAT WAS NOT MEASURED, AND IS OWED

  * `tests/logos/test-levels.sh L4 bc` was **not run**. The corpus columns here
    are the runtime oracle (6,661 pass fixtures RUN) and the stdlib build, which
    together dominate a stdlib-source change; the `fail`-text column was not
    taken because `fail_text_oracle.py` self-invalidates across a rebuild and
    both arms would have needed their own configure. The landing owes the gate.
  * The upstream `.stderr`/source evidence for Rust's `BufWriter::drop`: **not on
    this box** (no `library/` tree, no rustc binary). Flagged above.

## POST-ROUND — THE TREE, PROVEN

```
git checkout stdlib/std/io/buffered/buffered.logos    → 0 `Drop for Buf*` in source
build-copy stdlib rebuilt from the reverted source    → s1 leaks 4,096 b again
build_hash (build/, never rebuilt this round)  260d74a881ec778e 43   — UNMOVED
soundness_queue gate                                   rc 0
tests/logos/test-levels.sh L1 (from build/)            rc 0 — 806/806, 12 684
                                                       generated cases, 143 gates
git status                                             only docs/probes/bufdrop-2026-09-11/
```

No probe is installed, no compiler source was touched at any point in this round,
and both build dirs hold archives that match the committed sources.
