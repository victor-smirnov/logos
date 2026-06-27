# B109 — surfaced gaps (tests/ui run-pass: pattern/binding/tuple/typeck/lifetimes/type-alias + deref retry)

Batch B109 mined run-pass tests from `tests/ui/{pattern,binding,tuple,typeck,
lifetimes,type-alias,shadowed,range}` plus retried the B108 deref/methods skips
that two new compiler fixes (method-autoderef-through-user-Deref, `&f[i]`
place-borrow) had unblocked. Upstream commit
`4b0c9d76ae7d387229caea55cfa73c280b08b8a7`.

11 tests landed: pattern (3: issue-6449, size-and-align,
const-pattern-str-match-lifetime), binding (1: match-pipe-binding), deref (1:
dereferenceable-type-behavior-22992 — transitive method-autoderef-through-Deref,
the now-unblocked subset), lifetimes (2: trait-object-constructor-14821,
matcher-trait-equality-13323), tuple (1: tuple-index-fat-types), type-alias (1:
type-param), typeck (2: issue-2063, infer-struct-with-trait-object).

The remaining run-pass pool in these categories is heavily skewed toward parser
gaps in pattern syntax and a handful of codegen bugs; every skip below is
recorded with its minimal trigger.

Categories: `compiler-bug` (crash / wrong runtime value / verifier fail),
`missing-feature` (language feature absent or unparsed), `missing-stdlib`
(library API absent), `intentional-divergence` (Logos design differs from Rust).

---

## compiler-bug

### 1. Match over a tuple of two enums SIGSEGVs at runtime — FIXED (2026-05-21)
**Fixed.** Codegen tested each tuple element's enum discriminant but never
bound the nested variant payload (`(E::A(x), F::B(y))` → `x`/`y` unbound), so
the arm body read garbage → SIGSEGV. Added a shared
`MLIRGenImpl::bind_enum_payload(enum_ptr, te, pvd, added)` invoked from the
tuple-element recursion in BOTH the statement (`extract_payload`) and
expression (`extract_arm_payload`) match paths. Ported the upstream test
(renamed 1-char enums per gap #4 below, `()` payloads → i32, `format!` → code
asserts); regression `tests/logos/pass/match_tuple_of_enums_binding.logos`.

Original report:
- Upstream: `pattern/tuple-enum-match-15129.rs`.
- Minimal trigger (compiles + links cleanly, then segfaults when run):
  ```
  enum Te { T1(i32), T2(i32) }
  enum Ve { V1(i64), V2(bool) }
  fn foo(t: Te, v: Ve) -> i64 {
      match (t, v) {
          (Te::T1(_), Ve::V1(i)) => 100i64 + i,
          (Te::T1(_), Ve::V2(_)) => 1i64,
          (Te::T2(_), Ve::V1(_)) => 2i64,
          (Te::T2(_), Ve::V2(_)) => 3i64,
      }
  }
  fn main() -> i32 { foo(Te::T1(0i32), Ve::V1(99i64)); return 0; }
  ```
- Symptom: `exit code 139` (SIGSEGV) at runtime. Narrowed: a single-enum match
  with payload binding (`match v { Ve::V1(i) => .. }`) works; the crash needs the
  **tuple-of-two-enums** scrutinee `match (t, v)` with payload-binding arms. The
  wildcard `_` catch-all arm is NOT required (an exhaustive 4-arm version also
  crashes). Likely the tuple-of-enums match lowering computes wrong payload
  offsets / drops/binds the wrong element. HIGH-VALUE fix candidate.

### 2. `if let _ = expr` (irrefutable wildcard in if-let) emits malformed MLIR
- Upstream: `pattern/usefulness/irrefutable-let-patterns.rs`.
- Minimal trigger:
  ```
  fn main() -> i32 { if let _ = 5i32 { return 1; } return 0; }
  ```
- Symptom: MLIR verify failure `'func.return' op expects parent op 'func.func'`
  (and, in the `while let _` form, `missing LLVMTranslationDialectInterface ...
  for op: cf.br`). The wildcard-only `if let`/`while let` produces a malformed
  region. Plain `if let Some(x) = ..` works; only the irrefutable `_` binder
  breaks.

### 3. Slice patterns inside a tuple-of-slices match — codegen type mismatch
- Upstream: `binding/match-vec-alternatives.rs`.
- Minimal trigger:
  ```
  fn classify(l1: &[i64], l2: &[i64]) -> str {
      match (l1, l2) {
          ([], []) => "both empty",
          ([_, ..], [_, ..]) => "both non-empty",
          _ => "one empty"
      }
  }
  ```
- Symptom: `'func.call' op operand type mismatch: expected operand type
  '!llvm.ptr', but provided 'i32' for operand number 0`. Matching slice patterns
  through a tuple scrutinee miscomputes operand types. (Single-slice slice-match
  not retested here; the tuple form is the trigger.)

