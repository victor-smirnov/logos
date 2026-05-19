# Mono redesign — phased refactor plan

**Date**: 2026-05-19. **Status**: design doc, Phase 1 in flight.

## Why a redesign

Mono (the monomorphization pass between sema and mlir-gen) accumulated
~4-5 years of incremental fixes (6700 lines across `mono*.cpp` /
`mono_impl.hpp`). The fixes work locally but the same structural bug
class keeps surfacing:

- Eager blanket-impl instantiation runs **before** any worklist
  drain, with **shallow** bound checks. Lazy fn/struct instantiation
  runs **after**, with **deep** bound checks. The two paths don't
  share invariants — a fix in one leaves the other broken.
- TypeVar threading has no completion guard. A free-fn signature
  like `option_unzip<A, B>(Option<(A, B)>)` triggers eager cloning
  of `OptionIter<(A, B)>`'s trait default methods (`take_while`,
  `step_by`, etc.) **while A,B are still TypeVars** → `<error>`-typed
  iter wrappers mlir-gen can't lower.
- Method registration is flat `<target>__<method>` per type, no
  trait disambiguation. Forces every `Display::fmt` / `Debug::fmt`
  pair into suffix workarounds (`fmt_display` / `fmt_debug`).
- No cycle detection, only depth limit. Eager blanket loop ignores
  the depth check entirely.

Open baghunts that all trace back to mono structure:
- [[baghunt-mono-eager-typevar-default-clone]] — symptom we hit on
  `option_unzip` / `result_flatten` / `result_transpose`.
- [[baghunt-mono-fn-ptr-field-typevar]] — `struct S<T> { f: fn(T)->R }`
  + `.f(v)` mismatch.
- [[baghunt-mono-void-payload-specs]] — stale VarRefs from
  void-payload pattern bindings.
- [[baghunt-replace-ref-option-cascade]] — mono cascade through
  Option<T> + mem::replace_ref.
- [[baghunt-trait-aware-method-mangling]] — flat method registry.
- [[baghunt-iter-method-generic-mono]] — method-generic trait-default
  on Iterator segfaults.

This doc commits to a phased refactor, NOT a full rewrite. The
existing data flow (index → template clone → worklist scan →
fixpoint) is sound; the bugs are localized invariant violations.

## Invariants the redesign must enforce

1. **Substitution-complete before trait-method clone.** No method
   may be cloned for a generic shape that still contains TypeVars
   from the enclosing fn's own type-param list. Today nothing checks
   this — fix in Phase 1.

2. **Single source of truth for trait satisfaction.** Every check
   ("does `Concrete` satisfy `Trait`?") routes through
   `mono_concrete_satisfies_bound`. The shallow
   `mono_has_impl_recursive` becomes a private helper used only by
   the deep version. Fix in Phase 2.

3. **Eager and lazy paths share the worklist.** Blanket-impl method
   instantiation should NOT happen upfront. Instead, enqueue into
   the same fixpoint-controlled worklist as generic fns + structs.
   Fix in Phase 2.

4. **Struct instantiation inline with scan.** When a cloned fn body
   references a struct type, instantiate it in the same scan cycle
   (or enqueue with explicit depth), not in a deferred
   `instantiate_struct_templates()` post-pass. Fix in Phase 3.

5. **Trait-aware method symbols.** Method registration includes
   trait name in the mangling key: `<target>__<trait>__<method>`
   (was: `<target>__<method>`). Dispatch routes through the trait
   from the bound in generic context; errors with disambiguation
   guidance on concrete-receiver ambiguity. Fix in Phase 4.

6. **Cycle detection.** A `(template, subst)` pair already on the
   instantiation stack must be detected and short-circuited (not
   just gated by depth). All worklist consumers track this. Fix
   spread across phases as a guardrail.

## Phase 1 — Substitution-complete gate

**Status**: PARTIAL (2026-05-19). `contains_typevar` helper landed +
record_needed_* routed through it. 3259/3259 ctest green. The
`TakeWhileIter$G2$OptionIter$G1$<error>` cascade class is killed.
Deferred port surface (option_unzip / result_flatten /
result_transpose) STILL FAILS with a different shape
(`mlir_gen: unknown tagged enum 'Option__<error>'`) — upstream
sema/mono emits Kind::Error in a substituted body that reaches the
EnumLit emit site. Need to track down the Kind::Error source or
gate the EnumLit emit in coordination with Phase 2's bound-driven
instantiation. Phase 1 helper is a defensive guard, NOT a full fix.

