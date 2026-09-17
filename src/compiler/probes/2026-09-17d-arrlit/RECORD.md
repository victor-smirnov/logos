# 2026-09-17d-arrlit — AN AGGREGATE LITERAL THAT CARRIES NO PROVENANCE

site: src/compiler/borrow_check.cpp::prov_of_raw (the `case Code::ArrLit` arm)
base: build_hash 20900979284e248d 43 · HEAD 563552f35 · tree clean at start
armed: build-arrlit (fresh Ninja dir, clang++-20 from build/CMakeCache.txt)

## STEP 1 (measured this round)
- HEAD 563552f35, tree clean. `# TOTAL`: soundness_queue 235 · bc_admits 60 · bc_admits_blocked 8.
- Direct listing: 235 rows — tier1 37 · tier2 66 · tier3 121 · tier4 11.
- `probe-log-lint.py`: 396 records, every site symbol resolves.
- `build_hash.py`: 20900979284e248d 43.
- queue gate WITH `LOGOS_LIB_DIR`: **rc 0**, "soundness queue holds — 235 open row(s)".
- `tools/dlog/selftest.sh`: **rc 0**, control reads 19 walkers / 24 findings / try_path 1-5 / domain 42-5.

⚠ A CORRECTION TO MY OWN FIRST READING: I reported the two dangling-related tier-3
row programs as MISSING. They are not — the ledger's path column omits the `.logos`
extension and my `cat` used the bare path. The tree is right and the gate's rc 0 was
right. A complaint about the tree is a claim with a timestamp, and this one was false.

## THE DEFECT, AND HOW THE CARRIER FOUND IT
17c's counter-examples were re-measured on the SHIPPED binary with no probe armed:

| carrier | rustc 1.98.1 | shipped logosc |
|---|---|---|
| bare `&n`            | E0515 | refused, "cannot return reference to local variable 'n'" |
| tuple `(&n, 1i64)`   | E0515 | refused, same sentence |
| **array `[&n]`**     | **E0515** | ⛔ **compiles, runs 9** — reads a dead local |
| **struct field `&w`**| **E0515** | ⛔ **compiles, runs 9** — reads a dead local |

⚠ THE `[retgate]` PRINT SPLIT THE TWO, AND READING WOULD NOT HAVE. For `fn escape`:
bare and tuple print `prov{loc=1} srcs=[n,]`; the array prints `prov{loc=0 tmp=0 np=0}`
**beside its own `srcs=[n,]`**, and the struct `prov{loc=0} srcs=[w,]`. The §B6 source
walk NAMES the local; `prov_of` — which is what the return gate reads — answers nothing.

ONE-VARIABLE CONTROLS, multi-line, on base (the literal-vs-named hypothesis, REFUTED):

    pick([&n])            ADMITTED run 9        pick(a) where let a = [&n]   REFUSED
    pick((&n, 1i64))      REFUSED               pick(t) where let t = (&n,1) REFUSED
    pick(&w)              ADMITTED run 9        pick(w) by value             REFUSED

So it is neither "literal" nor "named" nor multi-line. TWO DIFFERENT DEFECTS:

* **A — A MISSING ARM.** `prov_of_raw` has `StructLit`, `TupleLit` and `EnumLitData`
  arms and NO `ArrLit` arm, so an array literal argument contributes `{}` and the
  dangling-return gate never sees the local. The callee's summary is CORRECT here
  (`pick__f__arr1_ref_i64: result<-0x1 EXACT`) and the argument IS merged — `prov_of`
  simply has nothing to say about an `ArrLit`. THIS ROUND'S ARM.
* **B — AN EXACT SUMMARY THAT IS WRONG.** `esc_struct$pick__f__ref_W: result<-0 EXACT`
  claims param 0 never reaches the result, though `pick` returns `x.r`. Being EXACT it
  also CANCELS elision (`an exact summary outranks elision`), so the argument is never
  merged at all. A different fact at a different site; this arm cannot touch it.

## THE CLASS, ENUMERATED WITH `tools/dlog` (not grep) — AND THE RULE WAS WRONG FIRST
New rule `tools/dlog/prov_arms.dl`: over (expr::Code enumerators) x (walker set),
which provenance walkers lack which arms. Developing dlog is the named exception to
the tooling freeze; the known-answer control is recorded IN the rule file.

