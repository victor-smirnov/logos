# Pattern / match gaps surfaced by Track 3 imports

Logos has tuple-only enum variants and a fairly narrow pattern
surface. Most rustc match/pattern tests sit deep in features Logos
hasn't built yet — Batch 4 is therefore small (5 tests) and the table
below is correspondingly large.

| ID | Surface | Gap | Surfaced by | Repro |
|---|---|---|---|---|
| P4-pm-01 | Struct-shape enum variants `enum E { V { field: T } }` | Logos enum variants are tuple-shape only. Struct-variants don't parse. | `issue-8351-1`, `issue-8351-2`, `issue-11577`, `issue-5530`, `issue-114691` | `enum E { Foo { f: isize } }` ⇒ "syntax error near 'enum'" |
| P4-pm-02 | Nested patterns inside enum-variant payloads | `Option::Some(A { foo: _foo, })` rejected. Workaround per diag: bind to a name and match in the body. | `issue-10392` | `Option::Some(A { foo: _ }) => …` ⇒ "nested patterns inside enum-variant payloads are not yet supported" |
| P4-pm-03 | Tuple types as match scrutinees | Logos doesn't have first-class tuple types `(T, T)` — match scrutinees can't be tuples. | `issue-33498`, `issue-18060`, `issue-72680`, `match-tuple-slice`, `guards` (struct half) | `match (x, y) { ... }` |
| P4-pm-04 | Array-prefix patterns `[1, ..]` | Slice/array prefix patterns not in the pattern grammar. | `match-large-array` (trimmed), `borrowck-slice-pattern-element-loan-rpass` | `[1, ..] => …` |
| P4-pm-05 | `if let` form | Logos has `match` but no `if let` sugar. | `match-on-negative-integer-ranges`, many others | `if let -128i8..=-101i8 = x { ... }` |
| P4-pm-06 | Const-pattern with str type | `match s { TEST_STR => () }` where `TEST_STR: str` const trips mlir-gen. | `issue-11940` | `'func.return' op expects parent op 'func.func'` |
| P4-pm-07 | Byte-string literals `b"..."` / byte-string patterns `b"foo"` | Lexer-/parser-level — Logos has memory note that `b"..."` parses (B-pt-03 closed), but matching against a `b"..."` pattern and `&[u8; N]` const-patterns aren't wired. | `issue-46920-byte-array-patterns` | `match x { b"foo" => … }` |
| P4-pm-08 | `mka()` returning struct-by-value + destructure inside `test()` body (mlir-gen GEP crash) | `let A { foo, } = mka();` triggers `'llvm.getelementptr' op operand #0 must be LLVM pointer type … but got '!llvm.struct<…>'`. Codegen treats the by-value struct return as if it were a pointer for the destructure. | `issue-10392` (trimmed) | as above |
