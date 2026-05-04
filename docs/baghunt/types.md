# Bug catalog: Type System

**Group**: 5 — Type System
**Grammar rules covered**: `type_ref`, `simple_type`, `path_step`, `ptr_type`, `ref_type`, `ref_pointee`, `slice_type`, `arr_type`, `tagged_type`, `dyn_type`, `closure_type`, `closure_type_args`, `fn_ptr_type`, `fn_ptr_type_args`, `unit_type`, `tuple_type`, `impl_type`, `assoc_type_ref`, `antiquot_type`, `typeof_type`, `cfg_slot_type`, `cfg_slot_assoc_ref`, `hstatic_lit_type`, `hermes_arr_type`, `hermes_map_type`, `type_or_lt_arg`
**Reference doc**: [docs/language/reference/types.md](../language/reference/types.md)
**Implementation entry points**:
- [src/compiler/sema.cpp](../../src/compiler/sema.cpp) — `resolve_type` (~line 2400-2750), `compute_type_uid`, `types_equal`, `mangle_type_for_name`, `concrete_struct_name`, `type_str`
- [src/compiler/sema_impl.hpp](../../src/compiler/sema_impl.hpp) — `make_struct_type`, `make_datatype_type`, `make_generic_struct`, etc.
- [src/compiler/mono_subst.cpp](../../src/compiler/mono_subst.cpp) — `subst_type`
- [src/compiler/mlir_gen_types.cpp](../../src/compiler/mlir_gen_types.cpp) — `logos_to_mlir`

**Hunt date**: 2026-05-04
**Repros**: `/tmp/baghunt/types/`

## Bugs

### B-ty-01: Empty tuple type `()` → SEGFAULT

**Severity**: P0 hard (compiler crash)
**Status**: fixed-in-Sprint1 (param-position rejected with diagnostic; tests/logos/fail/unit_param_type)
**Repro**: `B02/` —
```logos
fn helper(x: ()) -> () { return; }
fn main() -> i32 { helper(()); return 0; }
EOF```
**Observed**: `[1] segmentation fault (core dumped)`. Exit 139.
**Expected**: `()` is the unit type — should be a normal type. The compiler should accept it, lower to `void`/i0 in LLVM, and run cleanly.
**Suspected root**: `type_ref` dispatch may special-case `unit_type` and miss the (no-args) tuple form, OR `tuple_type` with zero elements crashes in the resolution path.
**Tags**: `oversight:simple`

### B-ty-02: `impl Trait` at parameter position → SEGFAULT

**Severity**: P0 hard (compiler crash)
**Status**: fixed-in-Sprint1 (param-position rejected with diagnostic; tests/logos/fail/impl_trait_param)
**Repro**: `B19/` —
```logos
trait Foo { fn x(self: *const Self) -> i32 { return 0; } }
fn helper(x: impl Foo) -> i32 { return 0; }
fn main() -> i32 { return 0; }
```
**Observed**: `[1] segmentation fault (core dumped)`. Exit 139.
**Expected**: Either accept `impl Foo` at parameter position (Rust-style implicit generic) and translate to `fn helper<T: Foo>(x: T)`, OR reject with clear "impl Trait only allowed at function return position".
**Suspected root**: `impl_type` is allowed by `type_ref` but [src/compiler/sema_decl.cpp](../../src/compiler/sema_decl.cpp)'s parameter handling crashes when it encounters a non-concrete type. Lower-fn doesn't gracefully fail for `impl Trait` params.
**Tags**: `oversight:simple`, `tech-debt:missing-position-check`

### B-ty-03: Too many type arguments silently accepted

**Severity**: P0 (silent miscompile potential)
**Status**: fixed-in-M0.1 (check_type_arg_arity)
**Repro**: `B06/` —
```logos
struct Foo<A, B> { a: A, b: B }
fn helper(x: Foo<i32, i64, bool>) -> i32 { return 0; }
fn main() -> i32 { return 0; }
```
**Observed**: Compiles cleanly. Extra type args are silently dropped.
**Expected**: Sema diagnostic "type 'Foo' takes 2 type-args, got 3".
**Suspected root**: `resolve_type` for generic instantiation doesn't check `args.size() == sinfo->type_params.size()`. Same family as missing-uniqueness checks.
**Tags**: `oversight:simple`, `tech-debt:missing-arity-check`

### B-ty-04: Too few type arguments leaks to MLIR-gen with cryptic error

