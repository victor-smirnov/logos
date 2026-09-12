# PREDICTION — 2026-09-12b, the plain-deref-write door of `lifereg.B`

Declared BEFORE editing the compiler, after the arrival census and before any repair.

## THE CLASS, ENUMERATED BY PROPERTY (not by spelling)
A statement arm that calls `place_write_loans` — i.e. the LOAN channel already
treats it as a write into a holder — but makes NO `note_holder_escape_prov`
deposit, so the PARAMS channel does not. By reading, SEVEN such doors exist:
`FieldWrite` `IndexWrite` `FieldIndexWrite` `ChainFieldWrite` `DerefFieldWrite`
`TupleWrite`, and `DerefWrite` for every `ptr.kind()` other than `AddrOfTemp`
and `MethodCall`.

## ARRIVAL CENSUS (probe `dwclassarr`, one build, 8 hand programs)
SIX of the seven are DEAD ARMS — `.stmt` did not fire once, so they are not
entered at all by the natural spelling; every one of those spellings lowers to
`DerefWrite(AddrOfTemp(...))`. ONE is live:

    dwclass.derefwritevar.stmt   1   dwclass.derefwritevar.params 1   (`*d = y`)
    dwclass.derefwritevar.stmt   1   dwclass.derefwritevar.params 1   (`*h.r = y`)

So the LIVE class is ONE door with TWO spellings. That is what is repaired.

## PREDICTION — A NUMBER AND A LIST
Rows closed: **1**
  - soundness_queue `lifereg_deref_store_param_admits`   (`*d = y`, VarRef ptr)
Additional programs closed, no row exists (new coverage, to be pinned):
  - `*h.r = y` (FieldRead ptr) — the same door, found by this census, not by the ledger

bc_admits ledger rows closed: **0**. `--t17` / `lifereg.NEW-B2` is a DIFFERENT
root — the `AddrOfTemp` descent's step set — and is NOT touched here.

## DECLINED BY NAME, WITH THE NUMBER
  - `d2 out[0]=y`, `d3 h.a[0]=y`, `d5 (*r).v=y` — admitted rc 0, `dwclass` arrival
    **0** and `liferegbpath.deposit` **0**. Root is the `AddrOfTemp` descent loop,
    which steps only through `FieldRead`/`TupleIndex` and BREAKS at `IndexRead`
    and at `Deref`. Not a missing door; a descent that stops. Separate root,
    separate round.

## LEGAL BATTERY — 14 programs, my own shapes
All 14 must stay rc 0 after the fix. Shapes the pricing round did not use:
loop-carried store, match-arm store, two-level `**e = y`, generic holder,
by-value scalar deref, arg-passed-not-stored, store with no return at all.
