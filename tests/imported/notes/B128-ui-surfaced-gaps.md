# B128 — UI-surfaced gaps

Batch B128 imported 16 run-pass tests distilled from `tests/ui/match/` and
`tests/ui/borrowck/` (pinned rustc `4b0c9d76ae7d387229caea55cfa73c280b08b8a7`).
All 16 imported files compile + link + exit 0. The gaps below were surfaced
while probing distinct features; the affected facet was rewritten to the
working Logos form in the imported test, and each gap is recorded here for the
grind.

Both new gaps are §B catch-up TODOs (must converge to Rust). No new §A blessed
divergences.

---

## G128-1 — `&Pat` reference-pattern in a match arm (matching a `&Enum`/`&Struct` scrutinee without an explicit deref)

**Symptom.** Matching a borrowed scrutinee `x: &Option<i64>` with a
reference-PATTERN in the arm crashes mlir-gen:

```logos
fn classify(x: &Option<i64>) -> i64 {
    match x {
        &Option::Some(_) => { return 1i64; }   // &Pat arm
        &Option::None    => { return 0i64; }
    }
}
// error: 'llvm.load' op operand #0 must be LLVM pointer type, but got 'i32'
// mlir_gen: module verification failed
```

The canonical Logos deref form `match *x { Option::Some(_) => …, Option::None => … }`
compiles + runs (used in `ref-some-pattern-m2`). So the variant machinery is
fine; what is missing is the `&Pat` arm desugar — a leading `&` in the pattern
should peel one borrow off the scrutinee (equivalent to deref-then-match), and
mlir-gen instead tries to load the enum payload off a non-pointer.

**Feature / §.** §B — reference-patterns (`&Pat`, and by extension `&mut Pat`)
in match arms. Rust desugars these to "deref the scrutinee borrow, then match
`Pat`"; Logos requires the explicit `match *x` form today. Related to the
existing KNOWN-OPEN "match over &Enum/&Struct no-deref", but this is the
arm-side `&Pat` facet specifically (the scrutinee-side deref form already works).

**Where it bit.** `ref-some-pattern-m2` (rewritten to `match *x`).

---

## G128-2 — struct field-binding pattern in a match-AS-EXPRESSION miscompiles / SIGSEGVs

**Symptom.** A struct destructuring pattern that binds fields and is used as
the *value* of a `match` expression produces a wrong result or SIGSEGV:

```logos
struct A { x: i64, y: i64 }
let v: A = A { x: 7i64, y: 9i64 };
let r: i64 = match v { A { x: a, y: b } => { a } };   // r is garbage / crash
let r2: i64 = match v { A { x: ref a, y: ref b } => { *a } }; // wrong value (240)
let pair: (i64,i64) = match v { A { x:a, y:b } => (a,b) };
//   error: let 'pair': type mismatch — expected (i64, i64), got (<error>, <error>)
```

The SAME pattern in STATEMENT position works correctly:

```logos
match v { A { x: a, y: b } => { return a as i32; } }                 // OK → 7
match v { A { x: ref a, y: ref b } => { /* *a==7, *b==9 */ } }       // OK
```

So the field-binding extraction is sound in the statement-position match
lowering (`EMatchExpr`) but the match-AS-EXPRESSION codegen
(`gen_expr_kind(EMatchExprView)`) does not bind the struct fields into the arm
scope before evaluating the arm's tail value — the bindings read uninitialized
storage (scalar → garbage value; tuple-build → `<error>` typed). Mirrors the
B5 slice-pattern note's "the match-AS-EXPRESSION codegen … note statement
matches lower to `EMatchExpr` too" split — here the gap is on the OTHER
(expression) side, for struct field bindings.

**Feature / §.** §B — struct field-pattern bindings (value and `ref`) in a
match used as an expression. Fix = make the `EMatchExprView` arm lowering bind
struct-field patterns into the arm scope (the same `bind_pattern_ref` recursion
the statement-position path already runs) before lowering the arm tail.

**Where it bit.** `rvalue-match-equiv-binding-m2` (rewritten to the
statement-position `match v { … }` form).

---

## Re-confirmed KNOWN-OPEN (NOT re-reported)

- byte-string / byte-array `match` patterns (`b"."`, `b".."` arms) —
  `pattern-deref-miscompile` skipped (byte-string is KNOWN-OPEN).
- `Box<T>` / `box`-patterns — `issue-42679` (`box Pat`), `borrowck-box-sensitivity`,
  `borrowck-mut-uniq` (Box field) skipped or distilled to stack values.
- dynamic `&[T]` slice-as-value + `&[T;N]`→`&[T]` coercion —
  `borrowck-freeze-frozen-mut` distilled (slice → `*mut i64` cell);
  `borrowck-slice-pattern-element-loan-rpass` (`[ref mut head, ref mut tail @ ..]`)
  skipped (slice-as-value).
- `match`-as-expression on a `&Enum`/`&Struct` without deref — see G128-1.

---

## Skipped sources (no port)

- `match/issue-26996.rs`, `match/issue-27021.rs` — upstream `//@ ignore-test`
  (bogus during the #54986/#54987 window).
- `match/postfix-match/*` — postfix-`.match` syntax (feature-gated sugar; no
  Logos equivalent surface).
- `borrowck/two-phase-bin-ops.rs` — operator-overload `*Assign` traits via
  `macro_rules!` (§A3 macro layer; operator traits are user-impl-only — the
  two-phase-borrow facet is covered by `two-phase-method-receiver-bc`).
- `borrowck/*-static-*`, `borrowck-assignment-to-static-mut.rs` — top-level
  `static`/`static mut` items (not ported; see B127 note).