**Scope**: ~250 LOC, 2–3 days, low risk.

**Goal**: kill the nested-generic TypeVar cascade class of bugs
(option_unzip, result_flatten, result_transpose, replace_ref Option
cascade, fn-ptr field TypeVar).

**Fix sites**:

- Add helper `Mono::is_fully_substituted(TypeRef tv) const` in
  `mono_impl.hpp`. Recursive walk: returns false if `tv` or any
  child type contains a TypeVar / unresolved AssocType.
- Gate every trait-method clone site:
  - `mono_clone.cpp` `clone_struct_def` line ~3993–4020 (bulk-clone
    methods loop) — skip method `m` if any field type used by `m`'s
    body contains an unsubstituted TypeVar.
  - `mono_clone.cpp` `instantiate_enum_templates` line ~4649 — same
    gate before enqueueing enum-method clones.
  - `mono.cpp` eager blanket-instantiation loop line 227–461 —
    skip a candidate `concrete` if any of `bi.target_typeref`'s
    type-args remain TypeVar after the candidate substitution.
  - `mono_scan.cpp` `drain_method_worklist` — gate before
    `clone_fn(m, subst)` when `subst` doesn't bind every TypeVar
    referenced by `m`'s param/return types.

**Tests** (new pass cases):
- `tests/logos/pass/option_unzip_pair.logos` — `Option<(i32, i64)>::unzip()` works.
- `tests/logos/pass/result_flatten_basic.logos` — `Result<Result<T,E>,E>::flatten`.
- `tests/logos/pass/result_transpose_basic.logos`.
- Promote the deferred entries in
  `tests/logos/pass/option_result_method_surface.logos` from skipped
  to active.

**Risk register**:
- Some currently-emitted mono instances may be skipped that were
  reaching mlir-gen as dead code. If those get pruned cleanly, fine;
  if mlir-gen depends on their presence, surface as new errors and
  fix in mlir-gen.
- 5–10 tests may flake during transition. Each gets diagnosed
  before commit.

**Phase 1 ships when**: 3258+ ctest green, deferred test cases
above all pass, no new regressions in iter / fmt suites.

## Phase 2 — Unify eager+lazy under worklist + deep bounds everywhere

**Status**: STEP 1 LANDED 2026-05-19 (commit 8821a5d4). Eager
blanket-impl extra-bound check now routes through
`mono_concrete_satisfies_bound` (deep) for struct/enum candidates;
primitive candidates keep shallow fallback. Lint baseline bumped
8→10 for two added inline TypeBuilders (queued for the worklist
refactor). 3259/3259 ctest. Remaining: audit the other 3
`mono_has_impl_recursive` call sites (mono.cpp:263, 296, 300),
convert eager blanket method-clone into worklist enqueue, add
cycle detection.

**Scope**: ~800 LOC, 3–5 days, medium risk.

**Goal**: eliminate the asymmetry between eager blanket and lazy
fn/struct paths. Deep bound checking everywhere.

**Fix sites**:

1. **Refactor blanket-impl instantiation**
   (`mono.cpp:227–461`) — split into:
   - Index-time: populate `blanket_impls_` (unchanged).
   - Worklist enqueue: each blanket's `(trait, target_pattern, ...)`
     enqueues a `BlanketWorkItem` per candidate concrete type. The
     worklist drain (current `mono.cpp:513–540`) handles them in
     the same fixpoint as generic fns.
   - Per-candidate gate uses `mono_concrete_satisfies_bound` (deep),
     not `mono_has_impl_recursive` (shallow). The shallow helper
     becomes internal-only.
2. **Audit all `mono_has_impl_recursive` call sites** and route
   through `mono_concrete_satisfies_bound` where the bound check
   needs to be deep:
   - `mono.cpp:263, 296, 300, 365, 422` — blanket eager loop.
   - `trait_engine.cpp` populate helpers.
   - ADR 0008 assoc-eqs path.
3. **Single worklist drain loop** consolidating fn, blanket,
   struct, enum work items. Each item carries depth + cycle key.
4. **Cycle detection**: `worklist_seen_` set keyed by
   `(template_name, mangle(subst))`. Insert before processing,
   error if re-entered. Replaces blind depth-only gate.