⚠ **VERSION 1 ANSWERED "NOTHING IS MISSING", AND IT FAILED PERMISSIVELY.** It defined
a walker as `arm_call(F, K, F)` — descending by calling ITSELF BY NAME. That returned
14 walkers, all of which happen to self-name, and `prov_of_raw` was NOT among them: it
recurses through its wrapper `prov_of`. `prov_missing` was therefore EMPTY while the
per-site read said `ArrLit`. A missing ARM and a missing WALKER are both silence in the
output. Cross-checking against the per-site read is what caught it — exactly the
standing rule, and the tool erred on the permissive side, the same failure class as
the compiler defect this round fixes.

VERSION 2 admits mutual recursion (`F->G->F`). Cross-check, both numbers side by side:

| question | dlog v2 | per-site read |
|---|---|---|
| kinds `prov_of_raw` handles | 21, `ArrLit` absent | 21, `ArrLit` absent — AGREE |
| `prov_of_raw` missing arms | `ArrLit` (9 sibling walkers have it) | `ArrLit` — AGREE |
| domain size (guard) | 42 expr::Code enumerators | — |
| walkers | 15 (incl. `prov_of_raw`) | — |

⚠ LIMITATION STATED: v2 admits 1- and 2-hop cliques only, and `decl_name` collapses
overloads, so identity here is by NAME, not by the canonical declaration location.

## THE RESIDUE THE ENUMERATION NAMED — DISPOSED BY MEASUREMENT, NOT BY ASSERTION
`prov_missing` also named seven other kinds. Each was probed on the BASE binary:

| kind | siblings | probe | verdict |
|---|---|---|---|
| `BinOp`  | 3 | `return &(a + b);`      | already REFUSED ("temporary value") — NOT a hole |
| `Unary`  | 3 | `return &(-a);`         | already REFUSED — NOT a hole |
| `SliceLen` | 3 | `return &s[0u64];`    | already REFUSED — NOT a hole |
| `PtrArith`/`PtrDiff` | 2 | `unsafe { &*p }` | compiles — and **rustc ACCEPTS it too**: raw-pointer derefs are unchecked in Rust. PARITY, not a defect |
| `FormatCall` | 2 | `format!` then `s.as_str()` | already REFUSED ("local variable 's'") — NOT a hole. ⚠ My FIRST probe was malformed (`fn_macro: unknown callee 'format!'` — missing `use logos.std.fmt`), which is my bug and not a finding; re-probed with the imports the working fixture uses. |
| **`Try`** | **6** | `get(&n)?` then return | ⛔ **base compiles it CLEAN and RUNS 0; rustc REFUSES with E0515** — a REAL sixth hole, independent of this arm |

## THE ARM, AND WHAT IT MEASURED
One case label in `prov_of_raw`, mirroring `TupleLit` one arm up: merge each element's
`prov_of`. 10 lines, no other change.

⚠ **THE FIRST ARMED READ WAS A BROKEN READER, NOT A VERDICT.** Run against the armed
binary while its own stdlib was still building, ALL THIRTEEN programs answered
`cc=4 diag=0` — including `L3_scalar_elems`, which contains no borrow at all. A uniform
answer across programs that must differ is a reader failure:
`module_loader: cannot find package 'logos.std.prelude'`. Read as a verdict it would
have said "the arm changes nothing". Re-run with a lib dir present, it separates.

