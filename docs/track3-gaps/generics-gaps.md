# Generics gaps surfaced by Track 3 imports

Status legend: `Open` — not started; `Partial` — partial fix landed,
notes inline; `✅ Closed` — gap closed (commit, date).
`Divergence` — deliberately not closing.

| ID | Surface | Status | Gap | Surfaced by | Repro |
|---|---|---|---|---|---|
| G3-tg-01 | Turbofish on an enum variant constructor | Open | `Enum::Variant::<T>(arg)` and `Enum::Variant::<T>(pat)` (in match) both fail to parse. | `generic-tag-local`, `generic-tag-match`, `generic-tag-values` | `clam::a::<isize>(3)` ⇒ "syntax error near 'a'" |
| G3-tg-02 | Bare `fn(T)` (no `-> ()`) at type position | Open | Logos's fn-pointer type-grammar requires an explicit return type. `fn(T) -> ()` works. | `generic-temporary` | `fn apply<T>(c: fn(T)) {}` ⇒ "syntax error near 'fn'" |
| G3-tg-03 | Auto-Copy for scalar-only structs | Divergence | Rust's `#[derive(Copy)]` opt-in vs Logos's "everything is move by default, even all-scalar structs" — known divergence. No fix planned. | `generic-fn` | `let q = id::<Triple>(p); let _ = p.z;` ⇒ "use of moved variable 'p'" |
