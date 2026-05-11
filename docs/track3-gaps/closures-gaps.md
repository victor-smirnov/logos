# Closure gaps surfaced by Track 3 imports

Logos has working closures as **local-typed bindings** with explicit
parameter and return types. The Rust surface that most rustc tests
exercise — `F: FnOnce(...) -> T` / `F: FnMut(...)` / `F: Fn(...)` as a
generic bound — is not in Logos. As a result, Batch 5 is very small
(3 tests). Closing the gap is a significant compiler-level project
(closure-as-trait-object lowering + Fn/FnMut/FnOnce trait family) and
sits with the trait-resolver work targeted for Track 3's gap-grind
phase.

| ID | Surface | Gap | Surfaced by | Repro |
|---|---|---|---|---|
| C5-cl-01 | `Fn` / `FnMut` / `FnOnce` trait family + closures as generic args | Logos has no `Fn*` trait family; closures can't be passed via `F: FnOnce()` bound. This blocks the majority of rustc closure tests (which pass closures into generic helpers). | `basic-closure-syntax`, `old-closure-arg-call-as`, `old-closure-iter-1`, `closure-mut-argument-6153`, dozens more | `fn force<F>(f: F) where F: FnOnce() -> isize` |
| C5-cl-02 | `move` keyword on closures | Required for moving captures out of the closure body in Rust; Logos closures' capture-mode determination needs to be checked. | `once-move-out-on-heap`, `moved-upvar-mut-rebind-11958` | `move \|\| { ... }` |
| C5-cl-03 | `ref` in closure parameter pattern | `\|ref x: T\| ...` binds `x: &T`; not supported. | `issue-5239-2` (rewritten) | `\|ref x: isize\| { *x }` |
| C5-cl-04 | `Box<dyn FnMut() + 'a>` (closure boxing) | Closure-as-trait-object boxing needs dyn-trait + Fn family. | `boxed-closure-lifetime-13808` | `let _: Box<dyn FnMut()> = Box::new(\|\| ());` |
| C5-cl-05 | Addr-of a captured local inside closure body | Already logged as B3-bg-04 in borrowck-gaps.md. Cross-link here. | `borrowck-closures-two-imm` (b/c arms) | `\|\| -> i32 { return get(&x); }` |
