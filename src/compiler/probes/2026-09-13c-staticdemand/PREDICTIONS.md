# 2026-09-13c-staticdemand — PREDICTIONS BY NAME, written before the build

Priced by probe-batch: stmutassign, stmutdecl, cooutstatt, cooutall.
Hand-armed only (same build): stmutbare, cooutstat, cooutstate.

| probe | predicted ceiling set | predicted cost |
|---|---|---|
| stmutassign | {issue-69114-static-mut-ty} | 0 pass / 0 cfail / stdlib ok |
| stmutbare | {} (door 2 missing: the row's `&u8` stays empty) | 0 |
| stmutdecl | {issue-69114-static-mut-ty} | 0; risk = reads of a filled static type |
| cooutstat | {regions-static-bound} IF a param's elided region is minted at expr_type; else {} | 0 |
| cooutstatt | = cooutstat (the row has no transitive pair) | 0 |
| cooutstate | cooutstatt ∪ nothing on the ledger | 0 corpus; hand risk = an elided-annotation local that is really 'static |
| cooutall | cooutstatt ∪ ? (method site checks ALL outlives pairs, not only 'static) | UNKNOWN, may be non-zero |

Hand verdicts predicted (illegal → refuse): c09 c10 c13 row-69114 under stmutassign/stmutdecl; c09 c13
under stmutbare; c07 row-L2 under cooutstat; +d26 under cooutstatt; +c06 under cooutstate; +d24 under cooutall.
Legal → accept under every name: c12 l05 l11 c14 region_2 regions-static-bound-ok region-where-outlives-static-id-rg
regions-static-bound-rpass, plus the M1/M2 hand battery written during the build.
Diagnostic predicted: M1 `assignment to 'BAR': variance mismatch — expected &'static u8, got &u8`;
M2 `call to 'static_id': borrowed data escapes — the callee's bound `'a: 'static` requires a 'static argument`.
