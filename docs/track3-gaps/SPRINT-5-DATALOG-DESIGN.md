# Sprint 5 — minimal Datalog trait resolver

> **Status: implemented + integrated.** Phase 1 engine landed in
> commit `f7c1cfa` (sprint 5 phase 1: minimal Datalog-style trait
> resolver engine, no integration); Mono wiring landed in `8c3f468`
> (sprint 5.4+5.6: route `mono_has_impl_recursive` through the
> engine); closure shape-auto-impl in `34d45c5`; fn-ptr-as-Fn-bound
> in `74a7fe4`. C5-cl-01 (Fn-family + closures as generic args) and
> C5-cl-08 (by-ref capture for mutated scalars) sit on top.
>
> The remainder of this doc is now the **implementation report**,
> not a design proposal. See "Extension points" at the bottom for
> what's worth touching when the next resolver-shape pressure
> surfaces.

## Why a separate engine (recap)

Before Sprint 5 the entire trait resolver was an ad-hoc walker in
`mono_clone.cpp` — `mono_has_impl_recursive` looked through
`concrete_impls_` (direct impls) and `blanket_impls_` (one-level
blankets) with a `seen` set for cycle-cutting. It worked for the
direct + single-blanket case but didn't extend cleanly to:

- Closure-as-`Fn`/`FnMut`/`FnOnce` — every closure is a synthetic
  type; we needed the resolver to **generate** facts (every closure
  shape satisfies `Fn*`) rather than just look them up.
- Recursive blanket chains where the blanket's bound is itself a
  blanket (multi-layer `impl<T: A> B for T` / `impl<T: B> C for T`).
- Auto-trait derivation under struct fields / tuple elements (the
  parallel `sema_auto_trait.cpp` machinery — structural recursion
  with explicit unsafe-impl carve-outs).

User constraint (2026-05-11): "Маленькую реализацию, только для
нужд компилятора и не более того" — no general Datalog language,
no rule parser, no user-facing surface. Just a fixpoint engine that
the existing sites delegate to.

## What landed

### Phase 1: pure-data engine — `src/compiler/trait_engine.{hpp,cpp}`

219 LOC of `.cpp` + 159 LOC of `.hpp` exposing a single class
`logos::compiler::trait_engine::TraitEngine`. String-keyed
relations (TraitName + TypeName are `std::string`); substitution
deliberately out-of-scope at this phase (sema/mono substitute
**before** querying — the engine sees only concrete instantiations).

Fact kinds:

| Kind | Constructor | Semantics |
|---|---|---|
| Direct | `add_impl(T, X)` | `impls(T, X)` — explicit `impl T for X` |
| Blanket (single bound) | `add_blanket(T, Tb)` | ∀ X: Tb. `impls(T, X)` |
| Blanket (multi-bound AND) | `add_blanket(T, {Tb1, Tb2, …})` | every bound must hold |
| Auto | `add_auto_impl(T)` | unconditional impl-for-all |
| Shape-auto | `add_shape_auto_impl(T, tag, predicate)` | impl-for-all-types-of-this-shape |
| Negative | `add_negative(T, X)` | beats every positive derivation |

Empty `bounds` on a blanket = unconditional impl-for-all (used to
preserve the prior "blanket with no bound" semantics from
`blanket_impls_`).

Queries:

- `satisfies(T, X) -> bool`
- `resolve(T, X) -> ImplId` (NO_IMPL on miss; first matching
  derivation wins for diagnostics)
- `trace_satisfies(T, X) -> vector<string>` — breadcrumb of which
  rule fired

Derivation order (first match wins, inside `resolve_impl_`):

1. Negative carve-out → NO_IMPL.
2. Memo hit → cached result.
3. Cycle-in-flight → NO_IMPL (the outer rule tries alternatives).
4. (D) Direct fact.
5. (B) Blanket — every bound recursively `satisfies`.
6. (A) Auto.
7. (S) Shape-auto.

A strict semi-naive fixpoint isn't needed at this phase since each
query can only multiply through blanket chains (no other recursive
rule forms). Memo + cycle guard are the practical pieces.

### Phase 2: Mono integration — `src/compiler/mono_clone.cpp`

Mono carries a `trait_engine::TraitEngine trait_engine_` member +
a `bool trait_engine_dirty_` flag (declared in
`src/compiler/mono_impl.hpp:169-174`). The engine is **rebuilt
lazily** from Mono's own tables whenever a query arrives after a
dirty mark:

