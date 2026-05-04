# Meta-strategy: improving development infrastructure to prevent bug classes

**Companion to** [strategy.md](strategy.md). Where strategy.md sequences the *fixes*, meta-strategy describes the *infrastructure work* that should land FIRST so the fixes ripple cleanly and so future development doesn't re-introduce the same bug families.

The bag-hunt found ~122 bugs. **Most of them are not isolated mistakes** — they're symptoms of ~12 systemic patterns. Three observations drive this document:

1. **Each cluster has the same shape**: a family of N call-sites doing the same thing, where K < N of them have a missing check / threaded field / validation. The fix isn't "add the check at site k+1"; it's "remove the possibility of forgetting the check at site N+1".

2. **Some clusters trace back to the toolchain, not the code**: the PEG generator emits `LOGOS_ASSERT` on missing tokens; the bug is in the *default*, not in any individual production.

3. **Existing test coverage is non-adversarial**: ~all tests use happy-path inputs. Hermes nested literals (B-he-01) didn't parse for *months* because no test exercised the recursive corner.

The correct response is: build infrastructure that makes these classes hard to introduce, then apply the infrastructure mechanically to close the current bug list.

## Principle: Meta-Sprint 0 before Sprint 1

[strategy.md](strategy.md) starts with **Sprint 1 — Hard P0 crashes**. That's the right destination but the wrong starting point. Inserted before Sprint 1:

**Meta-Sprint 0 — Build the scaffolding (3-5 days)**

Land the architectural primitives, registry, validation passes, and KB scaffolding so that subsequent point-fixes use shared infrastructure rather than recreating ad-hoc patches.

After Meta-Sprint 0:
- Sprint 1 fixes don't add new code — they *call* the validators built in Sprint 0.
- Each fix commit becomes "applied helper X to N sites" rather than "patched site Y in isolation".
- New code in any future feature uses the same scaffolding by default.

## Meta-Sprint 0 deliverables

### M0.1 — Centralized validation primitives (~1 day)

**`validate_unique_names()` helper** in [src/compiler/sema_impl.hpp](../../src/compiler/sema_impl.hpp):

```cpp
template <class Items, class GetName>
void check_unique_names(const Items& items, GetName get_name,
                        const char* kind_label,    // "field" / "variant" / ...
                        const char* container);    // "struct Foo" / ...
```

