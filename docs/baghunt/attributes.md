# Bug catalog: Attributes & Meta Blocks

**Group**: 13 — Attributes & Meta Blocks (numbered #5 in feature-group inventory)
**Grammar rules covered**: `annotation`, `annot_val`, `annot_args`, `meta_block`
**Reference doc**: [docs/language/reference/attributes.md](../language/reference/attributes.md)
**Implementation entry points**:
- [src/compiler/sema_collect.cpp](../../src/compiler/sema_collect.cpp) — `parse_annotation`, `extract_meta_val`
- [src/compiler/sema_decl.cpp](../../src/compiler/sema_decl.cpp) — `eval_static_hermes_lit` for meta blocks

**Hunt date**: 2026-05-04
**Repros**: `/tmp/baghunt/attributes/`

**Group note**: The attribute system has *minimal* validation today. Most "wrong attribute on wrong target" cases are silently accepted. Per [attributes.md](../language/reference/attributes.md): "Unknown attributes today produce no diagnostic; they will become a warning, then a hard error once the supported list is frozen." This catalog records the gap explicitly.

## Bugs

### B-at-01: Unknown `#[unknown_attr]` silently accepted (known/documented)

**Severity**: P1 design
**Status**: deferred-to-Phase-5 (cross-module trigger registry not closed at collect-time; whole-program fact-base needed before warning is reliable)
**Repro**: `B01/` —
```logos
#[unknown_attr]
struct Foo { x: i32 }
```
**Observed**: Compiles cleanly.
**Expected**: Warning (eventually hard error per docs roadmap).
**Tags**: `oversight:simple`, `tech-debt:no-attribute-validation`

### B-at-02: `#[type_code]` on generic template silently accepted

**Severity**: P1 (per-spec violation per docs)
**Status**: fixed-in-M0.3 (regression: tests/logos/fail/attr_type_code_on_generic_struct + existing template_genos_type_code)
**Repro**: `B03/` —
```logos
#[type_code = 42]
struct Foo<T> { x: T }   // generic template — should reject
```
**Observed**: Compiles cleanly.
**Expected**: Per [attributes.md](../language/reference/attributes.md): "`#[type_code]` is rejected on a template genos / generic struct; only fully-specialised forms or non-generic datatypes may carry it."
**Suspected root**: [sema_collect.cpp](../../src/compiler/sema_collect.cpp) annotation-handler doesn't check `type_params.empty()` before accepting `#[type_code]`.
**Tags**: `oversight:simple`, `tech-debt:no-attribute-validation`

### B-at-03: Duplicate `#[type_code]` silently accepted

**Severity**: P1 (silent shadowing)
**Status**: fixed-in-M0.1 (collect_module exclusive-attr dup check; tests/logos/fail/dup_type_code)
**Repro**: `B04/` —
```logos
#[type_code = 42]
#[type_code = 100]
pub eidos Foo { x: i32 }
```
**Observed**: Compiles cleanly. Implementation-defined which type_code wins.
**Expected**: "duplicate `#[type_code]` annotation".
**Suspected root**: Annotation aggregation (per-item) doesn't dedup-check on attribute name. Same family as `missing-uniqueness-check` cluster.
**Tags**: `oversight:simple`, `tech-debt:missing-uniqueness-check`

### B-at-04: `#[tag_dispatch]` on non-trait silently accepted

**Severity**: P1 (per-spec violation)
**Status**: fixed-in-M0.3 (regression: tests/logos/fail/attr_tag_dispatch_on_struct)
**Repro**: `B08/` —
```logos
#[tag_dispatch(SomeSystem)]
struct Foo { x: i32 }   // tag_dispatch is for traits only
```
**Observed**: Compiles cleanly.
**Expected**: "`#[tag_dispatch]` only applies to traits".
**Suspected root**: Same as B-at-01/02 — no target-validation on attribute application.
**Tags**: `oversight:simple`, `tech-debt:no-attribute-validation`

### B-at-05: `#[zoned]` on non-struct silently accepted

**Severity**: P1 (per-spec violation)
**Status**: fixed-in-M0.3 (regression: tests/logos/fail/attr_zoned_on_enum)
**Repro**: `B09/` —
```logos
#[zoned] enum E { A, B, }   // zoned is for struct only
```
**Observed**: Compiles cleanly.
**Expected**: "`#[zoned]` only applies to struct".
**Suspected root**: Same family as B-at-04.
**Tags**: `oversight:simple`, `tech-debt:no-attribute-validation`

### B-at-06: `#[derive(NonExistentTrait)]` silently accepted

**Severity**: P1 (silent miscompile potential)
**Status**: deferred — no general derive() registry yet
**Repro**: `B10/` —
```logos
#[derive(NonExistentTrait)]
struct Foo { x: i32 }
```
**Observed**: Compiles cleanly. The derive simply doesn't fire because no handler exists.
**Expected**: "trait 'NonExistentTrait' is not derivable" or "no derive handler registered for 'NonExistentTrait'".
**Suspected root**: `#[derive(...)]` runs through `#[metaprog_handler]` lookup; missing trigger silently does nothing.
**Tags**: `oversight:simple`, `tech-debt:no-attribute-validation`

### B-at-07: `#[type_code]` in reserved range (1-127) silently accepted

**Severity**: P2 (spec violation)
**Status**: fixed (warn when type_code in 1..128 outside std.* package)
**Repro**: `B13/` —
```logos
#[type_code = 100]   // per docs: 1-127 reserved for system
pub eidos Foo { x: i32 }
```
**Observed**: Compiles cleanly.
**Expected**: Per [attributes.md](../language/reference/attributes.md): "Codes 1–127 are reserved for system use; user codes start at 128." Should warn or error.
**Suspected root**: No range check on `#[type_code]` value.
**Tags**: `oversight:simple`, `tech-debt:no-attribute-validation`
**Deferred** to Phase 5: stdlib primitives legitimately use codes in [1..128]; a static range check here is noisy without a "user-package" predicate. The fact-base will attribute each `#[type_code]` to its package and warn only on user code.

### B-at-08: `#[deprecated]` no diagnostic at use-site (planned, not implemented)

**Severity**: P2 (roadmap)
**Status**: confirmed-known (per docs roadmap)
**Repro**: `B07/` —
```logos
#[deprecated(reason = "use new_api")]
fn old() -> i32 { return 0; }
fn main() -> i32 { return old(); }   // no warning here
```
**Observed**: No warning at the call site.
**Expected**: Warning: "use of deprecated function 'old': use new_api".
**Suspected root**: `#[deprecated]` is in the planned-not-implemented list per docs. The annotation parses but isn't consumed.
**Tags**: `design:incomplete`

## Tag summary

| Tag | Count | Bugs |
|---|---|---|
| `oversight:simple` | 7 | B-at-01..07 |
| `tech-debt:no-attribute-validation` | 6 | B-at-01, B-at-02, B-at-04, B-at-05, B-at-06, B-at-07 |
| `tech-debt:missing-uniqueness-check` | 1 | B-at-03 |
| `design:incomplete` | 1 | B-at-08 |

**Cluster preview**:
- **`tech-debt:no-attribute-validation`** is the dominant cluster (6 bugs). The architectural fix is a per-attribute spec table: `name → (allowed_targets, value_type, range_check, dup_policy)`. One pass + one table closes most of these.

## Regression-confirmed (NOT bugs)

- **A02**: missing value `#[type_code = ]` rejected.
- **A05**: malformed meta block rejected.
- **A06**: `#[type_code = "string"]` rejected (type-mismatch).
- **A11**: meta block in fn body rejected (only allowed on items).
- **A14**: user-defined `#[annotation]` typed-attribute works.

## Notes for Phase 3

- The whole attribute system needs a **spec table + validation pass**. This is a Phase 4 architectural item: define a registry of recognized attributes with their target-kind and value-type constraints, then a single pre-lower pass validates every annotation against the registry.
- This also pairs naturally with future AST analysis (Phase 5) — the spec table becomes machine-queryable, enabling downstream tools to check "what attributes can target X" without reading sema.