### 4. Single-letter type name collides with stdlib generic type-params — WON'T-FIX (rename; 2026-05-21)
**Resolution: rename the user type, don't fix the compiler** (maintainer
decision 2026-05-21). The collision only arises when a *concrete* type is named
with a single uppercase letter (`T`/`I`/`R`/`V`), which violates the universal
convention every port already follows (concrete types are CamelCase words;
single letters are reserved for type-parameters). The affected ports were all
adapted by renaming (e.g. `enum T` → `enum Te`), which is the idiomatic form
anyway — no test remains blocked. Reclassified from compiler-bug/deferred to
intentional-divergence; the root-cause + fix-shape notes below are retained only
as a pointer if the namespace-isolation work is ever wanted for its own sake,
but it is NOT scheduled.

Original report:
- Surfaced while porting `pattern/tuple-enum-match-15129.rs`.
- Minimal trigger: declaring `enum T { .. }` (or `struct T`, `struct V`) at top
  level poisons stdlib lowering — emits spurious errors like
  `FilterIter field 'pred': expected fn(<error>) -> bool, got fn(T) -> bool` and
  `ObjectMap::init undefined`. Renaming the user type to a multi-char name
  (`Te`, `Clam`) makes them vanish. The user type-name namespace is not isolated
  from the stdlib generic-parameter namespace. Low-severity but a real footgun;
  worked around in every B109 port by avoiding 1-letter type names.
- **Root cause (investigated 2026-05-21):** stdlib generic templates are
  re-lowered from AST in the *user's* compilation context (per the .wr0 design).
  When the user declares a concrete type `T`, it is registered in the global
  struct/enum registry; while re-lowering a stdlib generic whose TYPE-PARAMETER
  is also named `T` (e.g. `FilterIter<T>`), `resolve_type("T")` finds the user's
  concrete `enum T` (or, here, resolves the param to `<error>`) instead of the
  template's type-parameter. Repro (no iterators needed beyond the assert
  imports): `package p; use logos.std.fmt; use logos.mem.string; use
  logos.lang.str; enum T { A(i32), B(i32) } fn main()->i32 { let _=match T::A(0i32)
  { T::A(x)=>x, T::B(y)=>y }; return 0i32; }` → `iter.logos:107: FilterIter field
  'pred': expected fn(<error>) -> bool, got fn(T) -> bool`.
