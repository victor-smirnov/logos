# Method-dispatch gaps surfaced by Track 3 imports

Status legend: `Open` — not started; `Partial` — partial fix landed,
notes inline; `✅ Closed` — gap closed (commit, date).

| ID | Surface | Status | Gap | Surfaced by | Repro |
|---|---|---|---|---|---|
| M7-mt-01 | Method name `new` inside a `trait { ... }` body | ✅ Closed (`77316df`, Sprint 1.1) | `fn new(self: &Self) -> bool;` inside a trait failed to parse. KW_NEW now admitted as identifier in fn/field/call positions. | `trait-method-resolution-7575` | as above |
| M7-mt-02 | Trait method default bodies | ✅ Closed (`6c38a27`, Sprint 1.2) — bind Self to primitive type for default-method inheritance; residual sema for some shapes may remain. | Logos required trait method declarations (no body); `trait T { fn f(&self) {} }` rejected. | `trait-method-resolution-7575` (rewritten to put bodies in impls) | `fn f(&self) {}` inside `trait { … }` |
| M7-mt-03 | Generic method-dispatch via `Sized` bound | ✅ Closed (2026-05-11) — `Sized` admitted as a compiler-builtin marker bound. check_trait_bounds_well_formed and check_type_bounds both `continue` on `trait_name == "Sized"` since every concrete Logos type is size-known at mono. `?Sized` opt-out remains unexpressible (no unsized types yet, so meaningless). | `T: Sized` was rejected as unknown trait. | `sized-marker-bound` (slim port; upstream `issue-24010` etc. depend on dyn-trait + Fn-trait-supertrait — separate gaps). | `fn id<T: Sized>(x: T) -> T` works |
| M7-mt-04 | UFCS-style explicit method call `Foo::bar(&self)` | ✅ Closed (2026-05-11) — call_expr's first alt already accepts `Class::method(args)`; verified end-to-end. | n/a | `ufcs-explicit-method-call` (slim port; upstream `ufcs-polymorphic-paths` needs HRTB/Cow which sit on other gaps) | `Foo::get(&f)` and `Foo::add(&f, 7)` work |
