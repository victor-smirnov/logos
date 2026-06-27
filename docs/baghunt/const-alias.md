# Bug catalog: Const & Type aliases

**Group**: 4 — Const & Type aliases
**Grammar rules covered**: `const_def`, `type_alias`
**Reference doc**: [docs/language/reference/items.md](../language/reference/items.md), [docs/language/reference/metaprog.md](../language/reference/metaprog.md) (for parametric WritStatic)
**Implementation entry points**:
- [src/compiler/sema_collect.cpp](../../src/compiler/sema_collect.cpp) — `collect_const`, `collect_type_alias`
- [src/compiler/sema.cpp](../../src/compiler/sema.cpp) — `resolve_hstatic_value`, `generic_consts_` storage

**Hunt date**: 2026-05-04
**Repros**: `/tmp/baghunt/const-alias/`
**Group size**: small (only 2 productions). Underweight catalog — 6 confirmed bugs.

## Bugs

### B-ca-01: Self-referential const → SEGFAULT

**Severity**: P0 hard (compiler crash)
**Status**: fixed-in-Sprint1 (shallow self-ref check in collect_const; tests/logos/fail/self_referential_const). Note: deep walks through writ literals deferred to Phase 5.
**Repro**: `B11/` —
```logos
pub const X: i32 = X + 1;
fn main() -> i32 { return X; }
```
**Observed**: `[1] 671739 segmentation fault (core dumped)`. Exit 139.
**Expected**: Sema diagnostic: "const 'X' references itself in initializer".
**Suspected root**: `collect_const` doesn't check that the initializer expression doesn't refer to the const being defined. When sema later evaluates `X` to substitute, it recurses unboundedly. Same family as B-it-01/02 (missing-cycle-guard cluster).
**Tags**: `oversight:simple`, `tech-debt:missing-cycle-guard`

### B-ca-02: Const initializer type-mismatch surfaces at MLIR-verifier instead of sema

**Severity**: P1 diagnostic
**Status**: fixed (lower_const_def types_compatible check + WritStatic special-case)
**Repro**: `B02/` —
```logos
pub const X: i32 = "hello";
fn main() -> i32 { return X; }
```
**Observed**: Sema accepts cleanly. MLIR-gen produces `error: type of return operand 0 ('!llvm.ptr') doesn't match function result type ('i32')` and verifier-failure.
**Expected**: Sema diagnostic at the const definition: "const 'X' declared as i32 but initializer is &[u8]".
**Suspected root**: `collect_const` doesn't typecheck the initializer expression against the declared type. Same pattern as a missing borrow-check before lower.
**Tags**: `oversight:simple`, `tech-debt:type-check-deferred-to-codegen`

### B-ca-03: Const initializer = function call silently accepted

**Severity**: P2 design (Rust-style const-evaluability not enforced)
**Status**: fixed — `collect_const` now requires the initializer to be const-evaluable: literal expressions (LIT_INT/LIT_BOOL/LIT_STR/LIT_FLOAT/LIT_CHAR/LIT_HSTATIC), simple arithmetic over them (BINOP/UNARY/PAREN_EXPR/CAST recursing on const-evaluable children), the explicit `metacall <fn>(...)` form, or writ-lit/array/tuple shapes that downstream checks already cover. A bare fn call as initializer (`pub const X = compute();`) is rejected with a specific message pointing at `metacall` as the explicit comptime-eval opt-in. Lock-in: fail test `const_bare_fn_call` (rejected), pass test `const_metacall_fn` (`pub const Y: i32 = metacall double_it(7);` works end-to-end).
**Repro**: `B03/` —
```logos
fn compute() -> i32 { return 42; }
pub const X: i32 = compute();
fn main() -> i32 { return X; }
```
**Observed**: Compiles cleanly.
**Expected**: Either reject ("const initializer must be const-evaluable") or document that Logos treats `pub const` as "lazy initialization" or "init at first read". Currently undefined behavior — what does X actually evaluate to, and when?
**Suspected root**: `collect_const` doesn't tag the initializer as const-required. Const-fold engine returns are uncertain ([feat_const_fold_metacall](../../../.claude/projects/-home-victor-devel-logos/memory/feat_const_fold_metacall.md)).
**Tags**: `design:incomplete`, `tech-debt:const-eval-policy-undefined`

### B-ca-04: Duplicate const def silently accepted

**Severity**: P0 (silent shadowing)
**Status**: fixed-in-M0.1 (collect_const dup check; tests/logos/fail/dup_const_def)
**Repro**: `B06/` —
```logos
pub const X: i32 = 1;
pub const X: i32 = 2;
fn main() -> i32 { return X; }
```
**Observed**: Compiles cleanly.
**Expected**: "duplicate const 'X'" — same as `duplicate type alias 'Foo'` which IS emitted (B-ca regression confirmed by `type Foo = i32; type Foo = i64;` test C07).
**Suspected root**: const-registration path doesn't dedup-check. Same family as B-it-05 (duplicate trait silent), B-fn-02 (duplicate param silent) — `missing-uniqueness-check` cluster.
**Tags**: `oversight:simple`, `tech-debt:missing-uniqueness-check`

