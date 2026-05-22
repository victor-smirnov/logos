# B135 — UI-surfaced gaps

Batch B135 imported 23 rustc UI run-pass tests (pinned SHA `4b0c9d76ae7d387229caea55cfa73c280b08b8a7`)
across cast, enum, structs-enums, self, generics, where-clauses, coercion,
functions-closures, expr, binop, numbers-arithmetic. All 23 compile + link + exit 0.

## NEW gap surfaced (1)

### (G135-1) `==` on `Option<T>` always returns false — confirms B111
`Option<i64> == Option<i64>` mis-evaluates: both `None == None` and
`Some(3) == Some(3)` return **false** (the operator path produces a constant-false
result). `Some(3) == Some(4)` "correctly" returns false too, so the bug is masked for
the unequal case. Minimal repro:
```
let a: Option<i64> = None;
let b: Option<i64> = None;
if a == b { ... }   // NOT taken — wrong
```
This is the **same root as B111** ("stdlib `Option<i64> == Option<i64>` derived `==`
mis-codegens"), here surfacing as a silently-wrong runtime result rather than a codegen
dump. NOT re-reported as a fresh bug — recorded for cross-reference. Candidate
`structs-enums/compare-generic-enums.rs` was dropped on this gap (it relies entirely on
`Option<isize> ==`).

This looks like a **tractable parallel-mapping / missing-case** bug: the operator-`==`
lowering for an enum operand likely is not routing to the variadic/derived enum
structural-eq the way `assert_eq!`/`.eq()` paths do (coretests assert Option equality
fine through other channels), so a single operator-dispatch site for enum operands is
the candidate fix point.

## Re-confirmed known-open (NOT re-reported; candidates dropped or distilled)

- **block-wrapped closure literal** `{ |i| i }` — parse error `syntax error near '|'`
  (`functions-closures/closure-inference2.rs` dropped; matches B134 known-open).
- **generic tuple-struct** `struct S<T>(T)` — known-open (G134); `generics/generic-newtype-struct.rs`
  dropped.
- **positional tuple-struct + named-index field literal/pattern** `struct S(u8,u16); S{0:a,1:b}`
  — Logos has no positional tuple-struct syntax; `structs-enums/numeric-fields.rs` dropped
  (the plain-tuple borrow facet imported instead via `borrow-tuple-fields-b135`).
- **payload-binding inside a nested-variant struct-field pattern in a match arm**
  (`t3::c(T2{x: t1::a(m), ..}, _)`) — known-open; `structs-enums/record-pat.rs` dropped.
- **in-language `mod` + `::path()` call** — known-open; `generics/generic-fn-twice.rs` and
  `expr/scope.rs` dropped.
- **scientific-notation float literal with no decimal point in mantissa** `5e-11` — parse
  error; the supported form is `5.0e-11` (mechanical adaptation in `floatlits-b135`, NOT a
  reported gap — this is the documented `<digits>.<digits>e<exp>` lexer shape).
- cross-crate `aux-build` tests (`methods/method-self-arg-aux{1,2}.rs`) — not portable
  (separate-crate harness), dropped.

## G135-1 follow-up (2026-05-22 investigation) — NOT just routing
`a == b` on `Option<i64>` → `llvm.icmp` mismatched-operands verify-fail. `a.eq(b)`
ALSO fails ("receiver is not a struct (got Option)"). Root: there is NO
`impl PartialEq/Eq for Option` (or Result) in stdlib (grep: none), so there is
nothing for `==` to route to — AND the operator-overload dispatch (sema_expr.cpp
~1269) only fires for `lt.kind()==Struct`, not Enum. Two-part fix:
(1) stdlib: add `impl<T: PartialEq> PartialEq for Option<T>` (+ Result) written
   with NESTED match (`match self { Some(a)=>match o {Some(b)=>a==b,...},...}`),
   NOT a tuple-of-enum match (that segfaults — see B132-N1), and using by-value
   or `*self` receiver (the `&Enum`-param match caveat, B121-G3).
(2) sema: extend the `==`/`!=` operator dispatch to ENUM operands → the enum's
   eq method (mirror the Struct branch). Generic-eq instantiation must follow.
Moderate-deep (stdlib + sema + generic eq); common (Option comparison). This is
the B111 root surfacing as a runtime/verify failure. Focused session.
