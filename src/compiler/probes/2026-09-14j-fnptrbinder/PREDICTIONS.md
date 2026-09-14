# 2026-09-14j-fnptrbinder — PREDICTIONS, by name, written before the batch build

Base c52dcb19dff987ff 43. Spec: fnptrbinder.spec (four names).

## Names
  fnptrelide     door E whole: check_variance, when `to` is a FnPtr and `from` a fn value — sup's elided slots are
                 fresh rigid placeholders ('%fN, one per slot, an elided return tied to a sole elided input); the
                 sub's own binders (elided / '%h / every non-'static name of a fn ITEM) take the sup's region at their
                 first position.
  fnptrelidesup  rule-9 twin: the SUP half only (no sub existential).
  fnptrhrtb      door H: resolve_type(FN_PTR_TYPE) renames `for<'r>` binders apart ('%hN). No subtype change.
  fnptrbinder    E + H, the whole.

## Ledger ceiling, by name
  fnptrelide     {issue-54124}                 (101280: its `""` is a sub binder mapped onto the sup's 'r — same name)
  fnptrelidesup  {issue-54124}
  fnptrhrtb      {issue-101280}                (sup `for<'r>` -> '%h rigid; `outlives('%h, 'r)` strict -> false)
  fnptrbinder    {issue-54124, issue-101280}   additive: 1 + 1 = 2 (predicted, to be checked, rule 13)
  No other bc_admits row holds a top-level fn-pointer let/return; copy-modulo-regions, multiple-sources--t24/--c24b,
  propagate-fail-to-approximate-longer-no-bounds, regions-nested-fns-2 carry `fn(&`/`for<'` only in bounds or
  unrelated positions: predicted NOT to move.
  Queue: fnitem_to_fnptr_array_elem_admits (array-literal element, nested) predicted NOT to move under any name.

## Cost predictions (the shapes that would break the arm — hand battery hb.*, base verdicts recorded)
  fnptrelidesup  REFUSES legal h02 h03 h04 h09? (h09 is a struct literal: permissive, so NOT) h13? h15 h16 h21 h22 h23
                 — every fn ITEM with a named region assigned to an elided pointer at a strict site. Cost > 0 in the pass
                 corpus predicted (regions-fn-subtyping-return-static-fail--c30's `let ok = foo` is a FAIL fixture whose
                 first line is legal: its text may gain a line).
  fnptrelide     REFUSES legal h06 (`add2<'a>(&'a, &'a)` into `fn(&i64, &i64)`: one existential offered two
                 placeholders, first wins, the second compares rigid-vs-rigid). Rust admits it. That is the arm's
                 own predicted over-refusal, written before the build.
  fnptrhrtb      re-words pinned text where a `for<'a>` type is printed (hr-fn-aaa-as-aba, fn-subtype,
                 placeholder-outlives-existential): '%h names are minted and hidden by type_str. cfail > 0 predicted.
  fnptrbinder    h08 (legal, refused on base) ADMITTED; h22 (legal, refused on base) ADMITTED; h06 still REFUSED.

## Hand illegal shapes, predicted
  i01 (return named -> elided) i02 (let param named -> elided) i03 (= 54124) i12 (two params, one named): REFUSED by
  fnptrelide / fnptrbinder.  i04 (= 101280): REFUSED by fnptrhrtb / fnptrbinder only.
  i08 struct literal, i09 call argument: STILL ADMITTED under every name — permissive sites (the outlives() permissive
  tail), a door in series. i10 nested Option: STILL ADMITTED — not reached (top-level only). i11 assignment: depends
  on the assign site's permissive flag — unknown before the run. i14 tuple-wrapped closure param: REFUSED by E.
