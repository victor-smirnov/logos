# TARGETS — 2026-09-10a-selfrecvland, written BEFORE the compiler was touched

Base build `911811129fca26d5 43`, HEAD `1fad68657`, tree clean at STEP 1.
Queue gate rc **0**, 75 rows (tier1 19 / tier2 9 / tier3 40 / tier4 7), `# TOTAL` 75.
`bc_admits` 90, `bc_admits_blocked` 8. probe-log-lint: 264 records, every site symbol resolves.

## SUBJECT (fixed by the prompt, not selected)

LAND `sigselfty` — the RECEIVER slot of the impl-vs-trait signature conformance
check, priced to completion in `2026-09-09k-selfrecv` §9 (fires 3128198,
ceiling 0, cost 8, cfail 0 of 1478, stdlib clean, runtime 30/6610 → 29 after
`cast-region-to-uint`). Owner decision 2026-09-09: receiver is Rust-canonical.

## ORDER, AND WHY

 1. **The 90-file measurement first** — it is the only thing that could turn the
    landing into an owner escalation, and it costs one hand pair.
 2. **The BLOCKING queue-row rewrite**: `tests/soundness/open/
    unsized_local_binds_place_dropped_after_free.logos` carries an INCIDENTAL
    `fn drop(self: A)`. Armed it is refused for the RECEIVER, its `admits` row
    goes red, and the gate reads that as the row being CLOSED. It must be
    rewritten before the arm lands.
 3. **Corpus + spec, on the UNMODIFIED compiler, its own commit, proven green**
    — 30 pass fixtures whose stdlib-`Drop` impl spells a by-value receiver, plus
    the three spec clauses the landing falsifies.
 4. **The arm, its own commit, its own control revert.**

## TARGET FIXTURES BY NAME (the 29, from `tables/run_oracle_diff_sigselfty.txt`)

The 33 files carrying a by-value `Drop` receiver bound to the STDLIB `Drop`
(`tables/files_byvalue_no_local_trait.txt`); three of them are `fail` fixtures
already refused for another reason and text-unchanged (cfail 0 of 1478):
`tests/spec/fail/borrow_diag_2__ref-from-temp`, `tests/logos/fail/
auto_trait_fst_drop`, `tests/logos/fail/borrow_temp_dropped_while_borrowed`.

## SPEC CLAUSES THE LANDING OWES

 * `type.drop.receiver-shapes` (docs/spec/types.md) — says the drop method is
   matched "by value, `&T`, or `&mut T`". True of the GLUE MATCHER, and after
   the arm no longer reachable for the stdlib `Drop` trait, whose declaration is
   `&mut Self`.
 * `intrinsic.drop.owner-drops-fields-after-user-drop` (docs/spec/expressions.md)
   — still says "a nested drop calls only the user `impl Drop` and stops, because
   the by-value `self` consumes its own fields". `1979d72f4` deleted that
   behaviour and `e6a13b523` repaired the two `expr.drop.*` twins but MISSED this
   `intrinsic.*` one. Its reason is the one this landing deletes outright.
 * A new clause for impl-vs-trait RECEIVER conformance — the sibling of the
   parameter and return rules, which the tree has never had.

## WHAT IS **NOT** IN SCOPE

 * ⛔ `sigselfnone` — breaks the stdlib at the four `str` DST sites.
 * `sigselfdepth` alone — half a mechanism (misses `&Self` vs `&mut Self`).
 * The 213 by-value impls under a LOCAL `trait Drop` in 90 files — owner's.
