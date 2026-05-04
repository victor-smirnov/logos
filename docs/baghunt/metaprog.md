# Bug catalog: Metaprog

**Group**: 13 — Metaprog (quote / template / metacall / instantiate)
**Grammar rules covered**: `metacall_item_decl`, `instantiate_decl`, `pub_instantiate_decl`, `template_decl`, `template_inner`, `quote_item_expr`, `quote_expr_expr`, `quote_ty_expr`
**Reference doc**: [docs/language/reference/metaprog.md](../language/reference/metaprog.md)
**Implementation entry points**:
- [src/compiler/sema_expr.cpp](../../src/compiler/sema_expr.cpp) — `lower_quote_item`, `lower_quote_expr`, `lower_quote_ty`, `lower_metacall_item`
- [src/compiler/sema.cpp](../../src/compiler/sema.cpp) — `generic_consts_`, parametric HermesStatic substitution
- [stdlib/std/compiler/metaprog/](../../stdlib/std/compiler/metaprog/) — runtime metaprog machinery, `logos_emit_item_blob_subst`

**Hunt date**: 2026-05-04
**Repros**: `/tmp/baghunt/metaprog/`

**Group note**: Metaprog has SUBSTANTIALLY MORE validation than Attributes. Most "wrong shape" cases are caught with clear diagnostics. The catalog is **light** with 4 confirmed bugs.

## Bugs

### B-mt-01: `metacall non_existent_fn();` diagnostic shows wrong fn context

**Severity**: P1 diagnostic
**Status**: not-reproduced (verified 2026-05-04 — diagnostic now correctly shows `[fn main]`. Earlier sema work fixed the context propagation.)
**Repro**: `B02/` —
```logos
metacall non_existent_fn();
fn main() -> i32 { return 0; }
```
**Observed**:
```
error [fn OView__decimal_limb]: call to undefined function 'non_existent_fn'
error [fn OView__decimal_limb]: metacall (item position): callee must return QuoteItemBlob or ItemList
```
**Expected**: The error context should reference the actual call site (item-position metacall in `package main`), not some random stdlib function (`OView__decimal_limb`).
**Suspected root**: Metacall lowering at item-position runs in some synthetic context, and when the callee resolution fails, the diagnostic uses the wrong `cur_fn_` slot. Same family as `tech-debt:misleading-diagnostic`.
**Tags**: `tech-debt:misleading-diagnostic`, `tech-debt:wrong-error-context`

### B-mt-02: `instantiate Foo;` on non-generic struct silently accepted

**Severity**: P1 (per-spec violation)
**Status**: fixed (resolve_type empty-type-args check in INSTANTIATE_DECL; tests/logos/fail/instantiate_non_generic)
**Repro**: `B03/` —
```logos
struct Foo { x: i32 }
instantiate Foo;   // not a generic
fn main() -> i32 { return 0; }
```
**Observed**: Compiles cleanly.
**Expected**: Per [metaprog.md](../language/reference/metaprog.md): "`instantiate Foo<T>;` materializes Foo<T> as a root for monomorphization (analog of C++ `template class Foo<int>;`)". Non-generic types don't need instantiation hints. Should error: "type 'Foo' is not generic; `instantiate` only applies to generic templates".
**Suspected root**: `instantiate_decl` lowering doesn't check that the target type has type-params.
**Tags**: `oversight:simple`, `tech-debt:no-validation`

### B-mt-03: `instantiate Foo<i32>;` with arity mismatch leaks to MLIR-gen

**Severity**: P1 diagnostic
**Status**: fixed-in-M0.1 (closed by `check_type_arg_arity` via resolve_type on the INSTANTIATE_DECL TYPE node)
**Repro**: `B11/` —
```logos
struct Foo<T, U> { x: T, y: U }
instantiate Foo<i32>;   // missing U
fn main() -> i32 { return 0; }
```
**Observed**:
```
mlir_gen: unresolved TypeVar 'U' — mono_pass required
mlir_gen: unknown field type in 'Foo$G1$i32'
logosc: MLIR generation failed
```
**Expected**: Sema error at the `instantiate` site: "`Foo` takes 2 type-args, got 1".
**Suspected root**: Same as B-ty-04 (too-few-type-args pattern). `instantiate_decl` doesn't validate arity against the template's type-params count.
**Tags**: `tech-debt:missing-arity-check`, `tech-debt:diagnostic-from-codegen`

