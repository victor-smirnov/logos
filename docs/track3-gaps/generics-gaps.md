# Generics gaps surfaced by Track 3 imports

| ID | Surface | Gap | Surfaced by | Repro |
|---|---|---|---|---|
| G3-tg-01 | Turbofish on an enum variant constructor | `Enum::Variant::<T>(arg)` and `Enum::Variant::<T>(pat)` (in match) both fail to parse. Type-argument inference at the call/match site fills it in. The explicit-turbofish form should be accepted for consistency with type/fn-call sites. | `generic-tag-local`, `generic-tag-match`, `generic-tag-values` | `clam::a::<isize>(3)` ⇒ "syntax error near 'a'" |
| G3-tg-02 | Bare `fn(T)` (no `-> ()`) at type position | Logos's fn-pointer type-grammar requires an explicit return type. `fn(T) -> ()` works. | `generic-temporary` | `fn apply<T>(c: fn(T)) {}` ⇒ "syntax error near 'fn'" |
| G3-tg-03 | Auto-Copy for scalar-only structs (Logos divergence) | Rust's `#[derive(Copy)]` opt-in vs Logos's "everything is move by default, even all-scalar structs" — known divergence; flagged here for the record. No fix planned. | `generic-fn` | `let q = id::<Triple>(p); let _ = p.z;` ⇒ "use of moved variable 'p'" |
