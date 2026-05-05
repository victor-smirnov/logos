# Bug catalog: Expressions & Precedence

**Group**: 10 — Expressions & Precedence
**Grammar rules covered**: `expr`, `log_expr`, `cmp_expr`, `bitwise_expr`, `add_expr`, `mul_expr`, `cast_expr`, `unary_expr`, `atom`, `primary_expr`, `paren_expr`, `call_expr`, `call_arg_list`
**Reference doc**: [docs/language/reference/expressions.md](../language/reference/expressions.md)
**Implementation entry points**:
- [src/compiler/sema_expr.cpp](../../src/compiler/sema_expr.cpp) (~9000 lines) — every expression kind
- [src/compiler/mlir_gen_expr.cpp](../../src/compiler/mlir_gen_expr.cpp) — emit-side

**Hunt date**: 2026-05-04
**Repros**: `/tmp/baghunt/expressions/`

## Bugs

### B-ex-01: Compile-time integer overflow in literal arithmetic silently wraps

**Severity**: P0 (silent miscompile)
**Status**: deferred — awaits const-fold validation pass
**Repro**: `B01/` —
```logos
fn main() -> i32 {
    let x: i32 = 2147483647;  // INT_MAX
    let y: i32 = x + 1;       // overflow
    return y;
}
```
**Observed**: Compiles, runs to exit 0 (presumably wraps to INT_MIN, then returned-as-int → 0 mod 256 for shell exit).
**Expected**: Either reject at sema with "literal overflow", or emit a runtime overflow check (Rust does in debug builds), or document wrapping policy explicitly.
**Suspected root**: Constant-fold engine doesn't bounds-check arithmetic. See [feat_const_fold_metacall](../../../.claude/projects/-home-victor-devel-logos/memory/feat_const_fold_metacall.md) — folder is in flux; bounds-check absent.
**Tags**: `oversight:simple`, `tech-debt:no-overflow-check`

### B-ex-02: Division by zero literal silently accepted

**Severity**: P0 (silent runtime crash)
**Status**: deferred — awaits const-fold validation pass
**Repro**: `B02/` —
```logos
fn main() -> i32 { return 10 / 0; }
```
**Observed**: Compiles cleanly. Runtime would SIGFPE.
**Expected**: Sema error: "division by zero literal".
**Suspected root**: Constant-fold doesn't guard against zero divisor. Same family as B-ex-01.
**Tags**: `oversight:simple`, `tech-debt:no-const-fold-validation`

### B-ex-03: Shift by negative count silently accepted

**Severity**: P0 (UB in LLVM)
**Status**: deferred — awaits const-fold validation pass
**Repro**: `B05/` —
```logos
fn main() -> i32 { return 1 << -1; }
```
**Observed**: Compiles cleanly. LLVM treats negative shift as UB.
**Expected**: Sema error: "shift count must be non-negative".
**Suspected root**: Same family as B-ex-01/02 — const-fold doesn't validate operand ranges.
**Tags**: `oversight:simple`, `tech-debt:no-const-fold-validation`

### B-ex-04: Unary minus on unsigned type silently accepted

**Severity**: P0 (silent miscompile)
**Status**: deferred — awaits const-fold validation pass
**Repro**: `B08/` —
```logos
fn main() -> i32 {
    let x: u32 = 5;
    let y: u32 = -x;  // negation on unsigned
    return y as i32;
}
```
**Observed**: Compiles cleanly. The `-x` likely produces 0 - 5 = wrapping to UINT_MAX - 4.
**Expected**: Sema error: "unary `-` not defined for unsigned type 'u32'". Rust rejects.
**Suspected root**: Unary-op type-check doesn't verify signedness for `-`.
**Tags**: `oversight:simple`, `tech-debt:no-signedness-check-for-unary`

### B-ex-05: Cast `int as struct` silently accepted

**Severity**: P0 (silent miscompile / UB)
**Status**: fixed-in-Sprint3.4 (scalar→aggregate cast rejected; tests/logos/fail/cast_int_to_struct)
**Repro**: `B13/` —
```logos
struct Foo { x: i32 }
fn main() -> i32 {
    let i: i32 = 5;
    let f = i as Foo;  // can't cast scalar to struct
    return 0;
}
```
**Observed**: Compiles cleanly.
**Expected**: Sema error: "non-primitive cast: i32 as Foo".
**Suspected root**: Cast-expr type-check accepts any type as target, doesn't verify cast is well-defined (allowed pairs: prim→prim, ptr→ptr, ptr→int, etc.).
**Tags**: `oversight:simple`, `tech-debt:no-cast-validation`

### B-ex-06: Address-of integer literal silently accepted (lifetime issue)

**Severity**: P1 (potentially dangling reference)
**Status**: deferred — lifetime-extension policy undecided
**Repro**: `B15/` —
```logos
fn main() -> i32 {
    let p = &5;  // &<temporary>
    return 0;
}
```
**Observed**: Compiles cleanly. The `&5` borrows a temporary that may not outlive the expression.
**Expected**: Either reject (Rust requires named binding before `&`) or document lifetime-extension policy. Currently undefined.
**Suspected root**: Address-of operator doesn't require its operand be a place expression.
**Tags**: `design:incomplete`, `tech-debt:address-of-temporary`

### B-ex-07: Integer literal overflow silently saturated/parsed (known)

