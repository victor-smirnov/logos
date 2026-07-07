# Bug catalog: Writ Literals

**Group**: 12 — Writ Literals
**Grammar rules covered**: `writ_lit`, `writ_map`, `writ_array`, `writ_entry`, `writ_val`, `writ_typed_array`, `writ_typed_map`, `writ_list_comp`, `writ_map_comp`
**Reference doc**: [docs/spec/writ.md](../spec/writ.md)
**Implementation entry points**:
- [src/compiler/sema_expr.cpp](../../src/compiler/sema_expr.cpp) — `lower_writ_val`, `lower_writ_lit`, `lower_writ_typed_*`
- [src/compiler/sema_decl.cpp](../../src/compiler/sema_decl.cpp) — `eval_static_writ_lit` (compile-time eval)
- [stdlib/std/writ/](../../stdlib/std/writ/) — runtime Writ layer

**Hunt date**: 2026-05-04
**Repros**: `/tmp/baghunt/writ/`

## Bugs

### B-he-01: Nested `@{...}` / `@[...]` literals fail to parse

**Severity**: P0 hard (major feature gap)
**Status**: fixed-in-Sprint4.1 (added AT-prefixed alts to writ_val; tests/logos/fail/writ_nested_at_literals_parse)
**Repro**: `B17/`, `B19/`, `B20/` — all of these fail:
```logos
let h: WritStatic = @{ "k": @{} };           // map containing map
let h: WritStatic = @[@{}];                  // array containing map
let h: WritStatic = @[@[1, 2]];              // array containing array
let h: WritStatic = @{ "k": @[1, 2] };       // map containing array
```
**Observed**: `syntax error near 'fn' at line 3` (or near `]` depending on position).
**Expected**: Per grammar `writ_val` allows `AT writ_typed_array / AT writ_map / AT writ_array` recursively. Nested literals should parse. All existing tests in `tests/logos/pass/writ_*` use only FLAT literals (verified by grep) — this corner has never been exercised, so it slipped.
**Suspected root**: Parser dispatch for nested `@`-prefixed values inside `writ_val` has a token-handling bug. The `AT` token at value position may be consumed by the wrong rule. Worth digging into the PEG generator output for `writ_val`.
**Tags**: `oversight:simple`, `tech-debt:nested-form-untested`

### B-he-02: Duplicate key in Writ map silently accepted

**Severity**: P0 (silent miscompile)
**Status**: fixed (resolve_wstatic_value walker also checks dup keys; covers `pub const X: WritStatic` path that bypassed the older eval_static_writ_lit check)
**Repro**: `B03/` —
```logos
let h: WritStatic = @{ "k": 1, "k": 2 };
```
**Observed**: Compiles cleanly. Whatever the resulting Writ map contains is implementation-defined (likely last-write-wins).
**Expected**: Sema/eval-time error: "duplicate key 'k' in Writ map".
**Suspected root**: `eval_static_writ_lit` ([sema_decl.cpp](../../src/compiler/sema_decl.cpp)) appends entries without dup-check. Same family as `missing-uniqueness-check` cluster (now 10+ bugs).
**Tags**: `oversight:simple`, `tech-debt:missing-uniqueness-check`

### B-he-03: Integer key in Writ map silently accepted

**Severity**: P1 (per-spec violation)
**Status**: not-a-bug — integer Writ-map keys are an intentional feature. The grammar's `writ_entry` accepts `STRING|INTEGER|MINUS INTEGER` keys for TinyObjectMap-style maps; the catalog's "per-Writ-spec violation" hypothesis was wrong (the reference doc never said string-only). Closed without code change.
**Repro**: `B14/` —
```logos
let h: WritStatic = @{ 42: 99 };  // int key, not string
```
**Observed**: Compiles cleanly.
**Expected**: Per Writ spec ([docs/spec/writ.md](../spec/writ.md)) — Writ maps require string keys. Should reject: "Writ map keys must be string literals".
**Suspected root**: `writ_entry` grammar likely doesn't restrict key type to STRING; or the value-time validation accepts any `writ_val`.
**Tags**: `oversight:simple`, `tech-debt:no-key-type-validation`

### B-he-04: Huge integer literal in Writ value silently saturates

**Severity**: P0 (silent miscompile)
**Status**: fixed-in-Sprint2.3 (LIT_INT overflow check covers Writ-value literals too)
**Repro**: `B13/` —
```logos
let h: WritStatic = @{ "k": 99999999999999999 };  // > i64 max
```
**Observed**: Compiles cleanly. Value silently saturates or wraps.
**Expected**: Same as B-ex-07 — reject literal-out-of-range.
**Suspected root**: Same family as B-ex-07 — `parse_int_literal` doesn't bound-check; no specific guard in Writ-eval path.
**Tags**: `oversight:simple`, `tech-debt:literal-saturation-no-error`

### B-he-05: `${capture}` in `WritStatic` produces confusing diagnostic

**Severity**: P1 diagnostic
**Status**: fixed — `let` mismatch site detects `WritStatic ← Writ` and emits a capture-specific hint instead of generic type mismatch.
**Repro**: `B05/` —
```logos
fn main() -> i32 {
    let x: i32 = 5;
    let h: WritStatic = @{ "v": ${x} };
    return 0;
}
```
**Observed**: `error: let 'h': type mismatch — expected WritStatic, got Writ`. The user sees a type mismatch but the real issue is "captures aren't allowed in WritStatic" (per [docs/spec/writ.md](../spec/writ.md)).
**Expected**: Sema error: "`${capture}` not allowed in WritStatic literal; use `Writ` (runtime) instead OR remove the capture".
**Suspected root**: Sema differentiates `Writ` (runtime, captures OK) from `WritStatic` (compile-time, no captures) but the error path reuses generic type-mismatch instead of capture-specific message.
**Tags**: `tech-debt:diagnostic-imprecise`