PRELIMINARY armed column (armed logosc + BASE lib dir, so ABI-freshness-warned;
re-verified against the armed binary's own archives before the commit):

| program | rustc | base | armed |
|---|---|---|---|
| X1 `[&n]`        | E0515 | cc0 **run 9** | **REFUSED** "local variable 'n'" |
| X2 `[&V,&n]`     | E0515 | cc0 **run 8** | **REFUSED** "local variable 'n'" |
| X3 `[[&n]]`      | E0515 | cc0 **run 6** | **REFUSED** — the merge recurses |
| X4 `[&s.f]`      | E0515 | cc0 **run 3** | **REFUSED** "local variable 's'" |
| X5 `[&n; 2]`     | E0515 | cc0 **run 2** | **REFUSED** — ⛔ PREDICTION REFUTED |
| L1 param elems   | ACCEPTS run 0 | cc0 run 0 | cc0 clean |
| L2 in-scope use  | ACCEPTS run 0 | cc0 run 0 | cc0 clean |
| L3 scalar elems  | ACCEPTS run 0 | cc0 run 0 | cc0 clean |
| L4 static elems  | ACCEPTS run 0 | cc0 run 0 | cc0 clean |
| nb_try `get(&n)?`| **E0515** | cc0 run 0 | cc0 — **UNMOVED**, as predicted |
| nb_binop/unary/slicelen | — | refused | refused — controls hold |

⚠ **MY OWN PREDICTION WAS WRONG ABOUT X5**, and in the safe direction: I predicted the
array-REPEAT spelling `[&n; 2]` might be a separate node kind this arm could not reach,
and named it the neighbour test. It is refused by this arm, so it lands as a fixture
rather than a row. Rule 17 applies to my own list as much as to a handed-down one.

## CONTROL REVERT — EVERY FIXTURE, ON THE BASE BINARY
Required before the commit, and it is what proves a pin discriminates rather than
matching both binaries. Base `build/bin/logosc`, base lib dir:

| fixture | base cc | base run | pinned sentence on base |
|---|---|---|---|
| bc_esc_arrlit_elem_dangle       | 0 | **9** | ABSENT — control holds |
| bc_esc_arrlit_mixed_elem_dangle | 0 | **8** | ABSENT — control holds |
| bc_esc_arrlit_nested_dangle     | 0 | **6** | ABSENT — control holds |
| bc_esc_arrlit_field_elem_dangle | 0 | **3** | ABSENT — control holds |
| bc_esc_arrlit_repeat_dangle     | 0 | **2** | ABSENT — control holds |
| bc_esc_arrlit_param_elem_admit  | 0 | 0 | (pass fixture) |
| bc_esc_arrlit_inscope_admit     | 0 | 0 | (pass fixture) |
| bc_esc_arrlit_scalar_elem_admit | 0 | 0 | (pass fixture) |
| bc_esc_arrlit_static_elem_admit | 0 | 0 | (pass fixture) |

Each fail fixture's exit code on base IS the dead local's value read back — 9, 8, 6, 3, 2
are the clobber constants, not coincidences.

## BASE COLUMNS, ALL ON ONE CONFIGURE (build/, build_hash 20900979284e248d 43)
| column | base |
|---|---|
| queue gate, 235 rows | **rc 0** — "soundness queue holds", tier1 37 / tier2 66 / tier3 121 / tier4 11 |
| `fail_text_oracle.py` | **1887** fail fixtures recorded (rc, stderr sha, .expected match) |
| `stdlib-cost.sh` | **4 of 4** layers compile |
| `run_oracle.py` | **7461** pass fixtures compiled, linked and RUN |
| spec fail tier | measured this round (see armed table) |

⚠ THE PROMPT'S RUNTIME-COLUMN FIGURE IS STALE: it says `run_oracle.py` covers "all ~6270
run_test.sh pass fixtures"; measured today it is **7461**. Recorded so the next round does
not reconcile against the old number.

## AUTHORITATIVE ARMED COLUMN (build-arrlit binary + ITS OWN archives, no ABI warning)
| program | rustc | base | armed |
|---|---|---|---|
| X1 `[&n]`         | E0515 | cc0 run **9** | **REFUSED** "local variable 'n'" |
| X2 `[&V,&n]`      | E0515 | cc0 run **8** | **REFUSED** "local variable 'n'" |
| X3 `[[&n]]`       | E0515 | cc0 run **6** | **REFUSED** "local variable 'n'" |
| X4 `[&s.f]`       | E0515 | cc0 run **3** | **REFUSED** "local variable 's'" |
| X5 `[&n; 2]`      | E0515 | cc0 run **2** | **REFUSED** "local variable 'n'" |
| L1 param elems    | ACCEPTS run 0 | cc0 run 0 | **cc0 run 0** |
| L2 in-scope use   | ACCEPTS run 0 | cc0 run 0 | **cc0 run 0** |
| L3 scalar elems   | ACCEPTS run 0 | cc0 run 0 | **cc0 run 0** |
| L4 static elems   | ACCEPTS run 0 | cc0 run 0 | **cc0 run 0** |
| nb_array_named    | E0515 | refused | refused — control holds |
| nb_binop / nb_unary / nb_slicelen | — | refused | refused — controls hold |
| **nb_struct_ref** (defect B) | **E0515** | cc0 run 9 | **cc0 run 9 — UNMOVED** |
| **nb_try** (Try carrier)     | **E0515** | cc0 run 0 | **cc0 run 0 — UNMOVED** |

Five illegal shapes closed, RUN-verified in the only direction that counts here: each was
COMPILING AND RUNNING on base, reading a dead local (the exit code IS the clobber value),
and each is now refused with a sentence I have read. Four legal programs still compile AND
RUN 0 — an over-refusal here would be worth more than the five closures.

## NEIGHBOURS — TESTED AGAINST THE LANDED CHANGE, NOT ASSUMED
| neighbour | closed here / rowed | reason and the number |
|---|---|---|
| `[&n; 2]` array REPEAT | **CLOSED, same commit** | lowers through the same `ArrLit` node; armed = refused. My prediction that it was a separate kind was REFUTED |
| `[[&n]]` nested literal | **CLOSED, same commit** | the element merge recurses; armed = refused |
| `[&s.f]` field-borrow element | **CLOSED, same commit** | armed = refused, names the struct local |
| `TupleLit`, `StructLit`, `EnumLitData` | already present | the arms this one was missing from |
| **`Try`** (dlog: 6 sibling walkers) | **ROWED — reason 1, no carrier in this arm** | a different node kind; MEASURED unmoved on the armed binary (cc=0 run=0) while rustc gives E0515. New row `try_operator_result_local_borrow_escape_admitted` |
| **struct-field via EXACT summary** (defect B) | **ROWED — reason 1, the fact has no carrier here** | the argument is never merged at all because an EXACT mask says param 0 does not reach the result; MEASURED unmoved (cc=0 run=9). New row `struct_ref_param_field_return_exact_summary_escape_admitted` |
| `BinOp` / `Unary` / `SliceLen` / `FormatCall` | not neighbours | MEASURED already refused on base — no hole to close |
| `PtrArith` / `PtrDiff` | not neighbours | rustc ACCEPTS the same program — deliberate raw-pointer parity |

## THE COLUMNS — BASE vs ARMED, ONE CONFIGURE EACH
| column | base | armed | delta |
|---|---|---|---|
| `fail_text_oracle.py` | 1887 | 1887 | **0 changed** — rc 0, stderr-sha 0, `.expected`-match 0; 0 added, 0 removed |
| spec fail tier (`logos_25_spec_fail`) | **494/494 rc 0** | **494/494 rc 0** | 0 |
| `stdlib-cost.sh` | 4 of 4 layers | 4 of 4 layers | 0 |
| `run_oracle.py` | 7461 run | (measured below) | |
| queue gate, 235 rows | rc 0 | (measured below) | |

⚠ A COLUMN I HAD TO MEASURE TWICE, AND THE FIRST READING WAS MY OWN BUG: the first armed
`fail_text_oracle.py` died `FileNotFoundError: 'build-arrlit/bin/logosc'` — `LOGOS_BUILD`
given as a RELATIVE path does not resolve in the worker processes. Re-run with an absolute
path it completes. An rc 1 there reads like a tree failure and is not one.

## THE QUEUE GATE, ARMED, OVER THE 235 PRE-EXISTING ROWS
rc 1 — and the WHOLE of it is two lines, both my own bookkeeping:

    FAIL: tests/soundness/open/struct_ref_param_field_return_exact_summary_escape_admitted
          is on the open-defects shelf with NO ledger row.
    FAIL: tests/soundness/open/try_operator_result_local_borrow_escape_admitted
          is on the open-defects shelf with NO ledger row.

I had written the two new row PROGRAMS onto the shelf before starting this run, and the
gate holds the shelf in both directions — a program with no row REDS it. PREDICTED before
reading, and it is the artifact, not the arm.

⚠ **NOT ONE `NO LONGER REPRODUCES` LINE.** Zero of the 235 pre-existing rows moved, which
is what I predicted in writing before the compiler was touched: this arm only ADDS
refusals, every tier-3 row is an OVER-refusal, so none can be closed by refusing more.
The five shapes it does close had NO rows — 17c priced them and opened none — so they land
as FAIL FIXTURES, not as row deletions.

## THE LEDGER AND THE PINS, RE-DERIVED BY LISTING
| | before | after |
|---|---|---|
| `# TOTAL` | 235 | **237** |
| rows by direct listing | 235 | **237** (tier1 **39** ← 37, tier2 66, tier3 121, tier4 11) |
| shelf `.logos` programs | 235 | **237** — equals the row count |
| REGISTRY-ALL | 11003 | **11014** (+11 = 9 fixtures + 2 squeue row tests) |
| REGISTRY-NOIMPORTED | 6501 | **6512** (+11) |
| REGISTRY-TIERCOMMIT | 353 | **355** (+2 — only the squeue row tests carry that label) |

⚠ The squeue per-row tests are globbed from the SHELF (`_squeue_srcs`), not from the
ledger, so the two programs added +2 ctest tests the moment they were written, before the
rows existed. The ledger edit itself moves no ctest count.

## THE FINAL COLUMNS, ON THE SHIPPED TREE
| column | base | armed (shipped) |
|---|---|---|
| queue gate | rc 0, 235 rows | **rc 0, 237 rows** — "soundness queue holds", tier1 39 / tier2 66 / tier3 121 / tier4 11; ZERO `NO LONGER REPRODUCES` |
| `fail_text_oracle.py` | 1887 | 1887 — **0 changed** (rc 0, sha 0, match 0), 0 added, 0 removed |
| `run_oracle.py` | 7461 | 7461 — **0 added, 0 removed, 1 changed** |
| spec fail tier | 494/494 rc 0 | 494/494 rc 0 |
| `stdlib-cost.sh` | 4 of 4 | 4 of 4 |

⚠ THE ONE run_oracle CHANGE IS THE NAMED EXCLUSION, AND I CHECKED IT RATHER THAN ASSUMING:
`logos_02_semantic_core_pass_cast-region-to-uint` — rc `0`->`0` and exit `0`->`0` IDENTICAL,
only the stdout SHA differs. That fixture PRINTS A STACK ADDRESS, which is why the prompt
says to subtract it by name. Nothing else in 7461 moved.

## TWO GATES I RED MYSELF, AND WHAT THEY WERE
The first L1 run came back rc 1: 808/808 at L1.1 and the enumerator smoke tier green, but
the GATES tier failed on two tests — both my own bookkeeping, neither the arm:

* `logos_00_probe_log_lint` — my PROBES.md record carried `base:`/`armed:` lines but no
  `build:` and no `fires:`. The lint requires both of every record: a measurement with no
  build identity cannot be told from its own repeats, and a ceiling with no fire count
  cannot distinguish a refutation from an unreached site. FIXED by giving the record a real
  `build:` line and a `fires:` line that says plainly that NO PROBE WAS INSTALLED — the arm
  is unconditional, so `probe::on` counts nothing and a fire count would be an invention;
  the EFFECT SET was measured directly instead. Re-run: 397 records, ctest 1/1 Passed.

* `logos_00_population_pin_lint` — `direct_door PIN['corpus']` pinned 3835 / listed 3839 and
  `PIN['nonglob']` pinned 3644 / listed 3648. That pin family is SEPARATE from the census
  pins I had already re-derived, and it counts `tests/logos/pass/*.logos` ONLY — which is
  why the drift is +4 (my four PASS fixtures) and not +9: the five fail fixtures are not in
  this population. RE-DERIVED BY DIRECT FILE LISTING, never by adding to the previous line:
  corpus 3839, glob 191, nonglob 3648, partition closes 3839 = 191 + 3648. Re-run: 4/4 OK.

⚠ THE LESSON IS THE ONE THE LINT ITSELF STATES: "how big is the pass corpus" had TWO
predicates and a round can re-derive one and not the other. I did exactly that — updated
the census pins hours earlier and never touched the direct_door pins, because nothing in my
head connected them. The lint exists because that has happened before.

## GATES ON THE LANDED TREE (build/ rebuilt WITH the arm, rc 0)
| gate | rc | detail |
|---|---|---|
| L1 (re-run, after both lint fixes) | **0** | 808/808 at L1.1 · enumerator smoke 12 684 generated cases · **gates tier 355/355** |
| `logos_00_probe_log_lint` | **0** | 397 records, every site symbol resolves |
| `logos_00_population_pin_lint` | **0** | corpus 3839 / glob 191 / nonglob 3648, all OK |
| `logos_00_census_pin` | **0** | ALL 11014 / -LE imported 6512 / tier_commit 355 |
| soundness queue gate (237 rows) | **0** | "soundness queue holds", zero `NO LONGER REPRODUCES` |

⚠ NO PROBE IS INSTALLED IN THE LANDED TREE, and I checked it rather than asserting it: the
compiler diff contains ZERO occurrences of `probe::` (`git diff -- src/compiler/borrow_check.cpp
| grep -c 'probe::'` -> 0). The single `probe::on` string in the whole diff is PROSE, in this
round's `fires:` line in PROBES.md, saying that no probe was installed.

## `L4 bc` — BOTH PHASES, READ SEPARATELY
rc **0**. The level exits with the WORSE of two phases, so one number is not the answer:
  * phase 1: **6512/6512 tests passed, 0 failed**
  * phase 2: **1601/1601 tests passed, 0 failed**
Run detached with `LOGOS_L4_BG=1` — test-levels.sh REFUSES L4 in the foreground (rc 2 with a
usage message, which is not a test failure and must not be read as one; my first attempt hit
exactly that and the column was unmeasured until re-invoked correctly).
