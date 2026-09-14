# 2026-09-14c-meetobl — PREDICTIONS BY NAME, written before the batch builds (base 47bea7f0aeb42d36 43)

Spec: meetobl.spec. One process arms one name; every minting record also answers to meetobl / meetoblsome / meetoblinv.

    name           mints at                         comparator          predicted ledger closings (bc_admits)
    meetobl        structlit + call + enum (both)   sub token: ALL      {regions-creating-enums3, regions-glb-free-free--glb-free-free}
    meetoblsl      structlit meet only              ALL                 {regions-glb-free-free--glb-free-free}
    meetoblcall    call meet only                   ALL                 {}  (no ledger program reaches meet.call.applied — census)
    meetoblenumd   enum literal, ENUM_LIT_DATA path ALL                 {}  (flagged: the parse of `Ast::Add(x, y)` is READ, not measured)
    meetoblenums   enum literal, static-call path   ALL                 {regions-creating-enums3}
    meetoblsome    all four sites                   sub token: ANY      {}  — the control twin (rule 18): a kept candidate discharges ANY;
                                                                            at the enum sites it may UN-refuse a first-wins refusal (cfail)
    meetoblinv     all four sites                   ALL, and at Inv/eq  = meetobl's rows; cost >= meetobl's

Soundness queue: 0 rows predicted to move under any name.

Hand battery (battery dir, 35 programs + L09 which is E0106 in Rust too — not legal), under meetobl:
  REFUSED (today admitted): X01 struct ret · X02 enum ret · X03 call `pick(x, y)` ret · X04 `let p: P<'a>` · X07 `-> P<'static>`
    with an 'a candidate · X08 tuple ret · X09 nested literal ret · X11 `&str` glb (the row's own shape) · X12 meet through an
    unannotated let, then returned.
  STILL REFUSED: X05 X06 X10.
  LEGAL, still compile AND run with the same exit: L01-L08, L10-L24.
  Named risks (the shapes most likely to break the arm): L06 / L11 / L12 / L17 / L18 / L21 (a `where` bound must reach the
  comparison through adj), L07 / L10 (a call's meet compared at an ARGUMENT: sup-side ANY), L16 (self-referential enum,
  two locals — empty candidates never enter the set), L24 (closure-minted candidates).
A minted spelling `'%^N` is new text: every refused program's stderr is scanned for it.
