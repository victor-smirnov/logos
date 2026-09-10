# TARGETS — 2026-09-09k, written BEFORE the compiler was touched

## THE ROWS, BY NAME
    struct_fields_dropped_reverse_order   tier 3  run 1   (PIECE 1, carried)
    tuple_elems_dropped_reverse_order     tier 3  run 1   (PIECE 1, carried)
    <no row>                              PIECE 2 — the by-value `Drop::drop`
                                          receiver has NO queue row; it is a
                                          permissive defect and the queue's own
                                          discipline says a permissive defect is
                                          invisible to a green corpus.

## WHY THIS BLOCK OVER THE OTHERS
1. The prompt names it and the OWNER decided it (2026-09-09): field order
   Rust-canonical, receiver Rust-canonical. Every other open block in the
   77-row queue still needs a semantic answer; this one has both answers.
2. PIECE 1 IS ALREADY PRICED TO COMPLETION — `dropdecl_*`, PROBES.md, build
   a440722a531c9492, measured 2026-09-09, seven names over four reverse walks,
   nine hand programs, twelve runtime triples, the spec cost read with rule ids.
   Its recorded verdict was "DO NOT FUND WITHOUT THE OWNER". The owner has now
   spoken. Re-pricing it identically would be a repeated operation that cannot
   change the hypothesis. So PIECE 1 is CARRIED, not re-measured; what this
   round owes it is a decay check and the observation that its blocker is gone.
3. PIECE 2 IS UNPRICED AND ITS TWO RECORDED CENSUSES DISAGREE (176/136 in
   PROBES.md vs 251/123 in the prompt). A census that disagrees with itself is
   the cheapest thing in the round to settle and it gates the whole landing.
4. Preferred shape, per the prompt: an ARM THAT EXISTS reached through a fact
   the code does not carry. The receiver-conformance diagnostic
   ("the receiver is declared '{}' and the impl declares '{}'",
   sema_collect.cpp:4686) is IN THE TREE and, on this binary, UNREACHABLE.

## PREDICTIONS, BY NAME, BEFORE THE ARMED BINARY EXISTED
  * `sigselfdepth` refuses exactly the impls whose written receiver has a
    different INDIRECTION PREFIX from the trait's declared one:
      - the 38 by-value `fn drop(self: T)` sites in the 33 files that bind the
        STDLIB `Drop` (which declares `&mut Self`);
      - `tests/imported/pass/drop/drop-trait-enum-b154.logos`, the one file
        whose LOCAL `trait Drop` declares by-value while an impl writes `&mut`
        — the hand-edited port the prompt itself condemns;
      - the shared-ref (`&Self`) drop impls, 8 sites.
    It must NOT touch the 213 by-value sites in the 90 files that declare their
    own by-value `trait Drop`: those conform to their own declaration.
  * `sigselfnone` (crude twin, rule 18) refuses a strict superset, including
    every legitimate Self-SHAPE disagreement (`&str` vs `&[u8]`).
  * CONTROLS that must not move: h3/h4 (`&mut self` both spellings),
    h11 (non-receiver param mismatch — already refused, proves the site live),
    h12 (return mismatch — already refused).

## ADDENDUM — PREDICTION WRITTEN WHILE THE BATCH WAS STILL BUILDING
Read after the spec was launched: PROBES.md 2026-09-09sig already priced the
receiver arm as `sigrecvty` (fires 1561784, "ceiling" 100 which is a broken-hop
artefact, cost 1453, cfail 1457, stdlib ⛔, STOP) and named the repair:
*"the exemption must be keyed on the SHAPE OF THE DIFFERENCE (one reference
layer over an identical DST pointee), not on the kind of `Self`."*
`sigselfdepth` keys on the shape of the difference — the indirection PREFIX —
so it is that named follow-up. But the four stdlib sites that condemned
`sigrecvty` are `impl Pattern for str` (`find_in`, `match_len`),
`impl ToString for str`, `impl WritField for str`, and their surface spellings
are IDENTICAL (`fn find_in(self: Self)` declared, `fn find_in(self: str)`
written) while the record says the internal pair is `[u8]` vs `&[u8]`.
**PREDICTION: `sigselfdepth` therefore ALSO breaks the stdlib at those same four
sites, because a prefix comparison alone does not carry the DST clause.** If it
does, the arm that deserves funding is a THIRD one — prefix difference AND NOT
(exactly one leading reference layer over an identical DST pointee).
