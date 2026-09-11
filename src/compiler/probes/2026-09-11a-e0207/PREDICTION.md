# PREDICTION — written before the armed binary existed

Two names at ONE site (`sema_decl.cpp::lower_impl_block`), because the inner predicate
IS the question (rule 9):

  e0207unc    an impl lifetime binder appearing in neither the resolved self type nor
              the trait args  →  refuse.
  e0207assoc  the same, AND the binder is named by an ASSOCIATED TYPE definition of the
              impl. This is rustc's actual rule.

The two are predicted to be SEPARABLE only on programs the whole-corpus census already
named; the harness columns alone may not split them, so hand programs carry it.

## e0207assoc  — predicted ceiling 3, cost 0
CLOSES, by name: missing-lifetime-in-assoc-type-1 / -5 / -6. Nothing else in either ledger.
HAND: refuses x9_target_shape and x10 (binder 'b only); admits l1..l8.

## e0207unc — predicted ceiling 4, cost >= 1
CLOSES the same three PLUS `trait-associated-constant` (nll, root nllmoves.R18) — and that
fourth is NOT a closed row: upstream's .stderr for it, READ on the box, is **E0308 "const
not compatible with trait"**, not E0207. A row closed by a wrong diagnostic is not closed.
COSTS, by name: tests/logos/pass/bc_mcallvar_legal_twins.logos:50 `impl<'a> W { fn f(self:
&Self, x: &'a i64) -> &'a i64 }` — a binder used only in a METHOD signature, which rustc
ALLOWS (its `lifetimes_in_associated_types` set is built from associated TYPES, not fns).
HAND: also refuses l4_methodsig_only and l7_assoc_const_only, both legal.

## STDLIB COLUMN, PREDICTED 0 FOR BOTH, AND THE ZERO IS STRUCTURAL, NOT A PROBE ZERO
`grep -rnE "^ *impl<[^>]*'" --include=*.logos stdlib` = **0**. The stdlib declares no impl
with a lifetime binder at all, so the site cannot be reached there. Recorded as a population
fact (rule 1: a zero is not an answer until the site is proven live — here the site is proven
UNREACHABLE by direct listing, which is the honest form of the same statement).

## THE WHOLE-CORPUS ARRIVAL CENSUS, TAKEN BEFORE THE PROBE WAS WRITTEN
221 `impl<'…>` headers under tests/, 0 under stdlib/. Of those, the binders that appear
nowhere else in the header line — the crude arm's whole population — are SEVEN, by direct
listing:
  missing-lifetime-in-assoc-type-1 / -5 / -6      (the targets)
  tests/imported/admit/nll/trait-associated-constant:20            'c
  tests/logos/pass/bc_mcallvar_legal_twins:50                      'a   ← the cost
  tests/logos/pass/bc_ltbndenv_legal_shapes:57                     'static  ← NOT a binder
  tests/logos/pass/bc_objlt_impl_bound:6                           'static  ← NOT a binder
The last two are `'static` inside a TYPE-param bound and are a defect of the textual census,
not of the compiler: the probe reads LIFETIME_PARAM nodes and must not see them. If the
measured arrival count for `e0207.unc` is 5 and not 7, the textual census was wrong in
exactly that way and the compiler was right.
