# PREDICTION — 2026-09-09j-capret (written BEFORE the compiler was touched)

base build hash: 06f0c66be0400a1d 43 (READ)
HEAD at write time: 272ec2068, tree clean

## THE SITE, AND THE CENSUS THAT PICKED IT

`src/compiler/borrow_check.cpp::check_return_value`, the CAUSE-B exemption:

    if (!is_temp && !src.empty() && closure_capture_names_.count(src)) return;

`closure_capture_names_` has exactly ONE reader (decl, one insert, one restore,
this read) — the 2026-08-31 scoping repair. So the blast radius of a change to
this condition is this verdict and nothing else.

Census with the compiler's OWN `LOGOS_DUMP_RETGATE`, on the unmodified binary,
over all EIGHT `*.NEW-CESC` rows (no build needed):

    issue-40510-1                          prov{loc=1} srcs=[x]   ← ARRIVES
    issue-40510-3                          no arrival (returns a CLOSURE)
    issue-48697--t16                       np=1 at both returns, loc=0
    issue-42574-…--t15 / --b               no arrival
    issue-95079-missing-move-…             typed=0 mcb=0, gate not opened
    borrowed-data-escapes-closure-148392   no arrival
    anonymous-region-in-apit--closure-…    no arrival

⚠ THE PROMPT'S HANDED-DOWN "D-RET — THREE ROWS" IS REFUTED BY THIS CENSUS
(rule 17). `issue-40510-3` returns a closure, not a reference; `issue-48697--t16`
reaches the site with PARAM provenance at both of its returns. **One row arrives.**

## THE CLASS, ENUMERATED BY PROPERTY (not by spelling)

Property: *inside a closure body, a `return` whose value is a reference whose
region belongs to the capture's PLACE (the environment slot) rather than to the
capture's REFERENT.* Measured on the base binary, one shape per line:

    a1  || -> &mut i64 { return &mut x; }  bound to a let and CALLED   rc 1 (loan conflict at the call, not this site)
    a2  || -> &i64     { return &x; }      bound and called            rc 0   ← HOLE
    a7  || -> &i64     { &x }              tail spelling               rc 0   ← HOLE
    a8  || -> &i64     { return thru(&x); } through a call             rc 0   ← HOLE
    40510-1  || { return &mut x; }         bare statement closure      rc 0   ← HOLE (the ledger row)
    a3  || -> &i64 { return &s.f; }        field of a capture          rc 1 (refused, message says "temporary" — WRONG SENTENCE, filed)
    a4  || -> &i64 { return &a[0]; }       index into a capture        rc 1 (same wrong sentence)
    a9  || { let q = &x; return q; }       rebound through a body local rc 1  ✓ correct today
    a6  move || -> &i64 { return &x; }     move, address of a capture  rc 1  ✓ correct today

  THE OTHER DIRECTION OF THE SAME PREDICATE — the capture's VALUE, legal Rust:

    a5  let p:&i64=&t; || -> &i64 { return p; }        rc 0  ✓ correct today (this is what the exemption is FOR)
    a10 let p:&i64=&t; move || -> &i64 { return p; }   rc 1  ← OVER-REFUSAL, "cannot return reference to local variable 'p'"
    a11 fn g(q:&i64) { || -> &i64 { return q; } }      rc 0  ✓ correct today

The exemption is keyed on a NAME SET and is therefore wrong in BOTH directions:
it exempts every `&<capture>` because the name matches, and it exempts nothing at
all under `move` because the names were never inserted.

## THE CHANGE (one predicate, one site)

Exempt iff the returned expression is the capture's own VALUE; report iff it is
the ADDRESS of a captured place. Applied to `move` and non-`move` alike (move
capture names are inserted too, so the predicate — not the closure kind — decides).

DIFF BUDGET declared before implementing: <= 60 lines net in
`src/compiler/borrow_check.cpp`, prose in PROBES.md, a one-line marker at the site.

## PREDICTION — A NUMBER AND A LIST

bc_admits.ledger rows CLOSED: **1**
    issue-40510-1   (nllmoves.NEW-CESC)

bc_admits.ledger rows NOT closed, named because the census says why:
    issue-40510-3 · issue-48697--t16 · issue-42574-…--t15 · issue-42574-…--b ·
    issue-95079-missing-move-in-nested-closure · borrowed-data-escapes-closure-148392 ·
    anonymous-region-in-apit--closure-param-escapes      (zero arrivals, 7 rows)

soundness_queue rows: **0 closed, 1 OPENED** if a10 reproduces after the change is
priced and is not itself repaired here (it IS repaired here, so: 0 opened, and the
a10 shape lands as a PASS fixture that RUNS).

New pinned fixtures predicted: a2/a7/a8 (fail half, diagnostic pinned in full),
a5/a10/a11 (pass half, RUN with an asserted exit code), one token apart in pairs.

COST predicted: 0 rc flips. The exemption's only reader is this verdict; the
programs that lose it are exactly those returning `&<capture>` out of a closure
body, which is illegal Rust in every spelling. The move-half insertion can only
ADD exemptions, i.e. can only un-refuse, and the only shape it un-refuses is
"return a moved reference capture's value", which is legal.
  ⚠ predicted-cost 0 is NOT a safety claim (rule 5). Measured on all columns below.

## ⚠ CORRECTION, BEFORE ANY BINARY WAS BUILT — THE FIRST PREDICATE WAS WRONG

The address-vs-value predicate above would have REFUSED the shared spelling, and
the counter-example was already in the tree: `tests/logos/pass/bc_capretsc_
closure_returns_capture.logos` pins `|| -> &i64 { return &u; }` at exit 0.

The upstream oracle on the box settles it, and it is upstream's OWN one-variable
pair (/home/logos/cxx/rust @ da5114692c9):

    tests/ui/nll/issue-40510-1.rs   `|| { &mut x }`   ERROR captured variable cannot escape `FnMut` closure body
    tests/ui/nll/issue-40510-2.rs   `|| { &x }`       //@ check-pass
    tests/ui/nll/issue-40510-3.rs   inner closure MUTATES the outer capture   ERROR
    tests/ui/nll/issue-40510-4.rs   inner closure only READS it               //@ check-pass

Neither -2 nor -4 was imported (the import took compile-fail tests only), so the
corpus carried the erroring half of each pair and not the legal half. **The
discriminator is MUTABILITY, not the address.** A shared reborrow reaches the
capture's own region through `Fn::call(&self)`; a mutable reborrow is bounded by
`FnMut::call_mut(&mut self)` and cannot.

REVISED RULE, one predicate at one site:
  report iff the returned reference is the ADDRESS of a captured place AND
    (the closure is `move`  → it dangles on the closure's own env: the EXISTING
                              "local variable" message, unchanged)
    (else the return type is `&mut` → the `FnMut` escape: a new sentence)
  exempt otherwise — the capture's VALUE, and every SHARED reborrow.

REVISED PREDICTION, unchanged in the count:
  bc_admits rows CLOSED: **1** — issue-40510-1
  rows NOT closed: the other 7 NEW-CESC rows (zero arrivals, census above)
  COST: 0 rc flips. CONTROL that must not move: pass/bc_capretsc_closure_returns_capture.
  New behaviour predicted, each to be pinned in a one-token pair:
    `|| -> &mut i64 { &mut t }`            rc 0 -> REFUSED   (the row)
    `|| -> &mut i64 { thru(&mut t) }`      rc 0 -> REFUSED
    `move || -> &i64 { return r; }`  (r a ref) REFUSED -> rc 0  (an over-refusal repaired)
    `|| -> &i64 { &t }`                    rc 0 -> rc 0 UNCHANGED
