# Method-dispatch gaps surfaced by Track 3 imports

Status legend: `Open` — not started; `Partial` — partial fix landed,
notes inline; `✅ Closed` — gap closed (commit, date).

| ID | Surface | Status | Gap | Surfaced by | Repro |
|---|---|---|---|---|---|
| M7-mt-01 | Method name `new` inside a `trait { ... }` body | ✅ Closed (`77316df`, Sprint 1.1) | `fn new(self: &Self) -> bool;` inside a trait failed to parse. KW_NEW now admitted as identifier in fn/field/call positions. | `trait-method-resolution-7575` | as above |
| M7-mt-02 | Trait method default bodies | ✅ Closed (`6c38a27`, Sprint 1.2) — bind Self to primitive type for default-method inheritance; residual sema for some shapes may remain. | Logos required trait method declarations (no body); `trait T { fn f(&self) {} }` rejected. | `trait-method-resolution-7575` (rewritten to put bodies in impls) | `fn f(&self) {}` inside `trait { … }` |
| M7-mt-03 | Generic method-dispatch via `Sized` bound | Deferred — needs `Sized` marker trait surface (auto-impl'd for all size-known types). Currently all Logos generics are implicitly sized at mono, but the syntactic `T: Sized` bound and the negative form `T: ?Sized` aren't wired. ~2-3 days. | `impl<T: Sized> …` — `Sized` not in Logos. | `method-two-traits-distinguished-via-where-clause` (not imported) | n/a |
| M7-mt-04 | UFCS-style explicit method call `Foo::bar(&self)` | ✅ Closed (2026-05-11) — call_expr's first alt already accepts `Class::method(args)`; verified end-to-end. | n/a | `ufcs-explicit-method-call` (slim port; upstream `ufcs-polymorphic-paths` needs HRTB/Cow which sit on other gaps) | `Foo::get(&f)` and `Foo::add(&f, 7)` work |