**Severity**: P0 (silent miscompile)
**Status**: fixed-in-Sprint2.3 (parse_int_literal_overflows + suffix-bound check; tests/logos/fail/literal_overflow_u64 + literal_suffix_overflow_u8)
**Repro**: `B17/` —
```logos
fn main() -> i32 {
    let x: u64 = 99999999999999999999u64;  // > u64 max
    return 0;
}
```
**Observed**: Compiles cleanly. Literal silently saturates or wraps.
**Expected**: Reject: "integer literal '...' is out of range for u64".
**Suspected root**: `parse_int_literal` saturates on overflow per the memory file. Known bug.
**Tags**: `oversight:simple`, `tech-debt:literal-saturation-no-error`

### B-ex-08: Comparison chain `a < b < c` produces cryptic syntax error

**Severity**: P1 diagnostic
**Status**: fixed — added CHAINED_CMP AST node (code 221); grammar's `cmp_expr` now matches the chained form first and produces CHAINED_CMP, falling back to the existing 0-or-1 single-comparison form. Sema rejects with a helpful "split into `a < b && b < c`" diagnostic. Lock-in: fail test `chained_cmp`.
**Repro**: `B06/` —
```logos
fn main() -> i32 {
    let x: i32 = 3;
    if 1 < x < 5 { return 1; }
    return 0;
}
```
**Observed**: `syntax error near 'x' at line 4`.
**Expected**: Either accept (Python-style chained comparison) or reject with helpful message: "chained comparisons not supported; use `1 < x && x < 5`". Most modern languages don't support chaining; Logos correctly rejects but the diagnostic is unhelpful.
**Suspected root**: Grammar's `cmp_expr` is non-associative on a single level, but the failure mode (raw syntax error) doesn't lead the user to the fix.
**Tags**: `tech-debt:misleading-diagnostic`

### B-ex-09: Bitwise-vs-comparison precedence is C-style, not Rust-style

**Severity**: P2 design (footgun)
**Status**: not-a-bug — Logos uses C-style operator precedence (bitwise tighter than `==`) by design. The grammar's `cmp_expr → bitwise_expr → add_expr` chain is intentional. Documenting in the reference doc would help, but the current behavior is correct and stable; changing it would silently break every program that uses `&` / `|` / `^` near comparisons.
**Repro**: `B19/` —
```logos
fn main() -> i32 {
    let x: i32 = 5;
    if x & 1 == 1 { return 1; }   // parses as (x & 1) == 1
    return 0;
}
```
**Observed**: `(x & 1) == 1` (bitwise binds TIGHTER than `==`). Returns 1.
**Expected**: Documented either way. Rust parses as `x & (1 == 1)` (comparison binds tighter, so bitwise applies to the bool result). Logos's precedence chain `cmp_expr → bitwise_expr → add_expr → ...` puts bitwise *inside* cmp, giving bitwise higher precedence (C-style).
**Suspected root**: Grammar precedence ordering — see [logos.peg](../../tools/peg_gen/grammars/logos.peg). Design choice.
**Tags**: `design:incomplete`, `tech-debt:precedence-c-style`

### B-ex-10: Cast expression chain `(a as b as c)` not surveyed (deferred)

**Severity**: deferred
**Status**: not-tested-this-pass
**Note**: `cast_expr` parses as `unary_expr (KW_AS type_ref)*` — chained casts should work. Worth a positive-test pass.
**Tags**: deferred

## Tag summary

| Tag | Count | Bugs |
|---|---|---|
| `oversight:simple` | 6 | B-ex-01, B-ex-02, B-ex-03, B-ex-04, B-ex-05, B-ex-07 |
| `tech-debt:no-const-fold-validation` | 2 | B-ex-02, B-ex-03 |
| `tech-debt:no-overflow-check` | 1 | B-ex-01 |
| `tech-debt:no-signedness-check-for-unary` | 1 | B-ex-04 |
| `tech-debt:no-cast-validation` | 1 | B-ex-05 |
| `tech-debt:address-of-temporary` | 1 | B-ex-06 |
| `tech-debt:literal-saturation-no-error` | 1 | B-ex-07 |
| `tech-debt:misleading-diagnostic` | 1 | B-ex-08 |
| `tech-debt:precedence-c-style` | 1 | B-ex-09 |
| `design:incomplete` | 2 | B-ex-06, B-ex-09 |

**Cluster preview**:
- **Const-fold validation** super-cluster (4 bugs: B-ex-01/02/03/07) — single architectural fix in const-fold engine to validate ranges + zero divisors + signedness.
- **Cast/coerce validation** (B-ex-04/05) — cast-expr should pre-check pair compatibility.

## Regression-confirmed (NOT bugs)

- **E03**: precedence `+` vs `*` correct (2 + 3*4 = 14).
- **E04**: cast f64→i32 works (truncation).
- **E07**: short-circuit `&&` works (no div-by-zero on x=0).
- **E09**: `!x` on int does bitwise NOT.
- **E10**: parens work.
- **E11**: `?` on non-Result correctly diagnosed.
- **E12**: chained method calls work.
- **E14**: deref non-ptr correctly diagnosed.
- **E16**: assignment in if-cond rejected.
- **E18**: method on tuple correctly diagnosed.
- **E20**: i64 min literal works.

## Notes for Phase 3

- The **const-fold-validation** cluster is fixable as one slice once the const-fold engine returns ([feat_const_fold_metacall](../../../.knowledge/projects/-home-victor-devel-logos/memory/feat_const_fold_metacall.md)). Should be a Phase 4 priority — these are silent miscompiles in everyday code.
- **Cast validation** (B-ex-05) is a small standalone fix at the cast-expr type-check site. High value: prevents UB.
- B-ex-09 (precedence) is a language-design decision. Rust users will be confused. Document explicitly + lint when ambiguous.
