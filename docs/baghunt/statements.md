# Bug catalog: Statements & Assignments

**Group**: 8 — Statements & Assignments
**Grammar rules covered**: `block`, `unsafe_block`, `stmt`, `let_stmt`, `let_else_stmt`, `assign_stmt`, `compound_assign_op`, `compound_assign_stmt`, `tuple_field_write_stmt`, `field_write_stmt`, `chain_path_id`, `chain_field_path`, `chain_field_write_stmt`, `field_compound_assign_stmt`, `chain_field_compound_assign_stmt`, `index_compound_assign_stmt`, `deref_field_write_stmt`, `deref_field_compound_assign_stmt`, `tuple_field_compound_assign_stmt`, `field_index_write_stmt`, `field_index_compound_assign_stmt`, `deref_write_stmt`, `index_write_stmt`, `for_stmt`, `labeled_loop_stmt`, `continue_stmt`, `match_stmt`, `match_arm`, `while_stmt`, `return_stmt`, `if_expr`
**Reference doc**: [docs/language/reference/statements.md](../language/reference/statements.md)
**Implementation entry points**:
- [src/compiler/sema_stmt.cpp](../../src/compiler/sema_stmt.cpp) — `lower_let`, `lower_assign`, `lower_compound_assign`, etc.
- [src/compiler/mlir_gen_stmt.cpp](../../src/compiler/mlir_gen_stmt.cpp) — emit-side per assignment form

**Hunt date**: 2026-05-04
**Repros**: `/tmp/baghunt/statements/`

## Bugs

### B-st-01: Block-scoped shadow LEAKS out of inner block

**Severity**: P0 hard (silent miscompile)
**Status**: fixed-in-Sprint3 (mlir_gen SBlock snapshot/restore of var maps; tests/logos/pass/block_shadow_does_not_leak)
**Repro**: `B17/` —
```logos
fn main() -> i32 {
    let x: i32 = 5;
    {
        let x: i32 = 10;  // shadow inside inner block
        let _ = x;
    }
    return x;  // expected: 5 (outer x); actual: 10 (inner shadow leaked)
}
```
**Observed**: Exit code 10. The IR shows the final `return` loads from the inner shadow's alloca, not the outer `x`.
**Expected**: Exit code 5. Inner-block `let x = 10` should be scoped to the inner block; after the block ends, `x` should refer to the outer binding.
**Suspected root**: [src/compiler/sema_stmt.cpp](../../src/compiler/sema_stmt.cpp) variable scope-stack push/pop is incorrect for nested blocks. Variables introduced inside `{...}` are not popped when the block ends.
**Tags**: `oversight:simple`, `tech-debt:scope-stack-incorrect`

### B-st-02: `let pat = expr;` with refutable pattern produces cryptic syntax error

**Severity**: P1 diagnostic
**Status**: fixed-in-Sprint4.2 (LET_PAT alt parses; lower_let_pat rejects refutable shapes with clear "use 'match' or 'let-else'" diagnostic)
**Repro**: `B01/` —
```logos
enum Opt { Some(i32), None, }
fn main() -> i32 {
    let Opt::Some(x) = Opt::Some(5);  // refutable, no else
    return x;
}
```
**Observed**: `error: syntax error near ')' at line 4`.
**Expected**: Sema diagnostic: "refutable pattern requires `let ... else { diverging }`; use `if let` for conditional bind".
**Suspected root**: Parser doesn't accept `let pat = expr;` with non-bare-ident pattern. The grammar may have separate productions for `let_stmt` (irrefutable) vs `let_else_stmt`, and the user's form falls between them.
**Tags**: `tech-debt:parser-no-recovery`, `tech-debt:misleading-diagnostic`

### B-st-03: `let-else` with non-diverging body silently accepted

**Severity**: P0 (silent miscompile)
**Status**: fixed-in-Sprint3 (block_always_diverts check in lower_let_else; tests/logos/fail/let_else_non_diverging)
**Repro**: `B02/` —
```logos
enum Opt { Some(i32), None, }
fn main() -> i32 {
    let Opt::Some(x) = Opt::Some(5) else { let _ = 0; };
    return x;
}
```
**Observed**: Compiles cleanly. The `else` block doesn't diverge (no `return`/`panic`/`break`), but sema accepts it.
**Expected**: Per [docs/language/reference/statements.md](../language/reference/statements.md): "The else block must diverge". Should error.
**Suspected root**: `let_else_stmt` lowering checks for the else block but doesn't verify it diverges. Same family as `oversight:simple` checks missing throughout sema.
**Tags**: `oversight:simple`, `tech-debt:divergence-not-checked`

