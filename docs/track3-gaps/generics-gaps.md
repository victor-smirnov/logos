# Generics gaps surfaced by Track 3 imports

Status legend: `Open` — not started; `Partial` — partial fix landed,
notes inline; `✅ Closed` — gap closed (commit, date).
`Divergence` — deliberately not closing.

| ID | Surface | Status | Gap | Surfaced by | Repro |
|---|---|---|---|---|---|
| G3-tg-01 | Turbofish on an enum variant constructor | Open | `Enum::Variant::<T>(arg)` and `Enum::Variant::<T>(pat)` (in match) both fail to parse. | `generic-tag-local`, `generic-tag-match`, `generic-tag-values` | `clam::a::<isize>(3)` ⇒ "syntax error near 'a'" |
| G3-tg-02 | Bare `fn(T)` (no `-> ()`) at type position | ✅ Closed (2026-05-11) — fn_ptr_type grammar admits 3 new alts: with/without args × with/without `-> R`. sema defaults RET_TYPE to void. | Logos's fn-pointer type-grammar required an explicit return type. | `generic-temporary` (un-trimmed) | `fn apply<T>(c: fn(T)) {}` now parses |
| G3-tg-03 | Auto-Copy for scalar-only structs | ✅ Closed (2026-05-11) — `compute_auto_copy_types()` runs after `check_supertrait_impls`. Fixpoint promotes any struct with no `impl Drop` whose every field is itself Copy (primitive / raw ptr / ref / Copy struct / Copy enum / Copy tuple). Manual `impl Copy` entries still respected. | Rust copies all-scalar structs; Logos moved them, which forced `.clone()` clutter when porting. | `generic-fn`, `tests/logos/pass/struct_auto_copy` | `let q = id::<Triple>(p); let _ = p.z;` now compiles |
