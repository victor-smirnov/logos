# 2026-09-12r-escroot — PREDICTIONS BY NAME, WRITTEN BEFORE THE BATCH BUILT

Base build `fc8539b37318b78a 43`. Spec `escroot.spec`. Record labels are arbitrary; the ARMS:

| probe | door C (sema: static, method, generic binders) | deref stop | call arm | let door |
|---|---|---|---|---|
| stcg  | yes | – | – | – |
| esdrf | – | yes | – | – |
| escnd | – | – | yes (old walk) | – |
| esc   | – | yes | yes | – |
| lbd   | – | yes | yes | yes |
| lrall | yes | yes | yes | yes |

## LEDGER ROWS (bc_admits) PREDICTED CLOSED — NUMBER AND NAMES

    stcg   0
    esdrf  0
    escnd  0
    esc    0
    lbd    2  method-ufcs-inherent-4, regions-free-region-ordering-caller1
    lrall  3  method-ufcs-inherent-3, method-ufcs-inherent-4, regions-free-region-ordering-caller1

Not closed by any: adt-tuple-enums--t33 (the `'static` is decided at a ctor argument; let door
keys on FUNCTION binders only). Uncertain, not claimed: any row whose program is `*out = f(&local)`.

## QUEUE ROWS PREDICTED CLOSED (hand battery; the queue gate is not a pricing column)

    stcg, lrall: static_call_callee_region_named_in_result_refuses,
                 method_call_fn_binder_in_result_refuses (compile AND run 0)

## HAND BATTERY (scratchpad r0912r/ctl + r0912q/ctl), predicted verdicts

Newly REFUSED (illegal, base admitted):
    escnd: x1 x2 k14 k19 k20
    esc:   x1 x2 k14 k19 k20 k22
    lbd:   esc's set + d2 n4 k24 method-ufcs-inherent-4 caller1-shape
    lrall: lbd's set + q6 (re-refused by the call arm) + x6 (idem)
Newly COMPILING (legal, base refused):
    stcg:  a6 a10 e3 m1 m3 m5 o3 m4 + both queue programs
    esdrf, esc, lbd, lrall: y1 y2 y3 ; lrall also stcg's set
UN-REFUSAL RISK: stcg alone un-refuses q6 and x6 (measured 12q for q6) — stcg is NOT a landing
candidate by itself.
PREDICTED COST (legal, must stay compile+run): k2 k3 k4 k5 k6 k8 k11 k12 k13 k15 k16 k17 k18
k25 k26 k27 x8 + 12q's legal battery (L*, n7..n12, p13..p26, r4..r7, a3 a11 a12 e4 e5 o1 o2).
Predicted ESCND-ONLY COST: k17 k18 (old walk names the reference local through the call arm).
Not reached by any arm (measured admitted, predicted unchanged): x3 x10 (VarRef of a local
reference — no arm), x5 x9 (field write through a param — excluded plane), k21 (EnumLitData),
k23 (`&(call)` temp — temp case keys on AddrOf/AddrOfTemp inner only).