### B-st-04: `*p += v` syntax error (compound assign on bare deref doesn't parse)

**Severity**: P1 diagnostic / P2 incomplete
**Status**: confirmed (2026-05-04)
**Repro**: `B06/` —
```logos
fn main() -> i32 {
    let mut x: i32 = 5;
    let p: *mut i32 = &mut x as *mut i32;
    unsafe { *p += 10; }
    return x;
}
```
**Observed**: `syntax error near 'p' at line 5`.
**Expected**: Per [docs/language/reference/statements.md](../language/reference/statements.md), `(*p).field += v` works (B-st verified S16). The bare-deref form `*p += v` should also parse — it's a natural extension.
**Suspected root**: Grammar has `deref_field_compound_assign_stmt` (covers `(*p).f += v`) but no `deref_compound_assign_stmt` (for `*p += v`). Compound matrix has gaps. See assignment matrix in [statements.md](../language/reference/statements.md#lhs-shapes-grammar-productions).
**Tags**: `oversight:simple`, `tech-debt:assignment-matrix-incomplete`

### B-st-05: `break 'unknown_label` leaks to MLIR-gen

**Severity**: P1 diagnostic
**Status**: fixed-in-M0.5 (regression: tests/logos/fail/break_unknown_label + continue_unknown_label)
**Repro**: `B12/` —
```logos
fn main() -> i32 {
    loop { break 'nonexistent; }
}
```
**Observed**: `error: 'func.return' op has 0 operands ...; mlir_gen: module verification failed`. Sema lets it through.
**Expected**: Sema error: "label ''nonexistent' not found".
**Suspected root**: Label resolution at sema doesn't validate that the label was declared by an enclosing labeled-loop.
**Tags**: `oversight:simple`, `tech-debt:diagnostic-from-codegen`

### B-st-06: `continue 'unknown_label` silently accepted

**Severity**: P0 (silent miscompile potential)
**Status**: fixed-in-M0.5 (active_loop_labels_ stack; tests/logos/fail/continue_unknown_label)
**Repro**: `B13/` —
```logos
fn main() -> i32 {
    loop { continue 'nonexistent; }
}
```
**Observed**: Compiles cleanly. Whatever `continue` jumps to is implementation-defined.
**Expected**: Sema error: "label ''nonexistent' not found".
**Suspected root**: Same as B-st-05 — label resolution missing — but `continue` doesn't even surface a downstream error.
**Tags**: `oversight:simple`, `tech-debt:label-not-validated`

### B-st-07: Unreachable wildcard arm not detected

**Severity**: P2 design (lint-quality)
**Status**: confirmed (2026-05-04)
**Repro**: `B20/` —
```logos
enum E { A, B, C, }
fn main() -> i32 {
    let e: E = E::A;
    match e {
        E::A => { return 1; }
        E::B => { return 2; }
        E::C => { return 3; }
        _    => { return 99; }  // unreachable — all variants covered
    }
}
```
**Observed**: Compiles cleanly.
**Expected**: Warning: "match arm `_` is unreachable; preceding arms cover all cases".
**Suspected root**: No reachability analysis on match arms.
**Tags**: `design:incomplete`, `tech-debt:no-reachability-lint`

### B-st-08: Dead code after `return` not warned

**Severity**: P2 design (lint-quality)
**Status**: fixed-in-Sprint5.2 (warning in lower_block when stmt follows Return/Break/Continue)
**Repro**: `B25/` —
```logos
fn helper() -> i32 {
    return 5;
    let _ = 99;  // dead code
    return 7;
}
```
**Observed**: Compiles cleanly.
**Expected**: Warning: "unreachable code after `return`".
**Suspected root**: No control-flow-graph reachability analysis at function scope. Same family as B-st-07 (unreachable arm).
**Tags**: `design:incomplete`, `tech-debt:no-reachability-lint`

### B-st-09: While condition type-check fires correctly (regression confirmation, NOT a bug)

Moved to Regression list below.

### B-st-10: For-iteration over non-iterable detects but lacks "what's iterable" hint

**Severity**: P2 (diagnostic quality)
**Status**: confirmed (2026-05-04, low priority)
**Repro**: `B11/` —
```logos
fn main() -> i32 { for x in 5 { let _ = x; } return 0; }
```
**Observed**: `for-in: '{integer}' is not iterable (not a struct)`.
**Expected**: Better diagnostic suggesting "use a `Range` or `Vec`/`Array`/...".
**Suspected root**: Generic "not iterable" diagnostic without follow-up help.
**Tags**: `design:incomplete`, `tech-debt:diagnostic-help-missing`

## Tag summary

| Tag | Count | Bugs |
|---|---|---|
| `oversight:simple` | 5 | B-st-01, B-st-03, B-st-04, B-st-05, B-st-06 |
| `tech-debt:scope-stack-incorrect` | 1 | B-st-01 |
| `tech-debt:divergence-not-checked` | 1 | B-st-03 |
| `tech-debt:assignment-matrix-incomplete` | 1 | B-st-04 |
| `tech-debt:label-not-validated` | 1 | B-st-06 |
| `tech-debt:diagnostic-from-codegen` | 1 | B-st-05 |
| `tech-debt:misleading-diagnostic` | 1 | B-st-02 |
| `tech-debt:parser-no-recovery` | 1 | B-st-02 |
| `tech-debt:no-reachability-lint` | 2 | B-st-07, B-st-08 |
| `tech-debt:diagnostic-help-missing` | 1 | B-st-10 |
| `design:incomplete` | 3 | B-st-07, B-st-08, B-st-10 |

**Cluster preview**:
- **B-st-01 (scope-stack-incorrect)** is potentially the most-impactful bug found so far — silent miscompile in everyday code. Tracking this carefully; needs a single fix in sema's block-scoping push/pop.
- **B-st-04 (assignment-matrix-incomplete)** — the matrix in [docs/language/reference/statements.md](../language/reference/statements.md#lhs-shapes-grammar-productions) lists 15 assignment forms; need to verify which are wired and fill gaps. `*p += v` is one identified gap.
- **no-reachability-lint** (2 bugs) — pattern: missing CFG-walk-based reachability. This is a Phase 4 architectural addition: a small reachability pass over LIR.

## Regression-confirmed (NOT bugs)

- **S03**: assignment to non-mut detected.
- **S04**: 5-level chain assignment works (`a.b.c.d.e.v = 42` → exit 42).
- **S05**: `bool += int` correctly diagnosed.
- **S07**: `t.0 += v` works.
- **S08**: `arr[i] *= v` works.
- **S09**: chain compound `a.b.c.v += v` works.
- **S10**: while-non-bool detected.
- **S14**: break outside loop detected.
- **S15**: return-val-from-unit detected.
- **S16**: `(*p).v += v` works (deref-field compound).
- **S18**: assignment-as-expression `(x = 5)` rejected.
- **S19**: non-exhaustive match detected with clear list of missing variants.
- **S21**: same-block shadow `let x: i32 = x + 10;` works (15).
- **S23**: if-let works.
- **S24**: basic while works.
- **S26**: empty blocks `{}` accepted.

## Notes for Phase 3

- **B-st-01 (block-scope shadow leak)** is a P0 bug that compiles + runs to wrong values silently. This is exactly the kind of bug a property-based fuzzer would catch. The fix is simple but the regression-test coverage should be comprehensive (every pattern of `let x; { let x; ...; } use(x);`).
- The **assignment matrix** ([statements.md](../language/reference/statements.md#lhs-shapes-grammar-productions)) needs an exhaustive check pass. B-st-04 is one gap; there are likely 5-10 more LHS-shape × compound-op holes. Worth a dedicated "assignment-matrix sweep" mini-task before Phase 4.
- **Reachability lint cluster** (B-st-07/08) is a small architectural addition: walk LIR statement by statement, track unreachable status. Maps cleanly to Phase 5 AST-analysis tier 1.
