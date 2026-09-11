# PREDICTION — 2026-09-11e, `lifereg.B` PER-PATH PARAM RECORD
# WRITTEN BEFORE ANY LINE OF src/compiler WAS EDITED. HEAD b98e04040,
# build_hash 411ed23b6b9fea05 43 (READ), queue gate rc 0 / 78 rows,
# gate-run -L bc baseline = build 1041, 6694 recorded, 0 failed (READ from the store).

## THE CLASS, BY PROPERTY (not by spelling)
NOT "the c17 program". The class is:
  **a borrow whose provenance is a PARAM, stored into a holder place, loses its
  `params` component at the deposit — at EVERY deposit door — because
  `note_holder_escape_prov` returns early on `!vp.is_local && !vp.is_temp`
  and by its own comment never records `params`.**
The class members are the DOORS, and they are enumerated by dlog `provdep.dl`
(`holder_call`, 5 call sites / 3 contexts; hand grep 5 — agree), NOT by grepping
for the row's construct:
  1. borrow_check.cpp:4945  "outparam"    (apply_flow_outparams)  path ""
  2. borrow_check.cpp:12820 "assign"      (whole-value reassign)  path ""
  3. borrow_check.cpp:13372 "derefwrite"  (field/tuple write)     path = fp2
  4. borrow_check.cpp:13488 "derefwrite"  (index_mut recv store)  path "[]"
  5. borrow_check.cpp:15506 "recvstore"   (container push)        path "[]"
ONE structural change: the `params` set is carried per PLACE PATH, replacing its
own path and merging across paths — the same shape this file already built for
dropck (`dropck_field_srcs_[root][path]`, :13322), for the same reason (a
root-keyed record cannot tell a sibling from an overwrite).

## PREDICTED CLOSED SET — A NUMBER AND A LIST OF ROWS
  COUNT = 1
  ROWS  = { mut-slice-struct-lifetime-transmute--c17 }   (bc_admits, root lifereg.B)
  NOT   = { mut-slice-struct-lifetime-transmute--t17 }   (lifereg.NEW-B2 — 0 arrivals
            at all five doors, MEASURED last round; its door does not exist, so
            carrying a fact to a door cannot reach it)
Prediction rests on last round's arrival census (`liferegb.arrive` = 1 over all
86 ledger programs) — a HANDED-DOWN NUMBER, therefore a hypothesis; it is
re-measured in this round with my own census before the closed set is reported.

## PREDICTED NON-ROW EFFECTS (must be REPORTED, never counted as rows)
  P1_deref_norow   `{ let d:&mut &i64 = &mut out; *d = y; } return out;`
                   PREDICT **STILL ADMITTED**: `*r = v` has ptr.kind()==VarRef and
                   reaches neither the AddrOfTemp branch nor the index_mut door
                   (borrow_check.cpp:13500 says so in its own words).
  P2_array_index   `d[0]=y` on `&mut [&i64;1]` — PREDICT still admitted (needs the
                   array-index descent last round's `liferegbboth` added, which is
                   NOT part of this change).
  P3_container     `v.push(y); return v[0];` — door 5 exists, PREDICT REFUSED.

## MY OWN COUNTER-EXAMPLES — 12 LEGAL PROGRAMS, ALL rc 0 ON THE BASE BINARY
Shapes the pricing phase did NOT use (it used: sibling field, sibling element,
overwrite — all three already condemned the root-keyed form):
  N1 same-lifetime store         N2 `where 'b:'a`            N3 whole-value re-own
  N4 nested-path sibling         N5 holder not returned      N6 both branches safe
  N7 elided return, 1 ref param  N9 `&STATIC` store          N10 tuple sibling
  N11 shadowed holder            N12 sibling under 'a
PLUS the three the last round used (sibling FIELD / sibling ELEMENT / OVERWRITE),
re-written here. EVERY ONE MUST STILL BE rc 0. A single new refusal among them
condemns the arm, exactly as three did last round.
