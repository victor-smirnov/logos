# Bug catalog: Pattern Matching

**Group**: 9 — Pattern Matching
**Grammar rules covered**: `pattern`, `pat_binding`, `pat_binding_list`, `pat_field`, `pat_field_list`, `pat_slice_elem`, `pat_slice_elems`, `pat_writ_map_entry`, `pat_writ_map_entries`, `pat_writ_arr_elem`, `pat_writ_arr_elems`
**Reference doc**: [docs/spec/patterns.md](../spec/patterns.md)
**Implementation entry points**:
- [src/compiler/sema_pat.cpp](../../src/compiler/sema_pat.cpp) (or sema_stmt.cpp/sema_expr.cpp — pattern lowering scattered) — pattern lowering, binding registration
- [src/compiler/mlir_gen_stmt.cpp](../../src/compiler/mlir_gen_stmt.cpp) — match-arm emission

**Hunt date**: 2026-05-04
**Repros**: `/tmp/baghunt/patterns/`

## Bugs

### B-pt-01: Duplicate binding name in tuple pattern silently accepted

**Severity**: P0 (silent miscompile)
**Status**: fixed-in-M0.1 (check_unique_names in lower_let_destruct; tests/logos/fail/dup_tuple_pat_binding)
**Repro**: `B01/` —
```logos
fn main() -> i32 {
    let t: (i32, i32) = (1, 2);
    let (x, x) = t;  // both bind to x
    return x;
}
```
**Observed**: Compiles cleanly. Whichever binding wins is implementation-defined.
**Expected**: "duplicate binding 'x' in pattern" — Rust rejects.
**Suspected root**: Pattern lowering doesn't dedup-check binding names. Same family as B-st missing-uniqueness-check cluster.
**Tags**: `oversight:simple`, `tech-debt:missing-uniqueness-check`

### B-pt-02: Struct pattern in `let pat = expr;` not supported (only in match)

**Severity**: P2 design
**Status**: fixed-in-Sprint4.2 (LET_PAT alt + lower_let_pat for struct patterns; tests/logos/pass/let_struct_pattern). Variant/tuple-via-pat_single forms still rejected — refutable, deferred to match infrastructure.
**Repro**: `B03/` and `B04/` —
```logos
struct Foo { x: i32, y: i32 }
fn main() -> i32 {
    let f: Foo = Foo { x: 1, y: 2 };
    let Foo { x, y } = f;  // syntax error
    return x;
}
```
**Observed**: `syntax error near 'f' at line 5`.
**Expected**: Struct pattern destructure should work in `let`. Logos accepts it in `match` arms (verified P11). Asymmetry between `let pat = expr;` (only tuple/wildcard supported) and `match` arms (full pattern set).
**Suspected root**: `let_stmt` grammar's `pattern` non-terminal is restricted compared to `match_arm`'s pattern. Likely the same restriction that affects refutable pats (B-st-02).
**Tags**: `design:incomplete`, `tech-debt:asymmetric-pattern-positions`

### B-pt-03: Byte-string literal pattern (`b"..."`) syntax error

**Severity**: P2 design (incomplete feature)
**Status**: parses-in-Sprint4.2 (added BYTE_STRING token + PAT_BYTES alt; sema diagnostic until codegen lands). Test: tests/logos/fail/byte_string_pattern.
**Repro**: `B07/` —
```logos
fn main() -> i32 {
    let s: &[u8] = "hello".as_bytes();
    match s {
        b"hi"    => { return 1; }
        b"hello" => { return 2; }
        _        => { return 3; }
    }
}
```
**Observed**: `syntax error near 'b' at line 5`.
**Expected**: Byte-string literal patterns should parse (Rust supports). Useful for matching `&[u8]`/string-bytes.
**Suspected root**: Lexer/grammar may not support `b"..."` as a pattern-position literal.
**Tags**: `design:incomplete`

### B-pt-04: Nested pattern in enum variant payload syntax error