### B-mt-04: Unknown trigger attribute (`#[no_such_trigger]`) silently accepted

**Severity**: P1 (silent miscompile)
**Status**: confirmed (2026-05-04, also B-at-01)
**Repro**: `B14/` —
```logos
#[no_such_trigger]
struct Foo { x: i32 }
```
**Observed**: Compiles cleanly. The annotation has no registered `#[metaprog_handler]`, so nothing fires.
**Expected**: Warning: "no `#[metaprog_handler]` registered for trigger 'no_such_trigger'; annotation has no effect".
**Suspected root**: Trigger resolution is silent-on-miss. Same family as B-at-01 (unknown attribute) but specific to metaprog hooks.
**Tags**: `oversight:simple`, `tech-debt:no-attribute-validation`

### B-mt-05: quote_stmt! / quote_pat! / quote_ident! not implemented (per docs)

**Severity**: P2 design (roadmap)
**Status**: confirmed-known
**Repro**: `B09/` —
```logos
fn make() -> StmtBlob {
    return quote_stmt! { let x = 5; };
}
```
**Observed**: `syntax error near 'quote_stmt' at line 4`.
**Expected**: Per [metaprog.md](../language/reference/metaprog.md): "`quote_stmt!`, `quote_pat!`, `quote_ident!` — not implemented." Syntax error is correct; could provide a helpful message ("not yet implemented").
**Tags**: `design:incomplete`

## Tag summary

| Tag | Count | Bugs |
|---|---|---|
| `oversight:simple` | 2 | B-mt-02, B-mt-04 |
| `tech-debt:no-validation` | 1 | B-mt-02 |
| `tech-debt:misleading-diagnostic` | 1 | B-mt-01 |
| `tech-debt:wrong-error-context` | 1 | B-mt-01 |
| `tech-debt:missing-arity-check` | 1 | B-mt-03 |
| `tech-debt:diagnostic-from-codegen` | 1 | B-mt-03 |
| `tech-debt:no-attribute-validation` | 1 | B-mt-04 |
| `design:incomplete` | 1 | B-mt-05 |

**Cluster preview**:
- B-mt-03 joins `missing-arity-check` cluster (now 5 bugs).
- B-mt-04 joins `no-attribute-validation` cluster (now 7 bugs).

## Regression-confirmed (NOT bugs) — metaprog is well-validated

- **M01**: quote_item! body brace mismatch → syntax error.
- **M04**: `template extern fn` rejected (extern_fn not in template_inner — good).
- **M05**: antiquot of undefined var → clear sema error.
- **M06**: `template_of::<X>()` undef → clear sema error.
- **M07**: `type_of::<X>()` undef → clear sema error.
- **M08**: antiquot wrong type → clear `quote_expr!: #x — expected Ident or ExprBlob`.
- **M10**: `#[metaprog_handler]` hook with wrong sig → clear "must take exactly one parameter" error.
- **M12**: metacall undef in expr position → clear error chain.

## Notes for Phase 3

- Metaprog is **substantially better-validated than Attributes** (~95). The validation patterns here (`#[metaprog_handler]` hook signature check, type-param scope for parametric HermesStatic, antiquot type-checking) are good examples to apply to the under-validated areas.
- B-mt-03 (instantiate arity) is fixable in the same pass as B-ty-04 (generic-instantiation arity). One arch fix unblocks both.
- B-mt-01 (wrong fn context in error) hints at a broader issue: when sema runs in synthetic context (metacall, derive, etc.) the diagnostic-context tracking gets confused. Phase 4 worth a dedicated audit of "what context does each error see".
