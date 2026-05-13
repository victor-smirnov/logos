# Track 3 — gap-per-imported-test trend

The headline metric: **how many catalogued gaps does an average imported
rustc test surface?** Phase 1 ran at ~2 gaps/test (every test tripped
something new). The trend should fall as Logos's surface grows.

## Per-batch ledger

Counted *at the time the batch landed*.

- **Gaps** = new catalog entries (any status — Closed, Partial, Open,
  Divergence) emitted between the previous batch's commit and this
  batch's commit. Retroactive fixes that silently un-trim earlier
  imports don't count here (logged in `docs/track3-gaps/*-gaps.md`
  instead).
- **Bugs** = fixes landing in this batch that do *not* show up in
  any gap catalog: silent miscompiles, codegen ABI bugs, parser
  off-by-ones, mono regressions, mangling collisions, stdlib
  inconsistencies, etc. Counted per *root cause* (one logical
  fix = one bug, even if it took multiple commits or touched
  multiple files). Use `—` when the count couldn't be reconstructed
  reliably from the batch's commit/history (early batches were
  import-only and didn't track this).

| Batch | Date | Tests | New gaps | Bugs fixed | gaps / test | Cumulative tests | Cumulative gaps | Cum bugs |
|---|---|---|---|---|---|---|---|---|
| B1   | 2026-05-11 | 11 |  ≈ 18 | — | 1.6 | 11  | 18  | — |
| B2   | 2026-05-11 | 11 |  ≈ 14 | — | 1.3 | 22  | 32  | — |
| B3   | 2026-05-11 | 12 |  ≈ 15 | — | 1.3 | 34  | 47  | — |
| B4   | 2026-05-11 |  5 |  ≈ 11 | — | 2.2 | 39  | 58  | — |
| B5   | 2026-05-11 |  3 |  ≈  5 | — | 1.7 | 42  | 63  | — |
| B6   | 2026-05-11 |  5 |  ≈  9 | — | 1.8 | 47  | 72  | — |
| B7   | 2026-05-11 |  3 |  ≈  4 | — | 1.3 | 50  | 76  | — |
| B8   | 2026-05-11 |  6 |  ≈  6 | — | 1.0 | 56  | 82  | — |
| B9   | 2026-05-11 |  2 |  ≈  5 | — | 2.5 | 58  | 87  | — |
| B10  | 2026-05-11 |  3 |  ≈  3 | — | 1.0 | 61  | 90  | — |
| B11  | 2026-05-11 |  3 |  ≈  3 | — | 1.0 | 64  | 93  | — |
| B12  | 2026-05-11 |  6 |     1 | — | 0.17 | 70  | 94  | — |
| B13  | 2026-05-11 |  4 |     0 | — | 0.00 | 74  | 94  | — |
| B14  | 2026-05-11 |  4 |     1 | — | 0.25 | 78  | 95  | — |
| B15  | 2026-05-11 |  4 |     0 | — | 0.00 | 82  | 95  | — |
| B16  | 2026-05-11 |  3 |     1 | — | 0.33 | 85  | 96  | — |
| B17  | 2026-05-11 |  5 |     0 | — | 0.00 | 90  | 96  | — |
| B18  | 2026-05-11 |  3 |     0 | — | 0.00 | 93  | 96  | — |
| B19  | 2026-05-11 |  5 |     2 | — | 0.40 | 98  | 98  | — |
| B20  | 2026-05-11 | 12 |     6 | — | 0.50 | 110 | 104 | — |
| B21  | 2026-05-11 |  9 |     0 | — | 0.00 | 119 | 104 | — |
| B22  | 2026-05-11 |  4 |     2 | — | 0.50 | 123 | 106 | — |
| B23  | 2026-05-11 |  7 |     0 | — | 0.00 | 130 | 106 | — |
| B24  | 2026-05-11 |  3 |     0 | — | 0.00 | 133 | 106 | — |
| B25  | 2026-05-11 |  2 |     0 | — | 0.00 | 135 | 106 | — |
| B26  | 2026-05-11 |  4 |     0 | — | 0.00 | 139 | 106 | — |
| B27  | 2026-05-11 |  2 |     0 | — | 0.00 | 141 | 106 | — |
| B28  | 2026-05-11 |  1 |     0 | — | 0.00 | 142 | 106 | — |
| B29  | 2026-05-11 |  2 |     0 | — | 0.00 | 144 | 106 | — |
| B30  | 2026-05-11 |  1 |     0 | — | 0.00 | 145 | 106 | — |
| B31  | 2026-05-11 |  1 |     0 | — | 0.00 | 146 | 106 | — |
| B32  | 2026-05-11 |  1 |     0 | — | 0.00 | 147 | 106 | — |
| B33  | 2026-05-11 |  1 |     0 | — | 0.00 | 148 | 106 | — |
| B34  | 2026-05-11 |  2 |     0 | — | 0.00 | 150 | 106 | — |
| B35  | 2026-05-12 |  1 |     0 | — | 0.00 | 151 | 106 | — |
| B36  | 2026-05-12 |  1 |     0 | — | 0.00 | 152 | 106 | — |
| B37  | 2026-05-12 |  0 |     0 | — | 0.00 | 152 | 106 | — |
| B38  | 2026-05-12 |  2 |     0 | — | 0.00 | 154 | 106 | — |
| B39  | 2026-05-12 | 13 |     0 | — | 0.00 | 167 | 106 | — |
| B40  | 2026-05-12 |  5 |     0 | — | 0.00 | 172 | 106 | — |
| B41  | 2026-05-12 |  3 |     0 | — | 0.00 | 175 | 106 | — |
| B42  | 2026-05-12 |  5 |     0 | — | 0.00 | 180 | 106 | — |
| B43  | 2026-05-12 |  2 |     0 | — | 0.00 | 182 | 106 | — |
| B44  | 2026-05-12 |  3 |     0 | — | 0.00 | 185 | 106 | — |
| B45  | 2026-05-12 |  6 |     0 | — | 0.00 | 191 | 106 | — |
| B46  | 2026-05-12 |  3 |     0 | — | 0.00 | 194 | 106 | — |
| B47  | 2026-05-12 |  3 |     0 | — | 0.00 | 197 | 106 | — |
| B48  | 2026-05-12 |  3 |     0 | — | 0.00 | 200 | 106 | — |
| B49  | 2026-05-12 |  5 |     0 | — | 0.00 | 205 | 106 | — |
| B50  | 2026-05-12 |  2 |     0 |  1 | 0.00 | 207 | 106 |  1 |
| B51  | 2026-05-12 |  1 |     0 |  0 | 0.00 | 208 | 106 |  1 |
| B52  | 2026-05-12 |  3 |     0 |  0 | 0.00 | 211 | 106 |  1 |
| B53  | 2026-05-12 |  2 |     0 |  0 | 0.00 | 213 | 106 |  1 |
| B54  | 2026-05-12 |  3 |     0 |  1 | 0.00 | 216 | 106 |  2 |
| B55  | 2026-05-12 | 36 |     0 |  0 | 0.00 | 252 | 106 |  2 |
| B56  | 2026-05-12 |  1 |     1 |  0 | 1.00 | 253 | 107 |  2 |
| B57  | 2026-05-12 | 53 |     5 |  0 | 0.09 | 306 | 112 |  2 |
| B58  | 2026-05-12 |  3 |     0 |  3 | 0.00 | 309 | 112 |  5 |
| B59  | 2026-05-12 |  2 |     0 |  2 | 0.00 | 311 | 112 |  7 |
| B60  | 2026-05-12 |  2 |     0 |  1 | 0.00 | 313 | 112 |  8 |
| B61  | 2026-05-12 |  0 |     0 |  1 |  —   | 313 | 112 |  9 |
| B62  | 2026-05-12 |  0 |     0 |  1 |  —   | 313 | 112 | 10 |
| B63  | 2026-05-12 |  0 |     0 |  1 |  —   | 313 | 112 | 11 |
| B63.2| 2026-05-12 |  0 |     0 |  1 |  —   | 313 | 112 | 12 |
| B63.3| 2026-05-12 |  0 |     0 |  1 |  —   | 313 | 112 | 13 |
| B65  | 2026-05-12 |  0 |     0 |  2 |  —   | 313 | 112 | 15 |
| B64  | 2026-05-12 |  0 |     0 |  1 |  —   | 313 | 112 | 16 |
| B64.2| 2026-05-12 |  0 |     0 |  0 |  —   | 313 | 112 | 16 |
| B66  | 2026-05-12 | 12 |     0 |  3 | 0.00 | 325 | 112 | 19 |
| B66.1| 2026-05-12 |  1 |     0 |  0 |  —   | 326 | 112 | 19 |
| B67  | 2026-05-12 |  0 |     0 |  1 |  —   | 326 | 112 | 20 |
| B68  | 2026-05-12 |  2 |     0 |  1 |  —   | 328 | 112 | 21 |
| B68.1| 2026-05-12 |  1 |     0 |  1 |  —   | 329 | 112 | 22 |
| B68.2| 2026-05-12 |  0 |     0 |  2 |  —   | 329 | 112 | 24 |
| B68.3| 2026-05-12 |  0 |     0 |  1 |  —   | 329 | 112 | 25 |
| B69  | 2026-05-12 |  1 |     0 |  1 |  —   | 330 | 112 | 26 |
| B70  | 2026-05-12 |  0 |     0 |  0 |  —   | 330 | 112 | 26 |
| B71.0| 2026-05-12 |  0 |     0 |  0 |  —   | 330 | 112 | 26 |
| B71.1| 2026-05-12 |  0 |     0 |  0 |  —   | 330 | 112 | 26 |
| B71.2| 2026-05-12 |  0 |     0 |  0 |  —   | 330 | 112 | 26 |
| B71.3| 2026-05-12 |  0 |     0 |  0 |  —   | 330 | 112 | 26 |
| B72  | 2026-05-12 |  0 |     0 |  1 |  —   | 330 | 112 | 27 |
| B73  | 2026-05-12 |  0 |     0 |  0 |  —   | 330 | 112 | 27 |

