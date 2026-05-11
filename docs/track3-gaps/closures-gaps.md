# Closure gaps surfaced by Track 3 imports

Logos has working closures as **local-typed bindings** with explicit
parameter and return types. The Rust surface that most rustc tests
exercise — `F: FnOnce(...) -> T` / `F: FnMut(...)` / `F: Fn(...)` as a
generic bound — was the biggest single import blocker. Sprint 5
(Datalog trait resolver + Fn-family bound syntax + fn-ptr-as-Fn-bound)
closed the core, landing 2026-05-10/11.

Status legend: `Open` — not started; `Partial` — partial fix landed,
notes inline; `✅ Closed` — gap closed (commit, date).

| ID | Surface | Status | Gap | Surfaced by | Repro |
|---|---|---|---|---|---|
| C5-cl-01 | `Fn` / `FnMut` / `FnOnce` trait family + closures as generic args | ✅ Closed (`f7c1cfa` + `8c3f468` + `34d45c5` + `3998c54` + `74a7fe4`, Sprint 5 epic) — Datalog trait engine, Fn-family shape-auto-impl, `F: Fn(args) -> R` bound syntax, fn-ptr-as-Fn-bound. | Logos had no `Fn*` trait family; closures couldn't be passed via `F: FnOnce()` bound. Blocked majority of rustc closure tests. | `basic-closure-syntax`, `old-closure-arg-call-as`, `old-closure-iter-1`, `closure-mut-argument-6153`, dozens more | `fn force<F>(f: F) where F: FnOnce() -> isize` |
| C5-cl-02 | `move` keyword on closures | Open | Required for moving captures out of the closure body in Rust; Logos closures' capture-mode determination needs to be checked. | `once-move-out-on-heap`, `moved-upvar-mut-rebind-11958` | `move \|\| { ... }` |
| C5-cl-03 | `ref` in closure parameter pattern | Open | `\|ref x: T\| ...` binds `x: &T`; not supported. | `issue-5239-2` (rewritten) | `\|ref x: isize\| { *x }` |
| C5-cl-04 | `Box<dyn FnMut() + 'a>` (closure boxing) | Open — needs Sprint 5.8 (closure-as-trait-object dyn lowering). | Closure-as-trait-object boxing needs dyn-trait + Fn family. | `boxed-closure-lifetime-13808` | `let _: Box<dyn FnMut()> = Box::new(\|\| ());` |
| C5-cl-05 | Addr-of a captured local inside closure body | Open | Already logged as B3-bg-04 in borrowck-gaps.md. Cross-link here. | `borrowck-closures-two-imm` (b/c arms) | `\|\| -> i32 { return get(&x); }` |