**Severity**: P2 design (incomplete feature)
**Status**: parses-in-Sprint4.2 (pat_variant_args uses pat_single; sema diagnostic until match-lowering supports nested guards). Test: tests/logos/fail/nested_variant_pattern.
**Repro**: `B08/` —
```logos
struct Pair { a: i32, b: i32 }
enum E { Some(Pair), None }
fn main() -> i32 {
    let e: E = E::Some(Pair { a: 5, b: 7 });
    match e {
        E::Some(Pair { a, b }) => { return a + b; }
        E::None => { return 0; }
    }
}
```
**Observed**: `syntax error near 'Pair' at line 7`.
**Expected**: Nested pattern (enum variant containing struct pattern) should parse. This is a common, ergonomic destructure form.
**Suspected root**: `pat_binding`'s payload grammar accepts only flat bindings, not nested patterns.
**Tags**: `design:incomplete`, `tech-debt:no-nested-patterns`

### B-pt-05: Slice pattern with `..rest` syntax error

**Severity**: P2 design (incomplete feature)
**Status**: not-a-parse-bug (verified 2026-05-04) — slice patterns parse in match arms; the catalog repro fails because `&arr[..]` slice-expression syntax isn't supported, not because of the pattern. Codegen rejects suffix-after-rest with "not supported for dynamic slices". Deferred — needs slice-expression + dynamic-slice-pattern codegen.
**Repro**: `B12/` —
```logos
fn main() -> i32 {
    let arr: [i32; 5] = [1, 2, 3, 4, 5];
    match &arr[..] {
        [a, .., b] => { return a + b; }
        _          => { return 0; }
    }
}
```
**Observed**: `syntax error near '[' at line 4`.
**Expected**: Slice patterns (Rust-style `[a, .., b]`, `[head, tail @ ..]`) should parse. Grammar [logos.peg](../../tools/peg_gen/grammars/logos.peg) has `pat_slice_elem` / `pat_slice_elems` productions but apparently the position rules don't admit them in `match` against `&[T]`.
**Suspected root**: Either the slice-pattern grammar doesn't connect to the `pattern` dispatch at the right place, or the receiver type-check rejects `&[T]` as a non-matchable kind.
**Tags**: `design:incomplete`, `tech-debt:slice-pat-not-wired`

### B-pt-06: Float literal in pattern syntax error

**Severity**: P2 design
**Status**: parses-in-Sprint4.2 (FLOAT alt in pat_single_base; sema rejects with "IEEE equality semantics undecided"). Test: tests/logos/fail/float_pattern.
**Repro**: `B16/` —
```logos
fn main() -> i32 {
    let x: f64 = 3.14;
    match x {
        3.14 => { return 1; }
        _    => { return 0; }
    }
}
```
**Observed**: `syntax error near '{' at line 4`.
**Expected**: Float literal in pattern should parse. (Rust deprecated this for IEEE-equality reasons, but Logos may want a different policy.) At minimum, parse + diagnose.
**Suspected root**: Pattern grammar's literal alts accept INTEGER but not FLOAT.
**Tags**: `design:incomplete`

### B-pt-07: Match arm AFTER catch-all leaks to MLIR-gen

