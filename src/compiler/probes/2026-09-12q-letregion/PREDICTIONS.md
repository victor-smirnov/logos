# 2026-09-12q-letregion — PREDICTIONS BY NAME, WRITTEN BEFORE THE BATCH BUILT

Base build `fc8539b37318b78a 43`. Spec `letregion.spec`, committed `b04ac0d88`.

| probe | arms | ledger rows predicted closed | predicted NOT closed |
|---|---|---|---|
| letnamedc | L (lt_eq: EMPTY sub vs a binder of this scope) + C static + C method | method-ufcs-inherent-3, method-ufcs-inherent-4 | adt-tuple-enums--t33 (its `'static` arrives at the ctor ARGUMENT through a turbofish, not at the let) |
| letnamed | L only | method-ufcs-inherent-4 | method-ufcs-inherent-3 (admitted by NAME COINCIDENCE until C substitutes), t33 |
| stcallret | C static + C method | none (door in series with L) | all three |
| stcallrets | C static only | none | all three |

## Hand battery (scratchpad r0912q/ctl), predicted verdicts

Door L illegal, base ADMITTED; predicted REFUSED under letnamed and letnamedc:
d2 d6 d7 d8(assign) n1 n2 n3 n4 n6 r1(assign) r2 r3 r8. ⚠ d8/r1 are ASSIGNMENT sites and
may not reach the let compare; predicted uncertain, not claimed.

Door C legal, base REFUSED; predicted COMPILE+RUN under stcallret and letnamedc:
a6 a10 e3 o3 m2 m3 m4 m5 (static) and m1 (method). Under stcallrets: all but m1.

Door C illegal, base refused ON THE CALLEE'S BINDER NAME: q1..q6.
Predicted under stcallret alone: q1 stays refused (two binders of foo); q2 q3 q5 q6 and q4
are the UN-REFUSAL RISK — the name that refused them is gone and door L is off. Under
letnamedc: q4 refused by the landed 'static arm; q2/q3/q5 at RETURN sites depend on
`lt_static_yield()` and borrow_check's return rule — uncertain, not claimed.

Legal, base COMPILES; predicted to stay compiling under every arm (the cost battery):
L1 L2 L3 L5 L6 L9 n7 n8 n9 n10 n11 n12 p13 p14 p15 p16 p18 p19 p20 p21 p22 p23 p24 p25 p26
r4 r5 r6 r7 a3 a11 a12 e2(illegal! admitted base; under L refused) e4 e5 o1 o2.

u7/u8 (`bc_ltunmentbind_*`): struct literals, no call — unchanged under all four.
