# Repair Strategy & Architectural Roadmap

**Phase 4 deliverable** of the [refactor + adversarial bag-hunt](../../../.claude/plans/memoized-whistling-cherny.md). Translates the 12 clusters identified in [categorization.md](categorization.md) into a concrete, dependency-ordered fix plan with future-aware sequencing.

## Strategy in one sentence

**Hit the architectural multipliers first** (single-helper / single-pass fixes that close 5+ bugs each), then the targeted P0 silent-miscompile fixes, then ergonomics and roadmap items, with explicit "defer to Phase 5" tags on issues that get a cleaner Datalog/AST-analysis solution.

## Sequencing principles

1. **Architecture before pointwise.** Land the helpers/passes that introduce new validation primitives FIRST, so subsequent point-fixes use them consistently.
2. **P0 crashes outweigh ergonomics.** A crash on `package fn;` is worse for new users than `&& vs && ` whitespace requirements. Sequence accordingly.
3. **Land in small commits per cluster.** Each cluster fix should be its own commit/PR with regression test cases drawn from the per-feature catalog repros.
4. **Test before fix.** Add adversarial tests from `/tmp/baghunt/<group>/` repros into `tests/logos/` BEFORE applying the fix; then green confirms.
5. **Future-aware.** Mark anything that gets a cleaner solution post-AST-analysis / Datalog as `[deferred-to-phase5]`. Don't pre-engineer those.

## Sprint 1 — Hard P0 crashes

### Sprint 1.1 — Cluster 1: assertion-as-diagnostic (6 P0 crashes)

**Bugs closed**: B-mv-05, B-mv-06, B-mv-07, B-mv-08, B-lx-01, B-lx-02

**Estimated effort**: 1-2 days

**Approach**:
1. Audit every `LOGOS_ASSERT.*PARSE.*` site in `src/compiler/parser_*` (generated) and in [tools/peg_gen/](../../tools/peg_gen/) — find all "expected token X" assertions.
2. Replace each with `error()` recovery + synthesized error AST node. The PEG generator may need an extension here; check `peg_gen/` for how error productions are emitted.
3. Add regression tests at `tests/logos/fail/parse_*` covering: empty file, missing package decl, keyword as pkg name, trailing dot in path, BOM, malformed paths.

**Verification**: `ctest` passes; new fail-mode tests confirm the diagnostic shape; no `LOGOS_ASSERT` triggered on any of the 6 P0 inputs.

**Files touched**:
- [tools/peg_gen/](../../tools/peg_gen/) (generator)
- [build/.../parser_*.cpp](../../build/) (regenerated)
- `tests/logos/fail/` (new tests)

### Sprint 1.2 — Cluster 6: missing-cycle-guard (5 P0 crashes)

**Bugs closed**: B-it-01 (recursive struct), B-it-02 (recursive enum), B-ca-01 (self-ref const), plus 2-3 latent

**Estimated effort**: 1 day

**Approach**:
1. In [mlir_gen_types.cpp](../../src/compiler/mlir_gen_types.cpp) `register_struct`, add a thread-local "currently being registered" set. On re-entry, error with "infinite-size type 'Foo' (cannot contain itself by value); use a pointer or `Box<Self>`".
2. Same for `register_tagged_enum`.
3. In [sema_collect.cpp](../../src/compiler/sema_collect.cpp) `collect_const`, detect self-reference in initializer expression — walk + check identifier vs the const-being-defined.
4. Add regression tests.

**Files touched**: `src/compiler/mlir_gen_types.cpp`, `sema_collect.cpp`, `tests/logos/fail/`

### Sprint 1.3 — 3 individual P0 crashes (B-ty-01, B-ty-02, others)

**Bugs closed**: B-ty-01 (empty tuple `()`), B-ty-02 (impl Trait at param)

**Approach**: Targeted fixes — these are likely small ad-hoc bugs in `resolve_type` / `lower_fn` rather than cluster patterns.

## Sprint 2 — Architectural validation primitives

### Sprint 2.1 — Cluster 2: missing-uniqueness-check (8 sites)

**Bugs closed**: B-it-03/04/05 (struct/enum/trait), B-fn-02 (fn params), B-ca-04 (consts), B-gn-01/02 (type/lifetime params), B-pt-01 (pattern bindings), B-he-02 (Writ-map keys), B-at-03 (annotations)