(Phase-1 gap counts are estimates — pre-batch gap-as-code triage gave
coarse totals only; precise per-batch arrival-order numbers weren't
recorded. Phase-2 onward — from B12 — gap counts are exact. **Bug
counts** start from B50 — earlier batches were import-driven and
incidental bug fixes weren't separated from gap-closure commits;
backfilling reliably isn't possible. From B50 onward each batch's
commit message lists the bugs it touched, and the column counts
root-cause fixes.)

Gaps observed in B57 (lifetime/HRTB/GAT sweep):
- **L1** — `Trait<'a>` as a fn / where bound triggers "unexpected
  type node code 131" — parametric trait-ref at bound position
  hits a type-pool fold path that doesn't expect LIFETIME_PARAM at
  the trait_bound's TYPE_PARAMS slot. Largest single blocker
  (5+ tests including `regions-self-impls`, `regions-early-bound-trait-param`,
  `regions-trait-1`). Probably one-spot fix in the bound-arg
  resolver.
- **L2** — `&'a [T]` slice with explicit lifetime: grammar admits
  `&'a T` and `&[T]` but the combination `&'a [T]` falls between
  the slice and ref alts. ~3 tests blocked (slice-heavy lifetime
  tests like `regions-dependent-autoslice`).
- **L3** — `'_` underscore-lifetime token not accepted at type
  position or in impl-header position. ~4 tests blocked.