**Severity**: P1 diagnostic
**Status**: fixed-in-M0.1 (closed by `check_type_arg_arity` in resolve_type)
**Repro**: `B07/` —
```logos
struct Foo<A, B> { a: A, b: B }
fn helper(x: Foo<i32>) -> i32 { return 0; }
```
**Observed**: `mlir_gen: unresolved TypeVar 'B' — mono_pass required` then `mlir_gen: unknown field type in 'Foo$G1$i32'`. Sema lets it through.
**Expected**: Sema error: "type 'Foo' takes 2 type-args, got 1".
**Suspected root**: Same as B-ty-03 — arity check missing at sema. Symptom differs (B-ty-03 silently drops extras, B-ty-04 leaks unresolved TypeVar).
**Tags**: `tech-debt:missing-arity-check`, `tech-debt:diagnostic-from-codegen`

### B-ty-05: Type arguments on non-generic type silently accepted

**Severity**: P0 (silent miscompile)
**Status**: deferred (attempted check_type_arg_arity tightening regressed 319 tests; needs proper phase-tracking — many legitimate prepass sites resolve types with args before type_params are populated)
**Repro**: `B08/` —
```logos
struct Foo { x: i32 }
fn helper(x: Foo<i32>) -> i32 { return 0; }
```
**Observed**: Compiles cleanly.
**Expected**: "type 'Foo' is not generic; cannot accept type-args".
**Suspected root**: Generic instantiation path doesn't verify the target IS generic before accepting `<args>`.
**Tags**: `oversight:simple`, `tech-debt:missing-arity-check`

### B-ty-06: `cfg_slot_type` on non-HermesStatic type silently accepted

**Severity**: P1 diagnostic
**Status**: confirmed (2026-05-04)
**Repro**: `B18/` —
```logos
fn helper<T>(x: <type:T.field>) -> i32 { return 0; }
fn main() -> i32 { return 0; }
```
**Observed**: Compiles. `<type:T.field>` is meaningful only when T has bound `HermesStatic`; for an unconstrained TypeVar T this should error.
**Expected**: "cfg_slot extraction `<type:T.field>` requires T to be `const HermesStatic`".
**Suspected root**: `resolve_type` for `cfg_slot_type` resolves to a phantom CfgSlotType TypeRef without checking that the carrier is actually a const-generic HermesStatic param.
**Tags**: `oversight:simple`, `tech-debt:missing-bound-check`

### B-ty-07: `&&mut T` syntax error (lexer collapse `&&`)

**Severity**: P2 design (lexer ambiguity)
**Status**: fixed-in-Sprint6.2 (DOUBLE_REF_TYPE / DOUBLE_REF_MUT_TYPE alts; tests/logos/pass/double_ref_type)
**Repro**: `B15/` —
```logos
fn helper(p: &&mut i32) -> i32 { return 0; }
```
**Observed**: `syntax error near 'fn' at line 2`. The `&&` is presumably tokenized as logical-AND (`AMPAMP`), not two refs.
**Expected**: Either parse `&&` in type position as `& &` (two refs) — Rust does this — or document the requirement to space them: `& &mut i32`.
**Suspected root**: Lexer greedy-tokenizes `&&` to `AMPAMP`. The parser sees `AMPAMP` after `:` and fails. Workaround: space them.
**Tags**: `tech-debt:lexer-greedy-collision`, `design:incomplete`

### B-ty-08: `||` closure-with-no-args syntax error (lexer collapse `||`)

**Severity**: P2 design (lexer ambiguity)
**Status**: fixed-in-Sprint6.2 (closure_type accepts OR ARROW; tests/logos/pass/zero_arg_closure_type)
**Repro**: `B29/` —
```logos
fn helper(f: || -> i32) -> i32 { return 0; }
```
**Observed**: `syntax error near 'fn' at line 2`. The `||` is tokenized as logical-OR (`PIPEPIPE`).
**Expected**: Either parse `||` in closure-type position as zero-arg-closure (Rust does), or document.
**Suspected root**: Same family as B-ty-07 — lexer greedy-tokenizes operator-pairs that shadow type-position semantics.
**Tags**: `tech-debt:lexer-greedy-collision`, `design:incomplete`

### B-ty-09: Paren'd type `(i32)` rejected at let-position

