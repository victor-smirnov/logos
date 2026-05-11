# Pattern / match gaps surfaced by Track 3 imports

Logos has tuple-only enum variants and a fairly narrow pattern
surface. Most rustc match/pattern tests sit deep in features Logos
hasn't built yet — Batch 4 is therefore small (5 tests) and the table
below is correspondingly large.

Status legend: `Open` — not started; `Partial` — partial fix landed,
notes inline; `✅ Closed` — gap closed (commit, date).

| ID | Surface | Status | Gap | Surfaced by | Repro |
|---|---|---|---|---|---|
| P4-pm-01 | Struct-shape enum variants `enum E { V { field: T } }` | Deferred — grammar/sema/mlir-gen across many subsystems; needs its own slice. ~1 week. | Logos enum variants are tuple-shape only. | `issue-8351-1`, `issue-8351-2`, `issue-11577`, `issue-5530`, `issue-114691` | `enum E { Foo { f: isize } }` |
| P4-pm-02 | Nested patterns inside enum-variant payloads | Deferred — sema lowering: recurse pattern build into variant payload sub-patterns. Current diagnostic is intentional ("bind to a name and match in the body"). | `Option::Some(A { foo: _foo, })` rejected. | `issue-10392` | `Option::Some(A { foo: _ }) => …` |
| P4-pm-03 | Tuple types as match scrutinees | Partial (`ead8f39`, 2026-05-11) — basic tuple types in patterns; value form via tuple-arm AND-chain `8c51edc`; tail-position `80bef0c`. Remaining: deeper tuple-type integration. | Logos doesn't have first-class tuple types `(T, T)` — match scrutinees can't be tuples. | `issue-33498`, `issue-18060`, `issue-72680`, `match-tuple-slice`, `guards` (struct half) | `match (x, y) { ... }` |
| P4-pm-04 | Array-prefix patterns `[1, ..]` | Deferred — grammar accepts `[1, ..]` but mlir-gen mishandles (`arith.cmpi operand must be signless-integer-like, but got '!llvm.ptr'`). Needs proper slice-pattern codegen. | Array-prefix pattern in match. | `match-large-array` (trimmed), `borrowck-slice-pattern-element-loan-rpass` | `[1, ..] => …` ⇒ MLIR verifier fail |
| P4-pm-05 | `if let` form | ✅ Closed (`e380aab`, 2026-05-11) — `if let` works (Sprint 1.3 rescind). | Logos has `match` but no `if let` sugar. | `match-on-negative-integer-ranges`, many others | `if let -128i8..=-101i8 = x { ... }` |
| P4-pm-06 | Const-pattern with str type | Deferred — mlir-gen bug specific to str-typed const-pattern matching. Narrow reproducer; needs codegen slice. | `match s { TEST_STR => () }` where `TEST_STR: str` const trips mlir-gen. | `issue-11940` | `'func.return' op expects parent op 'func.func'` |
| P4-pm-07 | Byte-string literals `b"..."` / byte-string patterns `b"foo"` | Deferred — pattern-side wiring for `b"…"` const-patterns and `&[u8; N]` const-patterns. Lexer + parser side closed (B-pt-03). | Matching against a `b"..."` pattern not wired. | `issue-46920-byte-array-patterns` | `match x { b"foo" => … }` |
| P4-pm-08 | `mka()` returning struct-by-value + destructure inside `test()` body (mlir-gen GEP crash) | Deferred — codegen treats by-value struct return as if it were a pointer for the destructure. Needs spill-to-alloca before GEP. | `let A { foo, } = mka();` ⇒ `'llvm.getelementptr' op operand #0 must be LLVM pointer type`. | `issue-10392` (trimmed) | as above |
| P4-pm-09 | Char-range patterns `'a' ..= 'z'` | ✅ Closed (`ca17040`, 2026-05-11) — PAT_CHAR / PAT_CHAR_RANGE end-to-end. | char-range pattern arms in match. | `inc-range-pat` | `match c { 'a'..='z' => … }` |
| P4-pm-10 | Match-as-expression Range arm codegen | ✅ Closed (`ca17040`, 2026-05-11) — Range-arm AND-chain in EMatchExpr path; signed/unsigned. | `let b = match c { 0..=10 => true, _ => false };` returned wrong value (Range disc=0). | `inc-range-pat` (un-trim) | as above |
| P4-pm-11 | Match-as-tail-expression in non-void fn | ✅ Closed (`80bef0c`, 2026-05-11) — gen_function_body emits `llvm.unreachable` instead of bare `func.return` on non-terminated fallthrough. | Tail-position `match (x,y) {...}` in `fn -> T` produced "func.return op has 0 operands". | `match-tuple-tail` | `fn foo() -> u32 { match (x,y) { ... } }` |
