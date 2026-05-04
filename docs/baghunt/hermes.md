# Bug catalog: Hermes Literals

**Group**: 12 — Hermes Literals
**Grammar rules covered**: `hermes_lit`, `hermes_map`, `hermes_array`, `hermes_entry`, `hermes_val`, `hermes_typed_array`, `hermes_typed_map`, `hermes_list_comp`, `hermes_map_comp`
**Reference doc**: [docs/language/reference/hermes.md](../language/reference/hermes.md)
**Implementation entry points**:
- [src/compiler/sema_expr.cpp](../../src/compiler/sema_expr.cpp) — `lower_hermes_val`, `lower_hermes_lit`, `lower_hermes_typed_*`
- [src/compiler/sema_decl.cpp](../../src/compiler/sema_decl.cpp) — `eval_static_hermes_lit` (compile-time eval)
- [stdlib/std/hermes/](../../stdlib/std/hermes/) — runtime Hermes layer

**Hunt date**: 2026-05-04
**Repros**: `/tmp/baghunt/hermes/`

## Bugs

### B-he-01: Nested `@{...}` / `@[...]` literals fail to parse

**Severity**: P0 hard (major feature gap)
**Status**: fixed-in-Sprint4.1 (added AT-prefixed alts to hermes_val; tests/logos/fail/hermes_nested_at_literals_parse)
**Repro**: `B17/`, `B19/`, `B20/` — all of these fail:
```logos
let h: HermesStatic = @{ "k": @{} };           // map containing map
let h: HermesStatic = @[@{}];                  // array containing map
let h: HermesStatic = @[@[1, 2]];              // array containing array
let h: HermesStatic = @{ "k": @[1, 2] };       // map containing array
```
**Observed**: `syntax error near 'fn' at line 3` (or near `]` depending on position).
**Expected**: Per grammar `hermes_val` allows `AT hermes_typed_array / AT hermes_map / AT hermes_array` recursively. Nested literals should parse. All existing tests in `tests/logos/pass/hermes_*` use only FLAT literals (verified by grep) — this corner has never been exercised, so it slipped.
**Suspected root**: Parser dispatch for nested `@`-prefixed values inside `hermes_val` has a token-handling bug. The `AT` token at value position may be consumed by the wrong rule. Worth digging into the PEG generator output for `hermes_val`.
**Tags**: `oversight:simple`, `tech-debt:nested-form-untested`

### B-he-02: Duplicate key in Hermes map silently accepted

**Severity**: P0 (silent miscompile)
**Status**: fixed (resolve_hstatic_value walker also checks dup keys; covers `pub const X: HermesStatic` path that bypassed the older eval_static_hermes_lit check)
**Repro**: `B03/` —
```logos
let h: HermesStatic = @{ "k": 1, "k": 2 };
```
**Observed**: Compiles cleanly. Whatever the resulting Hermes map contains is implementation-defined (likely last-write-wins).
**Expected**: Sema/eval-time error: "duplicate key 'k' in Hermes map".
**Suspected root**: `eval_static_hermes_lit` ([sema_decl.cpp](../../src/compiler/sema_decl.cpp)) appends entries without dup-check. Same family as `missing-uniqueness-check` cluster (now 10+ bugs).
**Tags**: `oversight:simple`, `tech-debt:missing-uniqueness-check`

### B-he-03: Integer key in Hermes map silently accepted

**Severity**: P1 (per-spec violation)
**Status**: deferred — integer Hermes keys are valid feature; bug is design
**Repro**: `B14/` —
```logos
let h: HermesStatic = @{ 42: 99 };  // int key, not string
```
**Observed**: Compiles cleanly.
**Expected**: Per Hermes spec ([docs/language/reference/hermes.md](../language/reference/hermes.md)) — Hermes maps require string keys. Should reject: "Hermes map keys must be string literals".
**Suspected root**: `hermes_entry` grammar likely doesn't restrict key type to STRING; or the value-time validation accepts any `hermes_val`.
**Tags**: `oversight:simple`, `tech-debt:no-key-type-validation`

### B-he-04: Huge integer literal in Hermes value silently saturates

**Severity**: P0 (silent miscompile)
**Status**: fixed-in-Sprint2.3 (LIT_INT overflow check covers Hermes-value literals too)
**Repro**: `B13/` —
```logos
let h: HermesStatic = @{ "k": 99999999999999999 };  // > i64 max
```
**Observed**: Compiles cleanly. Value silently saturates or wraps.
**Expected**: Same as B-ex-07 — reject literal-out-of-range.
**Suspected root**: Same family as B-ex-07 — `parse_int_literal` doesn't bound-check; no specific guard in Hermes-eval path.
**Tags**: `oversight:simple`, `tech-debt:literal-saturation-no-error`