```cpp
// Mono::populate_trait_engine_() — mono_clone.cpp:3092
trait_engine_ = trait_engine::TraitEngine{};   // fresh
for (auto& k : concrete_impls_) /* split "trait::type", add_impl */;
for (auto& bi : blanket_impls_) /* primary + extra bounds, add_blanket */;
trait_engine_.add_shape_auto_impl("Fn",     "closure", is_closure_typename);
trait_engine_.add_shape_auto_impl("FnMut",  "closure", is_closure_typename);
trait_engine_.add_shape_auto_impl("FnOnce", "closure", is_closure_typename);
trait_engine_dirty_ = false;
```

Closure type-name detection is a one-liner — Logos's canonical
closure type name starts with `|` (e.g. `|i32| -> i32`), matching
`type_str` in `sema.cpp`. The shape predicate inspects the leading
character; no need to parse the rest.

`mono_has_impl_recursive` (the original walker) is now a thin
shim:

```cpp
bool Mono::mono_has_impl_recursive(const std::string& trait_name,
                                   const std::string& concrete_name,
                                   StrSet& /*seen*/) {
    if (trait_engine_dirty_) populate_trait_engine_();
    return trait_engine_.satisfies(trait_name, concrete_name);
}
```

The `seen` parameter is kept for ABI compatibility with existing
call sites (`__has_trait__`, `__has_trait_of__` intrinsics in
`mono_clone.cpp:1138` / `1209`, and `method_bound_ok` at `3141`).
Engine's per-query cycle guard handles cycle detection on its own;
the passed-in `seen` is ignored.

### Phase 3: structural auto-trait fabric — `src/compiler/sema_auto_trait.cpp`

204 LOC. Distinct from the Datalog engine — handles the
**structural** half of auto-trait derivation (Send/Sync/Copy):
walk a type's substructure, recurse on each field/elem, honor
explicit `unsafe impl` carve-outs at every level.
`is_auto_trait_satisfied` in `sema_auto_trait.cpp:24` is the entry
point. Called from `sema_collect.cpp:503` during trait-bound
checking.

This logic doesn't currently live in `trait_engine` because its
input is a `TypeRef` (with substructure), not a flat type-name
string. When the engine grows substitution-aware matching (see
Extension points), the two paths will likely merge.

## Tests — `src/compiler/trait_engine_test.cpp` (164 LOC)

11 unit tests exercising each rule kind in isolation:

- `test_direct_impls` — basic positive/negative resolution.
- `test_dedup_direct_impls` — duplicate `add_impl` returns same id.
- `test_blanket_impl` — single-bound blanket chains through direct.
- `test_blanket_chain` — multi-level blanket-of-blanket.
- `test_blanket_multi_bound_and` — AND-conjunction across bounds.
- `test_blanket_cycle_does_not_loop` — circular blanket terminates.
- `test_auto_impl` — unconditional auto-impl.
- `test_shape_auto_impl_closures` — predicate-matched shape.
- `test_clear_derived_invalidates_memo` — explicit memo flush.
- `test_adding_fact_after_query_invalidates_memo` — implicit flush
  when new facts arrive.
- `test_trace_basic` — `trace_satisfies` breadcrumb.

Run via `ctest -L trait_engine` or directly:
`./build/src/compiler/trait_engine_test`.

## Diagnostics

`trace_satisfies(T, X)` returns a `vector<string>` listing which
rule fired at each derivation step. Indentation marks nested
sub-queries through blanket bounds. Use it from a debug-print or
expose at a future `--trace-traits=T,X` CLI flag (not wired up
today — when needed, plumb through `SemaOptions` and call from
the `__has_trait__` intrinsic for ergonomic per-call tracing).

## Limits (intentional, today)

1. **No substitution-aware matching.** A type like `Option<i32>`
   is stored as the flat string `"Option<i32>"`. If the resolver
   needs to ask "does any `Option<T>` impl Display when T: Display"
   the caller must pre-substitute. Sema/mono do this everywhere
   today via `subst_type_sema` / `subst_type` before querying.
2. **No negation under derivation.** `add_negative` only blocks
   the leaf (T, X) pair; it doesn't propagate ("if X doesn't impl
   Send, then `&X` doesn't impl Sync"). That structural rule lives
   in `sema_auto_trait.cpp` instead.