**Tests**:
- `tests/logos/pass/blanket_bound_recursion_deep.logos` — chain
  blankets where shallow check would FAIL incorrectly.
- `tests/logos/pass/blanket_cycle_detection.logos` — A: Trait
  requires B, B: Trait requires A, must error not loop.
- All existing iter/fmt/baghunt tests still green.

**Risk**: medium-high. Order changes may expose latent issues in
mlir-gen's expectations.

## Phase 3 — Inline struct instantiation in scan path

**Scope**: ~400 LOC, 2–3 days, medium risk.

**Goal**: tighten the "no cloned body references an undefined
struct" invariant.

**Fix sites**:

1. **Fold `instantiate_struct_templates` into scan**: when
   `scan_fn` encounters a generic struct type reference, instead of
   `record_needed_struct(tr)` (defer) → `enqueue_struct_inst(tr)`
   (worklist push with depth + cycle key).
2. **Drain struct items in the unified loop** from Phase 2. The
   struct-instantiation logic moves out of the post-pass and into
   the worklist consumer.
3. **Remove `instantiate_struct_templates` and
   `instantiate_enum_templates` as separate passes**. Their bodies
   become worklist-consumer branches.
4. **Depth threading**: struct instantiation increments depth in
   the same way fn instantiation does; cycle key includes
   substitution.

**Tests**:
- All struct-instantiation tests stay green. Performance may
  improve (no separate pass loop).
- Add `tests/logos/pass/deeply_nested_struct_inst.logos` if not
  already covered: `Vec<Vec<Vec<Vec<i32>>>>` should terminate.

**Risk**: medium. Order-of-instantiation changes can shake out
mlir-gen field-layout assumptions.

## Phase 4 — Trait-aware method mangling

**Scope**: ~1100 LOC, ~1 week, high risk. Already speced in
[[baghunt-trait-aware-method-mangling]] with full dispatch-site map.

**Goal**: replace flat `<target>__<method>` mangling with
trait-prefixed `<target>__<trait>__<method>` for trait-impl
methods; let `Display::fmt` and `Debug::fmt` coexist on the same
type without `fmt_display` / `fmt_debug` workarounds.

**Fix sites**: see baghunt file. ~30 dispatch lookup sites + ~5
registration sites + mlir-gen method call emission.

**Tests**:
- Trait method collision passes when distinct method/sig.
- Disambiguation syntax `<Type as Trait>::method(...)` test.
- `Display::fmt(...)` and `Debug::fmt(...)` on i32 coexist.

**Risk**: high. Method dispatch is hot path; subtle regressions
cascade through all of sema + mono + mlir-gen.

## Phase 5 — Resurrect deferred port surface (catch-up)

After Phase 1-4 land:

- Promote `option_unzip` / `result_flatten` / `result_transpose`
  to methods (now safe per Phase 1).
- Rename methods `fmt_display`/`fmt_debug`/`_fmt_lower_hex` →
  `fmt` (per Phase 4).
- Migrate derive_debug metaprog hooks to canonical `fn fmt(&self,
  &mut Formatter) -> Result<(), Error>` shape.

Total stdlib touch: low (mostly removing workarounds).

## Migration discipline

- Each phase commits to a single named milestone in git, NOT
  individual fix sites within. Atomic so we can bisect cleanly.
- ctest must stay green between phases (3258+ passing). Any
  failures get diagnosed in-phase before commit.
- Each phase updates this doc with "Landed YYYY-MM-DD" + commit
  ref under its section.

## Why NOT a full rewrite

- Mono's core data flow (index → clone → worklist → fixpoint) is
  sound. The bugs are local invariant violations.
- Existing partial fixes (`mono_concrete_satisfies_bound`, M2 dual
  struct keys, L1.x lazy-method infrastructure) show the team can
  engineer targeted improvements correctly. The patches are
  already half-done; we're consolidating.
- Full rewrite would lose ~4 years of edge-case knowledge encoded
  in conditional handling.
- Phase 1 alone unblocks the immediate port surface; remaining
  phases buy long-term soundness without halting the roadmap.

## What stays out of scope

- AssocType resolution (ADR 0008) — already specified, working.
- Hermes external-ref body resolution — orthogonal multi-arena work.
- ABI/codegen layout (mlir-gen-side) — out of mono scope.
- Stdlib export catalog refactor (M3) — depends on this redesign,
  picks up after Phase 4.