- **L4** — multi-input-ref elision picks no lifetime even when
  exactly one input could match the output ref (e.g.
  `fn f<'a, 'b>(x: Foo2<'a, 'b>) -> &'b u8` rejected). False
  negative — Logos's elision rule is more conservative than Rust.
- **L5** — `Option<i64>` through `&Option<T>` in generic match arm
  with `ref v` binding SIGSEGVs at runtime — codegen ABI mismatch
  (fat-ptr vs by-value). Not lifetime-related; surfaced by
  `regions-return-interior-of-option`. Worked around in B57 by
  switching the imported test to by-value scrutinee.

Closed in B60 (lifetime epic Phase 6):
- **GAT projection `Self::Item<X>` in impl method body/signature** —
  previously failed with "no associated type 'Item' found for 'X'"
  because `impls_["Trait::Target"]` isn't populated until
  collect_impl finishes, but the method signature is type-checked
  DURING collect_impl. Fix: new `current_impl_trait_name_` scope on
  SemaChecker, set by both collect_impl + lower_impl_block via an
  RAII restore guard. resolve_type's ASSOC_TYPE_REF path consults
  it directly to find the assoc-type definition on the impl's
  trait — bypassing the chicken-and-egg impls_ lookup. Works for
  both type-arg (`Item<U>`) and lifetime-arg (`Item<'a>`) GATs.

