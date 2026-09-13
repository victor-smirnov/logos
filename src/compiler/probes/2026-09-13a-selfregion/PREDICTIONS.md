# 2026-09-13a-selfregion — PREDICTIONS, WRITTEN BEFORE THE BATCH BUILT

Spec `selfregion.spec`. Five names, one build. Doors:
  D3 implanon  the impl header's `'_` becomes a NAMED impl binder (`'__anonN`, pre-order):
               collect renames `Self`; lowering renames the same resolved type and pushes
               the names into `current_impl_lifetime_params_`.
  D1 selfbody  body-level `Self` (lower_impl_block seed_self) carries the header's lifetime
               args when the target is a struct/enum with lifetime args and no impl type params.
  D2 selflit   a struct literal SPELLED `Self` takes `Self`'s lifetime args instead of the
               value-substituted ones (non-generic and generic literal paths).
  selfpair = D1 + D2 · selfall = D1 + D2 + D3.

## bc_admits ledger, BY NAME
  implanon   {issue-55394--b}
  selfbody   {}      (the literal is still typed from its values; the return check is unchanged)
  selflit    {}      DOORS IN SERIES: body `Self` is bare, so the override has nothing to read.
                     Census `selflit.site` > 0 on r13b, `selflit.override` 0.
  selfpair   {}      issue-98170's impl is `MyStruct<'_>`; with D3 absent the literal becomes
                     `MyStruct<'_>` and I have NOT read how the return comparison treats `'_`.
                     If it closes, that reading was wrong, and the record says so.
  selfall    {issue-55394--b, issue-98170}
Queue rows: none moves under any name.

## HAND PROGRAMS (scratchpad ctl/ ty/), BY NAME
  implanon  newly REFUSED: n3a, s5 (trait impl Foo<'_>), e6 (enum E<'_>), s12 (P<'_,'_>),
            h1 (impl for &'_ i64). e5 (impl<T> W<'_,T>): predicted refused, low confidence
            (collect's target_resolved branch). h4's second fn stays refused (reason changes).
  selfbody  newly REFUSED: s1 (`let s: Self = MyStruct{f}` under impl<'q>). h5 unchanged.
  selfpair  newly REFUSED: s1, r13b, h5 (`Self { r: &v }` local, E0597 upstream).
  selfall   all of the above, plus r13a.
  LEGAL, UNCHANGED UNDER EVERY NAME (compile AND run to the recorded exit code):
            n3e 7 · n3f 8 · n3g 9 · r13c 11 · r13e 12 · s8 21 · s9 22 · s10 24 · s11 25 · s13 26
            e2 3 · e3 4 · e7 31 · e8 32 · h6 42 · h7 44 · h8 43 · h9 44 · h10 45 · h11 46
            h12 47 · h13 48 · h14 49
