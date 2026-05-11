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
| C5-cl-02 | `move` keyword on closures | ✅ Closed (2026-05-11) — `move ||` parses + lowers correctly, capture-by-move respected. Tested with `FnOnce` bound. | `move \|\| { ... }` works. | `once-move-out-on-heap` (slim port — original needs auto-Deref for Box) | `foo(move \|\| -> i64 { return x; });` works |
| C5-cl-03 | `ref` in closure parameter pattern | Divergence — `ref`-binding in patterns is Rust-specific borrow-mode syntax. Logos closures take params by their declared type; rewrite as `\|x: &T\|` (explicit ref-typed param) at the call site. | `\|ref x: T\| ...` rejected. | `issue-5239-2` (rewritten) | rewrite to `\|x: &isize\| { *x }` |
| C5-cl-04 | `Box<dyn FnMut() + 'a>` (closure boxing) | Deferred to Sprint 5.8 — closure-as-trait-object dyn lowering arc (shared with C6-cc-09 / T9-tr-04). | Closure-as-trait-object boxing needs dyn-trait + Fn family. | `boxed-closure-lifetime-13808` | `let _: Box<dyn FnMut()> = Box::new(\|\| ());` |
| C5-cl-05 | Addr-of a captured local inside closure body | Deferred — sema/codegen slice; see B3-bg-04. Crashes mlir-gen with "& undefined 'x'" when closure body takes `&captured_local`. Needs capture-spill or ref-mode capture inference. | Cross-link to B3-bg-04. | `borrowck-closures-two-imm` (b/c arms) | `\|\| -> i32 { return get(&x); }` |
