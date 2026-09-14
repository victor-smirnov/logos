# 2026-09-14j-fnptrbinder — BATCH 2 PREDICTIONS, by name, written before the second build

Batch 1 (build 268c7e872b43e250 43) measured every ledger set as predicted (fnptrelide {issue-54124}, fnptrhrtb
{issue-101280}, fnptrbinder {both}, 1 + 1 = 2) and the harness priced fnptrelide/fnptrbinder at cost 0 in pass,
fail-text and stdlib. The hand battery REFUTED that: under fnptrelide, 8 legal programs refused —
  h01 h24 h31 h32  a CLOSURE's elided parameter is a closure-MINTED name ('%N, closure_minted_lts()), not "", so the
                   sub view kept it rigid ("expected fn(&i64) -> i64, got fn(&i64) -> i64" — one spelling twice);
  h16 h25          an `if` join of two fn ITEMS is a FnPtr carrying one item's NAMED region ('b), read as rigid;
  h26              h01's shape inside a fn with its own 'a;
  h06              one named item binder offered two sup placeholders; first wins, the second compares rigid.

## Names (batch 2, spec fnptrbinder2.spec)
  fnptrbinder2    E' + H. E' = E, and a SUB binder is also: a closure-minted name; any non-'static name no enclosing
                  scope declares (not in current_lt_binders()); and a named binder offered >= 2 distinct sup regions
                  is instantiated at a MEET token of them (mint_meet_token) instead of the first.
  fnptrelide2     E' alone (no H).
  fnptrbinder2nm  E' + H WITHOUT the meet (rule 9/13: prices the meet increment).

## Ledger ceiling, by name
  fnptrbinder2 {issue-54124, issue-101280} · fnptrelide2 {issue-54124} · fnptrbinder2nm {issue-54124, issue-101280}

## Hand battery, by name
  fnptrbinder2 / fnptrelide2: ADMIT legal h01 h06 h16 h24 h26 h31 h32 (and keep h20 h22 h27 h28 admitted);
     REFUSE illegal i01 i02 i03 i12 i14 i15 i16 i17 i18.
     STILL REFUSE legal h25 — its join carries 'a, which the enclosing `test<'a>` also declares: a name set cannot
     say which binding 'a denotes (rule 12). Predicted over-refusal, written before the build.
  fnptrbinder2nm: as fnptrbinder2 except h06 REFUSED (no meet); i15 i17 refused either way.
  fnptrbinder2: i04 refused (H). fnptrelide2: i04 admitted.
  Unchanged under every name (other doors): i08 struct literal and i09 call argument (permissive sites), i10 nested
  Option, i11 plain re-assignment (no variance consumer at lower_assign unless lifereg_varassign), h08's line-9 call.
## Cost columns
  cfail 3 for the H names (fn-subtype, hr-fn-aaa-as-aba, placeholder-outlives-existential: '%h hidden by type_str,
  still refused); 0 for fnptrelide2. Pass / stdlib 0 predicted; the runtime column is run on fnptrbinder2 only.
