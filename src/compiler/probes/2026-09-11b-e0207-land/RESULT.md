# RESULT — 2026-09-11b, `lifereg.NEW-E0207` LANDED

site: `src/compiler/sema_decl.cpp::lower_impl_block` (the one context dlog's
      `impl_lt_readers` enumeration put the arm in — see 2026-09-11a).
diff:  76 added / 0 removed in ONE compiled file. Two candidates were written and the
       SECOND was landed: the first carried a duplicated NAME-string walker (+35 lines)
       purely to size the type half, and its census bucket `e0207.impl.with_lt_binders`
       counted every impl rather than what its name says. The number it produced is kept
       in the record with its known-answer control; the crude walker is not kept in the
       compiler.

## CLOSED SET, DIFFED BOTH WAYS
PREDICTED (written before the compiler was touched, PREDICTION.md): exactly
`missing-lifetime-in-assoc-type-1`, `-5`, `-6`.
MEASURED: exactly those three. **Both differences EMPTY.**
Each row's diagnostic was READ, not inferred from an exit code:

    …/missing-lifetime-in-assoc-type-1.logos:38: error [impl Tr for $ref_S]:
      the lifetime parameter `'a` is not constrained by the impl trait,
      self type, or predicates (E0207)

and the same sentence at `-5` (line 30) and `-6` (line 33) — each file's own `impl` header.

## COST, EVERY COLUMN
    `-L bc` (gate-run)     2751 passed / 0 failed of 2753 recorded — the +10 are this
                           round's own fixtures and all ten pass.
    run_oracle.py          6646 rows joined base vs armed, ONE differs:
                           `cast-region-to-uint`, the named stack-address exclusion.
                           COST 0 of 6646.
    fail_text_oracle.py    1485 rows, ONE population (reverted and landed binary, same
                           configure), both differences EMPTY. SEVEN rows differ: the three
                           imported rows and the four new native fail fixtures, each rc 0 /
                           unmatched -> rc 1 / matched. 0 of the 1478 pre-existing moved.
    stdlib                 all four layers build. STRUCTURAL zero, claimed as such:
                           `grep -rnE "^ *impl<[^>]*'" --include=*.logos stdlib` = 0.

## THE ROUND'S REAL FINDING — THE SCOPE OF THE SKIP MASK, CAUGHT BY A COUNTER-EXAMPLE
On the FIRST armed build, three LEGAL hand programs were REFUSED: `impl<'a> Tr3<'a> for S`,
`impl<'a> Tr2<W<'a>> for S`, `impl<'a> Tr for W<'a>`. The mask that excludes the binder
list and the body names DIRECT children of the impl node, and I carried it into the
recursion — where `ITEMS` is a generic's own argument list. Every lifetime nested one level
down was invisible. `&'a mut S` worked throughout, which is why it looked correct: the
depth-1 case passes and reads like the general case. The pricing round's four zero columns
and seven correct hand verdicts could not see it, because none of its shapes nested a
lifetime (rule 5 — vary the SHAPE, not the count).

## THE CLASS, BY THE PROPERTY
Census at the site over 9839 `.logos` files and 6,341,664 impl-lowering arrivals:
7 unconstrained-and-named-by-an-assoc-type · 14 unconstrained-and-not-named (LEGAL in
Rust, all 14 admitted) · **0** unconstrained TYPE parameters. Known-answer control for
that last counter: the hand program `impl<U> Tr for S { type Item = U; }` makes it read 2,
so the site is live and the zero is real.

## DECLINED, BY NAME, WITH THE NUMBER
· **The TYPE half of E0207** — population 0 in the whole tree, closes no ledger row and no
  queue row. It is now `soundness_queue` row `e0207_unconstrained_impl_type_param` (tier 2,
  `admits`) so it stays findable instead of being carried as prose.
· **A `where`-only E0207** — `WHERE` counts as constraining in the landed walk. Rust's rule
  is narrower (only a projection predicate constrains); with no rustc on this box the two
  cannot be separated from the AST, and the conservative direction is ADMIT.
· **The general stale-blame defect** — fixed at this site only; it stays open as
  `soundness_queue` row `e0184_blame_header_names_unrelated_impl`.
