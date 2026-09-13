# 2026-09-12r-escroot — TARGETS, WRITTEN BEFORE THE COMPILER WAS TOUCHED

Base build `fc8539b37318b78a 43` (read with `scripts/build_hash.py`), HEAD `3c18ecb06`.
Queue `# TOTAL 94` (94 rows by listing), bc_admits `# TOTAL 79`, blocked 8. Queue gate rc 0.
Baselines on this build: `gate-run.sh -L bc` build 1093 6753 recorded / 0 failed (store read);
`run_oracle.py` 6697 fixtures; `fail_text_oracle.py` taken.

## THE LANDING PHASE OF 2026-09-12q

12q priced door C (call-result region instantiation) and door L (let annotation vs local
borrow), declined door L as priced (EMPTY read as "local", rule 16) and said: fund door C only
together with a door that sees a local borrow, and extend C to `finish_generic_call`.

## WHAT THIS ROUND MEASURED BEFORE CHOOSING (base binary, scratchpad r0912r/ctl)

12q's only un-refusal under door C was q6 `*out = A::newa(&v)`. Its FREE-FN twins are ALREADY
ADMITTED today, so door C does not create that hole — it removes a name coincidence hiding it:
    x1 `*out = newa(&v)`  (A<'q> free fn)          rc 0   ILLEGAL (E0597)
    x2 `*out = id(&v)`    (&'q i64 free fn)        rc 0   ILLEGAL
    x3 `let r = id(&v); *out = r;`                 rc 0   ILLEGAL
    x4 `*out = &v`                                 rc 1   E0597 "stored through 'out'"
    x11 `*out = A { x: &v }`                       rc 1   E0597
And the same arm REFUSES LEGAL programs, a reference LOCAL indexed/projected:
    y1 `let ys: &'a [i64] = xs; *out = &ys[1];`    rc 1   "'ys' does not live long enough"
    y2 `let w2: &'a W = w; *out = &w2.f;`          rc 1
    y3 `let t2: &'a (i64,i64) = t; *out = &t2.1;`  rc 1

## THE CLASS, BY PROPERTY (tools/dlog/local_root_deciders.dl, selftest.sh rc 0 first)

"Decides that a value borrows a body-local's own storage" = tests expr::Code AddrOf/AddrOfTemp
AND reads `param_names_` (lambda reads lifted to the enclosing function — the first run MISSED
the subject, known-answer control failed, rule fixed). 9 deciders. Blind to Call/MethodCall: 4
(collect_borrowed_local_roots, collect_borrow_locals, value_local_root, carried_prov_of_recv).
Walks place steps without `extract_borrow_place` (the owner of `through_ref`): 3
(collect_borrowed_local_roots, value_local_root, prov_of_raw). In BOTH lists: exactly one,
`collect_borrowed_local_roots`, one consumer (visit_stmt DerefWrite param arm).

## TARGETS

Queue rows (tier 3, `refuses`): static_call_callee_region_named_in_result_refuses,
method_call_fn_binder_in_result_refuses — door C.
bc_admits rows: method-ufcs-inherent-3 (nllmoves.NEW-1), method-ufcs-inherent-4
(nllmoves.NEW-S7-1), regions-free-region-ordering-caller1 (lifereg.D) — the POSITIVE let door
(`lbd`): a let whose declared type names a FUNCTION lifetime parameter the value's own type
does not carry, fed by a borrow whose root is a body local NOT reached through a reference
(`extract_borrow_place` `through_ref` false), or through a call whose callee flow summary marks
that argument as reaching the result.
New defects found this round (not yet rowed): x1 x2 (call result escapes through an out
param), y1 y2 y3 (legal reference-local projection refused).

NOT the lifereg.B / NEW-B2 plane: x9 `out.x = &v` (field write through a param) reaches the
DerefWrite AddrOfTemp descent that stops at Deref — the excluded
`lifereg_derefwrite_descent_stops_at_deref_admits` door. Measured, not priced.