### B-ca-05: Const array sema-OK but MLIR-gen says "undefined"

**Severity**: P1 diagnostic / P2 incomplete
**Status**: fixed (rejected branch of the OR) — sema now rejects `pub const ARR: [T; N] = ...` and `pub const T: (T1, T2) = ...` with "const arrays/tuples are not yet supported (would need rodata-global lowering)" pointing the user at `let`-in-fn or WritStatic alternatives, instead of letting mlir-gen die with "undefined ARR" downstream. Real rodata-global lowering stays as a feature; the diagnostic gap is closed. Lock-in: fail test `const_array_unsupported`.
**Repro**: `B12/` —
```logos
pub const ARR: [i32; 3] = [1, 2, 3];
fn main() -> i32 { return ARR[0]; }
```
**Observed**: Sema accepts, but mlir-gen errors `mlir_gen: undefined 'ARR'`.
**Expected**: Either support const arrays (emit as rodata global) — most languages do — or reject at sema with "const arrays not supported".
**Suspected root**: mlir-gen's const-emit path handles scalar consts only; array consts fall through to "undefined" lookup.
**Tags**: `design:incomplete`, `tech-debt:silent-feature-gap`

### B-ca-06: Misleading diagnostic for `<type:T>` in non-parametric const

**Severity**: P1 diagnostic
**Status**: fixed — root cause was distinct from the catalog's hypothesis. `lower_const_def` lowered the value AST via `lower_expr` for *every* const, not pushing the const's own type-params first. For a generic const containing `<type:K>`, K was unbound at this lowering and silently emitted as a placeholder string. Fix: push the const's `type_params` (as TypeVars) into `current_type_params_` before `lower_expr` and pop after; sema_expr's HERMES_TYPE_LIT handler now also rejects truly-unbound names (lock-in test `type_lit_unbound`).
**Repro**: `B14/` —
```logos
pub const X: WritStatic = @{ "key": <type:T> };
fn main() -> i32 { return 0; }
```
**Observed**: `error: unknown type 'WritStatic'` — but `WritStatic` is a known type. Real issue is unbound `T` in the literal.
**Expected**: "type-var 'T' is not bound (const 'X' is not declared with type-params)".
**Suspected root**: When `<type:T>` resolution fails, the failure cascades back into the const's WritStatic-resolution which then misreports the outer type as unknown.
**Tags**: `oversight:simple`, `tech-debt:cascading-error-misleading`

## Tag summary

| Tag | Open | Fixed | Total | Bugs |
|---|---|---|---|---|
| `oversight:simple` | 0 | 4 | 4 | B-ca-01, B-ca-02, B-ca-04, B-ca-06 |
| `design:incomplete` | 0 | 2 | 2 | B-ca-03, B-ca-05 |
| `tech-debt:cascading-error-misleading` | 0 | 1 | 1 | B-ca-06 |
| `tech-debt:const-eval-policy-undefined` | 0 | 1 | 1 | B-ca-03 |
| `tech-debt:missing-cycle-guard` | 0 | 1 | 1 | B-ca-01 |
| `tech-debt:missing-uniqueness-check` | 0 | 1 | 1 | B-ca-04 |
| `tech-debt:silent-feature-gap` | 0 | 1 | 1 | B-ca-05 |
| `tech-debt:type-check-deferred-to-codegen` | 0 | 1 | 1 | B-ca-02 |

**Cluster preview**:
- **missing-cycle-guard** (continuation from items): B-ca-01 (self-ref const) joins B-it-01 (recursive struct), B-it-02 (recursive enum). Pattern: any name-resolution path needs a "currently being resolved" set.
- **missing-uniqueness-check** (continuation): B-ca-04 joins B-it-03/04/05, B-fn-02. Pattern: dedup-check needed at every list-of-names registration.

## Regression-confirmed (NOT bugs)

- **C01**: const without value rejected.
- **C04**: self-referential type alias rejected.
- **C05**: type alias cycle rejected.
- **C07**: duplicate type alias rejected.
- **C08**: type alias unbound `T` rejected.
- **C09**: generic type alias missing args rejected with clear message.
- **C10**: generic type alias instantiation works end-to-end.
- **C13**: type alias with trait bound parses + lowers.
- **C15**: const referring to another const works (transitivity OK).

## Notes for Phase 3

- **Const-eval policy** (B-ca-03) is the most strategic open question here. Logos has no language-level "const fn" concept; runtime fn calls in const initializers either need to be:
  - Forbidden (Rust-style — initializer must be const-evaluable; reject `compute()`)
  - Lazy (init-at-first-read — but then concurrency complicates)
  - Eager-at-startup (init in a `__logos_static_init__` ctor — ABI/ordering implications)

  Decision deferred to language design; baghunt just flags it.
- **Const arrays** (B-ca-05) suggests Logos const machinery is scalar-biased. Worth a deeper sweep of "what types CAN appear in a `pub const`?".
- The "unknown WritStatic" misleading diagnostic (B-ca-06) is a manifestation of cascading-error-recovery weakness — diagnostic shows the OUTER failure when the INNER one is the cause. Pattern likely repeats elsewhere.