**Severity**: P2 design (consistency)
**Status**: confirmed (2026-05-04)
**Repro**: `B25/` —
```logos
fn main() -> i32 { let t: (i32) = 5; return t; }
```
**Observed**: `syntax error near 'fn' at line 2`.
**Expected**: `(T)` should be a synonym for `T` (parenthesized type expression). Rust accepts. Worth verifying if this is intentional.
**Suspected root**: `tuple_type` requires the inner to be a comma-list (one elem unambiguously is `(T,)`); a single-elem-no-comma may reach a parse-error path in `type_ref` dispatch.
**Tags**: `design:incomplete`, `tech-debt:grammar-inconsistency`

### B-ty-10: Zero-sized array `[T; 0]` silently compiles

**Severity**: P2 design (intentional?)
**Status**: confirmed (2026-05-04)
**Repro**: `B05/` —
```logos
fn helper(x: [i32; 0]) -> i32 { return 0; }
```
**Observed**: Compiles cleanly.
**Expected**: Either intentional (zero-sized array is a valid type) — should be documented, OR reject as suspicious.
**Suspected root**: `arr_type` accepts INTEGER size without bounds-check. Legitimate ZSTs are useful, but the user-intent check is absent.
**Tags**: `design:incomplete`

## Tag summary

| Tag | Count | Bugs |
|---|---|---|
| `oversight:simple` | 5 | B-ty-01, B-ty-02, B-ty-03, B-ty-05, B-ty-06 |
| `tech-debt:missing-arity-check` | 3 | B-ty-03, B-ty-04, B-ty-05 |
| `tech-debt:missing-position-check` | 1 | B-ty-02 |
| `tech-debt:missing-bound-check` | 1 | B-ty-06 |
| `tech-debt:lexer-greedy-collision` | 2 | B-ty-07, B-ty-08 |
| `tech-debt:diagnostic-from-codegen` | 1 | B-ty-04 |
| `tech-debt:grammar-inconsistency` | 1 | B-ty-09 |
| `design:incomplete` | 4 | B-ty-07, B-ty-08, B-ty-09, B-ty-10 |

**Cluster preview**:
- **missing-arity-check** (3 bugs) — generic-instantiation type-arg count not validated. Single-helper fix at the `args.size() vs type_params.size()` check site catches all three (too-many, too-few, non-generic-takes-args).
- **lexer-greedy-collision** (2 bugs) — `&&` and `||` in type-position get mis-tokenized. Either lexer becomes context-aware (hard) or grammar accepts the alternate spellings (medium) or doc requires spaces (easy).
- **2× P0 SEGFAULTs** (B-ty-01 unit-tuple, B-ty-02 impl-Trait-param) — both crash on type forms that should be either accepted normally (unit) or rejected with a clear message (impl-Trait at param). Common root: `resolve_type` / `lower_fn` lacks `if (kind unsupported here) error(...)` recovery.

## Regression-confirmed (NOT bugs)

- **T01**: deeply nested `*const *const *mut i32` works.
- **T03**: 1-elem tuple `(i32,)` works.
- **T09**: `typeof(var)` works.
- **T10**: `typeof(undefined_var)` correctly rejected.
- **T11**: `&dyn UnknownTrait` correctly rejected with clear message.
- **T12**: closure type mismatch detected.
- **T13**: fn ptr works.
- **T14**: non-const array size correctly rejected.
- **T16**: `typeof(2 + 3)` works.
- **T17**: lifetime in fn sig works.
- **T20**: `impl Trait` in let works (only param-position is broken).
- **T21**: typeof of struct works.
- **T22**: nested generic `Foo<Foo<Foo<i32>>>` works (3 levels of mono).
- **T24**: trait associated type works.
- **T28**: `*const fn(i32) -> i32` works.

## Notes for Phase 3

- The 2× P0 segfaults (B-ty-01 and B-ty-02) bring the running SEGFAULT count to **5** across phases 2.1-2.5: B-it-01 (recursive struct), B-ca-01 (self-ref const), B-ty-01 (unit tuple), B-ty-02 (impl Trait param), plus B-mv-05/06/07/08 LOGOS_ASSERT crashes (4). Phase 3 cluster `compiler-crashes-on-malformed-input` should aggregate all 9.
- The `missing-arity-check` cluster can fold into the broader `tech-debt:missing-validation-at-sema` super-cluster: B-it-03/04/05 (uniqueness), B-fn-02 (param uniqueness), B-ca-04 (const uniqueness), B-ty-03/04/05 (arity), B-ty-06 (bounds). All want sema-time validation that's currently absent.
- Lexer greedy-collisions (B-ty-07/08) suggest a per-context lexer mode might be valuable later. For now, document the requirement to space.
