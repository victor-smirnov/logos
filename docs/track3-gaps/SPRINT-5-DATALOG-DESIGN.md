# Sprint 5 — minimal Datalog trait resolver design

Status: **design only, no code**. Authored 2026-05-11 after the
autonomous Phase 1+2+3 sweep. Scoped for a focused follow-up session.

## Why Datalog (and why minimal)

Current trait resolution (`mono_has_impl_recursive` in
`src/compiler/mono_clone.cpp:3134`) walks impls and applies bounds
with a per-attempt `seen` set for cycle-cutting. It works for direct
impls and a single layer of blanket impls. It does **not** scale to:

- Closures as `Fn` / `FnMut` / `FnOnce` (each closure is a synthetic
  type with auto-generated impl — needs trait-resolver to *generate*
  facts, not just look them up)
- Higher-ranked trait bounds (`for<'a> Fn(&'a T)`)
- Generic associated types (resolution-time projection of
  `T::Iter::Item` requires substituting through projected types)
- Recursive blanket impls where the blanket's bound is itself a
  blanket — Logos handles depth 1 (`impl<T: A> B for T`) but
  multi-layer chains can cycle or fail.

The user constraint (2026-05-11): "Маленькую реализацию, только для
нужд компилятора и не более того." — i.e., no general Datalog
language / no rule-parser / no datalog-as-feature. Just a fixpoint
engine that the trait resolver delegates to.

## Scope of the minimal engine

In:
- Semi-naive fixpoint evaluation over a small set of relations.
- Cycle detection + termination.
- Substitution-aware matching (a single `SubstMap` flows through
  derivations).
- ~3-5 relations covering trait impls + auto-impls + assoc-type
  projections.
- Direct C++ data structures (no parser, no DSL).

Out:
- General Datalog as a language / rules from user code.
- Negation, aggregation, recursion-through-negation.
- Tabling beyond the simplest "have we seen this query?" memo.
- Performance — `O(n²)` semi-naive is fine for the test-import
  workload; optimise only when CI gets slow.

## Relations

- `impls(Trait, ConcreteType, ImplId)` — base facts from `impls_` /
  `assoc_type_impls_` / etc., loaded once per compilation unit.
- `auto_impl(Trait, ConcreteType)` — synthesised facts (Copy, Send,
  Sync auto-traits; Fn-family for closures).
- `assoc_type(Trait, ConcreteType, AssocName, ResolvedType)` —
  associated-type projection (today scattered across sema.cpp /
  mono_subst.cpp).
- `bound_satisfied(BoundTrait, Type, Subst)` — derived relation
  that combines `impls` with the `Subst` substitution flowing
  through current type-args.

## Rules (informal)

```
bound_satisfied(T, X, S)         :- impls(T, X, _).
bound_satisfied(T, X, S)         :- auto_impl(T, X).
bound_satisfied(T, app(Op, Xs), S) :- impls(T, app(Op, Vs), I),
                                       unify(Xs, Vs, S').
bound_satisfied(T, X, S)         :- impls(T, BlanketImpl), 
                                     BlanketImpl is `impl<U: A> T for U`,
                                     bound_satisfied(A, X, S).
auto_impl(Fn,    Closure(args, ret)).
auto_impl(FnMut, Closure(args, ret)).
auto_impl(FnOnce,Closure(args, ret)).
```

(`Fn` / `FnMut` / `FnOnce` rules are the Sprint 5 keystone — they
make every closure carry these impls automatically.)

## Integration

- Replace `mono_has_impl_recursive` (in mono_clone.cpp) with a call
  into the engine.
- Replace `bound_check_*` paths in sema.cpp (currently several
  open-coded loops) with engine calls.
- `assoc_type_impls_` map becomes a relation; current lookups read
  the relation.
- Closure construction in sema_expr.cpp emits `auto_impl` facts at
  closure-typing time.

The integration is incremental: write the engine + relations, port
`mono_has_impl_recursive` first (clear semantics), then port sema
sites one at a time. Each port is one PR with a green ctest.

## Closure-as-trait-object (separate, but interrelated)

Adding the Fn family to the resolver isn't enough; closures need:
- A trait-object representation (`Box<dyn Fn(…)>` etc.).
- Dispatch lowering (vtable for the trait object).

This is its own ~400 LOC of mlir-gen. Keep it on the Sprint 5
follow-up — start with `impl FnOnce for Closure` (most permissive)
and `Box<dyn FnOnce(…) -> R>` as the first target.

## File layout (proposed)

- `src/compiler/trait_engine.hpp` — relations + engine API
- `src/compiler/trait_engine.cpp` — semi-naive fixpoint + cycle
  detection
- `src/compiler/trait_engine_facts.cpp` — fact-loader (reads from
  sema's impl_ tables, closure types, auto-trait list)
- `src/compiler/mono_clone.cpp` — `method_bound_ok` calls into
  trait_engine
- `src/compiler/sema.cpp` — `bound_check_*` calls into trait_engine

Expected total: ~1k LOC new code + ~200 LOC integration churn.

## Estimated effort

| Step | LOC | Days |
|---|---|---|
| Engine skeleton + tests | 400 | 1-2 |
| Fact loaders | 250 | 1 |
| Closure auto-impls (Fn/FnMut/FnOnce) | 200 | 1 |
| Replace `mono_has_impl_recursive` | 100 | 1 |
| Port sema bound-check sites | 150 | 1-2 |
| Closure-as-trait-object lowering | 400 | 2-3 |
| Regression sweep + un-trim tests | — | 1-2 |

**Total: ~1500 LOC over ~8-12 working days.**

## Acceptance criteria

1. All current tests pass (1269/1269 baseline).
2. The closures-gaps tests un-trim (C5-cl-01, C5-cl-04).
3. The traits-gaps tests un-trim (T9-tr-03).
4. New imports possible from rustc `iterators/`, `closures/`,
   `methods/` that previously needed Fn-bounds.

## Not started in 2026-05-11 session

This is a multi-day project. The autonomous session that produced
this doc stayed inside grammar/sema-level changes; the trait engine
remains the largest open Phase 2 item.
