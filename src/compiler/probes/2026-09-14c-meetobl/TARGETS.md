# 2026-09-14c-meetobl — TARGET ROWS, written before any compiler edit (base build 47bea7f0aeb42d36 43)

bc_admits.ledger rows:
  regions-creating-enums3                 lifereg.NEW-N1   `Ast::Add(x, y)` with x: &'a Ast<'a>, y: &'b Ast<'b> returned as Ast<'a>
  regions-glb-free-free--glb-free-free    lifereg.NEW-R19  `Flag { name: self.name, desc: s }` ('a and an elided s) returned as Flag<'a>

Hypothesis (a fact, not a symptom): a region binder offered TWO OR MORE candidate regions at an
aggregate / call discharges the obligation of the candidates it did not keep. The arm that exists is
the return / let variance check (lifetime_at -> outlives); the fact it never receives is the
candidate SET. Census on the base binary (LOGOS_CENSUS, per program):
  regions-glb-free-free--glb-free-free   meet.structlit.applied=4  (the meet mints "" — a wildcard at the return)
  regions-creating-enums3                meet.enumlit.multi.co=1   (no meet: first-wins, and the second payload
                                                                    is checked by types_compatible, region-blind)
  so ONE fact reaching TWO sites by TWO spellings; whether one change moves both is the measurement.

Why this block: neither root has a record that PRICED it (derived: PROBES.md split at `## `, records with a
`site:` line naming the root; lifereg.NEW-N1 appears only in the 2026-08-30 survey `## 7. SURVEY` (M-AGG,
"not priced") and `ltargarity` (an artefact closing); lifereg.NEW-R19's glb row only in the `ltslicelt`
record, which closed its `--e-field-lifetime` SINGLE-candidate twin and said nothing of the meet).
Not a taken plane: not Self/impl-header, not 'static demand, not declaration arrival, not cross-kind
coercion / Vec store, not the lifereg.B / NEW-B2 holder-deposit door.

Rows considered and not taken: lifereg.R2 (bck.D's two-phase mechanism, argresvact owner-blocked);
trait-method-lifetime-suggestion (M-SELF, a trait default body's `self` — the Self plane);
better-blame-constraint-for-outlives-static (Bytes has NO binder — the Slice/'static elision plane);
bck.A-FNMUT (blocked on three green pass fixtures, 2026-09 record); bck.NEW-1/NEW-4/NEW-A16 (A16);
bck.D, nllmoves.D, bck.NEW-CAPMOVE (declines in the file).