**Estimated effort**: 1 day

**Approach**:
1. Add helper to [sema_impl.hpp](../../src/compiler/sema_impl.hpp):
   ```cpp
   template <class Items, class GetName>
   void check_unique_names(const Items& items, GetName get_name,
                           const char* kind_label, const char* context_name);
   ```
2. Apply at every site that builds a list of named items:
   - `lower_struct_def` (fields)
   - `lower_enum_def` (variants)
   - `lower_trait_def` (already? verify)
   - `lower_fn` (params)
   - `collect_const`
   - `lower_struct_def` (type-params)
   - Lifetime-params collection
   - Pattern-binding collection
   - `eval_static_writ_lit` (Writ-map entries)
   - Annotation aggregation
3. Tests in `tests/logos/fail/dup_*`.

**Files touched**: `sema_impl.hpp`, `sema_collect.cpp`, `sema_decl.cpp`, `sema_stmt.cpp`, `tests/logos/fail/`

### Sprint 2.2 — Cluster 4: missing-arity-check

**Bugs closed**: B-ty-03/04/05 (type-arg arity), B-gn-04 (bound-arg arity), B-mt-03 (instantiate arity)

**Estimated effort**: 0.5 day

**Approach**:
1. Single helper:
   ```cpp
   void check_type_arg_arity(std::string_view template_name,
                              const std::vector<TypeParam>& params,
                              const std::vector<TypeRef>& args,
                              std::string_view context);
   ```
2. Apply at all generic-instantiation sites — `resolve_type` (already does this for some kinds), bound resolution, instantiate-decl lowering.
3. Replace cryptic mlir-gen errors with clean sema errors.

**Files touched**: `sema.cpp` (resolve_type), `sema_collect.cpp` (instantiate_decl), `sema_decl.cpp`

### Sprint 2.3 — Cluster 8: literal-saturation-no-error

**Bugs closed**: B-ex-07, B-he-04, B-lx-04 — all instances of one root.

**Estimated effort**: 0.25 day

**Approach**: One-line bounds check in `parse_int_literal` ([sema/lexer area](../../src/compiler/)). Reject overflow with "integer literal '...' out of range for <type>". See [feedback_literal_saturation](../../../.claude/projects/-home-victor-devel-logos/memory/feedback_literal_saturation.md).

### Sprint 2.4 — Cluster 5: diagnostic-from-codegen

**Bugs closed**: B-ty-04, B-st-05, B-pt-07, B-mt-03 (also closed by 2.2)

**Approach**: Each is a specific case where mlir-gen catches what sema should have. Move validation upstream:
- `break 'unknown_label` — validate at sema-stmt time.
- arm-after-catchall — needs Cluster 9 (reachability lint, see Sprint 3).

These are case-by-case fixes. Ship after the cluster work in Sprint 2.

## Sprint 3 — Targeted P0 silent miscompiles

### Sprint 3.1 — Block-scope shadow leak (B-st-01)

**Severity**: P0 silent miscompile in everyday code.

**Estimated effort**: 0.5 day (with comprehensive test coverage)

**Approach**: Audit [sema_stmt.cpp](../../src/compiler/sema_stmt.cpp) block-handling — variable scope-stack push/pop is incorrect for nested blocks. Find the missing `pop` or the incorrect lookup. Add property-test-style regression: every `(let x; { let x; ...; } use(x))` shape must see outer x.

### Sprint 3.2 — let-else divergence (B-st-03)

**Approach**: Add divergence-check to `let-else_stmt` lowering — the else block must end in `return`/`break`/`continue`/`panic`/`loop`. Use a helper from [borrow_check.cpp](../../src/compiler/borrow_check.cpp) or write a simple recursive check.

### Sprint 3.3 — Const-fold validation (Cluster: ex-01/02/03/04)

**Bugs closed**: B-ex-01 (overflow), B-ex-02 (div0), B-ex-03 (shift-neg), B-ex-04 (unary-on-unsigned)

**Approach**: Wait for [feat_const_fold_metacall](../../../.claude/projects/-home-victor-devel-logos/memory/feat_const_fold_metacall.md) const-fold engine to come back. Then add a `validate_constfold` pass that checks bounds + zero divisors + signedness for `-` operator.

