# RESULT — 2026-09-11a, `lifereg.NEW-E0207`

site: src/compiler/sema_decl.cpp::lower_impl_block  (the only context that holds the
      binders AND the self type AND the trait-arg node AND can refuse — see the dlog
      enumeration below)
build: base 03d9290a7ee96c2a 43 (READ) -> armed d369bf71c1cf4e7e 43 (READ).
       Two intermediate armed builds, 2204408582aba29a and a27b6d9316f179f4, are the
       two MISSING CARRIERS below; each was measured before the next was written.

    probe        ceiling  cost  cfail            stdlib  runtime
    e0207assoc         3     0  0 of 1478        4 of 4  see below
    e0207unc           4     1  0 of 1478        4 of 4  not run (declined on cost)

fires: e0207assoc 7 · e0207unc 5 (an impl header is lowered TWICE, so a fire count here
is about 2x its row count; the CENSUS below is the number that means something).

## THE PROBE'S OWN PREDICATE WAS WRONG TWICE, AND BOTH TIMES THE ARRIVAL CENSUS SAID SO

Rule 17 in its sharpest form: the hypothesis I wrote was refuted by the first number taken
against it, twice, before any corpus column existed.

1. `target_resolved` IS NULL FOR EVERY NAMED SELF TYPE. It is assigned only on the
   REF_TYPE / GENERIC_INST / TUPLE_TYPE branches of `lower_impl_block`; for the ordinary
   `impl<'a> Tr for W<'a>` it stays null. Measured on the first armed build with
   `LOGOS_DUMP_E0207`: `selfty=0` on SEVEN of the ten hand programs and `selfty=1` only on
   the `&S` shapes — i.e. only on the three target rows. A cost of 0 read off that binary
   would have been an UNREACHED-SITE zero for 70% of the shapes (rule 1). Carrying the fact
   (re-resolve the self type when `target_resolved` is null) made every arrival live.
2. A LIFETIME AT A **TRAIT-ARGUMENT** POSITION HAS NO CARRIER AT ALL, BY DESIGN AND WITH A
   COMMENT SAYING SO. `sema_decl.cpp` ~2971: *"L1: skip LIFETIME_PARAM at trait-arg
   position; Logos doesn't track regions structurally for trait dispatch"* — the name is
   checked for being declared and then DROPPED, so `current_impl_trait_args_` can never
   contain it. On the second armed build `impl<'a> Tr<'a> for W` (hand l1) and the `'b` of
   `impl<'a,'b> Tr2<'b> for W<'a>` (l6) both ARRIVED as "unconstrained" — two LEGAL impls
   that both arms would have refused. This is the round's finding: **the E0207 predicate is
   two doors in SERIES (rule 2) and the trait-arg door does not exist.** The third armed
   build reads the LIFETIME_PARAM names back off the AST at the probe; a LANDED fix has to
   decide whether to keep that read local or restore the fact at the collection loop.

## HAND PROGRAMS — TEN, AND THE SHAPE VARIES, NOT THE COUNT (rule 5)

Legal, all six correctly ADMITTED by both arms on the final build:
  l1 binder at a TRAIT-ARG position        `impl<'a> Tr<'a> for W`          (door 2)
  l2 binder in the SELF TYPE               `impl<'a> Tr for W<'a>`
  l3 INHERENT impl, binder in the self ty  `impl<'a> W<'a>`
  l5 the FIX for the target rows           `impl<'a> Tr for &'a S`
  l6 TWO binders, one per door             `impl<'a,'b> Tr2<'b> for W<'a>`
  l8 a `Drop` impl                         `impl<'a> Drop for W<'a>`
Legal, and THE TWO THAT SEPARATE THE ARMS — admitted by `e0207assoc`, REFUSED by `e0207unc`:
  l4 binder used only in a METHOD SIGNATURE `impl<'a> W { fn f(self:&Self,x:&'a i64)->&'a i64 }`
  l7 binder used only in an ASSOC CONST     `impl<'a,'c> Anything<'a> for S { const AC: i64 }`
Illegal, both arms refuse, `e0207assoc` for the right reason:
  x9  the target construct                 `impl<'a> Tr for &S { type Item = &'a i64; }`
  x10 two binders, ONE unconstrained        refuses `'b` only, admits `'a` — per-binder

Rule 9 is DISCHARGED and the inner predicate is the whole question: the two names are
identical on every corpus column except one row, and separate cleanly only on l4/l7.

## SETS, DIFFED BOTH WAYS AGAINST THE PREDICTION WRITTEN BEFORE THE BINARY EXISTED

`e0207assoc` CEILING = {missing-lifetime-in-assoc-type-1, -5, -6}. Predicted the same three.
Both differences EMPTY. COST 0 over pass(ledger+legal) + fail(text, 1478) + stdlib(4 of 4).

