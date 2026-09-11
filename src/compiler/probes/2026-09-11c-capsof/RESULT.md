ROUND 2026-09-11c — RESULT, against PREDICTION-2026-09-11c.txt

CEILING, measured over all 87 bc_admits programs on armed 401c273bb5a28b66 43:
    REFUSED issue-58776-borrowck-scans-children ::
        error [fn main]: cannot assign to 'greeting' because it is borrowed
    ... and nothing else.
  predicted = { issue-58776-borrowck-scans-children }
  predicted \ actual = EMPTY
  actual \ predicted = EMPTY
The GREP population (an immediately-invoked closure, by spelling, over the 87
programs) and the PROPERTY population agree at 1 here — stated because they did
not have to.

bc_admits_blocked.ledger, all 8 programs: 0 newly refused. issue-75904-move-
closure-loop (BUCKET-3 DIVERGENCE-A16) still compiles, so the blessed
divergence is not costed.

COST
  ctest -L bc, gate-db compare 1033 (base fbb52c741c4e8469, env e3b0c44298fc)
    -> 1040 (armed 401c273bb5a28b66, env e3b0c44298fc):
       2756 tests measured under both, 0 CHANGED.
  scripts/stdlib-cost.sh: all four layers compile.
  23 legal hand programs, shapes varied not counted: 0 refused.
  2 illegal hand programs: both refused, each with the sentence its
    named-closure twin already printed one token apart.

WHERE THE LANDING DIFFERS FROM ANYTHING THE PRICING ROUND MEASURED (rule 7):
it is not a capture DEPOSIT at all. No new loan is minted anywhere; one early
exit is removed from a name-keyed lookup, and four existing consumers — the
loan channel, the provenance channel, the source walk and the co-holder walk —
start seeing a callee they were structurally blind to. That is why the four
legal programs `capvisloanb` refused are untouched: they never enter the loan
channel, whose gate is the call RESULT's type.

DECLINED, each with its number: see PROBES.md 2026-09-11c-capsof §6.
