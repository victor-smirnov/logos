ROUND 2026-09-10b — TARGET ROWS, NAMED BEFORE THE COMPILER IS TOUCHED
build read at selection: afdc542ff6b5460b 43 (read TWICE, identical — see note)
queue gate rc at selection: 0, 75 rows (tier1=19 tier2=9 tier3=40 tier4=7)
bc_admits 90, bc_admits_blocked 8, probe-log-lint 264 records / 43 live probes
gate-run.sh -L bc at selection: 2739 passed / 0 failed / 2 disabled, rc 0

THE CLASS, BY PROPERTY (not by spelling)
  "`find_func_candidates(<Type>__<method>)` returned AT LEAST ONE candidate — the
   name EXISTS on this type — every candidate was rejected, and the diagnostic
   says the type HAS NO SUCH METHOD."
  Site: sema_expr.cpp, the struct-inherent method-dispatch candidate loops, whose
  single failure exit is `error("method call: '{}' has no method '{}'")`.

  THE CENSUS OF THAT LOOP'S OWN REJECTION EXITS, BY DIRECTION. Every exit below is
  a `continue`/`ok=false` and every one of them lands on the same sentence:
    1. `cand->param_types.size() != types.size()`      — ARITY
    2. `!types_compatible(actual0, formal0)`           — RECEIVER
    3. `!arg_compatible_for_dispatch(arg, at, pt)`     — ARGUMENT i
  There is no fourth: a candidate with type_params is handed to the generic
  fallback (`find_generic_func_for_args`), which ALREADY reports the right
  sentence — measured, m09 below.

MEMBERS MEASURED ON THE BASE BINARY BEFORE A LINE WAS EDITED (12 hand programs)
  m01 arity, too few, 1 param       rc 1  "'R' has no method 'thing'"   MEMBER (queue row)
  m02 arity, too many               rc 1  "'R' has no method 'one'"     MEMBER (no row)
  m03 receiver &S vs &mut self      rc 1  "'S' has no method 'mget'"    MEMBER (queue row)
  m04 argument type i64 vs bool     rc 1  "'H' has no method 'eat'"     MEMBER (no row)
  m08 trait-impl method, arity      rc 1  "'C' has no method 'tick'"    MEMBER (no row)
  m12 arity, too few, 2 params      rc 1  "'P' has no method 'two'"     MEMBER (no row)
  ---- ABUSE DIRECTION: these must NOT move ----
  m05 name truly absent, type HAS other methods   rc 1 "has no method"  CORRECT
  m06 name truly absent, no impl block at all     rc 1 "has no method"  CORRECT
  m09 GENERIC method, arg type mismatch  rc 1 "method 'W__put' arg 1: expected i64, got bool"  ALREADY CORRECT
  m10 two methods, both called correctly          rc 0                  CORRECT
  m11 struct FIELD holding a fn-pointer, `h.f(4)` rc 0                  CORRECT
  m07 by-value `self` through `&T`, POD payload   rc 0  (see FINDINGS — not this class)

THE FIX IS NOT A NEW SENTENCE. The three correct sentences ALREADY EXIST in this
same file, on the trait / dyn / generic method paths, and this one path answers
"has no method" instead of what its three siblings already say:
    arity     `method call '{}': expected {} args, got {}`   (sema_expr.cpp:10264, 8144, 8789)
    argument  check_assignable(at, pt, `method '{}' arg {}:`) (sema_expr.cpp:8160, 8840)
    receiver  `method '{}' receiver`                          (sema_expr.cpp:8827)
So the change is: make the inherent-overload path REPORT THE REJECTION REASON it
already computed and then threw away. Rust-canonical by the standing rule —
E0061 for arity, E0308 for the argument, and for the receiver a sentence that
names the mutability instead of claiming absence (rustc reaches E0596 there).

PREDICTION — NUMBER AND ROWS, BEFORE THE EDIT
  ROWS CLOSED: 2
    method_arity_mismatch_says_no_such_method   tier 4 diag   (m01)
    sharedref_recv_mut_method_diag              tier 4 diag   (m03)
  ROWS OPENED: 0 from this class (m02/m04/m08/m12 are members closed by the same
    change and land as fixtures, not as rows — a defect fixed in the round that
    finds it does not get a row).
  HAND PROGRAMS THAT MOVE: exactly m01 m02 m03 m04 m08 m12 — six.
  HAND PROGRAMS THAT DO NOT MOVE: m05 m06 m07 m09 m10 m11 — six, character for
    character.
  FIXTURES RE-PINNED: 1 — tests/logos/fail/method_arg_wrapper_unsize_dispatch.expected
    (an ARGUMENT member; its own header says the sentence is pinned "so that
    changing it is visible").
  FIXTURES NOT RE-PINNED: tests/logos/fail/trait_query_quote_item_uncheckable_method
    ('Plain' has no method 'clone' — genuinely absent, predicted UNCHANGED).
  COST: predicted 0 on run_oracle.py (no runtime behaviour changes — every
    member is already refused, only the sentence moves) and 1 changed text in
    fail_text_oracle.py, namely the fixture above.

A CONTRADICTION WITH A RECORDED CLAIM, FOUND AT SELECTION TIME
  tests/logos/fail/method_arg_wrapper_unsize_dispatch.logos:8 cites
  "a separate soundness-queue row (method_arg_mismatch_reported_as_no_method, `diag`)".
  THAT ROW DOES NOT EXIST and never did: 0 hits in soundness_queue.ledger, 0 in
  tests/soundness/open/, and `git log -S` finds no commit that ever added it. The
  fixture pinned a sentence against a row that was never filed.

────────────────────────────────────────────────────────────────────────────────
OUTCOME AGAINST THIS PREDICTION — WRITTEN AFTER, DIFFED BOTH WAYS
────────────────────────────────────────────────────────────────────────────────
HAND PROGRAMS: predicted six move / six do not. READ THE SAME, empty both ways,
  on BOTH implementations (v1 direct-format, v2 routed through `expect_type`).
ROWS CLOSED: predicted 2, closed 2 — the two named, diagnostics READ.
ROWS OPENED: predicted 0, opened 1 — `overload_set_arg_mismatch_says_no_method`.
  THE PREDICTION WAS WRONG AND THE WAY IT WAS WRONG IS THE ROUND'S FINDING: the
  ARGUMENT member is closed for a single candidate and NOT for an overload set,
  because `expect_type` accepts what `arg_compatible_for_dispatch` rejected.
FIXTURES RE-PINNED: predicted 1, landed 1 (`method_arg_wrapper_unsize_dispatch`).
  A second, `coerce_diag_1__intlit-dispatch-unsuffixed-fits-narrower`, was
  re-pinned against v1 and REVERTED when v2 measured it unchanged.
FIXTURES NOT RE-PINNED: predicted `trait_query_quote_item_uncheckable_method`
  unchanged — it is unchanged.
COST: predicted 0 on run_oracle — **0 of 6615 shared triples**, the single
  differing row being `cast-region-to-uint` by name. Predicted "1 changed text"
  on the fail column — the DEFAULT fail population showed 0 of 1478 and that
  zero was a POPULATION HOLE, not an answer; the whole-corpus sweep says 1 of
  2673. stdlib clean. L1 801/801 + 12 684 + 148 gates.