**Severity**: P1 diagnostic
**Status**: fixed-in-Sprint5.2 (is_catchall_pat check in lower_match{,_expr}; tests/logos/fail/match_arm_after_catchall)
**Repro**: `B19/` —
```logos
fn main() -> i32 {
    let x: i32 = 3;
    match x {
        _ => { return 1; }   // catch-all FIRST
        3 => { return 2; }   // unreachable — never fires
    }
}
```
**Observed**: `error: 'func.return' op has 0 operands ...; mlir_gen: module verification failed`.
**Expected**: Sema warning: "match arm after catch-all is unreachable" (preferably reject, definitely don't crash MLIR-gen).
**Suspected root**: Reachability analysis on match arms not present (same family as B-st-07 unreachable-arm). Sema lets it through; mlir-gen confused.
**Tags**: `tech-debt:no-reachability-lint`, `tech-debt:diagnostic-from-codegen`

### B-pt-08: Empty match block on uninhabited type rejected

**Severity**: P2 design
**Status**: fixed — root cause was the struct-lit/block-head ambiguity (the catalog's `match_arm+` hypothesis was wrong; grammar already used `*`). For `match e { ... }`, `e {` greedy-parsed as a struct literal `e { }`, leaving the match's own `{ }` unmatched. Added a first match_stmt alt `KW_MATCH IDENT LBRACE match_arm* RBRACE` (with a `match_head_var` helper that wraps IDENT as VAR_REF), which sidesteps the struct-lit alt for the bare-identifier case. Complex heads (`match foo.bar() { ... }`) fall through to the existing expr-based alt. Lock-in: pass test `match_bare_ident_head` covers both empty match on uninhabited type and ordinary bare-ident-head match.
**Repro**: `B20/` —
```logos
enum Empty {}
fn main() -> i32 {
    let e: Empty = todo();
    match e { }
}
fn todo() -> Empty { return todo(); }
```
**Observed**: `syntax error near '}' at line 5`.
**Expected**: Empty match `{}` should parse and be accepted as the canonical "match on uninhabited type" form. Currently the grammar's `match_stmt` requires at least one arm.
**Suspected root**: Grammar's `match_arm+` (one-or-more) instead of `match_arm*`. Once empty enums work cleanly (they currently compile silently — see B-it-06), empty match should round out the story.
**Tags**: `design:incomplete`, `tech-debt:grammar-too-strict`

### B-pt-09: Writ-pattern positions gated on missing stdlib helper

**Severity**: feature-incomplete
**Status**: confirmed-feature-incomplete (2026-05-07) — `match arr { @[a, b, c] => ... }` is rejected by sema with `Writ pattern needs stdlib helper 'writ_pat_array_slot'; use logos.mem.writ.pat;`, but `std.writ.pat` does not exist in stdlib. Sema gate works; the stdlib side was never written.
**Note**: To revive, write `stdlib/std/writ/pat.logos` exposing the helpers the gate names (`writ_pat_array_slot`, etc.). Same shape as B-he-09/B-he-10.
**Tags**: feature-incomplete:no-stdlib-helper, deferred-to-writ-group

### B-pt-10: Or-pattern with bindings — FIXED

**Severity**: was silent miscompile (mlir-gen "undefined" warning + bogus exit code)
**Status**: FIXED (2026-05-07, this sprint) — `Either::L(x) | Either::R(x) => x` now works. Sema's NG4 already enforced consistent binding name sets across alternatives; mlir-gen's `extract_arm_payload` (in `src/compiler/mlir_gen_expr.cpp`) didn't recognize PatOr. Added a recursive case that extracts from the first alt — sema's NG4 guarantees compatible payload shape across alts. Inconsistent-bindings (`L(x) | B`, where B has no payload) still rejected by sema.
**Regression test**: `tests/logos/pass/or_pattern_binding.logos`
**Tags**: codegen:missing-pattern-handler, fixed

### B-pt-11: array/slice pattern in match-AS-EXPRESSION dropped its bindings — FIXED

**Severity**: silent miscompile (arm read uninitialized scope slots → garbage result, no diagnostic)
**Status**: FIXED (2026-05-21, 47413e65) — `let r = match arr { [x, y] => x + y }` returned garbage. The match-expression codegen (`gen_expr_kind(EMatchExprView)`) extracted arm bindings via `extract_arm_payload`, which handled VariantData/Wild/Tuple/Struct/RefBind/RefPat/At/Or but NOT Slice (only the statement form did). Added a Slice case mirroring the stmt-form `bind_elem`.
**Regression test**: `tests/logos/pass/array_pattern_match_expr.logos`
**Tags**: codegen:missing-pattern-handler, fixed

### B-pt-12: dynamic-slice patterns over `&[T]` — FIXED (top-level + nested-in-tuple length)

**Severity**: feature gap (codegen rejected/miscompiled)
**Status**: FIXED (2026-05-22, 64be6e49) — `match s { [a, b] => …, [h, ..] => … }` over a runtime-length `&[T]` scrutinee, plus nested-in-tuple length discrimination `(2, [_, _])`. Implemented in the match-expression codegen (statement matches lower to `EMatchExpr` too). Slice value = ptr→{data_ptr, i64 len}; gate `len == N` / `len >= N`, GEP elements through data ptr. sema already accepted/checked these. **Remaining (B-pt-13)**: named element bindings nested inside a tuple (`(2, [a, b])`).
**Regression test**: `tests/logos/pass/slice_pattern_dynamic.logos`, `tests/imported/pass/match/match-tuple-slice.logos`
**Tags**: codegen:missing-pattern-handler, fixed

### B-pt-13: named slice bindings nested inside a tuple pattern — OPEN (clean error)

**Severity**: feature gap (clean "undefined variable" — NOT a miscompile)
**Status**: OPEN (2026-05-22) — `match x { (2, [a, b]) => a + b }`. sema's tuple-element handler (sema_stmt.cpp:2740-2804) builds the slice sub via the PAT_OR fallback (so length dispatch works, B-pt-12) but declares only a top-level `_` binding, never the nested names → `undefined variable 'a'`. Fix = declare nested-slice prefix names in scope (bind_pattern_ref recursion into the Slice sub) + the nested-slice binding GEP in the tuple binding extractor (the latter was prototyped then reverted as dead until the sema side lands). Low priority (works via a `let [a,b] = …` after a length-only tuple match).
**Tags**: sema:missing-binding-decl, open

### B-st-NN: refutable array/slice NAMED-binding pattern in a unit-result STATEMENT match → stray `arith.constant` — OPEN

**Severity**: LLVM-translate fail (was silent-miscompile before 47413e65; now errors)
**Status**: OPEN (2026-05-22) — `match a { [x, y] => { out = …; } _ => {} }` (array/slice, named bindings, block arms yielding `()`) → "missing LLVMTranslationDialectInterface registration … arith.constant". Narrowing: LITERAL array pattern `[1,_]` same shape COMPILES; TUPLE `(x,y)` / ENUM `Some(v)` named bindings same shape COMPILE+run; array NAMED bindings in match-AS-EXPRESSION (non-unit) COMPILE+run. So it's the array/slice element-binding path × unit-result statement match. 9 arith.constants pre-lowering (`--emit-mlir` ok); exactly one survives the arith→llvm conversion. Adjacent to 47413e65 (added the array binding path). Repro: `docs/baghunt/repro/stmt-array-bind-arith-constant.logos`.
**Tags**: codegen:arith-not-lowered, open

## Tag summary

| Tag | Open | Fixed | N/A | Total | Bugs |
|---|---|---|---|---|---|
| `design:incomplete` | 0 | 2 | 4 | 6 | B-pt-02, B-pt-03, B-pt-04, B-pt-05, B-pt-06, B-pt-08 |
| `oversight:simple` | 0 | 1 | 0 | 1 | B-pt-01 |
| `tech-debt:asymmetric-pattern-positions` | 0 | 1 | 0 | 1 | B-pt-02 |
| `tech-debt:diagnostic-from-codegen` | 0 | 1 | 0 | 1 | B-pt-07 |
| `tech-debt:grammar-too-strict` | 0 | 1 | 0 | 1 | B-pt-08 |
| `tech-debt:missing-uniqueness-check` | 0 | 1 | 0 | 1 | B-pt-01 |
| `tech-debt:no-nested-patterns` | 0 | 0 | 1 | 1 | B-pt-04 |
| `tech-debt:no-reachability-lint` | 0 | 1 | 0 | 1 | B-pt-07 |
| `tech-debt:slice-pat-not-wired` | 0 | 0 | 1 | 1 | B-pt-05 |

**Cluster preview**:
- **Pattern surface coverage gaps** (B-pt-02..06, 08) — pattern grammar is incomplete in several positions. Many "this should work" forms parsed by Rust don't parse here. Architectural fix: a pass through the pattern grammar to standardize what's allowed where.
- **B-pt-01 (dup binding)** joins the cumulative `missing-uniqueness-check` cluster (now **9 bugs**).
- **B-pt-07 (catch-all then arm)** joins `no-reachability-lint` (now **3 bugs** with B-st-07 + B-st-08).

## Regression-confirmed (NOT bugs)

- **P02**: tuple pattern arity mismatch detected.
- **P05**: enum variant payload arity detected.
- **P06**: literal int pattern works.
- **P09**: or-pattern works.
- **P10**: range pattern (`0..=9`) works.
- **P11**: struct pattern in match works.
- **P13**: pattern guard (`if cond`) works.
- **P14**: wildcard-only match works.
- **P17**: negative literal pattern works.
- **P18**: bool literal pattern works.

## Notes for Phase 3

- The pattern surface is **substantially incomplete**: 5/8 confirmed bugs are "this Rust-style pattern doesn't parse" issues. This is a roadmap-level gap, not a bug — but the catalog records what's missing.
- `let pat = expr;` vs `match e { pat => ... }` asymmetry (B-pt-02) is the most impactful day-to-day usability gap. A unified `pattern` non-terminal would close many B-pt-* entries at once.
- The `b"..."` byte-string lit pattern (B-pt-03) and `[a, .., b]` slice pattern (B-pt-05) are common idioms; their absence forces verbose alternatives.
