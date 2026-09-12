# RESULT — 2026-09-12b, the plain-deref-write door of `lifereg.B`

## WHAT LANDED
One structural change in `borrow_check.cpp::visit_stmt` `Code::DerefWrite`: the
fall-through (`ptr.kind()` neither `AddrOfTemp` nor `MethodCall`) now makes the
`note_holder_escape_prov` deposit that every other live write door already made.
12 lines of code, 9 of comment.

## THE CLASS, ENUMERATED BY PROPERTY — AND SIX SEVENTHS OF IT IS DEAD
The property: an arm that calls `place_write_loans` (the LOAN channel treats it
as a write into a holder) but makes no `note_holder_escape_prov` deposit (the
PARAMS channel does not). By reading, SEVEN arms qualify. The arrival census
(probe `dwclassarr`, one build, 8 hand programs, gate reproduced verbatim from
the deposit's own) measured:

| door | `.stmt` | `.params` | disposition |
|---|---|---|---|
| `FieldWrite` | 0 | 0 | DEAD ARM |
| `IndexWrite` | 0 | 0 | DEAD ARM |
| `FieldIndexWrite` | 0 | 0 | DEAD ARM |
| `ChainFieldWrite` | 0 | 0 | DEAD ARM |
| `DerefFieldWrite` | 0 | 0 | DEAD ARM |
| `TupleWrite` | 0 | 0 | DEAD ARM |
| `DerefWrite` non-AddrOfTemp | **1** | **1** | **LIVE — repaired** |

Every dead arm's natural spelling lowers to `DerefWrite(AddrOfTemp(...))` and is
already served. This CONFIRMS and EXTENDS the file's own recorded claim, which
said only `FieldWrite` and `TupleWrite` were dead; four more are.

⚠ THE ENUMERATION BY READING WAS 7 AND THE LIVE CLASS IS 1. A repair priced
against the reading would have edited six arms that never execute — rule 1, and
the reason the census ran before the repair rather than after.

## CLOSED SET, DIFFED BOTH WAYS
Predicted `{d7 *d=y, d7b *h.r=y}`; actual the same. **Both differences empty.**
Diagnostic READ on both, identical and correct — the Logos sentence for upstream
E0621, the same one `--c17` pins:
`lifetime mismatch: return type has lifetime 'a but 'y' has lifetime (elided)`

## WHERE THE FIX DIFFERS FROM ITS PROBE — IT IS SHAPE-GENERAL
Three spellings the prediction did NOT name also closed, 0 -> 1, all previously
admitted and all illegal: loop-carried store, match-arm store, two-level
`**e = y`. Same door, different control flow reaching it. A crude probe and a
correct fix do not close the same programs (rule 7) — here the fix closes MORE,
and they are named rather than discovered later.

## COST — EVERY COLUMN
`stdlib-cost.sh` rc 0, all four layers compile. L1 807/807 pass fixtures, failure
list READ. 14-program legal battery (my own shapes: loop, match, two-level,
generic holder, by-value scalar, arg-not-stored, no-return-at-all) — **0 legal
refusals**, base and fixed identical on all 14.
⚠ One battery program (L07) was rc 1 on the BASE binary. It was MY program's bug
(`let d`, not `let mut d`), not a compiler defect — corrected and re-measured,
rather than reported as a finding.

## TWO NEW ROWS, BOTH MEASURED ON THE BINARY THAT CLOSED THIS DOOR
so neither is this door in another spelling.
1. `lifereg_derefwrite_descent_stops_at_deref_admits` — the `AddrOfTemp` descent
   steps only through `FieldRead`/`TupleIndex` and BREAKS at `IndexRead` and at
   `Deref`. `(*r).v = y` admits where `h.v = y` refuses. Deposit 0.
   ⚠ The INDEX half of this same root is already carried by bc_admits
   `--t17`; only the `Deref` half was unnamed.
2. `lifereg_field_reborrow_deposit_unread_admits` — **deposit 1, read 0.** The
   door fires and the program still admits, because `&mut r.v` keys the record
   on a field PLACE while `return w.v` reads through the root.
   ⚠ THIS IS THE ROUND'S SHARPEST FINDING: a door-census of 1 is not a closure.
   Three of this arc's rounds have used "the deposit fired" as evidence a door
   was closed; here it fires and the defect stands.
