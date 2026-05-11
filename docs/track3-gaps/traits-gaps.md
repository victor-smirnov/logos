# Traits gaps surfaced by Track 3 imports

The traits/ area is a major front of Logos-vs-Rust divergence (some
deliberate, some real gaps). Batch 9 was therefore narrow.

Status legend: `Open` — not started; `Partial` — partial fix landed,
notes inline; `✅ Closed` — gap closed (commit, date).

| ID | Surface | Status | Gap | Surfaced by | Repro |
|---|---|---|---|---|---|
| T9-tr-01 | `pub fn name()` (no self) inside impl as constructor | Open — design choice; keeping `static fn` explicit may stay. | Logos requires explicit `static fn` for an associated function (no self). Rust uses the same `fn` keyword with no self-param. Cross-link to M7-mt-01. | `anon-static-method` | `impl T { fn new() -> T { … } }` rejected — needs `static fn` |
| T9-tr-02 | mlir-gen mangled lookup for impl-on-primitive-T + generic trait method | Open | `impl<A> thing<A> for isize { fn foo(self: &isize) -> Option<A> }` — calling `x.foo()` triggers `'func.call' op 'pkg.isize__foo__g__ref_isize' does not reference a valid function`. | `early-vtbl-resolution` (not imported) | as above |
| T9-tr-03 | `Fn` / `FnMut` / `FnOnce` family | ✅ Closed via C5-cl-01 (Sprint 5 epic) | already logged as C5-cl-01 — also blocked most trait tests. | `assignability-trait`, `where-clause-vs-impl`, dozens | n/a |
| T9-tr-04 | dyn-trait coercions / `Box<dyn Trait>` | Open — see C6-cc-09 / Sprint 5.8 | already logged as C6-cc-09 — blocks dyn-related trait tests. | `coercion-generic`, `alignment-gep-tup-like-1` | n/a |
| T9-tr-05 | Trait default method bodies | ✅ Closed via M7-mt-02 (`6c38a27`, Sprint 1.2) | already logged as M7-mt-02. | `default_method_simple`, `issue-3979-generics`, `bug-7295`, `astconv-cycle-between-and-type` | `trait T { fn f(&self) {} }` |
