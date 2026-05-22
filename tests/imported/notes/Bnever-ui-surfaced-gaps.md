# Bnever — never-type `!` UI run-pass import: surfaced gaps

Batch: **Bnever** — rustc UI never-type run-pass tests.
Upstream commit: `4b0c9d76ae7d387229caea55cfa73c280b08b8a7`.
Source dirs mined: `tests/ui/never_type/{,basic,regress}`, `tests/ui/reachable/`,
`tests/ui/loops/`. Date: 2026-05-22.

12 tests landed under `tests/imported/pass/never/*-bnever.logos`, all compile +
link + exit 0. Per the batch scope, ONLY language-feature divergence/coercion
tests were imported; `library/core` never tests were intentionally skipped.

## Tests landed (12)

| file | feature exercised | distilled from |
|---|---|---|
| return-coerce-bnever | `return e` in let-init coerces to expected type | basic/return-never-coerce.rs (was run-fail panic) |
| never-type-decl-bnever | `-> !` fn + `loop{}` body; `!`-call coerces to i32 | basic/call-fn-never-arg.rs |
| never-coercions-match-bnever | diverging `return` match-arm → LUB of other arms | basic/never_coercions.rs |
| nested-match-return-bnever | `return` in nested match-arm value position | never_coercions.rs (extended) |
| diverging-call-arg-bnever | `return` in fn-argument position | reachable/diverging-expressions-unreachable-code.rs |
| panic-value-position-bnever | `panic("..")` (`-> !`) in value position via if-else | basic/adjust_never.rs |
| break-value-loop-bnever | `break e` from `loop` yields a value | loops/loop-break-value.rs (run-pass core) |
| labeled-break-value-bnever | `break 'outer e` carries value out of nested loop | loops/loop-break-value.rs |
| plain-break-bnever | value-less `break` exits a `loop` | loops (break family) |
| continue-in-expr-bnever | `continue` in expression/let-init position | never-type divergence family |
| if-else-diverge-chain-bnever | if/else-if/else where `else` diverges & adopts type | coercion family |
| artificial-block-return-bnever | code after `return` (unreachable) typechecks+codegens | reachable/artificial-block.rs |

## NEW gaps surfaced (all impact the never-type feature — FIX candidates)

> **STATUS 2026-05-22:** Gnever-1 ✅ FIXED, Gnever-3 ✅ FIXED, Gnever-2 ✅ now a
> clean error; bare-block-tail observation partially addressed. All four never
> gaps resolved (Gnever-2 = clean diagnostic for the degenerate `!`-param case).

