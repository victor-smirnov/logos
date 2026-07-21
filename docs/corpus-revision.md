# Corpus revision — full ledger

The honest number replacing the hunch: across the 77 rust `tests/ui` categories
we have drawn from, **864/6819 = 12%** of originals are traceably imported
(via `Original path:` headers). 5955 untraced. This is NOT a bug count: each untraced
original owes a verdict — imported | divergence §N | gap-ticket | not-applicable. A skip
without a verdict is the survivor-bias mechanism (see project_imported_corpus_revision).

Tiering steers triage: PORTABLE = behavior Logos should match, a gap likely means a real
bug; DIVERGENT/NA = rust-specific diagnostics, const-eval (→ metacall §A1), rustc
regressions, unstable features — gaps expected and cheap to verdict; MIXED = case by case.

## PORTABLE — 351/1609 = 21%  (1258 untraced)

| category | covered | originals | gap |
|---|--:|--:|--:|
| pattern | 17 | 129 | 112 |
| closures | 27 | 136 | 109 |
| self | 16 | 100 | 84 |
| methods | 10 | 89 | 79 |
| drop | 22 | 100 | 78 |
| unboxed-closures | 21 | 95 | 74 |
| match | 23 | 93 | 70 |
| associated-consts | 3 | 69 | 66 |
| coercion | 17 | 81 | 64 |
| array-slice-vec | 20 | 83 | 63 |
| generics | 32 | 95 | 63 |
| iterators | 1 | 61 | 60 |
| cast | 15 | 73 | 58 |
| binop | 8 | 48 | 40 |
| enum | 20 | 57 | 37 |
| let-else | 4 | 40 | 36 |
| for-loop-while | 39 | 69 | 30 |
| loops | 5 | 25 | 20 |
| range | 2 | 22 | 20 |
| tuple | 4 | 23 | 19 |
| destructuring-assignment | 2 | 18 | 16 |
| functions-closures | 19 | 33 | 14 |
| autoref-autoderef | 6 | 19 | 13 |
| deref | 2 | 14 | 12 |
| str | 1 | 13 | 12 |
| overloaded | 15 | 24 | 9 |

## MIXED — 320/2035 = 15%  (1715 untraced)

| category | covered | originals | gap |
|---|--:|--:|--:|
| traits | 49 | 333 | 284 |
| associated-types | 27 | 290 | 263 |
| impl-trait | 5 | 198 | 193 |
| structs | 19 | 82 | 63 |
| inference | 11 | 73 | 62 |
| moves | 5 | 62 | 57 |
| type | 2 | 55 | 53 |
| fmt | 1 | 48 | 47 |
| statics | 2 | 48 | 46 |
| fn | 5 | 49 | 44 |
| static | 3 | 47 | 44 |
| reachable | 3 | 46 | 43 |
| numbers-arithmetic | 27 | 69 | 42 |
| binding | 68 | 108 | 40 |
| unsized | 2 | 41 | 39 |
| structs-enums | 33 | 71 | 38 |
| variance | 2 | 38 | 36 |
| or-patterns | 9 | 44 | 35 |
| enum-discriminant | 5 | 39 | 34 |
| dst | 1 | 33 | 32 |
| where-clauses | 5 | 34 | 29 |
| type-inference | 1 | 22 | 21 |
| return | 2 | 22 | 20 |
| box | 1 | 20 | 19 |
| recursion | 5 | 24 | 19 |
| reborrow | 1 | 19 | 18 |
| type-alias | 1 | 14 | 13 |
| block-result | 1 | 13 | 12 |
| type-alias-enum-variants | 2 | 14 | 12 |
| builtin-superkinds | 1 | 12 | 11 |
| numeric | 2 | 13 | 11 |
| shadowed | 1 | 9 | 8 |
| mut | 3 | 10 | 7 |
| ufcs | 1 | 7 | 6 |
| never_type | 0 | 4 | 4 |
| ptr_ops | 2 | 6 | 4 |
| zero-sized | 1 | 5 | 4 |
| expr | 11 | 13 | 2 |

## DIVERGENT/NA — 193/3175 = 6%  (2982 untraced)

| category | covered | originals | gap |
|---|--:|--:|--:|
| consts | 4 | 483 | 479 |
| borrowck | 26 | 454 | 428 |
| parser | 12 | 419 | 407 |
| issues | 52 | 394 | 342 |
| nll | 2 | 224 | 222 |
| typeck | 12 | 226 | 214 |
| regions | 42 | 218 | 176 |
| const-generics | 2 | 160 | 158 |
| attributes | 1 | 139 | 138 |
| privacy | 2 | 126 | 124 |
| lifetimes | 6 | 123 | 117 |
| mir | 30 | 139 | 109 |
| codegen | 2 | 70 | 68 |

## Verdicts landed

### cast (58 originals; first triaged category)
30 PORTABLE (7 works, 13 GAP, 10 capped) · 21 DIVERGENCE (mostly region-variance dyn-ptr cast legality, an NLL-integrated subsystem) · 7 not-applicable.
**Root-cause finding, verified in sema_expr.cpp:1082**: `as`-cast validity is a BLOCKLIST (aggregate→scalar) not the RFC0401 permitted-cast allowlist, so `()`/`&ref`/`char→float`/`bool→ptr`/`enum→ptr` are accepted; two are memory-unsafe at runtime (`Box<[T;N]> as Box<[T]>` double-frees, `usize as *const [u8]` segfaults). Recorded as an arc-candidate (bug_cast_validity_blocklist) — a positive allowlist rewrite, PAIR (how strict `as` should be is a language-surface question). Plus a small parser ticket: zero-arg tuple variant `Foo()` doesn't parse.
