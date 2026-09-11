ROUND 2026-09-11c — PREDICTION, WRITTEN BEFORE ANY COMPILER SOURCE WAS EDITED
HEAD 7d92a3a85, tree clean.  build at open: fbb52c741c4e8469 43 (READ)

THE ROOT IS NOT THE ONE THE PRICING ROUND RECORDED, AND ONE PAIR SAYS SO.
The pricing round (2026-09-11b) re-rooted `*.NEW-CAPLOAN` as "the non-`let`
ClosureBox arm of borrow_check.cpp::visit mints no capture loan", and proposed
funding a capture DEPOSIT with a holder. Measured today on the base binary,
one variable — the callee's expression KIND, everything else identical:

    let c = || -> &i64 { return &g; };  let r: &i64 = c();   g = 2;   REFUSED
        "cannot assign to 'g' because it is borrowed"
    let r: &i64 = (|| -> &i64 { return &g; })();             g = 2;   ADMITTED

    fn get() -> &i64 { let l=5; let c = || -> &i64 {return &l;}; return c(); }  REFUSED
        "cannot return reference to local variable 'l': dangling reference"
    fn get() -> &i64 { let l=5; return (|| -> &i64 {return &l;})(); }           ADMITTED

The deposit arm is NOT missing. `take_ref_borrows`' ClosureCall arm ALREADY
records a shared borrow of every capture root WITH THE HOLDER when the call's
result carries a borrow (borrow_check.cpp, the `case Code::ClosureCall` that
reads `closure_caps_of`). It is unreachable for an IIFE because
`closure_caps_of` early-exits on `callee.kind() != Code::VarRef`: the capture
list is recorded at the BINDING (`note_closure_caps`) and keyed by NAME, and a
closure LITERAL called directly has no name. A LOOKUP KEY IS NOT AN IDENTITY.

THE CLASS, BY PROPERTY (not by spelling): every site that asks a call "what
does this closure capture" goes through `closure_caps_of`. There are FOUR, and
all four are blind to a literal callee in exactly the same way:
   1. collect_ref_sources_paths   (the §B6 source walk)
   2. bc_hop_roots                (co-holder roots)
   3. prov_of                     (the dangling/provenance channel)
   4. take_ref_borrows            (the LOAN channel — the one that buys the row)
ONE structural change at `closure_caps_of` covers all four. The census
`capsof.*` (installed with the change) reports the arrival population by callee
kind, so the class is enumerated by the property and not by the grep.

CEILING, BY NAME:  { issue-58776-borrowck-scans-children }   and nothing else.
  A grep of all 87 ledger programs for an immediately-invoked closure returns
  exactly that one file; the census will say whether the PROPERTY population is
  larger than the SPELLING population.
NOT predicted to close, reasons stated before the run:
  · mut-borrow-conflict-in-closures-vec--bounded — no IIFE; its closures are
    `Box::new(|| …)` arguments and its defect is `record_borrow` being
    record-only (2026-09-11b §5 T2). A second mechanism, untouched here.
  · issue-51268 — no IIFE; and it is a CORPUS DECISION (precise capture makes
    the paths disjoint; Rust 2024 accepts it).

COST, PREDICTED: 0 rc-visible. The loan channel is gated on
`is_ref_kind(rt) || is_borrow_carrying_type(rt)`, so every unit- or
scalar-returning IIFE is untouched BY CONSTRUCTION — which is exactly the four
legal hand programs (L2, M1, M2, M4) that condemned `capvisloanb` under rule 5.
Predicted hand cost: 0 of 10 legal programs (C1..C8 + the two position twins).
Risk direction named in advance: the provenance half (site 3) newly answers
`is_local` for an IIFE returning a ref to a captured LOCAL — a GAIN (D2) but
also the only place an over-refusal could come from.

NEW DEFECT FOUND ON THE WAY IN (not a ledger row): D2, a DANGLING REFERENCE
admitted. `fn get() -> &i64 { let l: i64 = 5i64; return (|| -> &i64 { return
&l; })(); }` compiles; the named-closure twin one token apart is refused. If it
produces garbage at run time it is a soundness_queue row, not a bc_admits row.