- **Fix shape (NOT scheduled — see WON'T-FIX above):** if ever wanted, during
  generic (re-)lowering the active template's type-parameter names would need to
  SHADOW global concrete type names in `resolve_type` (the type-param scope
  exists for binding; it would take precedence over the global registry lookup
  for bare single-segment names). Not worth doing — rename is the resolution.

---

## missing-feature

### 5. Refutable field sub-pattern on a *plain struct* (works for enum variants)
- Upstream: `pattern/struct-wildcard-pattern-14308.rs`,
  `pattern/usefulness/nested-exhaustive-match.rs`, `binding/match-struct-0.rs`.
- Trigger: `match s { S { f0: 1i64 } => .., S { .. } => .. }` over a plain
  `struct S { f0: i64 }` → `struct pattern: refutable field sub-pattern not yet
  supported`. The identical shape on an *enum struct-variant* (`E::Foo { f: 1 }`)
  works (see landed `pattern/issue-8351-1.logos`). Gap is plain-struct-only.

### 6. `..` rest-pattern in a single-field tuple variant / nested variant patterns
- Upstream: `pattern/issue-6449.rs` (adapted + landed).
- Trigger A: `Foo::Bar(..)` over `enum Foo { Bar(i64) }` → `syntax error near '('`.
  Multi-field `Other2(..)` two-field rest also rejected (`expected 2 bindings,
  got 0` when written `(_, _)` it's fine, but `(..)` fails to parse). Worked
  around with `Foo::Bar(_)`.
- Trigger B: nested enum pattern inside a variant payload
  (`Other::Other1(Foo::Baz)`) → `nested patterns inside enum-variant payloads
  are not yet supported; bind to a name and match in the body`. Worked around by
  binding + re-matching (per the compiler's own hint).

### 7. 1-tuple destructuring pattern `(y,)`
- Upstream: `tuple/one-tuple.rs`.
- Trigger: `let (y,) = x;` (1-tuple pattern) → `syntax error near ','`. The
  1-tuple *literal* `('d',)` parses; the destructuring *pattern* does not.

### 8. `@`-binding over a whole variant pattern breaks exhaustiveness
- Upstream: `binding/match-pattern-bindings.rs`,
  `binding/match-with-at-binding-8391.rs`.
- Trigger: `match opt { a @ Some(_) => a, b @ None => b }` → `match is not
  exhaustive — missing variant(s): Some, None`. The exhaustiveness checker does
  not see through the `@` binder to the underlying variant. `b @ _` (wildcard)
  works in match, but `let a @ _ = ..` does not (let only takes struct patterns).

### 9. `mut` binding modifier inside a tuple let-pattern / fn param / `mut x @ pat`
- Upstream: `binding/mut-in-ident-patterns.rs`.
- Triggers: `let (a, mut b) = (..)` → only struct patterns allowed in `let`;
  `fn foo(&self, mut x: i64)` → `syntax error near ','`; `match v { mut z @ 32 =>
  .. }` → `syntax error near 'z'`. Plain `let (a, b)` (no `mut`) works.

### 10. String-literal patterns in `match`
- Upstream: `binding/match-str.rs`, `pattern/const-pattern-str-match-lifetime.rs`
  (the original; landed via `==` rewrite).
- Trigger: `match s { "test" => .. }` → `syntax error near '{'`. String literals
  are not accepted as match patterns. String relational/eq *operators* work, so
  the const-pattern test was reframed with `if s == "..."`.

### 11. Slice rest-binding `xs @ ..` / `ref xs @ ..`
- Upstream: `pattern/slice-pattern-recursion-15104.rs`, `pattern/issue-15080.rs`.
- Trigger: `match *v { [_, ref xs @ ..] => .. }` → `syntax error near '@'` (or
  near `xs`). Slice rest-binding with a name does not parse. `[..]` / `[_, ..]`
  unnamed rest is fine; binding the tail is the gap.

### 12. Irrefutable range / const-name range let-binding & patterns
- Upstream: `pattern/integer-range-binding.rs`, `binding/match-range-static.rs`.
- Triggers: `let 0u8..=255u8 = 0u8;` → `let supports struct patterns only`;
  `const SMIN: i64 = 1; match x { SMIN..=EMAX => .. }` → `syntax error near
  'SMIN'`. Const-name endpoints in range patterns don't parse; range patterns in
  `let` are refutable-shape-rejected.

### 13. Parenthesized binding pattern `(pat)` in a match arm
- Upstream: `binding/pat-tuple-7.rs`.
- Trigger: `match 0 { (pat) => .. }` → `syntax error near 'pat'`. A single
  parenthesized binder is not accepted (parsed as a 1-tuple pattern, which also
  fails — see gap #7).

### 14. `return` (diverging expr) as a `match` scrutinee
- Upstream: `binding/match-bot-2.rs`.
- Trigger: `fn a() -> i64 { match return 1i64 { 2i64 => 3i64, _ => 4i64 } }` →
  `syntax error near 'fn'` (the `return` keyword in scrutinee position breaks the
  parse). cf. B108 gap #11 (`if (return) {}`).

### 15. Top-level `mod { .. }` blocks
- Upstream: `shadowed/use-shadows-reexport.rs`.
- Trigger: `mod foo { pub fn f() {} }` → `syntax error near 'mod'`. Logos uses
  packages, not in-file `mod` blocks; the test's point (a local `use` shadowing a
  re-export within a module) has no Logos analogue.

### 16. Ref-pattern in a `for`-loop binding (`for &ref x in v`)
- Upstream: `lifetimes/for-loop-region-links.rs`. (Same as B108 gap #9 — ref
  patterns in the loop binding don't parse.)

### 17. Unit-typed `()` function parameter / unit payloads / unit patterns
- Upstream: `pattern/unit-pattern-matching-in-function-argument-7519.rs`,
  `pattern/usefulness/irrefutable-unit.rs`, and the `()` payloads of
  `pattern/tuple-enum-match-15129.rs` / `pattern/issue-110508.rs`.
- Trigger: `fn foo(x: ())` → `unit-typed parameters carry no information`. Logos
  rejects unit-typed params/fields by design; `()` enum payloads were rewritten
  to `i32` flags. (Borderline intentional-divergence, listed here for the trigger.)

---

## intentional-divergence

- `pattern/issue-110508.rs` — the point is `impl`-block associated constants used
  as patterns (`Foo::A1` where `const A1: Foo = ..`). Logos has no const-in-
  pattern feature; reframing it to plain variant patterns would gut the test, so
  it is skipped rather than landed in a hollow form.
- `typeck/nested-generic-traits-performance.rs` — relies on associated types
  (`type Static: 'static`, `Self::Output`) and a deeply-nested tuple `Upcast`
  chain. Associated types are absent (see B108 gap #5).
- `pattern/issue-22546.rs` — turbofish-in-pattern (`Foo::<i32>(a, b)`), `Default`
  derive, qualified `<Foo<_> as Tr>::U` projection paths. Multiple absent
  features; the pattern-path-with-type-params point doesn't survive adaptation.
- `pattern/issue-27320.rs` — `macro_rules!`-wrapped or-pattern arms; macro_rules
  is out of scope.
- `lifetimes/lifetime-inference-across-mods.rs`, `pattern/cross-crate-enum-pattern.rs`,
  `pattern/tuple-struct-cross-crate.rs` — need `//@ aux-build` cross-crate
  fixtures; the import harness builds single files only.