### Gnever-1 — `!` as a generic type ARGUMENT to a stdlib enum — ✅ FIXED 2026-05-22
**FIXED:** two mlir-gen changes give `!` a zero-size uninhabited representation:
(1) a `!`-typed struct FIELD lowers to `array<0 x i8>` (was nullptr → "unknown
field type") + logos_abi_byte_size(Never)=0; (2) a function whose RETURN type is
`!` (a monomorphized `Option<!>::unwrap` etc. returning the `!` payload) emits an
OPERAND-LESS return — its MLIR signature is void (logos_to_mlir(Never)=nullptr),
so `return <payload>` would mismatch the 0-result signature. `Result<u32,!>`,
`Option<!>`, and the infallible-Result idiom (construct Ok, match, unwrap path)
now compile + run. Regression: never_type_generic_arg. `logos_to_mlir(Never)`
stays nullptr in value/result contexts (if/match diverging-branch paths rely on
it) — the zero-size repr is applied only at field/payload + return-signature
sites. (original diagnosis below)
- Repro: `fn f(r: Result<u32, !>) -> u32 { return 0u32; }` (just the type in a
  signature is enough — no use of the value needed).
- Error: `mlir_gen: unknown field type in 'StepByIter$G2$OptionIter$G1$!$!'` →
  `logosc: MLIR generation failed`.
- Diagnosis: when `!` appears as a type-arg to `Result`/`Option`, the stdlib's
  associated iterator machinery (`OptionIter<!>`, `StepByIter<...,!>`) gets
  monomorphized with `!` in field-type position, and mlir-gen has no lowering
  for a `!`-typed field. `!` should either be lowered as a zero-size /
  uninhabited field type, or the uninhabited-payload variant's machinery should
  be elided. Tractable — localized to mlir-gen field-type handling for the
  never type (and/or skipping iter-precompute for uninhabited specializations).
- Test DROPPED because of this: `basic/never-result.rs` (`Result<u32, !>` match).
  This is the Rust idiom for "infallible Result"; worth fixing.

### Gnever-2 — `!` as a function PARAMETER type segfaults the compiler — ✅ HARDENED (clean error)
**FIXED 2026-05-22:** a `!`-typed parameter is now rejected in sema with a clear
diagnostic ("the never type `!` is uninhabited and cannot be a parameter type")
instead of SIGSEGV-ing codegen. Full support (zero-size uninhabited param slot,
so `fn foo(x: !) -> ! { x }` compiles) is deferred with Gnever-1 — both need the
never type to have a real (zero-size) value representation. Fail-test:
never_param_rejected.

### (original Gnever-2 diagnosis below)
- Repro: `fn f(x: !) -> i32 { return x; }` → compiler exits rc=139 (SIGSEGV)
  during emit.
- Diagnosis: a `!`-typed parameter slot has no representation; the prologue /
  ABI lowering for a never-typed param crashes. Upstream `call-fn-never-arg.rs`
  is exactly `fn foo(x: !) -> ! { x }`. We distilled the landed test to NOT
  declare a `!` parameter (only `-> !` return + diverging body), so the feature
  is partially covered. Tractable — give `!` a zero-size param representation
  (the body is necessarily unreachable, so it never actually reads the slot).

### Gnever-3 — diverging RHS in `||` / `&&` rejected — ✅ FIXED 2026-05-22
The binop bool-operand check now accepts a Never operand; the `&&`/`||`
short-circuit codegen guards the RHS block with is_terminated (a diverging RHS
emits its terminator, no store/merge appended). Regression:
never_type_bool_operand.

### (original Gnever-3 diagnosis below)
- Repro: `let x: bool = c || (return false);`
- Error: `operator '||': right must be bool, got !`.
- Diagnosis: the boolean short-circuit operators typecheck operands as exactly
  `bool` and don't allow a `!` (diverging) operand to coerce. Rust accepts this
  (`x = false || (return);`). Tractable — the binop bool-operand check should
  accept a never-typed operand (coerce `!` → bool).
- Test DROPPED because of this: `reachable/expr_oror.rs`. The divergence in
  argument position (diverging-call-arg-bnever) and match/if positions are all
  covered, so the feature is otherwise well-exercised.

## Other observation (not blocking, minor)

- A bare block used as a fn-body TAIL that diverges is NOT recognized as
  diverging: `fn f() -> i64 { { return 3i64; } }` errors "not all paths return
  a value". A direct `return 3i64;` (with or without trailing unreachable code)
  works fine. The `artificial-block-return-bnever` test was adapted to the
  direct-`return` + trailing-unreachable form (which preserves the original
  test's actual point: unreachable code after a divergence typechecks +
  codegens). Likely the block-tail diverge-propagation in the return-path
  analysis doesn't look through a trailing-expr block. Low priority.

## Dropped tests summary

- `basic/never-result.rs` — Gnever-1 (`Result<u32,!>` mlir-gen crash).
- `basic/call-fn-never-arg.rs` (the `fn foo(x: !)` param form) — Gnever-2;
  landed a distilled variant without a `!` param.
- `reachable/expr_oror.rs` — Gnever-3 (`!` operand to `||`).
- Compile-fail tests (those with `.stderr` / `//~ ERROR`) — out of scope per
  batch rules (they assert errors, not divergence-runs): e.g.
  `loop-break-value.rs` is itself compile-fail; only its well-typed `break v`
  core was extracted.
- `library/core` never tests — intentionally skipped per batch scope.
