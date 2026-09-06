# Round 2026-09-06 (landing) — the compile-time regression, phenomenon A

build before: 6dd76d6d10202933 · build after: 596f7b385e46f0fe
L1 758 + 173 rc 0 · queue gate rc 0 on 41 · probe-log-lint 221 -> 223

## STEP 1, RE-DERIVED (the handed-down report is a paraphrase)

soundness_queue **39** · bc_admits **98** · bc_admits_blocked **25** ·
probe-log-lint **221** · build_hash **6dd76d6d10202933 43** · queue gate rc 0.
All four agree with the paraphrase. The paraphrase's correction is CONFIRMED
for the third round running: the STEP 1 gate command self-reports GATE BROKEN
without `LOGOS_LIB_DIR`; with `LOGOS_LIB_DIR=$PWD/build/lib/logos` it is rc 0.

## WHAT LANDED

`MLIRGenImpl::resolve_method_symbol` memoises its (symbol, pkg_owns_struct)
PAIR on (struct_name, method_name, pkg). 29 inserted lines, 4 removed.
Full numbers in PROBES.md 2026-09-06 §dropmemo and §rmsclass.

  red fixture, interleaved, 5 runs each:  31.24 s -> 7.06 s   **4.43x**
  as a ctest test against its 120 s property:              **7.14 s**
  `ctest -R 'logos_09_|logos_00_'`  3075/3075 passed — the three
     FIXTURES_SETUP dependents that could not run at all are running.
  the store's 20 named slow tests (list re-derived from runs.db):
     17 are pass fixtures, 140.9 s -> 57.9 s = 2.43x; 15 improved
     1.6x-4.4x, 2 unchanged.
  250-program random sample, interleaved: paired median **0.994**.

## WHAT DID NOT LAND, AND THE NUMBER THAT SAYS SO

`dropq0` — the crude revert of `163f043bc`. DECLINED BY NAME. It is 1.90x where
this is 4.43x, and it buys that by deleting the qualified `<T>__Drop__drop`
pre-search, which is the destructor-identity fix `163f043bc` paid for. A price,
never a fix, and this round makes it unnecessary: the memo keeps the pre-search
and pays for it 62 times instead of 29 666.

Phenomenon **B** — the global 20% over `a9c7b67fd..96fdf6235` — is NOT touched
and is NOT closed. The measurement that says so is this round's own: the paired
median over 250 programs is 0.994, i.e. the median fixture does not move, and
`rms.arrive` is EMPTY on it. A and B are separate roots and this fix is A's.

## THE CLASS

Enumerated by the property (a pure query over the immutable `prog_` done as a
full linear scan, invoked per node), settled by a census over all 2870 pass
programs, not by the grep that nominated the candidates. Three members:
`resolve_method_symbol` 14 080 547 357 scan iterations, `pkg_owns_symbol_owner`
171 arrivals with its index already built once, `gen_tagged_dispatch` 7
arrivals / 21 576 iterations. 650 000 : 1. The other two are closed already,
one structurally and one by rarity, and the census is what distinguishes that
from "the grep found nothing else".

## TWO NEW DEFECTS, BOTH REPRODUCING, BOTH PRE-DATING THIS ROUND

They came out of the COUNTER-EXAMPLES, which is what they are for.

1. `homonym_field_drop_glue_segv` (tier 1, `run 139`). PACKAGE-BLIND DROP GLUE
   REACHED THROUGH A FIELD. `mlirgen_odr_drop_glue_homonym` closed the channel
   where the homonym is the ELEMENT (`Vec<String>`); this is the same theft one
   level in — the element is `Holder`, a homonym of nothing, and the bare-name
   binding happens when `Holder`'s glue resolves its FIELD's destructor.
   ⚠ THE OBJECT FILE IS THE ORACLE, not the exit code:
       n2_vec_holder     `nm -C` -> U logos.mem.string.String__drop__f__String
       n3_ctl_no_homonym (`String` -> `ZqStr`, one token) -> that symbol ABSENT
   rc 139 vs rc 3. The control half is landed as the pass fixture
   `tests/logos/pass/mlirgen_odr_drop_glue_field_ctl.logos`.

2. `both_drops_destructor_is_inherent` (tier 1, `run 2`). ⚠ CONTRADICTS THE
   QUEUE LEDGER'S OWN HEADER, which records this candidate as having "not
   survived re-running" on 2026-09-04. It survives today, on THIS binary and on
   the pristine pre-round one, identically: a type with an inherent `drop` and a
   `Drop` impl, with NO explicit `.drop()` call anywhere, prints
   `trait=0 inherent=2` and exits 2 where Rust gives `trait=2 inherent=0` and
   exit 20 — the scope-exit destructor runs the INHERENT method and
   `Drop::drop` never runs. The 09-04 re-run must have used a shape with an
   explicit call. `nm -C` says where the repair is:
       T test.D__drop__f__ref_D      (inherent)
       T test.D__drop__f__refmut_D   (trait)
   and NO `D__Drop__drop`. `collect_fn`'s G156-5 never files the trait one
   under the qualified key here, so `163f043bc`'s pre-search — the thing that
   doubled the scan — has nothing to find. THE REPAIR IS AT THE FILING SITE,
   NOT AT THE LOOKUP. That is also the honest price of `163f043bc`: on
   `wql_domain_static_extremes` its qualified probe resolves ZERO times in
   29 666 queries.

Queue 39 -> **41**, `# TOTAL` re-derived by direct listing (41 rows, 41
programs on the shelf), gate rc 0. 0 rows closed — this round's deliverable is
a red gate turned green, and a row closed under a red FIXTURES_SETUP producer
would be closed against an unmeasured tree.
