# Bug catalog: Literals (non-Hermes)

**Group**: 11 — Literals (struct/array/tuple/closure)
**Grammar rules covered**: `enum_lit`, `struct_lit`, `generic_struct_lit`, `struct_update_lit`, `field_init`, `arr_lit`, `arr_fill_lit`, `tuple_lit`, `list_comp`, `map_comp`, `closure_expr`
**Reference doc**: [docs/language/reference/expressions.md](../language/reference/expressions.md) (closures + literals embedded)
**Implementation entry points**:
- [src/compiler/sema_expr.cpp](../../src/compiler/sema_expr.cpp) — `lower_struct_lit`, `lower_arr_lit`, `lower_tuple_lit`, `lower_closure_expr`, `lower_list_comp`
- [src/compiler/mlir_gen.cpp](../../src/compiler/mlir_gen.cpp) — `gen_struct_lit`
- [src/compiler/mlir_gen_dyn.cpp](../../src/compiler/mlir_gen_dyn.cpp) — closure capture lowering

**Hunt date**: 2026-05-04
**Repros**: `/tmp/baghunt/literals/`
**Group note**: Smaller-than-expected bug surface. Most struct-lit / arr-lit type checks are correctly wired. The catalog is **underweight** with 4 confirmed bugs.

## Bugs

### B-li-01: Empty array literal `[]` emits warning 3 times

**Severity**: P1 diagnostic (UX)
**Status**: fixed (Diags::print process-static dedup by level/file/line/message)
**Repro**: `B06/` —
```logos
fn main() -> i32 {
    let a: [i32; 0] = [];
    return 0;
}
```
**Observed**:
```
warning [fn main]: empty array literal: element type unknown
warning [fn main]: empty array literal: element type unknown
warning [fn main]: empty array literal: element type unknown
```
**Expected**: A single warning per empty literal occurrence, OR no warning when the type annotation pins the element type (here `[i32; 0]` is fully constrained).
**Suspected root**: Multi-pass lowering visits the literal multiple times; each pass emits the warning. Should be deduped on source location, OR skipped when type is already inferred from context.
**Tags**: `oversight:simple`, `tech-debt:duplicated-diagnostic`

### B-li-02: Closure without parameter type-annotation fails (no type inference)

**Severity**: P2 design (incomplete feature)
**Status**: deferred — closure type-inference is feature work, not a bug.
**Repro**: `B19/` —
```logos
fn main() -> i32 {
    let f: |i32| -> i32 = |x| -> i64 { return x as i64; };
    //                     ^ no type on x
    return 0;
}
```
**Observed**: `syntax error near 'x' at line 3`.
**Expected**: Closure parameters should infer types from the let-annotation (Rust does). Currently parameters require explicit types.
**Suspected root**: Grammar's closure-arg list requires `IDENT COLON type_ref` (no untyped form). Worth verifying grammar.
**Tags**: `design:incomplete`

### B-li-03: Struct update `..base` with mismatched base type silently accepted

**Severity**: P0 (silent miscompile / type confusion)
**Status**: fixed-in-Sprint3.4 (base-type check in lower_struct_lit; tests/logos/fail/struct_update_mismatched_base)
**Repro**: `B24/` —
```logos
struct Foo { x: i32, y: i32 }
struct Bar { x: i32, y: i32 }
fn main() -> i32 {
    let foo = Foo { x: 1, y: 2 };
    let bar = Bar { x: 5, ..foo };  // ..foo is Foo, but constructor is Bar
    return 0;
}
```
**Observed**: Compiles cleanly. The `..foo` (a Foo value) is spread into a Bar literal — even though Foo and Bar are different types. Likely the resulting `bar` has Foo's bytes mis-typed as Bar.
**Expected**: Sema error: "struct update `..base` of type 'Foo' incompatible with literal type 'Bar'".
**Suspected root**: `lower_struct_lit` for the update form doesn't compare `base.type` against the constructor type. Same family as B-ex-05 (no cast validation).
**Tags**: `oversight:simple`, `tech-debt:no-spread-type-check`

### B-li-04: Struct update `..base` must be last; if not last, cryptic syntax error

**Severity**: P1 diagnostic
**Status**: fixed — chose the "accept" branch of the OR. Grammar got a third `struct_update_lit` alt (`IDENT LBRACE DOTDOT expr COMMA field_init …`) that emits the same STRUCT_LIT shape with BASE + ITEMS, so `..base` may appear before fields. Lock-in: pass test `struct_update_base_first`. Field-order is semantically irrelevant; explicit fields override base regardless.
**Repro**: `B22/` —
```logos
struct Foo { x: i32, y: i32 }
fn main() -> i32 {
    let base = Foo { x: 1, y: 2 };
    let f = Foo { ..base, z: 5 };  // ..base before fields
    return 0;
}
```
**Observed**: `syntax error near 'base' at line 5`.
**Expected**: Either accept (allow `..base` anywhere — design choice) or reject with helpful diagnostic: "`..base` must come last in struct update literal".
**Suspected root**: Grammar's `struct_update_lit` likely requires `field_init* DOTDOT expr` order. The actual error message is the generic "syntax error" without context.
**Tags**: `tech-debt:misleading-diagnostic`

## Cross-group inconsistency observations (NOT new bugs, but worth noting)

### Inconsistency: struct lit dup-field detected (this group), struct def dup-field silent (Items group)

- L03 (this group): `let f = Foo { x: 1, y: 2, x: 3 };` → "struct literal 'Foo': duplicate field 'x'" ✓
- B-it-03 (items): `struct Foo { pub x: i32, pub x: i64, }` → silent ✗

The dup-detection pattern exists in struct-lit lowering but is missing in struct-def lowering. Confirms `missing-uniqueness-check` as a CLUSTER-level fix that needs a single helper applied consistently.

## Tag summary

| Tag | Count | Bugs |
|---|---|---|
| `oversight:simple` | 2 | B-li-01, B-li-03 |
| `tech-debt:duplicated-diagnostic` | 1 | B-li-01 |
| `tech-debt:no-spread-type-check` | 1 | B-li-03 |
| `tech-debt:misleading-diagnostic` | 1 | B-li-04 |
| `design:incomplete` | 1 | B-li-02 |

## Regression-confirmed (NOT bugs)

- **L01**: extra field detected.
- **L02**: missing field detected.
- **L03**: duplicate field in lit detected (contrast: struct DEF doesn't!).
- **L04**: struct update `Foo { x: 10, ..base }` works.
- **L05**: array lit mixed types detected.
- **L08**: empty tuple `()` works.
- **L09**: closure with no body rejected.
- **L10**: list-comp on non-iter detected.
- **L11**: 1-elem array literal works.
- **L12**: 1-elem tuple lit works.
- **L13**: closure capture works.
- **L15**: enum variant payload arity detected.
- **L16**: struct field type mismatch detected.
- **L18**: struct lit shorthand (`Foo { x, y }` for `Foo { x: x, y: y }`) works.
- **L20**: closure captures mut binding (compiles; borrow-check at use site).
- **L21**: arr fill of struct works.
- **L23**: arr fill with negative size rejected.

## Notes for Phase 3

- **B-li-03** (struct update type mismatch) joins the `no-validation` cluster — a tight, single-site fix at struct-update-lit lowering.
- **B-li-01** (3× empty arr warnings) suggests multi-pass lowering visits literals multiple times. Worth investigating if other diagnostics are also duplicated.
- The contrast L03 (works) vs B-it-03 (broken) is a clean illustration of the cumulative `missing-uniqueness-check` cluster pattern: same kind of validation needed in every parallel-name-list site, but only some sites have it.
