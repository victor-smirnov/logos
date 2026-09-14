# 2026-09-14c-meetobl batch 2 — PREDICTIONS BY NAME, written before its build (base 47bea7f0aeb42d36 43, re-read after batch 1's revert)

## WHAT BATCH 1 MEASURED (build 1520624dc7626a4f 43, read; L1 inert rc 0)
    meetobl  fires 5145 · ceiling 0 · cost 0 · cfail 0 of 1583 · stdlib 4 of 4 — "no effect", and it is a BROKEN HOP (rule 11), not a
    refutation: census on the armed binary, X01 X02 X10 X13 X04 under meetobl fire only `meetobl.eq.inert` and never `outl.arrive` —
    subtype()'s head asks types_equal_with_lifetimes, the equality-inert read answered "" == anything, the Co arm was never reached.
    Hand battery under meetobl: 0 illegal refused, TWO illegal UN-REFUSED (X10 enum first-wins, X13 method call meet) — the inert read is
    worse than no probe. Under meetoblinv (token asked at equalities too): X02 X03 X18 refused, 0 legal moved, but every NON-GENERIC
    struct literal still admitted (X01 X04 X07-X09 X11 X12 X14-X17 X19) with `outl.refuse.sub` firing — a second door: lower_struct_lit's
    own first-wins walk types a non-generic `P<'a>` literal from its first region; the meet reaches only its field checks, where a
    sup-side token is vacuous. A GENERIC `P<'a, T>` literal refuses (v5). Batch 1 stopped after its first line (the other six names
    carried the same broken hop); the trap reverted and rebuilt to 47bea7f0aeb42d36.

## BATCH 2 — the comparator asked everywhere, plus the non-generic literal door
    name       mints at                                             predicted ledger closings
    mobl       structlit meet + non-generic literal + call + enum×2  {regions-creating-enums3, regions-glb-free-free--glb-free-free}
    moblng     structlit meet + non-generic literal type             {regions-glb-free-free--glb-free-free}
    moblenum   both enum literal sites                               {regions-creating-enums3}
    moblcall   call meet                                             {}
    moblgen    structlit meet (generic literal type, field checks)   {}  (the glb row's Flag is non-generic: door in series)
    moblany    = mobl, enum sites without the covariance guard       = mobl's rows; cost >= mobl's
    moblsome   = mobl's sites, sub-side ANY (control twin)           {}; may un-refuse X10 / X13 shapes (cfail)
Soundness queue: 0 rows move.

Hand battery under mobl (61 programs, verdicts on the base copy beside each in battery base_copy_47bea7f0_all.txt):
  REFUSED (admitted on base): X01 X02 X03 X04 X07 X08 X09 X11 X12 X14 X15 X16 X17 X18 X19.  STILL REFUSED: X05 X06 X10 X13.
  LEGAL unchanged (compile + same exit): L01-L08 L10-L32 L35-L42.  L09 (E0106 in Rust too), L33, L34 (refused on base — found) cannot move.
  Named risks: L26 (`Option<&'y>` field: the walk's Struct/Enum arm), L27 (TraitObject region), L28 (Slice region), L35 (Box<P<tok>> Inv
  with 'b: 'a — mutual outlives must hold), L41 / variance-option-ref-intersection-rg (two minted params into one binder, elided let).
Every refused program's stderr is scanned for `'%^`.
