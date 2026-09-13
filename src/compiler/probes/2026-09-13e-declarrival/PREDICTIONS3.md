# 2026-09-13e-declarrival — BATCH 3 PREDICTIONS, by name, written before its build

## Why a third batch
`whundeclm` (batch 2) guarded a MISSING TYPE_PARAMS key. Its census `where.noparams.subject` reads 242 on EVERY
compile, a program with no where clause included, so all 242 are prelude/stdlib subjects and a USER method
contributes zero arrivals. `collect_fn` calls `read_type_params(node)` unconditionally, and `read_type_params` has
THREE returns before `fold_where_bounds` (no key / NULL / no ITEMS); a user method with no own params takes the NULL
one. Batch 3 guards "does not reach the fold" instead of one spelling of it.

The runtime column is owed for every arm this round would recommend, and one configure should give ONE baseline and
ONE armed run. So batch 3 re-installs the candidate arms under their own names (a re-measure: a ceiling decays) and a
union name `rtunion` that arms all of them at once.

| probe | site | predicted closed set (bc_admits) | predicted cost |
|---|---|---|---|
| whundeclfn | sema.cpp `read_type_params`, every arrival that does NOT reach the fold | {outlives-with-missing} | UNSURE: `where Self: Sized` on no-param trait methods is common in the stdlib; the 242 prelude subjects all resolved in batch 2, which is evidence for 0, not proof |
| whundeclimpl | sema_collect.cpp `collect_impl`, trait impl header subject that is not an impl param | ∅ | 0/0/ok |
| whundeclall | fold fallback ∪ whundeclfn ∪ whundeclimpl | {outlives-with-missing} | = whundeclfn (the fold half never fired over the population) |
| c3enum | = batch-1 ctltenum0 without the type-arity arm | {constructor-lifetime-early-binding-error} | 0/0/ok (re-measure of 1/0/0/ok) |
| c3struct | = batch-1 ctltstruct | ∅ | 0/0/ok |
| c3unit | = batch-2 ctltunit | ∅ | 0/0/ok |
| k3const | = batch-2 acregionsl | {trait-associated-constant} | 0/0/ok (re-measure of 1/0/0/ok) |
| k3sig | = batch-2 sigimplrgnsl | ∅ | 0/0/ok |
| rtunion | all eight arms above | {outlives-with-missing, constructor-lifetime-early-binding-error, trait-associated-constant} | = the sum of the parts if additive; checked, not assumed |

Hand (predicted): whundeclfn closes r17_e, I_m07, wh8, I_w11, wh5 and the row; whundeclimpl closes I_w13; every
L_w*/L_m*/wh1x legal program keeps compiling and running.