Closed in B61 (lifetime epic Phase 9):
- **NLL — flow-sensitive borrow check (minimum viable)** ✅ —
  borrows now release at the holder's last use, not at lexical
  scope end. Implementation: `BorrowRecord.holder` records the
  let/assign LHS that binds a `&x`/`&mut x`; a pre-pass walks the
  fn body computing `last_use_line_[v]` for every named local;
  `release_dead_borrows(cur_line)` runs after each statement in
  every block and erases borrows whose holder's max-use line is
  ≤ cur_line (un-flipping `mut_borrowed` / decrementing
  `shared_borrows`). Repro:
  ```
  let mut x = 1; let r = &mut x; *r = 2; let s = &x; *s
  ```
  Six legacy fail-tests (double_mut_borrow, cond_ref_borrow,
  mut_borrow_blocks_move, shared_then_mut_borrow,
  shared_while_mut_borrowed, use_while_mut_borrowed) had unused
  holders — under NLL their borrows die immediately and the
  conflicts are *correctly* accepted. Updated each test to use
  the holder after the offending line so it remains a real fail-
  test. Regression: `pass/nll_mut_then_shared.logos`. 1547/1547.

Closed in B62/B63 (lifetime epic Phase 5 — substantive):
- **Bound-shape region match** ✅ (B62) — at sema-time, when checking
  `T: SomeTrait<&'a U>` against `impl SomeTrait<&'X U> for T`, the
  trait-arg region must be compatible. Impl-side concrete regions
  (e.g. `'static`) no longer satisfy a bound whose trait-arg uses
  a generic-position lifetime — i.e. an impl that pins to one
  region cannot stand in for a `for<'a>`-style universal bound.
  Implementation: `SemaImplInfo` extended with `trait_type_args`,
  `trait_lifetime_args`, `impl_lifetime_params`; populated in
  `collect_impl`. The sema bound-check site
  (`sema_collect.cpp::check_type_bounds`) gains an inline
  `region_ok` predicate; mirrored onto `LImplBlock` + Mono's
  `method_bound_ok` for struct-method clones.
- **Impl-tie injectivity (B63)** ✅ — the region_ok walk now
  recurses through Ref/MutRef pointees and Struct/Enum
  `type_args`, building an impl-region → bound-region map.
  Reverse-direction injectivity: if the impl uses the SAME
  lifetime in two trait-arg positions, the bound must use the
  same binder there — else bound binders collapse to a single
  impl param and the impl is too restrictive to satisfy
  independent binders. Forward direction is intentionally not
  enforced: a tied bound `for<'z> Foo<&'z, &'z>` IS satisfied
  by an independent impl `impl<'a,'b> Foo<&'a, &'b>` (impl is
  strictly more general — set 'a='b=z).

  Calibration tests:
  - `fail/hrtb_concrete_impl_universal_bound.logos` — concrete-
    region impl vs generic-region bound.
  - `fail/hrtb_conflate_regions.logos` (rustc-derived) —
    `impl<'a> Foo<&'a, &'a>` rejects `for<'a,'b> Foo<&'a, &'b>`.
  - `pass/hrtb_uniform_binder_satisfies.logos` — uniform shape
    matches.
  - `pass/hrtb_two_independent_binders.logos` — independent
    binders + independent impl params.
  - `pass/hrtb_tied_bound_indep_impl.logos` — tied bound, more-
    general impl.

  Source: rust-lang/rust `tests/ui/higher-ranked/trait-bounds/
  hrtb-conflate-regions.rs`. 1553/1553.

