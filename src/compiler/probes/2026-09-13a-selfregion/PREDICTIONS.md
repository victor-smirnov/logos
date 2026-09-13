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

## BATCH 1 NEVER PRICED — L1 RED UNARMED, AND WHY (build 95f2163a0e7cf8dc 43, read)
`logos_00_key_identity_lint`: "find() bare entity-name arguments: count 14, ledger pins 12".
The two D2 records each added `current_type_params_.find("Self")`. A TEXT lint over the source,
not an un-gated probe. Not dodged by respelling: spec v2 reads `hint_struct_type_`, the Self the
top of `lower_struct_lit` already resolved, so the probe adds no lookup at all.

## BATCH 1 BY HAND, ON ITS OWN BINARY (87 programs x 6 arms, compile+link+run)
  implanon  as predicted: n3a e5 e6 h1 s5 s12 newly refused, plus r13d and I5 (both illegal);
            L1 (LEGAL, refused on base: `impl Foo<'_> { fn pick(self: Self, other: Self) -> Self }`)
            now COMPILES and runs 61. No legal program refused.
  selfbody  as predicted: s1 refused. No other verdict moved.
  selflit   as predicted: no lifetime verdict moved (doors in series).
  selfpair  CLOSES r13a (predicted open — `'_` is compared as a NAME), and REFUSES LEGAL s13 and L3
            (`Self { r: self.r }` / `let g: Self` under `impl Foo<'_>`), and UN-REFUSES ILLEGAL
            s3 and I1 (a literal spelled Self takes Self's regions; its field values are never
            checked against them).
  selfall   closes n3a and r13a and every implanon neighbour, s13/L3 compile again (D3 cures the
            name compare), and UN-REFUSES ILLEGAL s3, I1, I3, I5. CONDEMNED; not priced again.
  t10 t11 t12 (`let s: i64 = E::A(x)`) change exit code between two UNARMED runs: they read
            uninitialised memory. Subtracted by name; recorded as a finding, not a probe effect.

## BATCH 2 (spec selfregion2.spec) — PREDICTIONS BY NAME
Names: implanon (D3, CONTROL TWIN of batch 1) · selfbody (D1) · selfv (D1 + D2v + D3) ·
selfvg (selfv + D1 on generic impls). D2v = the literal's value type must pass
`check_variance(values, Self)` and then carries Self's args. `selflitv` (D2v alone) is a hand-only
name: doors in series, predicted to move nothing.
  bc_admits  implanon {issue-55394--b} · selfbody {} · selfv {issue-55394--b, issue-98170} ·
             selfvg {issue-55394--b, issue-98170}.   Queue: none moves.
  hand       implanon = batch 1 implanon, digit for digit (rule 18).
             selfv: refused s1 s3 r13a r13b n3a I1 I3 I5 e5 e6 h1 s5 s12 r13d; LEGAL s13 L3 L1
             compile; h5 STILL ADMITTED (a local borrow has no named region for variance — its
             repair is the E0597 plane, not this door); h2 (`Self::A(x)`) STILL ADMITTED.
             selfvg: selfv + I2 refused; L10 L13 (generic impls, legal) still compile.
  selfve     selfv + a CHECK-ONLY arm at `lower_static_call`'s variant constructor spelled `Self::V(..)`
             (`check_variance(literal, Self)`, reusing the lookup the Self rewrite already did; the
             literal keeps its value regions). bc_admits {issue-55394--b, issue-98170}. Hand: selfv +
             h2 refused. Census `selfve.site` > 0 on h2 — if 0, `Self::A(x)` does not reach this site
             and the zero is a DEAD SITE, not a refutation (rule 1).
Lint dry-run on the applied spec (all five records): `ctest -L lint` 20/20, key_identity_lint rc 0.

## BATCH 3 (spec selfregion3.spec) — PREDICTIONS BY NAME, WRITTEN BEFORE ITS BUILD
Why a third build: batch 2's `selfve` was gated on `selfve` ALONE, so it priced the enum check WITHOUT
D1 — and census on h2 reads `selfve.site 1`, no `selfve.check`: the body Self has no regions to check
against. Doors in series; the neighbour was never measured in the form that could close it. This
prediction file's batch-2 entry for `selfve` described a probe that was not built. Corrected here.
Three records, one per file, one priced name each (every gate widened so each name carries its doors):
  implanonx  implanon + an inherited default method's Self takes the header's named anonymous regions
             (collect_impl, the `self_lt_args_` default-method branch). bc_admits {issue-55394--b}.
             Hand: implanon's set + L14 COMPILES and runs 75 (legal, refused on base and under every
             batch-2 name). L15 (named twin) unchanged 76.
  selfvee    selfv + the enum data-literal check at `Self::V(..)` (lower_static_call).
             bc_admits {issue-55394--b, issue-98170}. Hand: selfv's set + h2 REFUSED
             ("enum literal 'Self': variance mismatch"); h8 legal unchanged 43.
  selfveu    selfv + a unit variant spelled `Self::V` typed as Self (lower_enum_lit).
             bc_admits {issue-55394--b, issue-98170}. Hand: selfv's set + h15 REFUSED (return type
             mismatch, expected E<'a>, got E<'q>); h16 legal unchanged 77; L5 legal unchanged 65.
Queue: none moves. `-fsyntax-only` on all three edited TUs rc 0; no added `find("Self")`.