This may be Phase 4.5 (after const-fold lands) rather than now.

### Sprint 3.4 — Cast validation (B-ex-05, B-li-03)

**Bugs closed**: B-ex-05 (`int as Struct`), B-li-03 (struct update with mismatched base)

**Approach**: Cast-expr type-check should accept only well-defined pairs (prim→prim, ptr→ptr, ptr→int, etc.). Struct-update lowering should compare base.type against constructor type.

## Sprint 4 — Pattern surface + Writ nesting

### Sprint 4.1 — Cluster 12: nested Writ literals (B-he-01)

**Severity**: P0 — major feature gap; `@{ "k": @{...} }` doesn't parse despite docs saying it should.

**Estimated effort**: 1-2 days (depending on PEG generator complexity)

**Approach**:
1. Reproduce — flat `@[]` and `@{}` work; nested do not. The grammar's `writ_val` allows `AT writ_typed_array / AT writ_map / AT writ_array` recursively.
2. Debug parser dispatch for nested `@` token at value position. May be a token-handling issue in the PEG generator.
3. Add nested test cases to `tests/logos/pass/writ_nested_*`.

**Files touched**: `tools/peg_gen/grammars/logos.peg`, generated parser, `tests/logos/pass/`

### Sprint 4.2 — Cluster 11: pattern surface coverage

**Bugs closed**: B-pt-02..06 (let pat, byte-string, nested, slice ..rest, float lit)

**Estimated effort**: 2-3 days (substantial grammar work)

**Approach**:
1. Audit grammar's `pattern` non-terminal. Identify positions where a restricted variant is used (e.g. `let_stmt` allows only flat patterns).
2. Unify: a single `pattern` non-terminal everywhere, with refutability classification at use site.
3. Add missing alternatives: byte-string literal (`b"..."`), float literal, slice patterns with `..rest`, nested patterns inside enum payloads.
4. Tests across `tests/logos/pass/pat_*`.

**Files touched**: `tools/peg_gen/grammars/logos.peg`, `sema_pat.cpp` (or wherever patterns lower), `tests/logos/pass/`

## Sprint 5 — Attribute validation + reachability

### Sprint 5.1 — Cluster 3: no-attribute-validation

**Bugs closed**: B-at-01..07, B-mt-04

**Estimated effort**: 1-2 days

**Approach**:
1. Define attribute-spec table in [sema_collect.cpp](../../src/compiler/sema_collect.cpp) or new file:
   ```cpp
   struct AttrSpec {
       std::string_view name;
       AllowedTargets targets;
       ValueShape value_shape;
       DupPolicy dup_policy;
       int64_t int_min, int_max;  // for type_code etc.
   };
   ```
2. Validation pass after annotation collection: every `#[...]` checked against registry.
3. Unknown attributes → warning (eventually error per docs roadmap).
4. Add tests at `tests/logos/fail/attr_*`.

**Files touched**: `sema_collect.cpp`, `tests/logos/fail/`

### Sprint 5.2 — Cluster 9: no-reachability-lint

**Bugs closed**: B-st-07 (unreachable arm), B-st-08 (dead code after return), B-pt-07 (arm after catch-all)

**Estimated effort**: 1 day

**Approach**: Small CFG-walk pass over LIR. Mark statements as reachable. Emit warnings on unreached statements. May overlap with Phase 5 AST-analysis if we wait.

**Decision**: Do NOW (small enough for current phase).

## Sprint 6 — Ergonomics + grammar consistency

### Sprint 6.1 — Trailing comma cluster (B-fn-03/04/05/09, B-gn-10)

**Estimated effort**: 0.5 day

**Approach**: Standardize `(COMMA T)* COMMA?` across all list productions in [logos.peg](../../tools/peg_gen/grammars/logos.peg).

### Sprint 6.2 — Lexer-greedy-collision (Cluster 7)

**Bugs**: B-ty-07 (`&&`), B-ty-08 (`||`), B-lx-07 (char `'A'`)

**Decision**: Document requirements (space them, no char literals); deferred for context-aware tokenization.

### Sprint 6.3 — Diagnostic improvements

Misc cluster (`tech-debt:misleading-diagnostic`, `tech-debt:diagnostic-no-pkg`):
- B-mv-02 (cross-pkg trait), B-mv-09 (ambiguous Pt) — diagnostic should show pkg in type names. `type_str()` needs pkg-aware variant for diagnostics.
- B-fn-08 + various — improve specific diagnostic messages.

