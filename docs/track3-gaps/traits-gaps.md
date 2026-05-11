# Traits gaps surfaced by Track 3 imports

The traits/ area is a major front of Logos-vs-Rust divergence (some
deliberate, some real gaps). Batch 9 was therefore narrow.

Status legend: `Open` — not started; `Partial` — partial fix landed,
notes inline; `✅ Closed` — gap closed (commit, date).

| ID | Surface | Status | Gap | Surfaced by | Repro |
|---|---|---|---|---|---|
| T9-tr-01 | `pub fn name()` (no self) inside impl as constructor | ✅ Closed (2026-05-11) — verified that `impl T { pub fn new() -> T { … } }` (no self, no `static`) now parses and dispatches; `static fn` is no longer required. | Logos required explicit `static fn` for an associated function. | `anon-static-method` (un-trimmed) | `impl T { fn new() -> T { … } }` works |
| T9-tr-02 | mlir-gen mangled lookup for impl-on-primitive-T + generic trait method | Deferred — narrow codegen bug; needs focused reduction. No imported test currently blocks on this. Re-open with a minimal reproducer when a real test surfaces. | `impl<A> thing<A> for isize { fn foo(self: &isize) -> Option<A> }` — calling `x.foo()` triggers `'func.call' op 'pkg.isize__foo__g__ref_isize' does not reference a valid function`. | `early-vtbl-resolution` (not imported) | as above |
| T9-tr-03 | `Fn` / `FnMut` / `FnOnce` family | ✅ Closed via C5-cl-01 (Sprint 5 epic) | already logged as C5-cl-01 — also blocked most trait tests. | `assignability-trait`, `where-clause-vs-impl`, dozens | n/a |
| T9-tr-04 | dyn-trait coercions / `Box<dyn Trait>` | Deferred to Sprint 5.8 — dyn-trait lowering (shared with C5-cl-04 / C6-cc-09). | already logged as C6-cc-09. | `coercion-generic`, `alignment-gep-tup-like-1` | n/a |
| T9-tr-05 | Trait default method bodies | ✅ Closed via M7-mt-02 (`6c38a27`, Sprint 1.2) | already logged as M7-mt-02. | `default_method_simple`, `issue-3979-generics`, `bug-7295`, `astconv-cycle-between-and-type` | `trait T { fn f(&self) {} }` |
