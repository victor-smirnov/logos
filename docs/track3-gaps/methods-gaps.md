# Method-dispatch gaps surfaced by Track 3 imports

Status legend: `Open` — not started; `Partial` — partial fix landed,
notes inline; `✅ Closed` — gap closed (commit, date).

| ID | Surface | Status | Gap | Surfaced by | Repro |
|---|---|---|---|---|---|
| M7-mt-01 | Method name `new` inside a `trait { ... }` body | ✅ Closed (`77316df`, Sprint 1.1) | `fn new(self: &Self) -> bool;` inside a trait failed to parse. KW_NEW now admitted as identifier in fn/field/call positions. | `trait-method-resolution-7575` | as above |
| M7-mt-02 | Trait method default bodies | ✅ Closed (`6c38a27`, Sprint 1.2) — bind Self to primitive type for default-method inheritance; residual sema for some shapes may remain. | Logos required trait method declarations (no body); `trait T { fn f(&self) {} }` rejected. | `trait-method-resolution-7575` (rewritten to put bodies in impls) | `fn f(&self) {}` inside `trait { … }` |
| M7-mt-03 | Generic method-dispatch via `Sized` bound | Open | `impl<T: Sized> A for *const T` chained against `impl<T> B for *const [T]` — `Sized` not in Logos. | `method-two-traits-distinguished-via-where-clause` (not imported) | n/a |
| M7-mt-04 | UFCS-style explicit method call `Foo::bar(&self)` | Open | Not yet tested — most rustc tests using it sit on other gaps too. | (future) | n/a |