**Approach**: Each diagnostic improvement is a 1-line site change; bundle them per affected file.

## Deferred to Phase 5 (AST analysis / Datalog)

These get **trivial solutions** when fact-base infrastructure lands:

| Cluster | Why defer |
|---|---|
| missing-uniqueness-check (Cluster 2) | Datalog query: `dup_name(...)` automatically catches all kinds |
| missing-arity-check (Cluster 4) | Datalog query: `arity_mismatch(use, def)` |
| missing-cycle-guard (Cluster 6) | Datalog: SCC over depends-on graph |
| no-reachability-lint (Cluster 9) | AST walk → reachability fact-base |
| no-attribute-validation (Cluster 3) | Spec registry queryable from external tools |

**Decision**: Don't actually defer the Cluster 2/3/4 fixes — these are 0.5-1 day each and unblock immediate P0/P1 bugs. But mark the fix with a comment "TODO: simplify when Datalog lands" so it's clear the implementation will be replaced.

Actually-deferred items (no current fix needed):
- Roadmap items: `#[deprecated]`, `quote_stmt!` / `quote_pat!` etc. — explicitly planned-not-implemented. No catalog entry needs a fix; they're feature work.
- Const-eval policy (B-ca-03) — language-design decision pending.

## Effort summary

| Sprint | Bugs closed | Effort |
|---|---|---|
| 1.1 — assertion-as-diagnostic | 6 P0 | 1-2 days |
| 1.2 — missing-cycle-guard | 5 P0 | 1 day |
| 1.3 — 3 individual P0s | 3 | 0.5-1 day |
| 2.1 — missing-uniqueness-check | 8-10 | 1 day |
| 2.2 — missing-arity-check | 5 | 0.5 day |
| 2.3 — literal-saturation | 3 (one root) | 0.25 day |
| 2.4 — diagnostic-from-codegen | ~3 (overlap) | 1 day |
| 3.1 — block-scope shadow | 1 (P0) | 0.5 day |
| 3.2 — let-else divergence | 1 | 0.25 day |
| 3.3 — const-fold validation | 4 | wait for const-fold |
| 3.4 — cast validation | 2 | 0.5 day |
| 4.1 — Writ nested literals | 1 (P0 major) | 1-2 days |
| 4.2 — pattern surface | 5 | 2-3 days |
| 5.1 — attribute validation | 5+ | 1-2 days |
| 5.2 — reachability lint | 3 | 1 day |
| 6.1 — trailing comma | 5 | 0.5 day |
| 6.2 — lexer collisions | docs only | 0.25 day |
| 6.3 — diagnostic improvements | scattered | 1-2 days |

**Total**: 14-22 days of focused work to close ~80-100 catalog bugs. Remaining ~20-40 are roadmap items / language-design pending / explicitly deferred.

## Architectural roadmap (non-goals here)

These are listed for context but are NOT part of this strategy:

- **Trait-based type comparison** (replace string `name == "AnyVal"` checks with traits) — long-term, after Phase 5 metaprog tier 2.
- **Whole-program-pass infrastructure** (Phase 2 transformative metaprog) — separate epic.
- **Self-hosting compiler in Logos** — separate epic.
- **Per-attribute Datalog spec** — replaces Sprint 5.1 once Datalog lands.

## Verification plan post-strategy

After all sprints land:
1. **Regression suite**: every catalog repro has a test in `tests/logos/`.
2. **Cluster-level invariants**: e.g. "no bare-name lookup that bypasses pkg" should be a checked invariant via Datalog (Phase 5).
3. **Documentation**: each cluster fix updates the relevant `docs/spec/*.md` page so users see the new behavior.
4. **Crash-rate metric**: `LOGOS_ASSERT` triggers on user input → 0. Track in CI.

## What this plan does NOT do

- Doesn't introduce new features.
- Doesn't change language semantics where current behavior is documented.
- Doesn't address the largest deferred clusters (Phase 5 work).
- Doesn't pre-engineer for hypothetical future requirements.

The goal is **restore correctness baseline + raise diagnostics quality**. Once landed, the codebase is ready for Phase 5 metaprog/Datalog work on a clean foundation.
