# Bug catalog: Functions & Methods

**Group**: 3 — Functions & Methods
**Grammar rules covered**: `pub_fn_def`, `fn_def`, `extern_fn_def`, `pub_static_fn_def`, `static_fn_def`, `param_list`, `param`, `method_def`
**Reference doc**: [docs/language/reference/items.md](../language/reference/items.md), [docs/language/reference/statements.md](../language/reference/statements.md)
**Implementation entry points**:
- [src/compiler/sema_decl.cpp](../../src/compiler/sema_decl.cpp) — `lower_fn`, `lower_static_fn`
- [src/compiler/mlir_gen_fn.cpp](../../src/compiler/mlir_gen_fn.cpp) — `make_fn_type`, `forward_declare`, `gen_function_body`
- [src/compiler/mono_clone.cpp](../../src/compiler/mono_clone.cpp) — `clone_fn`

**Hunt date**: 2026-05-04
**Repros**: `/tmp/baghunt/functions/`

## Bugs

### B-fn-01: `fn helper() { return; }` (no `-> T`) parses despite grammar requiring ARROW

**Severity**: P2 design (grammar/parser drift)
**Status**: not-a-bug (verified 2026-05-04) — fn_def has explicit no-ARROW alts at logos.peg:889/892 producing implicit `-> ()`. Catalog claim about "grammar requiring ARROW" was wrong.
**Repro**: `B01/` —
```logos
fn helper() { return; }
fn main() -> i32 { helper(); return 0; }
```
**Observed**: Compiles cleanly; runs to exit 0.
**Expected**: Per grammar [logos.peg:867](../../tools/peg_gen/grammars/logos.peg#L867): `fn_def <- KW_UNSAFE KW_FN HASH LPAREN expr RPAREN type_param_list? LPAREN param_list? RPAREN ARROW type_ref block` — `ARROW type_ref` is required. Either grammar should have an alt without arrow (and unit-return), or parser should reject the no-arrow form.
**Suspected root**: Likely an alternate `fn_def` production that allows omitting `-> ()` exists somewhere not visible in my line-867 read; or the parser is lenient. Worth `grep -n "fn_def"` in logos.peg.
**Tags**: `tech-debt:grammar-doc-drift`

### B-fn-02: Duplicate parameter names silently accepted

**Severity**: P0 (silent miscompile potential)
**Status**: fixed-in-M0.1 (check_unique_names; tests/logos/fail/dup_fn_param)
**Repro**: `B03/` —
```logos
fn helper(x: i32, x: i64) -> i32 { return 0; }
```
**Observed**: Compiles cleanly. Probably the second `x` shadows the first; first is dead.
**Expected**: Reject with "duplicate parameter name 'x'".
**Suspected root**: `lower_fn` param iteration doesn't dedup-check names. Same family as B-it-03/04/05 (missing-uniqueness-check cluster).
**Tags**: `oversight:simple`, `tech-debt:missing-uniqueness-check`

### B-fn-03: Trailing comma in param list rejected

**Severity**: P2 design (ergonomics)
**Status**: fixed-in-Sprint6.1 (param_list `(COMMA !DOTDOTDOT)?`; tests/logos/pass/trailing_comma_lists)
**Repro**: `B05/` —
```logos
fn helper(a: i32, b: i32,) -> i32 { return a + b; }
```
**Observed**: `error: syntax error near 'fn' at line 2`.
**Expected**: Trailing commas in lists are conventional in modern languages (Rust accepts) — improves diffs and refactoring. Should accept.
**Suspected root**: `param_list` grammar production likely uses `param (COMMA param)*` rather than `param (COMMA param)* COMMA?`.
**Tags**: `oversight:simple`, `design:incomplete`

### B-fn-04: Trailing comma in CALL-site arg list rejected

**Severity**: P2 design (ergonomics)
**Status**: fixed-in-Sprint6.1 (call_arg_list + bare-LPAREN call patterns)
**Repro**: `B18/` —
```logos
fn add(a: i32, b: i32) -> i32 { return a + b; }
fn main() -> i32 { return add(1, 2,); }
```
**Observed**: `syntax error near 'fn' at line 3`.
**Expected**: Same as B-fn-03 — accept for ergonomics consistency.
**Suspected root**: `call_arg_list` grammar lacks `COMMA?` at end.
**Tags**: `oversight:simple`, `design:incomplete`

### B-fn-05: Multi-line fn signatures rejected

**Severity**: P2 design (ergonomics)
**Status**: confirmed-known (already in [feedback_logos_fn_sig_oneline](../../../.claude/projects/-home-victor-devel-logos/memory/feedback_logos_fn_sig_oneline.md))
**Repro**: `B06/` —
```logos
fn helper(
    a: i32,
    b: i32,
) -> i32 { return a + b; }
```
**Observed**: `syntax error near ',' at line 4`.
**Expected**: Multi-line param lists with newlines + trailing comma should parse — natural for long signatures.
**Suspected root**: Lexer treats newlines as significant in some contexts; whitespace handling in the param-list rule probably restrictive.
**Tags**: `oversight:simple`, `design:incomplete`

### B-fn-06: Tail-expression as return rejected

**Severity**: P2 design (Rust-style ergonomics)
**Status**: fixed — implemented Rust-style tail-expression-as-implicit-return. Added `TAIL_EXPR` AST code (223); grammar's no-SEMI alt in `stmt` produces it. Sema gates with a `tail_as_return_` flag set around fn-body lowering and the reachability check, cleared in block-as-expression contexts (match-arm-body-block, unsafe-block-as-expr, if-as-expr branches) so those keep their existing block-value semantics. Lock-in: pass test `tail_expr_return` covers single-expr body, prelude+tail, if-as-tail, mixed return+tail, void fn with trailing call, and match-arm-block (block-value, NOT return).
**Repro**: `B09/` —
```logos
fn helper() -> i32 { 42 }
```
**Observed**: `error [fn helper]: not all paths return a value`.
**Expected**: Per Rust convention, the tail expression of a block (no `;`) is the value of the block; if the block is the function body, it's the return value. Logos requires explicit `return` everywhere.
**Suspected root**: Conscious design choice — block-as-value not implemented at function-body position. Documented absence rather than bug.
**Tags**: `design:incomplete`

### B-fn-07: Nested function definitions rejected

**Severity**: P2 design (Rust analog absent)
**Status**: not-a-bug — closures (`|args| -> R { … }`) are the supported nested-callable form. Nested fn-items capturing nothing would only duplicate that surface; intentionally absent. The current "syntax error" diagnostic is generic but the construct is correctly disallowed. Diagnostic polish (a specific "use a closure here") is small Phase-5 cleanup, not a feature gap.
**Repro**: `B14/` —
```logos
fn outer() -> i32 {
    fn inner() -> i32 { return 7; }
    return inner();
}
```
**Observed**: `syntax error near 'fn' at line 2` (the inner fn).
**Expected**: Either accept (Rust-style nested `fn` capturing nothing — basically a bare module-level fn confined to one scope) OR document that closures are the only nested-function form.
**Suspected root**: `block`'s `stmt` repetition doesn't include `fn_def` as an alternative.
**Tags**: `design:incomplete`

### B-fn-08: Underscore as function name accepted

**Severity**: P2 design (debatable)
**Status**: fixed-in-Sprint6.3 (rejected in lower_fn; tests/logos/fail/fn_named_underscore)
**Repro**: `B20/` —
```logos
fn _() -> i32 { return 7; }
fn main() -> i32 { return _(); }
```
**Observed**: Compiles + runs to exit 7.
**Expected**: Most languages reserve `_` for ignored-binding semantics (Rust uses `_` as wildcard pattern, not callable). Allowing `fn _()` makes `_(...)` a valid call expression — risk of confusion with future ignored-binding patterns.
**Suspected root**: `IDENT` token includes `_` as a valid identifier; no special-case for fn-def.
**Tags**: `design:incomplete`, `tech-debt:reserved-name`

### B-fn-09: Trailing-comma rejection inconsistent across forms

**Severity**: P2 design (consistency gap, summary of B-fn-03/04)
**Status**: fixed-in-Sprint6.1 (B-fn-03/04/B-gn-10 all closed; type_arg_list + simple_type generic forms also accept trailing comma)
**Repro**: B-fn-03 + B-fn-04
**Observed**: Some lists accept trailing comma (variant_list, field list), some reject (param_list, call_arg_list).
**Expected**: Pick one rule, apply uniformly to ALL list productions. Recommended: accept everywhere.
**Suspected root**: PEG rule authors didn't standardize on `(COMMA T)* COMMA?` pattern across all list productions.
**Tags**: `design:incomplete`, `tech-debt:grammar-inconsistency`

### B-fn-10: Self-receiver lowering: `self: *const T` outside `impl` block treated as plain ident

**Severity**: P2 design (corner case)
**Status**: fixed (lower_fn rejects `self` as param name when struct_ctx is empty)
**Repro**: `B11/` —
```logos
fn standalone(self: *const i32) -> i32 { unsafe { return *self; } }
fn main() -> i32 { let x: i32 = 5; return standalone(&x as *const i32); }
```
**Observed**: Compiles cleanly. `self` is treated as just a parameter name, not the magic self-receiver.
**Expected**: Either reject `self` as parameter name outside `impl` block (preferred), or document that `self` is just an ordinary identifier here.
**Suspected root**: `param` grammar accepts arbitrary IDENT including `self`. Sema doesn't track impl-context for the self-magic.
**Tags**: `design:incomplete`, `tech-debt:context-free-name`

## Tag summary

| Tag | Open | Fixed | N/A | Total | Bugs |
|---|---|---|---|---|---|
| `design:incomplete` | 1 | 6 | 1 | 8 | B-fn-03, B-fn-04, B-fn-05, B-fn-06, B-fn-07, B-fn-08, B-fn-09, B-fn-10 |
| `oversight:simple` | 1 | 3 | 0 | 4 | B-fn-02, B-fn-03, B-fn-04, B-fn-05 |
| `tech-debt:context-free-name` | 0 | 1 | 0 | 1 | B-fn-10 |
| `tech-debt:grammar-doc-drift` | 0 | 0 | 1 | 1 | B-fn-01 |
| `tech-debt:grammar-inconsistency` | 0 | 1 | 0 | 1 | B-fn-09 |
| `tech-debt:missing-uniqueness-check` | 0 | 1 | 0 | 1 | B-fn-02 |
| `tech-debt:reserved-name` | 0 | 1 | 0 | 1 | B-fn-08 |

**Cluster preview**:
- **Trailing-comma cluster** (B-fn-03/04/05/09) — single architectural fix: standardize `(COMMA T)* COMMA?` across all list productions in [logos.peg](../../tools/peg_gen/grammars/logos.peg).
- **missing-uniqueness-check** (B-fn-02) — same cluster as B-it-03/04/05. Single `unique-names` helper used at all list-of-names registration sites.
- **Design choices vs bugs** — B-fn-06/07 are intentional design decisions; should move to `docs/language/reference/roadmap.md` as "non-features" rather than bugs. Worth re-classifying in Phase 3.

## Regression-confirmed (NOT bugs)

- **F02**: `fn helper() -> ()` works.
- **F04**: zero-param fn works.
- **F07**: arg count mismatch detected.
- **F08**: missing `return` detected.
- **F10**: type-mismatch in return detected.
- **F12**: shadow param via `let x = ...` works (5 → 6 → 12).
- **F13**: extern fn with varargs works.
- **F15**: generic fn with turbofish works.
- **F16**: recursion works.
- **F17**: mutual recursion works (no forward decl needed).
- **F19**: too many args detected.
- **F21**: keyword as param name rejected (good).
- **F22**: generic fn without turbofish (type-inferred) works.
- **F23**: empty body `fn nothing() {}` works.

## Notes for Phase 3

- The `tech-debt:grammar-inconsistency` cluster (trailing commas) is small but reveals a project-wide convention gap. Worth a one-pass sweep of [logos.peg](../../tools/peg_gen/grammars/logos.peg) for ALL list productions and standardizing.
- B-fn-01 (grammar/parser drift on `-> T` requirement) hints at a wider issue — there may be other places where the parser is more permissive than the grammar suggests. A grammar-vs-parser audit is its own slice (out of scope here, but worth flagging).
- B-fn-06 / B-fn-07 should not stay in this catalog; they are roadmap items. Phase 3 should categorize them as "deferred features" rather than "bugs".
