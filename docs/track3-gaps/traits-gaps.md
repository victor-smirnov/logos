# Traits gaps surfaced by Track 3 imports

The traits/ area is a major front of Logos-vs-Rust divergence (some
deliberate, some real gaps). Batch 9 is therefore narrow.

| ID | Surface | Gap | Surfaced by | Repro |
|---|---|---|---|---|
| T9-tr-01 | `pub fn name()` (no self) inside impl as constructor | Logos requires explicit `static fn` for an associated function (no self). Rust uses the same `fn` keyword with no self-param. This is a Logos convention (logos.peg:780+) — keeping it explicit makes the constructor visible in the grammar. Cross-link to M7-mt-01 (same family). | `anon-static-method` | `impl T { fn new() -> T { … } }` rejected — needs `static fn` |
| T9-tr-02 | mlir-gen mangled lookup for impl-on-primitive-T + generic trait method | `impl<A> thing<A> for isize { fn foo(self: &isize) -> Option<A> }` — calling `x.foo()` triggers `'func.call' op 'pkg.isize__foo__g__ref_isize' does not reference a valid function`. The monomorphisation key probably misses the `A`-binding for the inherent-method dispatch. | `early-vtbl-resolution` (not imported) | as above |
| T9-tr-03 | `Fn` / `FnMut` / `FnOnce` family | already logged as C5-cl-01 — also blocks most trait tests. | `assignability-trait`, `where-clause-vs-impl`, dozens | n/a |
| T9-tr-04 | dyn-trait coercions / `Box<dyn Trait>` | already logged as C6-cc-09 — blocks dyn-related trait tests. | `coercion-generic`, `alignment-gep-tup-like-1` | n/a |
| T9-tr-05 | Trait default method bodies | already logged as M7-mt-02 — blocks default-method tests. | `default_method_simple`, `issue-3979-generics`, `bug-7295`, `astconv-cycle-between-and-type` | `trait T { fn f(&self) {} }` |