Used at all collection sites. Closes [Cluster 2](categorization.md#cluster-2--missing-uniqueness-check-8-distinct-sites-10-bug-instances) — 10 bug instances, 8 distinct sites.

**`check_type_arg_arity()` helper** for [Cluster 4](categorization.md#cluster-4--missing-arity-check-5-bugs):

```cpp
void check_type_arg_arity(std::string_view template_name,
                          const std::vector<TypeParam>& params,
                          const std::vector<TypeRef>& args,
                          std::string_view context);
```

Used at every generic-instantiation site. Closes 5 bugs.

### M0.2 — PEG generator: error productions instead of `LOGOS_ASSERT` (~1-2 days)

[Cluster 1](categorization.md#cluster-1--assertion-as-diagnostic-6-p0-crashes) is a generator-level fix. The PEG generator in [tools/peg_gen/](../../tools/peg_gen/) needs to emit error nodes when a required token is absent, not abort.

Once the generator change lands, the 6 P0 crashes (B-mv-05/06/07/08, B-lx-01, B-lx-02) become clean syntax errors automatically — no per-site fix.

### M0.3 — Attribute spec registry (~1-2 days)

Per [Cluster 3](categorization.md#cluster-3--no-attribute-validation-5-bugs). Declarative table:

```cpp
// In src/compiler/attr_registry.hpp (NEW)
struct AttrSpec {
    std::string_view name;
    AllowedTargets   targets;       // bitmask: Struct | Enum | Datatype | Trait | Fn | Const
    ValueShape       value_shape;   // None | Int(min, max) | String | EnumLit | KvList
    DupPolicy        dup_policy;    // Forbid | LastWins | Aggregate
    int64_t          int_min, int_max;
};

extern const std::vector<AttrSpec> kAttrRegistry;
```

Plus a single validation pass after annotation collection. Unknown attributes → warning. Closes Cluster 3 (5+ bugs) AND becomes the canonical place to add new attributes.

### M0.4 — Centralized `make_*_type` enforcement (~1 day)

Per the [pkg-name UID arc](../../../.claude/projects/-home-victor-devel-logos/memory/feat_type_uid_pkg_skip_bug.md). Audit `src/compiler/*.cpp` for inline `LogosTypeBuilder t; t.kind = K::Struct; ...` constructions. Each such site is an opportunity to forget pkg-threading. Replace with `make_struct_type(name, pkg)` calls.

Optionally: introduce a clang-tidy / grep-based lint that fires on direct `LogosTypeBuilder` field assignments outside the helper file. Future-proof against regressions.

### M0.5 — Sema completeness invariant (~2-3 days)

Per [Cluster 5](categorization.md#cluster-5--diagnostic-from-codegen-4-bugs). Sema currently lets through programs that mlir-gen rejects with cryptic errors (B-ty-04 unresolved TypeVar; B-st-05 break with bad label; B-pt-07 arm after catchall; B-mt-03 instantiate arity).

Add a post-sema verifier pass `verify_lir_program(lir::LProgram&)` that catches:
- Unresolved TypeVars in concrete TypeRefs
- Labels referenced but never declared
- Match arms after exhaustive coverage / catchall
- Generic instantiations with wrong arg count
- Anything else that mlir-gen currently catches but shouldn't have to

The invariant: **any LIR reaching mlir-gen is structurally valid**. Captured as a diagnostic, not a crash. Future mlir-gen errors become CI failures (sema bug, not codegen bug).

### M0.6 — Anti-pattern memos in memory/ (~0.5 day)

Move tacit knowledge into explicit anti-pattern files. New prefix `antipat_*.md` distinct from `feat_*.md`. Initial set:

- `antipat_inline_typebuilder.md` — direct `LogosTypeBuilder t; t.struct_name = ...` is suspicious; route through `make_*_type`. Lists historical violations + current sites still using the pattern.
- `antipat_list_no_dup_check.md` — every list-of-named-items registration must run `check_unique_names`. Lists 10 historical sites + the helper to call.
- `antipat_assertion_as_diagnostic.md` — `LOGOS_ASSERT` on user input is forbidden; use error productions. Lists 6 known cases + the generator policy.
- `antipat_validation_at_codegen.md` — sema must be complete; mlir-gen errors on user input are sema bugs. Lists 4 known leaks.

Each memo is a 1-2KB file: pattern description, why it's bad, list of historical violations, the mitigation.

### M0.7 — `memory/invariants.md` (~0.5 day)

Single document listing **all system-level invariants** with:
- Invariant statement.
- Where it must hold (which subsystem / which TypeRef construction site).
- Bag-hunt entries that violated it.
- Primary enforcement mechanism (helper / verifier / lint).

Cross-references each anti-pattern memo and each subsystem ref. Becomes the *checklist* document — anyone adding code consults this list.

### M0.8 — `memory/cluster_index.md` (~0.5 day)

Active checklist mapping observed patterns to the catalog. Format:

```markdown
## Pattern: list-of-named-items
**Symptom**: silently accepts duplicates.
**Check**: does the registration site call `check_unique_names`?
**Historical bugs**: B-it-03/04/05, B-fn-02, B-ca-04, B-gn-01/02, B-pt-01, B-he-02, B-at-03
**Fix-helper**: `validate_unique_names()` in sema_impl.hpp
```

Distinct from `categorization.md` (which is *descriptive*). cluster_index is *operational* — for grepping during code review.

### M0.9 — `docs/dev-checklist.md` (~0.5 day)

Definition-of-Done for new features. Pre-PR checklist:

- [ ] Adversarial corner cases tested (empty, single, multi, nested, neighbor-ambiguity)
- [ ] Cross-feature interaction tests (if relevant)
- [ ] Relevant invariants from `memory/invariants.md` checked
- [ ] Diagnostic on malformed input is clear (no crash, no cryptic codegen error)
- [ ] Reference doc updated
- [ ] No inline `LogosTypeBuilder` construction (route through `make_*_type`)
- [ ] No `LOGOS_ASSERT` on user input (use error productions)
- [ ] If new attribute: registered in `attr_registry`
- [ ] If new statement form: added to assignment matrix in [statements.md](../language/reference/statements.md)
- [ ] If new pattern shape: added to pattern surface coverage
- [ ] If new sema-side validation: invariant added to `memory/invariants.md`

Lives in repo so PR reviewers can refer.

### M0.10 — Per-subsystem "Pitfalls" sections (~0.5 day)

Existing `ref_subsystem_*.md` files in `memory/` have "Tech debt highlights" sections. Add **"Pitfalls when adding new code in this subsystem"** sections:

- Specific patterns to avoid in this file
- Helpers that exist (don't recreate)
- Invariants this subsystem maintains (don't break)
- Cross-references to the cluster index

Keeps the knowledge attached to the relevant subsystem rather than scattered across feat_*.md.

## What Sprint 1 looks like AFTER Meta-Sprint 0

The [strategy.md](strategy.md) sprints become much smaller:

| Sprint | Was | Becomes (with M0 done) |
|---|---|---|
| 1.1 assertion-as-diagnostic | 1-2 days, audit sites + replace each | **Already closed by M0.2**; 0 days |
| 1.2 missing-cycle-guard | 1 day | 0.5 day (specific addition to register_struct/register_tagged_enum/collect_const) |
| 1.3 individual P0s | 0.5-1 day | 0.5 day |
| 2.1 missing-uniqueness | 1 day applying helper to 10 sites | **Already wired by M0.1**; the sites are added during M0.1 itself |
| 2.2 missing-arity-check | 0.5 day | **Already wired by M0.1** |
| 2.3 literal-saturation | 0.25 day | 0.25 day |
| 2.4 diagnostic-from-codegen | 1 day | **Already closed by M0.5** |
| 5.1 attribute validation | 1-2 days | **Already closed by M0.3** |

**Net effect**: Meta-Sprint 0 absorbs ~5-7 days of "applying helpers" that would otherwise be Sprint 1-5 work. Sprints 1-6 shrink correspondingly. Total project length stays similar but the architectural primitives become reusable for future features.

## What stays in strategy.md

These are point-fixes that don't generalize cleanly to a primitive:

- **B-st-01 block-scope shadow leak** (Sprint 3.1) — sema scope-stack incorrectness. Specific fix.
- **B-he-01 nested Hermes literals** (Sprint 4.1) — parser dispatch debug. Specific fix.
- **Pattern surface coverage** (Sprint 4.2) — grammar-level work. Specific fix.
- **Cast validation** (Sprint 3.4) — small site.
- **B-st-04 assignment matrix gap** (Sprint 6) — grammar additions.
- **Trailing comma** (Sprint 6.1) — grammar standardization.

These are all "add 1-3 specific things" rather than "build a primitive everyone uses".

## Beyond Meta-Sprint 0 — what sets up Phase 5

Phase 5 is "AST analysis (transformative metaprog) + Datalog over fact-base". When that lands, several Meta-Sprint 0 deliverables become **redundant** (Datalog catches the violations directly):

- `validate_unique_names()` → Datalog query: `dup_name(item) :- in_list(...) ...`
- `check_type_arg_arity()` → Datalog query
- Cycle detection → SCC over depends-on
- `verify_lir_program()` → Datalog facts about LIR validity
- Reachability lint → AST walk

We want to land Meta-Sprint 0 NOW (immediate value, immediate cluster closure) BUT **mark each helper with a comment** indicating "TODO: replace with Datalog query when fact-base lands". So when Phase 5 ships, the migration from imperative helpers to declarative queries is cleanly scoped.

Anti-pattern memos and invariants.md don't go away — they describe the *language-level rules*, not the implementation. Datalog queries enforce them faster but the rules are the same.

## Adversarial-corpus infrastructure (deferred to Phase 4.5 or beyond)

The bag-hunt itself revealed that **non-adversarial testing missed major features for months**. Long-term, we want:

- **Per-production exhaustive corpus** — for each grammar rule, a test exercising every alternative + edge case (empty, single, multi, nested, neighbor-collision). Generated semi-automatically from the PEG grammar.
- **Property-based syntax fuzzer** — random valid AST → serialize → parse → diff. Random mutations to find parser crashes.
- **Doc-as-spec lint** — when reference says "X must Y", auto-generate a regression test.
- **Cross-feature interaction matrix** — automated test grid: each pair (generic × pattern, metacall × closure, etc.).

These are heavier lifts. **Not part of Meta-Sprint 0**, but should be on the roadmap as Phase 4.5.

The bag-hunt itself becomes a *recurring activity* once tooling exists — quarterly "adversarial sweep" that automatically generates a fresh catalog.

## Effort summary

| Item | Days | Cluster impact |
|---|---|---|
| M0.1 Validation primitives | 1 | Closes Clusters 2 + 4 (~15 bugs) |
| M0.2 PEG error productions | 1-2 | Closes Cluster 1 (6 P0) |
| M0.3 Attribute spec registry | 1-2 | Closes Cluster 3 (5+) |
| M0.4 Centralized type-construction | 1 | Closes pkg-thread bugs (~5) |
| M0.5 Sema completeness verifier | 2-3 | Closes Cluster 5 (4) |
| M0.6 Anti-pattern memos × 4 | 0.5 | Documentation |
| M0.7 invariants.md | 0.5 | Documentation |
| M0.8 cluster_index.md | 0.5 | Documentation |
| M0.9 dev-checklist.md | 0.5 | Documentation |
| M0.10 Subsystem pitfalls | 0.5 | Documentation |

**Total Meta-Sprint 0**: 8-12 days of architectural work. Closes ~30 bugs as a side-effect AND prevents future re-introductions.

## Verification

Meta-Sprint 0 is done when:

1. All 4 anti-pattern memos exist and are indexed in MEMORY.md.
2. invariants.md exists with at least 10 invariants documented.
3. cluster_index.md exists.
4. dev-checklist.md exists in `docs/`.
5. Each `ref_subsystem_*.md` has a "Pitfalls" section.
6. `validate_unique_names()` and `check_type_arg_arity()` helpers exist and are used at ≥1 site each.
7. `attr_registry` table exists in `src/compiler/`.
8. The 6 P0 parser-assertion crashes (B-mv-05/06/07/08, B-lx-01, B-lx-02) produce clean syntax errors instead of asserting.
9. 1030/1030 ctest still green.
10. Sprint 1 (per [strategy.md](strategy.md)) starts with helpers in place.

## Decision request

Two open questions for Phase 5 alignment:

1. **Should we build the per-production corpus tool now (in Meta-Sprint 0) or after Phase 5 lands?** Argument for "now" — bag-hunt was the last manual sweep we'll do, and tooling makes it cheap to repeat. Argument for "later" — Phase 5 metaprog gives much cleaner generation. Recommendation: **defer to Phase 5**, do the bag-hunt manually one more time post-fix to verify.

2. **Should `attr_registry` be a C++ table or a Hermes-driven config?** A Hermes-driven config would dogfood our own metaprog facilities and align with Phase 5 vision. Recommendation: **start C++**, migrate to Hermes once metaprog Phase 2 lands.

Both items deferred to "after Phase 5 design lock" rather than blocking Meta-Sprint 0.