`e0207unc` CEILING = the same three PLUS `logos_00_bc_admit_nll_trait-associated-constant`.
Predicted the same four. **AND THE FOURTH IS NOT A CLOSED ROW**: upstream's
tests/ui/nll/trait-associated-constant.stderr, READ on the box, is
`error[E0308]: const not compatible with trait — lifetime mismatch`, NOT E0207. rustc's
`lifetimes_in_associated_types` set is built from associated TYPES; an assoc CONST naming an
unconstrained binder is not this error. A row closed by a wrong diagnostic is not closed, so
`e0207unc`'s honest ceiling is 3 — the same three — at a cost of 1.
COST = {logos_02_semantic_core_pass_bc_mcallvar_legal_twins}, predicted by name, and the
construct is l4's: a binder used only in a method signature. Legal Rust.

## THE TEXTUAL CENSUS WAS WRONG IN EXACTLY THE WAY THE PREDICTION SAID IT MIGHT BE

Predicted: if `'static` inside a type-param bound reaches the site, the census (7 candidate
impls) is right; if it does not, the census over-counted by two and the compiler is right.
MEASURED: `bc_ltbndenv_legal_shapes` and `bc_objlt_impl_bound` are rc 0 under BOTH arms —
the probe reads LIFETIME_PARAM nodes and never sees a bound. Population 7 -> 5.

## STDLIB: A STRUCTURAL ZERO, NOT A PROBE ZERO
`grep -rnE "^ *impl<[^>]*'" --include=*.logos stdlib` = 0 headers. The site cannot be
reached from the stdlib at all, so "all four layers compile" is true and says nothing about
the arm. Stated rather than counted as evidence (rule 1).

## THE ARM'S SENTENCE IS RIGHT AND ITS SITE IS WRONG — THIS BLOCKS A LANDING
    missing-lifetime-in-assoc-type-1.logos:557: error [fn iter_partition_vec]:
      impl: the lifetime parameter ''a' is not constrained by the impl trait,
      self type, or predicates (E0207)
The file is 40 lines long and `iter_partition_vec` is a STDLIB function. No location is
plumbed at this site, so the blame header names whatever function was last lowered. Same
shape as the standing soundness_queue row `e0184_blame_header_names_unrelated_impl`. A row
closed by a diagnostic pointing at another package's function is not closed; the landing
needs the impl node's own loc before it is worth anything.
Also cosmetic and worth fixing at the same time: the binder prints as `''a'` (the stored
name already carries the leading quote).

## DLOG — THE READER CLASS, ENUMERATED BY PROPERTY, WITH THE PER-SITE CROSS-CHECK
`tools/dlog/selftest.sh` run first: **PASSES**, same known answer (19 walkers / 24 findings /
try_path 1-5 / domain 42-5; duty discriminates 1 -> 0 across 756aed65).
New question `tools/dlog/impl_lt_readers.dl` (NEW RULE — declared here as the instructions
require, with its control): which named contexts reference the impl binders, which of those
can `error`, and which also mention LIFETIME_PARAM.
  implt_read                 11 sites in FOUR contexts —
                             lower_impl_block (2532, 2533, 2557, 2976, 3179, 3235),
                             lower_fn (1044, 1513), compute_fn_lifetime_outlives (448, 576),
                             read_impl_lts (2545)
  implt_reader_can_refuse    lower_fn · compute_fn_lifetime_outlives · lower_impl_block
  implt_reader_sees_ltparam  read_impl_lts · compute_fn_lifetime_outlives · lower_impl_block
CROSS-CHECK, per-site, the two numbers side by side as the instructions require:
`grep -n current_impl_lifetime_params_ src/compiler/sema_decl.cpp` = **11** hits, and dlog
reports **11** reference sites. They agree term for term. What dlog adds and the grep cannot
is the ATTRIBUTION: three of lower_impl_block's six hits (2532/2533/2557) are the
save/clear/restore triple and are not reads of the fact at all, and 2545 is inside the
`read_impl_lts` lambda — a producer. The reader class is therefore THREE contexts, of which
exactly one also holds the self type and the trait-arg node. The arm's placement is not a
choice.
⚠ `ctx_of` coarsens, and this question is deliberately CONTEXT-level ("which function reads
it"), which is the granularity `ctx_of` answers correctly; it is not the site-level question
that produced the 37-vs-0 divergence.

## TOOL CORRECTION, MEASURED — `scripts/run_oracle.py` TAKES ITS OUTPUT PATH AS argv[1]
Driving it with a `>` redirect instead produces a table of the wrong provenance. A first
attempt produced base/armed tables whose join showed **19+ fixtures damaged** — drop-count
programs with a changed stdout SHA, several with ccrc 1. A per-site read of the first of
them, `tests/logos/pass/destruct_field_nested_owned.logos`, compiled **rc 0 under the arm
and rc 0 unarmed**. The table was a broken channel, not a measurement (rule 11), and the
per-site cross-check is what caught it. Re-run with the argument, both passes, on ONE build.