### B-he-06: Nested-literal failure cascades to confusing error position

**Severity**: P1 diagnostic (companion to B-he-01)
**Status**: fixed — added column tracking to the parse-error report. peg_gen now emits `furthest_column()` / `next_column()` accessors that walk back from the token's text pointer to the prior newline, and `module_loader`'s "syntax error" output appends `col M`. The PEG furthest-token tracking already pointed at the right offset; only the human-readable line+column was missing. Errors like "near '1' at line 3 col 37" now pinpoint the offending token even when multiple identical tokens share a line. The B-he-01 fix (Sprint 4.1) had already closed the cascading-to-`fn`/`]` symptom; this patch closes the residual "where on the line" gap.
**Repro**: same as B-he-01 cases — error reports "near 'fn'" or "near ']'" pointing to an unrelated token.
**Observed**: When nesting fails, the parse error lands far from the actual syntax issue, making it hard to locate.
**Expected**: At minimum, the parser should point to the `@` of the inner literal as the failure site.
**Suspected root**: Parser fail-recovery doesn't backtrack to the most-relevant token. Same family as the assertion-as-diagnostic cluster — when grammar fails, error positions are unhelpful.
**Tags**: `tech-debt:misleading-diagnostic`, `tech-debt:parser-no-recovery`

### B-he-07: Empty `@[]` and empty `@{}` accepted (consistency check, NOT a bug)

Confirmed working — same as B-ty-10 (zero-size array). Documented as design.

### B-he-08: Trailing comma in Writ map accepted (regression-confirm)

Confirmed working (H12). Trailing-comma-cluster: contrast with B-fn-03/04/05 (rejected in fn args). Inconsistency: Writ accepts, fns don't.

### B-he-09: writ_typed_array / writ_typed_map gated on missing stdlib helper

**Severity**: feature-incomplete
**Status**: confirmed-feature-incomplete (2026-05-07) — `@<I32>[10, 20, 30]` parses; type tag must be the capitalised Writ scalar set (I8/U8/.../F64). Sema then asks for `use logos.mem.writ.array;` because the typed-array stdlib helper (`ArrayI32`, …) hasn't been ported. lowercase `i32` is rejected with a list of accepted names.
**Note**: Sema gate works. Unblocking needs `stdlib/std/writ/array.logos` exposing `ArrayI32` … `ArrayF64` (and a `map.logos` analog for typed maps).
**Tags**: feature-incomplete:no-stdlib-helper

### B-he-10: writ_list_comp / writ_map_comp comprehensions gated on missing stdlib helper

**Severity**: feature-incomplete
**Status**: confirmed-feature-incomplete (2026-05-07) — `@[x * 2 for x in xs]` parses; sema rejects with two flavours:
  1. Iterating over a WritStatic (`@[...]`) → `only array/slice iteration supported (got WritStatic)`.
  2. Iterating over `[T; N]` / `&[T]` → `writ list comprehension requires use logos.mem.writ.ctr;`.
  `std.writ.ctr` does not exist in stdlib.
**Note**: Same shape as B-he-09 / B-pt-09 — comprehension lowering was sketched on the compiler side, but the stdlib builders never landed.
**Tags**: feature-incomplete:no-stdlib-helper

## Tag summary

| Tag | Open | Fixed | N/A | Total | Bugs |
|---|---|---|---|---|---|
| `oversight:simple` | 0 | 3 | 1 | 4 | B-he-01, B-he-02, B-he-03, B-he-04 |
| `tech-debt:diagnostic-imprecise` | 0 | 1 | 0 | 1 | B-he-05 |
| `tech-debt:literal-saturation-no-error` | 0 | 1 | 0 | 1 | B-he-04 |
| `tech-debt:misleading-diagnostic` | 0 | 1 | 0 | 1 | B-he-06 |
| `tech-debt:missing-uniqueness-check` | 0 | 1 | 0 | 1 | B-he-02 |
| `tech-debt:nested-form-untested` | 0 | 1 | 0 | 1 | B-he-01 |
| `tech-debt:no-key-type-validation` | 0 | 0 | 1 | 1 | B-he-03 |
| `tech-debt:parser-no-recovery` | 0 | 1 | 0 | 1 | B-he-06 |

**Cluster preview**:
- **B-he-01 (nested literals)** is the standout P0 — major feature gap that all existing tests happen to avoid. Should be high-priority architectural fix.
- **B-he-02** joins `missing-uniqueness-check` cluster (now ~10 bugs).
- **B-he-04** joins `literal-saturation-no-error` (B-ex-07).

## Regression-confirmed (NOT bugs)

- **H01**: empty `@{}` works.
- **H07**: float in Writ works.
- **H11**: `@{}` to `i32` correctly diagnosed as type mismatch.
- **H12**: trailing comma in Writ map accepted.
- **H18**: flat `@[1, 2, 3]` works.

## Notes for Phase 3

- **B-he-01 is huge**. The fact that nested Writ literals don't parse means many natural use cases (config trees, schemas, recursive structures) silently force users to flatten or build at runtime. Phase 4 priority.
- The cumulative `missing-uniqueness-check` cluster after Writ group: ~10 distinct bugs. Single-helper fix continues to be the highest-leverage Phase 3/4 task.
- Writ group has more **deferred** items than usual (B-he-09/10) — typed forms and comprehensions weren't probed. Worth a re-visit.