Polished in B63.2:
- **Explicit `for<>` binder capture** ✅ — grammar's `hrtb_binder`
  wraps each lifetime in a sub-rule `hrtb_lt → LIFETIME` so the
  parent's `$...` collector records them; ast.hpp gains
  `HRTB_BINDERS` (slot 41, reuses IMPL_TYPE_PARAMS since trait
  bounds never carry impl-type-params); `read_trait_bound_args`
  populates `TraitBound.hrtb_binders`. Error messages now name
  the binder list explicitly: `Trait for<'a, 'b>` bound …
- **Exact-region match** ✅ — a `'static`-pinned bound with a
  `'static`-pinned impl is correctly accepted (previously rejected
  because impl-side `'static` isn't in `impl_lifetime_params`).
  Calibration: `pass/hrtb_static_bound_static_impl.logos`.

Polished in B63.3:
- **HRTB at fn-ptr / dyn-trait type position** ✅ — grammar now
  admits `for<'a> fn(&'a T) -> R` and `dyn for<'a> Trait<&'a T>`
  (plus `&dyn`, `&mut dyn` variants). `HRTB_BINDERS` field
  captured on FN_PTR_TYPE / DYN_TYPE nodes. Sema's DYN_TYPE
  resolution skips the binder sub-node (no CODE) and
  LIFETIME_PARAM entries when walking ITEMS. Regression:
  `pass/hrtb_fn_ptr_type.logos`, `pass/hrtb_dyn_trait_type.logos`.

B64.2 — extended subtype gate to all coercion sites:
- `check_variance(from, to, ctx)` helper on SemaChecker emits a
  uniform "variance mismatch" error after `types_compatible`
  accepts but `subtype` rejects.
- Wired at: return statement, let-init with annotation,
  argument-pass for free fns / generic fns / methods / closure
  calls / tuple-struct ctor / fn-ptr calls.
- Outlives permissiveness: when both lifetimes are named
  generic regions and no constraint connects them, accept (caller's
  region inference would pick a unification). The check still
  rejects concrete-vs-named-static violations and any &mut /
  raw-ptr invariance failures.
- Calibration: `fail/variance_arg_mut_inv.logos` — passing
  `&mut &'static i32` to a fn expecting `&mut &'a i32` rejects.
  1562/1562.

Closed in B64 (lifetime epic Phase 8 — substantive):
- **Variance computation + variance-aware subtype** ✅ — per
  struct / enum / datatype, variance per type-param and per-
  lifetime-param computed via fixed-point over field types
  (`Variance` lattice: BiVar < Co | Contra < Inv, with compose
  for nesting and meet for join). Lives in
  `include/logos/compiler/variance.hpp` (lattice + table) and
  `SemaChecker::compute_variances()` (sema.cpp) — single pass
  over `structs_` / `datatypes_` / `enums_` iterated to fixed
  point (capped at 32 rounds).
  - Subtype check (`include/logos/compiler/subtype.hpp`):
    Co on `&T` and tuple/slice/array elements; Inv on `&mut T`
    pointee and raw ptr; Contra on fn-ptr params, Co on fn-ptr
    ret; per-struct variance from the computed table (default
    Co when entry missing — matches Rust's most-common case).
    Lifetime positions consult the outlives query from B65.
  - Wired at the return statement coercion site: if
    `types_compatible` accepts but `subtype` rejects, emit a
    "return type mismatch (variance)" error.
  - Calibration: `fail/variance_mut_ref_inv.logos` —
    `&mut &'static i32` ↛ `&mut &'a i32` (MutRef invariance);
    `pass/variance_ref_covariant.logos` — `&&'static i32` →
    `&&'a i32` legal. Existing
    `imported/pass/variance/variance-iterators-in-libcore.logos`
    continues to pass via the variance fixed-point on wrapper
    structs.
  1561/1561.

Closed in B65 (lifetime epic Phase 7):
- **Outlives reasoning** ✅ — explicit `'a: 'b` clauses (header
  and where), implied bounds from nested refs, type-outlives
  bounds (`T: 'a`), and the transitive outlives query land
  end-to-end.
  - Grammar: `outlives_lt` sub-rule wraps each outlives lifetime
    so `$...` captures them (previously dropped on the floor).
  - Schema: `LFunction/LStructDef/LEnumDef/LImplBlock` gain
    `lifetime_outlives: vec<(longer, shorter)>`. `TypeParam`
    gains `lifetime_outlives: vec<string>` for `T: 'a` bounds.
  - Capture: `read_lifetime_outlives_from(node, slot)` (works for
    both TYPE_PARAMS and WHERE slots). Implied bounds inferred by
    walking fn param / ret types and emitting (inner, outer)
    pairs whenever a Ref or Struct lifetime arg appears under
    another lifetime context.
  - Query: `include/logos/compiler/outlives.hpp` — BFS over the
    declared graph with `'static` as top, reflexive, transitive.
  - Validation: every lifetime mentioned in an outlives clause
    must be declared (or `'static`); else "use of undeclared
    lifetime name" — same diagnostic Rust produces. Closes the
    silent-accept gap for `fn f<'a>() where 'a: 'b`.
  - Calibration: `fail/outlives_undecl_lt.logos`,
    `fail/outlives_undecl_type_lt.logos`,
    `pass/outlives_declared.logos`. 1559/1559.
  - Hookpoints ready: the query is consumed by B64 (variance +
    subtype) and will later feed region inference if/when a real
    region-based borrow analyser replaces the min-viable NLL.

Lifetime epic — current status:
- HRTB binder semantics (B62/B63/B63.2/B63.3) ✅
- Outlives reasoning (B65) ✅
- Variance + variance-aware subtype (B64/B64.2) ✅
- NLL min-viable (B61) ✅

Region inference (B70 → B71.3 — orthogonal phase, in progress):
- B70: scaffolding — RegionId, StmtPoint, BorrowSite,
  RegionConstraint, CFG; per-fn analysis walks LStmt + LExpr via
  lir_view and assigns a fresh region to every `&x` / `&mut x` /
  `&temp` with a Contains-at-origin seed constraint. Wired behind
  LOGOS_DUMP_REGIONS env var.
- B71.0: multi-block CFG — branch / loop / match / let-else / block
  / return / break / continue all produce proper successor edges
  (verified with if-with-borrow probe → diamond CFG).
- B71.1: per-statement liveness via backward dataflow over the CFG
  (use/def + live-in/live-out, fixed-point capped at 64 rounds).
- B71.2: Contains constraints from holder liveness — a borrow's
  region must include every point where the holder is in live_in.
- B71.3: constraint solver — fixed-point growth of each region's
  point set; verified region 1 (`&mut x → r`) covers exactly its
  arm, region 2 (`&y → s`) covers exactly its arm.

Remaining (Polonius-style work):
- B72: wire the region-based conflict checker as the canonical
  borrow analyser, replacing the B61 min-viable NLL (last-use
  release). Conflicts emerge when two overlapping region point
  sets target the same var and at least one is `is_mut`.
- B73: region-blame diagnostics ("this borrow flows here, but is
  needed there").
- B74: import the `tests/ui/nll/*` corpus from rustc — the
  acceptance/rejection oracle for B72 + B73.

Closed in B59:
- **L4** ✅ — multi-input-ref elision: borrow check's
  `check_return_value` was rejecting `fn f<'a, 'b>(x: Foo2<'a, 'b>)
  -> &'b u8` because prov_of(`x.y`) traces to param `x`, but `x`
  itself isn't a ref (struct param) so `param_lifetimes_` has no
  entry. Fix: when the traced param is missing from `param_lifetimes_`,
  trust the type checker — it already verified the FieldRead's
  declared lifetime matches the return annotation. Ref params with
  empty (elided) lifetime still produce the original "(elided)
  mismatch" error.
- **L5** ✅ — `&Option<T>` + `ref v` binding + `*v`: two-part fix.
  (sema) `build_pattern`'s binding-type substitution now auto-derefs
  Ref/MutRef/Ptr to the inner Enum so the pattern binding gets the
  concrete payload type instead of a TypeVar — previously `v` was
  typed `T` (typevar) instead of `i64`, leading to a type-check
  diagnostic. (codegen) `gen_addr_of` for a tagged-enum non-mut let
  spills the enum-struct ptr to a stack slot on first `&o` and
  marks the var as slot-backed; subsequent reads load through the
  slot. Previously the var held the enum-ptr SSA value directly,
  and `&o` returned that SSA value as if it were a ptr-to-ptr —
  match's via_ref load then dereferenced garbage and SIGSEGV'd.

Closed in B58:
- **L1** ✅ — `Trait<'a>` bound: TraitBound gains `lifetime_args`;
  `read_trait_bound_args` (sema.cpp) routes LIFETIME_PARAM there
  instead of resolve_type. `lower_impl_block` (sema_decl.cpp) +
  `impl_collect`'s trait-type-args walker (sema_collect.cpp) skip
  LIFETIME_PARAM entries. Logos's trait dispatch ignores lifetime
  args at the resolver (consistent with lifetime-erasure-for-dispatch).
- **L2** ✅ — `&'a [T]` / `&'a mut [T]` slice with explicit lifetime:
  grammar `slice_type` adds two leading alts that capture LIFETIME
  in the SLICE_TYPE node.
- **L3** ✅ — `'_` underscore-lifetime: peg_gen lexer template
  (codegen.cpp) extended to admit underscore as the first char
  after `'` when the LIFETIME regex char-class includes `_`. LIFETIME
  regex updated to `'[a-z_][a-z0-9_]*`.

Bugs counted so far:
- **B50** — `slice_index` struct-element ABI: GEP stride was
  `logos_to_mlir(Struct) == ptr_type()` (8B) instead of the
  aggregate's actual `sizeof(Struct)`. Affected `[Struct;N]` array
  destructure (P4-pm-15 close surfaced it). Fix: detect struct/
  ZonedStruct element type and GEP with the LLVM struct type.
- **B54** — duplicate `str_eq` mangling collision: stdlib's
  `std.sys.args` carried a private `fn str_eq(a: str, b: str)`
  that mangled identically to the new `pub fn str_eq` in
  `std.lang.text.string`; mlir-gen rejected "duplicate function
  body for symbol …". Fix: delete the private copy.

## Reading

Phase 1 (B1–B11) baseline: **~1.5 gaps/test**. Many tests surfaced
multiple gaps because Logos's pattern, coercion, fn-family, generics,
and metaprog surfaces were all rough at once.

Phase 2 (B12+): trending toward **~0.2 gaps/test** as the easy/middling
surfaces close. New gaps are mostly narrow codegen issues (e.g.
P4-pm-17 ref-bind deref chain) or specialised patterns (tuple-rest,
exclusive ranges). Single gap can block multiple tests, so total
unblocked-tests-per-gap-closure is also worth eyeballing.

Phase 3 (B29–B35): "arc closure" batches — work was gap-closure-driven,
not import-driven. Each batch lands a slice of the Sprint 5.8 dyn-arc
(C6-cc-09 / C6-cc-08 / C5-cl-04 / C5-cl-08) and re-instates the rustc
test that originally surfaced the gap. Zero net new catalog entries
since every closure here re-fills an "Open" status flipped earlier.
The Sprint 5.8 dyn-arc is fully closed at B35.

Phase 5 (B50–B52): targeted gap-closure run. B50 closed
P4-pm-15 (array destructure Drop case — slice_index struct-element
fast path + move-track temp/RHS) and P3-pg-04 (break-as-expression
codegen — EBlockExpr + SBreak + terminated-block tolerance in
EBlockExpr / EIfExpr branches). B51 partial-closed P4-pm-01
(struct-shape enum variants) — declaration, construction, and
irrefutable struct-shape match patterns land end-to-end, including
shorthand, rename, `..` rest, and missing/duplicate/unknown-field
diagnostics. New IS_STRUCT_SHAPE flag (slot 47, reuses LABEL) on
VARIANT_DEF / ENUM_LIT_DATA / PAT_VARIANT_DATA; SemaVariantInfo
gains `payload_field_names` parallel to `payload_types`. mlir-gen
unchanged — sema resolves names → positions, downstream stays
positional. Still open: refutable inner patterns + let-destructure
with single-variant enum. ctest 1439 → 1442. Catalog flips: 2
Partial → ✅ Closed, 1 Deferred → Partial. B52 fully closed
P4-pm-01 — refutable inner literal patterns (PAT_INT,
PAT_NEG_INT, PAT_BOOL, PAT_CHAR) now lower via synthesized
`__refut_* == <value>` arm guards in BOTH tuple-shape and
struct-shape variant payloads (so `Option::Some(1)` works
alongside `E::V { f: 1 }`); single-variant struct-shape
let-destructure (`let E::V { f } = e;`) lowers as a temp
plus per-binding match-as-expression. Three rustc tests
un-trimmed: `issue-8351-1`, `issue-8351-2`, `issue-11577`.
IS_STRUCT_SHAPE moved into the new `variant` key group
(slot 47, sibling of `mod`) for cleaner slot documentation.
ctest 1442 → 1445. Catalog: P4-pm-01 Partial → ✅ Closed.
B53 closed two P4-pm-01 follow-ups: P4-pm-24 (variant pattern
as tuple-pattern element — sema allow-list extension + per-element
disc-check in mlir-gen tuple-arm dispatch, both stmt and expr
forms) and P4-pm-25 (or-patterns in match arm LHS — sema
fan-out when any alt is a variant pattern, so each alt goes
through the normal single-arm path with its own refutable
guard and payload extraction; mixed-shape `Opaque {with: true, ..}
| Transparent` now works correctly). Two more rustc tests
un-trimmed: `issue-5530`, `issue-114691`. ctest 1445 → 1447.
Catalog: P4-pm-24 Open → ✅ Closed, P4-pm-25 Open → ✅ Closed.
B54 closes the last three Partials in pattern-match-gaps: P4-pm-03
(or-pattern at tuple element via grammar + sema dispatch + mlir-gen
OR-chain — single-alt PAT_OR unwraps so existing tuple-elem
binding/scalar shapes still parse identically), P4-pm-06 (str-typed
const-pattern via synth `__str_<n>` binding + `str_eq(__str_<n>, CONST)`
guard pushed to refutable-guard side channel; new `pub fn str_eq` in
`std.lang.text.string`, parallel private copy in `std.sys.args`
deleted to avoid mangling collision), and P4-pm-07 (`b"…"` at
expression position via new LIT_BYTES AST code that decodes escapes
parity-with-PAT_BYTES and emits `[u8; N]` ArrLit; const-init allow-list
extended). Three more rustc ports un-trimmed: `issue-11940` (str-const
pattern), `issue-72680-g` (or-pat inside 4-element bool tuple), and a
homegrown `byte_string_expr` regression. ctest 1447 → 1449 (plus the
72680-g import → 1450). Catalog: P4-pm-03 / P4-pm-06 / P4-pm-07 all
Partial → ✅ Closed. **All pattern-match-gaps now Closed**, and
the only remaining cross-catalog open work is small follow-ups
documented inline.

Phase 6 (B55): import sweep through under-explored dirs. 36 new
tests across numbers-arithmetic, expr, for-loop-while, mir, tuple,
consts, traits, autoref-autoderef, moves, pattern, recursion,
functions-closures, binop, array-slice-vec. Zero new gaps caught,
zero bug fixes — all imports went in clean modulo the usual
isize→i64 / `assert!`-to-conditional-return / Box-skip adaptations.
Several test candidates skipped on the agent's pass with the same
flavour as Phase 4: features that conflict with Logos design
(unsafe trait markers, Box-heavy iterator chains, struct-variant
enums in the few corners P4-pm-01 doesn't reach via the let-pat
path, vec!-macro tests until that lands). Re-confirms that the
gap surface is genuinely drained: a 36-test sweep produces no
new catalog entries. ctest 1450 → 1484.

Phase 4 (B38–B49): "autonomous bulk-import" run. Single overnight
session through under-imported dirs (binding, match, for-loop-while,
typeck, drop, mir, moves, attributes, inference, tuple, generics,
binop, enum, coercion, closures, issues). 50+ tests added, zero new
gaps catalogued — Phase 2/3 had already drained the easy surface,
so the remaining stalls were variants of known gaps (struct enum
variants, Box/Vec ABI corners, FnMut indirection edge-cases) which
the batch silently skips. ctest 1389 → 1439.

Update this table whenever a numbered batch lands.