### B-he-05: `${capture}` in `HermesStatic` produces confusing diagnostic

**Severity**: P1 diagnostic
**Status**: deferred — diagnostic improvement needs flow-direction tracking
**Repro**: `B05/` —
```logos
fn main() -> i32 {
    let x: i32 = 5;
    let h: HermesStatic = @{ "v": ${x} };
    return 0;
}
```
**Observed**: `error: let 'h': type mismatch — expected HermesStatic, got Hermes`. The user sees a type mismatch but the real issue is "captures aren't allowed in HermesStatic" (per [docs/language/reference/hermes.md](../language/reference/hermes.md)).
**Expected**: Sema error: "`${capture}` not allowed in HermesStatic literal; use `Hermes` (runtime) instead OR remove the capture".
**Suspected root**: Sema differentiates `Hermes` (runtime, captures OK) from `HermesStatic` (compile-time, no captures) but the error path reuses generic type-mismatch instead of capture-specific message.
**Tags**: `tech-debt:diagnostic-imprecise`

### B-he-06: Nested-literal failure cascades to confusing error position

**Severity**: P1 diagnostic (companion to B-he-01)
**Status**: deferred — parser error-position improvement
**Repro**: same as B-he-01 cases — error reports "near 'fn'" or "near ']'" pointing to an unrelated token.
**Observed**: When nesting fails, the parse error lands far from the actual syntax issue, making it hard to locate.
**Expected**: At minimum, the parser should point to the `@` of the inner literal as the failure site.
**Suspected root**: Parser fail-recovery doesn't backtrack to the most-relevant token. Same family as the assertion-as-diagnostic cluster — when grammar fails, error positions are unhelpful.
**Tags**: `tech-debt:misleading-diagnostic`, `tech-debt:parser-no-recovery`

### B-he-07: Empty `@[]` and empty `@{}` accepted (consistency check, NOT a bug)

Confirmed working — same as B-ty-10 (zero-size array). Documented as design.

### B-he-08: Trailing comma in Hermes map accepted (regression-confirm)

Confirmed working (H12). Trailing-comma-cluster: contrast with B-fn-03/04/05 (rejected in fn args). Inconsistency: Hermes accepts, fns don't.

### B-he-09: hermes_typed_array / hermes_typed_map syntax not surveyed (deferred)

**Severity**: deferred
**Status**: not-tested-this-pass
**Note**: Grammar shows `hermes_typed_array <- LT IDENT GT LBRACKET ... RBRACKET` (i.e. `@<i32>[1, 2]`-style) but my initial test used wrong syntax. Worth a re-test with `@<i32>[1, 2, 3]` form.
**Tags**: deferred

### B-he-10: hermes_list_comp / hermes_map_comp comprehensions not surveyed (deferred)

**Severity**: deferred
**Status**: not-tested-this-pass
**Note**: Forms like `@[expr for x in iter]` exist in grammar but weren't probed.
**Tags**: deferred

## Tag summary

| Tag | Count | Bugs |
|---|---|---|
| `oversight:simple` | 4 | B-he-01, B-he-02, B-he-03, B-he-04 |
| `tech-debt:nested-form-untested` | 1 | B-he-01 |
| `tech-debt:missing-uniqueness-check` | 1 | B-he-02 |
| `tech-debt:no-key-type-validation` | 1 | B-he-03 |
| `tech-debt:literal-saturation-no-error` | 1 | B-he-04 |
| `tech-debt:diagnostic-imprecise` | 1 | B-he-05 |
| `tech-debt:misleading-diagnostic` | 1 | B-he-06 |
| `tech-debt:parser-no-recovery` | 1 | B-he-06 |

**Cluster preview**:
- **B-he-01 (nested literals)** is the standout P0 — major feature gap that all existing tests happen to avoid. Should be high-priority architectural fix.
- **B-he-02** joins `missing-uniqueness-check` cluster (now ~10 bugs).
- **B-he-04** joins `literal-saturation-no-error` (B-ex-07).

## Regression-confirmed (NOT bugs)

- **H01**: empty `@{}` works.
- **H07**: float in Hermes works.
- **H11**: `@{}` to `i32` correctly diagnosed as type mismatch.
- **H12**: trailing comma in Hermes map accepted.
- **H18**: flat `@[1, 2, 3]` works.

## Notes for Phase 3

- **B-he-01 is huge**. The fact that nested Hermes literals don't parse means many natural use cases (config trees, schemas, recursive structures) silently force users to flatten or build at runtime. Phase 4 priority.
- The cumulative `missing-uniqueness-check` cluster after Hermes group: ~10 distinct bugs. Single-helper fix continues to be the highest-leverage Phase 3/4 task.
- Hermes group has more **deferred** items than usual (B-he-09/10) — typed forms and comprehensions weren't probed. Worth a re-visit.
