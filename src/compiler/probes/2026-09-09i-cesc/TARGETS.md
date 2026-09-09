ROUND TARGET — bc_admits.ledger, chosen 2026-09-09, base build 06f0c66be0400a1d 43
BLOCK: bck.NEW-CESC (3) + nllmoves.NEW-CESC (5) = 8 rows, 7 distinct programs.

ROWS, BY ID:
  bck.NEW-CESC       anonymous-region-in-apit--closure-param-escapes
  bck.NEW-CESC       borrowed-data-escapes-closure-148392
  bck.NEW-CESC       issue-95079-missing-move-in-nested-closure
  nllmoves.NEW-CESC  issue-40510-1
  nllmoves.NEW-CESC  issue-40510-3
  nllmoves.NEW-CESC  issue-42574-diagnostic-in-nested-closure--b     (same program as --t15)
  nllmoves.NEW-CESC  issue-42574-diagnostic-in-nested-closure--t15
  nllmoves.NEW-CESC  issue-48697--t16

WHY THIS BLOCK OVER THE OTHERS:
 1. It is the LARGEST root in the ledger (5 + 3), and the only 8-row block.
 2. It is the prompt's preferred shape, verified in the sources rather than assumed:
    THE ARM EXISTS and THE FACT IS DEPOSITED, and the fact has exactly ONE reader.
      arm     SemaChecker::check_object_lifetime_bound, src/compiler/sema_expr.cpp:15231
              (walks a closure literal's captures; its K::Closure case reads
               closure_caps_by_id_ at sema_expr.cpp:15333)
      deposit closure_caps_by_id_ written at sema_expr.cpp:18310 for EVERY closure literal
      census  `grep closure_caps_by_id_ src/compiler/**` = 3 hits: the declaration
              (sema_impl.hpp:3755), the ONE read (15333), the ONE write (18310).
    Every closure literal in the program deposits its capture types; exactly one
    door — the `dyn` coercion — ever reads them.
 3. The root's recorded separating pair REPRODUCES TODAY (see below), so the
    minting measurement is not stale.
 4. The alternative largest block, bck.D + nllmoves.D (7 rows), carries a DECLINE
    dated 2026-09-08 in the ledger itself with the reading that condemns it: the
    seven ports are two populations by upstream error code (E0502/E0499 x3,
    E0716/E0597 x4) and the merge rests on one pair per block letter, not one per
    row.  The prompt calls that merge "the strongest standing recommendation in
    the ledger"; the ledger's own most recent entry supersedes the prompt.

CONTROLS RE-VERIFIED ON TODAY'S BINARY (06f0c66be0400a1d 43), BEFORE ANY EDIT:
  all 8 programs   still ADMITTED, rc 0 each (--emit-obj)
  recorded pair, one variable = the escape channel:
    Box::new(|| -> i64 { x }) coerced to Box<dyn Fn() -> i64>  -> REFUSED rc 1,
      "coercion to `dyn Fn` requires `|| -> i64: 'static` — lifetime `'_` may not
       live long enough (object lifetime bound)"   [diagnostic READ, matches the record]
    keep(|| -> &i64 { &x }) through `fn keep<F>(f: F) -> F`     -> ADMITTED rc 0
  => the 2026-09-07 minting pair reproduces exactly two days later.

GROUPING TO BE TESTED, NOT ASSUMED (the last two rounds refuted both handed-down
groupings this way).  By READING the seven programs the block is not obviously one
mechanism; it is at least four candidate doors:
  D-RET   the closure's RETURN type carries a region from its own frame
          40510-1 (`|| { return &mut x; }`), 40510-3 (inner closure escapes outer),
          48697--t16 (`f(x)` returns the closure's param region)
  D-STORE a write INTO a capture of a borrow of ANOTHER capture
          148392 (`move || { b = Option::Some(&a); }`)
  D-CAPRB a NON-move closure capturing `&'r mut T` and reborrowing it for a use
          that requires 'r     42574--b / --t15 (`|| { doit(data); }`, data: &'static mut)
  D-SIG   a closure PARAMETER's anonymous region flowing into a capture's region arg
          anonymous-region-in-apit (`|baz:&i64| foo.bar(baz)`, foo: impl Foo<&i64>)
  D-NEST  a `dyn Tr + 'a` bound checked against the enclosing FN's 'a rather than the
          enclosing CLOSURE body's frame     95079
The round's question is exactly: does ONE candidate change move more than one of
these?  If it does not, NEW-CESC is 4-5 roots wearing one name, and that is the
result to record.
