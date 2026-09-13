# 2026-09-13e-declarrival — BATCH 2 PREDICTIONS, by name, written before its build

Batch 1 hand results (binary 9dfd8daabf28f2eb 43) that this batch answers:
- `whundecl` does NOT close outlives-with-missing: the row's `set_handler` has NO type-param list, and
  `read_type_params` returns before `fold_where_bounds` — the where clause is never read. It closes the free fn,
  impl header, struct and `where T: 'a` shapes; batch-1 pricing reports NEVER FIRED over the whole population.
- `acregion` does NOT close trait-associated-constant: its census fires 6 and refuses nothing, because `&'c str` is a
  SLICE and `subtype()`'s Slice arm never compares the slice's own region (`ltslicevar`/`ltslicelt` record the same
  missing observation). It DOES refuse the `&'c i64` / `Option<&'c i64>` / `(&'c i64, i64)` twins — doors in series.
- `sigimplrgn` is live (refuses `Option<&'c i64>` against trait `Option<&'b i64>`, admitted unarmed) and misses
  `&'c str` for the same Slice reason.
- `ctltenum` closes its row; the unit variant `E::N::<'static>` routes through `lower_enum_lit`, which never reads
  TYPE_PARAMS.

| probe | site | predicted closed set (bc_admits) | predicted cost |
|---|---|---|---|
| whundeclm | sema.cpp `read_type_params`, the no-type-param-list arrival | {outlives-with-missing} | UNSURE — `where Self: Sized` on trait methods with no own params is everywhere in the stdlib; if `Self` is not in `current_type_params_` at collect_trait time this refuses the stdlib |
| whundeclx | whundeclm ∪ the fold fallback | {outlives-with-missing} | = whundeclm (the fallback half never fired in batch 1) |
| acregionsl | sema_collect.cpp assoc const + slice-region walk | {trait-associated-constant} | 0/0/ok |
| ctltunit | sema_expr.cpp `lower_enum_lit` | ∅ | 0/0/ok; census `ctlt.unit.turbofish.present` may read 0 if the parser drops the turbofish on a unit path |
| sigimplrgnsl | sema_collect.cpp method return + slice-region walk | ∅ | UNSURE |

Hand (predicted): whundeclm closes I_m07, r17_e, wh5, wh8, I_w11, and must keep L_m01..L_m06, L_w01, L_w02 compiling.
acregionsl closes cn1, cn8, I_k15, r18_b and keeps L_k01..L_k07 compiling. sigimplrgnsl closes cn2 and keeps L_k10, L_k12.
