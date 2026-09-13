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

## BATCH 2 — the neighbour extensions, predicted by name before its build
Written after batch 1's HAND battery (build f63616863ddeadfa 43) and before batch 1's harness price was read.
Batch 1 hand corrections to the table above: `cooutall` did NOT close d24 — d24's argument `&n` has an EMPTY
region, so it needs the empty rule AND the method site, which no batch-1 name arms together. My prediction error.

| probe | doors | predicted ceiling | hand closes (illegal) |
|---|---|---|---|
| stmutdeclx | stmutdecl + the fill recurses into Array / Tuple / borrowed Slice | {issue-69114-static-mut-ty} | c10 m1i_param_elided m1i_nested_ref + m1i_array_elided m1i_tuple_elided m1i_str_slice; NOT m1i_option_elided (the initializer `Option::None` is refused first — a door upstream) |
| cooutsites | cooutstatt at the exact, METHOD, GENERIC and STATIC-CALL sites | {regions-static-bound} | c07 d26 m2i_named_caller m2i_struct_arg m2i_mut_ref m2i_method_param + m2i_generic_T_where_static m2i_static_method |
| cooutsitese | cooutsites + an EMPTY argument region violates `'a: 'static` | {regions-static-bound} | + c06 m2i_let_local_ref d24 |
| sdwhole | stmutdeclx ∪ cooutsitese (for the runtime column) | {issue-69114-static-mut-ty, regions-static-bound} | union |

Legal, predicted accepted under every name: every m1l_* / m2l_* program, c12 c14, the four pass fixtures, and
m2l_method_recv_bound / m2l_method_param_named_ok / m2l_method_caller_bound_ok / m2l_method_param_static.
Rule-14 risk named in advance: cooutall/cooutsites re-word an already-red NON-static bound at a method call
(hand m2i_method_nonstatic_unrelated prints "caller does not satisfy callee's outlives bound" instead of the return
mismatch) — the cfail column must be read for text changes, not rc.