3. **No aggregation, no recursion-through-negation.** Pure
   monotone positive Horn clauses, which is fine for trait
   resolution proper.
4. **No GAT projection.** Associated-type queries (`T::Item`)
   resolve in `mono_subst.cpp` via the pre-existing path. The
   engine has no `assoc_type(Trait, Type, AssocName, Resolved)`
   relation yet — the original design listed it but Phase 1
   didn't need it.
5. **No HRTB scope.** `for<'a> Fn(&'a T)` parses but the engine
   doesn't quantify over lifetimes. Each closure type name carries
   no lifetime info today, so the question doesn't arise in
   practice.
6. **No coherence checks.** First matching derivation wins.
   Coherence (no two impls overlap) lives in sema's collection
   phase, before facts reach the engine.

## Performance

Semi-naive `O(n²)`-ish over relations. In practice:
- Total trait queries per compile: ~hundreds.
- Largest relation (concrete_impls_) on full stdlib: ~few hundred.
- Each query is O(1) after the first via `memo_`.
- `populate_trait_engine_` rebuilds from scratch on every dirty
  mark — cheap (<1ms in profile) because the input maps are
  small. If this ever shows up: switch to incremental add-on-dirty
  instead of full rebuild.

## Extension points

These are the threads to pull when new resolver pressure arrives:

| Pressure | What to add | Where |
|---|---|---|
| GATs (`T::Iter::Item`) | `assoc_type` relation + projection rule in `resolve_impl_` | new method in trait_engine.cpp + caller in mono_subst.cpp |
| HRTB (`for<'a> Fn(&'a T)`) | substitution-aware matching; bind lifetimes during query | engine API: `satisfies(T, X, SubstMap)`; mono pre-quantifies the binder |
| Multi-layer blanket coherence | overlap detection across blankets | sema collection phase (**NOT** engine); engine should still pick "first match" |
| Auto-trait under structural composition | merge `sema_auto_trait.cpp` into engine via a shape-predicate that recurses on fields | replace standalone `is_auto_trait_satisfied` with a shape-auto-impl callback |
| User-facing trace flag | wire `trace_satisfies` to a `--trace-trait=T,X` CLI knob | `SemaOptions` + `__has_trait__` intrinsic at call site |
| Performance (if it ever bites) | incremental `populate_trait_engine_` (track diffs of `concrete_impls_` / `blanket_impls_` instead of full rebuild) | mono_clone.cpp |
| Stats / introspection | `TraitEngine::stats()` already counts facts + fixpoint rounds — expose via `--dump-traits` | main.cpp |

## Files touched

- `src/compiler/trait_engine.hpp` — public API.
- `src/compiler/trait_engine.cpp` — engine impl.
- `src/compiler/trait_engine_test.cpp` — unit tests.
- `src/compiler/mono_clone.cpp:3089-3139` — populate + shim.
- `src/compiler/mono_impl.hpp:169-174` — engine member + dirty flag.
- `src/compiler/sema_auto_trait.cpp` — parallel structural fabric
  (Send/Sync/Copy), not yet folded into engine.

## Related commits (in landing order)

- `629a699` — sprint 5: design doc for minimal Datalog trait
  resolver (now this file).
- `f7c1cfa` — sprint 5 phase 1: engine itself, no integration.
- `8c3f468` — sprint 5.4+5.6: integrate into Mono;
  `mono_has_impl_recursive` routed through engine.
- `34d45c5` — sprint 5.5+5.7 (partial): Fn-family
  shape-auto-impl + dedup `__has_trait__`.
- `74a7fe4` — sprint 5.7 follow-up: fn-ptr-as-Fn-bound.
- `7ab4419` — C5-cl-08 closed: by-ref capture for mutated scalars
  (uses engine via the shape-auto-impl path).
- `97ea121` — T9-tr-02 + T9-tr-04: generic-trait method through
  TypeVar receiver, sema-side bound resolution riding on engine.
- `f7af099` — auto trait: fix 5 bugs in satisfaction engine and
  sema (structural fabric polish).

## When to leave it alone

The engine handles every trait resolution case currently exercised
by the imported rustc tests (1450 passing). Don't reach for the
extension points until imported tests actually trip on them — the
roadmap rule from `project_roadmap_2026_05.md` ("don't propose
Datalog before HRTB/GAT actually require it") applies to **growing**
the engine too. The simpler the relations stay, the easier it is
to reason about cycles and termination.
