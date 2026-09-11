# 2026-09-11a — TARGET ROWS, WRITTEN BEFORE THE COMPILER WAS TOUCHED

TARGET BLOCK: `lifereg.NEW-E0207`, three rows, named by id:

    missing-lifetime-in-assoc-type-1   tests/imported/admit/lifetimes/missing-lifetime-in-assoc-type-1
    missing-lifetime-in-assoc-type-5   tests/imported/admit/lifetimes/missing-lifetime-in-assoc-type-5
    missing-lifetime-in-assoc-type-6   tests/imported/admit/lifetimes/missing-lifetime-in-assoc-type-6

WHY THIS BLOCK OVER THE OTHERS — the reasoning the next round inherits:

1. THE LEDGER'S BEST-PRICED BLOCK IS BLOCKED ON THE OWNER and the prompt forbids it
   (`argresvact` + receiver reservation: two GREEN pass fixtures pin constructs upstream
   refuses with E0502; a row may not be bought by editing a pin).
2. `bck.D` + `nllmoves.D` carries a 2026-09-08 DECLINE IN THE FILE ITSELF (two populations
   by upstream error code). A note in the file outranks a recommendation in a prompt.
3. `*.NEW-CESC` (7 rows, the ledger's largest root) was surveyed 2026-09-09i and 09-09j and
   SPLIT into five mechanisms; six of eight rows never reached its door. Worked.
4. `bck.NEW-CAPMOVE` (3 rows) LOOKED never-surveyed by a name grep of PROBES.md (1 hit) and
   IS NOT: the site (`borrow_check.cpp` capture loop, ~line 10533) already carries five
   installed probe arms — `capmove` (ceiling 2 / cost 3, STOP), `capmoveloan` (ceiling 1 /
   cost 0, 1-row), `capmovety`/`capmovedrop`/`capmoveref` — and `in_move_closure_` IS
   carried. A root name is not a site. Rejected on that number, not on the name.
5. `lifereg.NEW-E0207` IS never surveyed, and by the property, not by the name: PROBES.md's
   only mentions are CENSUS lines saying "no E0207 rule anywhere" (R17-f, 4949/5149) and a
   DECLINE — "LEGAL RUST as ported, owner: retire or re-port with the assoc type"
   (15114 / 15515 / 21560). THAT DECLINE HAS DECAYED: the 2026-09-07 stage-3 re-port put the
   associated type back, which is exactly what the note asked for. No probe has ever been
   installed for it.
6. SHAPE: every fact the arm needs is already carried at ONE site,
   `sema_decl.cpp::lower_impl_block` — `current_impl_lifetime_params_` (the binders, read
   from BOTH `IMPL_TYPE_PARAMS` and `TYPE_PARAMS`), the resolved self type, the trait ref
   args (`current_impl_trait_args_`), and the impl's associated-type definitions. The arm
   is what is absent, not the fact.

THE GROUPING IS TESTED, NOT ASSUMED. -5 and -6 are byte-identical in the program body
(only the package name differs); -1 differs from them in ONE token, the self type `&S` vs
`&'_ S`. So ONE candidate change must move all three, and if it moves only -1 or only
-5/-6 the grouping is refuted and the root splits on the anonymous-lifetime spelling.

UPSTREAM ORACLE, READ ON THE BOX (there is no rustc binary here):
/home/logos/cxx/rust tests/ui/lifetimes/missing-lifetime-in-assoc-type-1.stderr carries
`error[E0207]: the lifetime parameter 'a is not constrained by the impl trait, self type,
or predicates`. rustc's E0207-for-a-lifetime fires only when the binder is named by an
ASSOCIATED TYPE; all three ports satisfy that. The ports are faithful and the rows are real.

PREDICTION (written before any binary existed): a correct arm closes exactly these THREE
and nothing else in the ledger.
