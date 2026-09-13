# 2026-09-13e-declarrival — PREDICTIONS, by name, written before the build

Base binary 4cf0bf5e5e07b0cf 43. Spec: declarrival.spec (11 names; the empty records are twins/unions armed
inside another record's edit — one name per DECISION, the site is named in each record).

| probe | site | predicted closed set (bc_admits) | predicted cost |
|---|---|---|---|
| whundecl | sema.cpp `fold_where_bounds` add-fallback | {outlives-with-missing} | pass 0, cfail 0, stdlib ok — UNSURE: the fallback's comment says it exists for "a genuinely-undeclared type-PARAM name"; any legal where-subject that is in scope but not yet in `current_type_params_` when read would be refused |
| ctltenum | sema_expr.cpp `lower_enum_lit_data_from_static` | {constructor-lifetime-early-binding-error} | 0/0/ok |
| ctltenum0 | same site, also a lifetime turbofish on an enum with NO lifetime params | {constructor-lifetime-early-binding-error} | 0/0/ok |
| cttyenum | same site, TYPE-arg count at the ctor turbofish | ∅ | UNSURE — partial turbofish; non-zero cost would not surprise |
| ctltstruct | sema_expr.cpp `lower_struct_lit` | ∅ | 0/0/ok |
| ctltall | ctltenum ∪ ctltstruct | {constructor-lifetime-early-binding-error} | 0/0/ok |
| acregion | sema_collect.cpp `collect_impl` assoc const (elided const regions filled 'static) | {trait-associated-constant} | 0/0/ok |
| acregionraw | same, WITHOUT the 'static fill (the inner predicate's twin, rule 9) | {trait-associated-constant} | harness columns likely identical to acregion; separates on a hand program (`const NAME: &str` against `&'static str`) |
| sigimplrgn | sema_collect.cpp `collect_impl` method-signature match, return slot | ∅ | UNSURE; neighbour of trait-associated-constant by fact, different site |
| ksall | acregion ∪ sigimplrgn | {trait-associated-constant} | = acregion + sigimplrgn if additive; checked, not assumed |

Neighbour programs predicted (hand, on the batch binary):
- whundecl closes the free-fn, inherent-method and trait-impl-method where shapes (all read `read_type_params` ->
  `fold_where_bounds`); predicted NOT to close the impl header (`read_type_params_from` does not fold), the struct
  where (`collect_struct` reads WHERE only for outlives), the trait method decl.
- ctltenum closes `E::V::<'static,'static,'static>` too (same row); ctltenum0 adds `E::V::<'static>` on a
  lifetime-free enum; ctltstruct closes `S::<'static,'static,'static> { .. }`; the unit variant `E::N::<'static>` is
  predicted NOT to route through `lower_enum_lit_data_from_static` (unknown route; census `ctlt.enum.turbofish`).
- acregion closes `&'c str` vs `&'b str` and `Option<&'b str>` vs `Option<&'a str>` under `'a: 'b`.
- sigimplrgn closes a method return `&'c str` vs trait `&'b str`; not the PARAM slot (not compared).
