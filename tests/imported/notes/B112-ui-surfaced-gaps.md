# B112 — surfaced gaps (tests/ui run-pass: enum-discriminant / issues / autoref-autoderef / binding / functions-closures / return / regions / parser / reachable / recursion / fmt / let-else / or-patterns / inference)

Batch B112 mined run-pass tests from `tests/ui/` source categories. The
historically-simple categories (numbers-arithmetic, expr, match, basic
binding/pattern, etc.) turned out to be almost entirely already-imported in
B1–B111, so the new yield concentrates in scattered `issues/*` and a handful of
small fresh dirs. Upstream commit `4b0c9d76ae7d387229caea55cfa73c280b08b8a7`.

**26 tests landed:**
- enum-discriminant (2): issue-90038, issue-104519
- issues (10): issue-3895, issue-41604, issue-25279, issue-4228, issue-3847,
  issue-18845, issue-4759, issue-4875, issue-26484, issue-3149
- autoref-autoderef (1): autoderef-method
- binding (2): expr-match-unique, match-value-binding-in-guard-3291
- functions-closures (1): closure-returning-closure
- return (2): return-nil, early-return-with-unreachable-code-24353
- regions (1): owned-implies-static
- parser (2): issue-17718-parse-const, unicode-multibyte-chars-no-ice
- reachable (1): boolean-negation-in-unreachable-code-7344
- recursion (1): instantiable
- fmt (1): println-float
- let-else (1): issue-99975
- or-patterns (1): lazy-and-or

Mechanical adaptations across the batch: `isize`/`usize`→`i64`/`u64`; all int
literals suffixed; `pub fn main()`→`fn main()->i32` with explicit `return 0`;
`assert_eq!`/`assert!`/`panic!`→return-code checks (a few `println!` tests kept
with `logos.std.fmt`+`mem.string`+`lang.str` imports); `Box<T>`→plain values;
`#[repr]`+explicit discriminants dropped (enum discriminant = variant index);
`()` payloads→a dummy-field struct or a suffixed int; unit structs→named-field
structs; tuple-structs→named fields; type-qualified UFCS (`<Self>::bar`)→direct
method call; by-value `self`→`self: Self`; in-file `mod{}` wrappers + in-fn item
decls hoisted to top level; 1-char type names→multi-char; the `Option<String>`
let-else payload retyped to `Option<i64>` (see compiler-bug #1 below).

Categories below: `compiler-bug` (crash / wrong runtime value / verifier fail),
`missing-feature` (language feature absent or unparsed), `missing-stdlib`
(library API absent), `intentional-divergence` (Logos design differs from Rust).

---

## compiler-bug

### 1. `Option<String>` (heap-String payload) through let-else mis-runs
- Surfaced while porting `let-else/issue-99975.rs`.
- Minimal trigger (compiles + links; SIGSEGV / wrong run):
  ```
  use logos.lang.option; use logos.mem.string; use logos.lang.str;
  fn rr() -> Option<String> { return Some(String::from("ok")); }
  fn start() -> String {
      let Some(content) = rr() else { return String::from("none"); };
      return content;
  }
  fn main() -> i32 { let _ = start(); return 0; }   // exit 139
  ```
- Symptom: a `let Some(x) = <Option<String>> else { … }` (let-else) over a heap
  `String` payload either segfaults (exit 139) or returns the wrong value. The
  identical shape over an `Option<i64>` payload works fine (`issue-99975` ported
  with the int adaptation). Also reproduced as the second half of
  `let-else-source-expr-nomove-pass.rs` (`Some(String)` arg → wrong `len()`).
  Likely an `Option<String>`/heap-payload move/binding issue in the let-else
  desugar, not a let-else-core issue.

### 2. `println!("{:?}", (str, str))` — Debug of a tuple of `str` SIGSEGVs
- Surfaced while porting `str/debug-print-basic-tuple.rs` (SKIPPED).
- Minimal trigger (compiles + links; SIGSEGV at runtime):
  ```
  use logos.std.fmt; use logos.mem.string; use logos.lang.str;
  fn main() -> i32 { println!("{:?}", ("hi there!", "you")); return 0; }   // exit 139
  ```
- Symptom: Debug-formatting (`{:?}`) a tuple whose elements are string slices
  segfaults. `println!("{}", 1.2f64)` and plain-string `println!` both work
  (println-float + unicode-multibyte landed), so this is specific to the
  variadic-tuple Debug path over `str` elements.

### 3. Array-typed param indexed inside a generic `F: Fn([T; N])` closure — MLIR verify fail
- Surfaced while porting `issues/issue-28181.rs` (SKIPPED).
- Minimal trigger (MLIR verify abort at compile time):
  ```
  use logos.lang.ops;
  fn bar<F: Fn([u64; 1]) -> u64>(f: F) -> u64 { return f([2u64]); }
  fn main() -> i32 {
      let r = bar(|u: [u64; 1]| -> u64 { return u[0i64]; });   // 'llvm.getelementptr' operand #0 must be LLVM pointer ... but got '!llvm.array<1 x i64>'
      if r != 2u64 { return 1; }
      return 0;
  }
  ```
- Symptom: indexing an array-typed parameter (`u[0]`) inside a closure passed to
  a generic `F: Fn([T; N]) -> …` bound emits a `getelementptr` on an SSA array
  value instead of a pointer, failing MLIR verification (with a large
  OptionIter/ParseIntError dump). The non-array closure-param form
  (`F: FnOnce(u64) -> bool`, `issue-26484`) works.

---

## missing-feature

### 4. Underscores inside a `\u{…}` char/string escape do not lex
- Upstream: `issues/issue-43692.rs` (`'\u{10__FFFF}'`, `"\u{10_F0FF__}"`).
- Trigger: `let a = '\u{10__FFFF}';` → `syntax error near '='`. A plain
  `'\u{10FFFF}'` lexes fine; only the digit-group underscores break it. Test
  skipped (its whole point is the underscore separators in unicode escapes).

### 5. Float exponent literal without a decimal point does not lex
- Upstream: `lexer/floating-point-0e10-issue-40408.rs` (`0E+10`, `0e+10`),
  `numbers-arithmetic/floatlits.rs`'s `5e-11` half.
- Trigger: `let a = 1e10f64;` / `0E+10f64` → `syntax error`. `0.0e10f64` and
  `1.5e-3f64` (with a decimal point in the mantissa) lex fine. So a float literal
  needs a `.` before the exponent. floating-point-0e10 skipped; floatlits ported
  with the `5e-11` literal rewritten to `0.00000000005f64`.

### 6. Return as a call argument (`f(return)`) does not parse
- Upstream: `reachable/diverging-expressions-unreachable-code.rs`
  (`_id(return) && _id(return)`).
- Trigger: `fn f() { idb(return); }` → `syntax error near 'fn'` (the parser
  chokes on `return` in argument position). Notably `break` in argument position
  DOES parse (cf. B111 `for-loop-while/break-value`). The diverging-expressions
  test's point is exactly the return-as-arg, so skipped.

### 7. Tuple-struct / struct name as a first-class constructor function value — re-confirm B111 #12
- Upstream: `issues/issue-5315.rs` (`let f = A; f(true);`).
- Trigger: `struct Ats(bool); let f = Ats; let _ = f(true);` → `undefined
  variable 'Ats'` + `call to undefined function 'f'`. A tuple-struct constructor
  is not usable as a value. Skipped.

### 8. String literal in a `match` pattern does not parse — re-confirm B109
- Upstream: `issues/issue-22008.rs`, `issues/issue-3574.rs`.
- Trigger: `match command { "foo" => …, _ => … }` → `syntax error near '{'`.
  String-literal match arms are not parsed (a `const S: str` name pattern DOES
  work — see the landed `match/issue-11940` from an earlier batch). Skipped.

### 9. Tuple pattern with a literal element in `match` does not parse — re-confirm B109
- Upstream: `match/tuple-usize-pattern-14393.rs`.
- Trigger: `match ("", 1u64) { (_, 42u64) => …, ("", _) => … }` → `syntax error
  near '('`. A parenthesised tuple pattern with a literal sub-pattern is not
  parsed. Skipped.

### 10. Const-name / const-char range in a `match` pattern does not parse — re-confirm B109
- Upstream: `match/match-range-char-const.rs`, `parser/issues/issue-7222.rs`.
- Trigger: `match '5' { LOW..=HIGH => … }` (const `char` bounds) → `syntax error
  near 'LOW'`. Const-name range patterns are not parsed. Skipped.

### 11. Nested patterns / or-patterns inside enum-variant payloads — re-confirm B109/B111 #14
- Upstream: all of `or-patterns/{basic-switch, struct-like, search-via-bindings,
  bindings-runpass-1/2, mix-with-wild, …}.rs`, `or-patterns/let-pattern.rs`.
- Trigger: `Some(Tst::Bar | Tst::Qux)` → `syntax error near 'Bar'`;
  `Some(Tst::Bar)` (named sub-pattern) → "nested patterns inside enum-variant
  payloads are not yet supported; bind to a name and match in the body";
  `let (Ok(y) | Err(y)) = x;` → `syntax error near ')'`. The whole `or-patterns`
  category is blocked on nested-payload patterns EXCEPT `lazy-and-or` (which is
  really a `||`/`&&` short-circuit test, landed).

### 12. `_y @ Some(_)` over-variant binding fails exhaustiveness — re-confirm B109
- Upstream: `binding/match-with-at-binding-8391.rs`.
- Trigger: `match Some(1) { _y @ Some(_) => 1, None => 2 }` → `match is not
  exhaustive — missing variant(s): Some`. An `@`-binding wrapping a variant
  pattern does not register as covering that variant. Skipped.

### 13. `if (return) {}` — return as an `if` condition does not parse — re-confirm B108
- Upstream: `expr/if/if-ret.rs`.
- Trigger: `fn foo() { if (return) {} }` → `syntax error near 'fn'`. Skipped.

### 14. Deferred-init `let` (`let mut n; n = 1;`) does not parse
- Upstream: `inference/simple-infer.rs`.
- Trigger: `let mut n; n = 1i64;` → `syntax error near 'n'`. An uninitialised
  `let` (init deferred to a later assignment) is not parsed. Skipped.

### 15. `&T`→`&dyn Trait` array-element coercion not supported
- Upstream: `issues/issue-41744.rs` (`let _: &[&dyn Tc] = &[&true];`).
- Trigger: `let _arr: [&dyn Tc; 1] = [&t];` → `type mismatch — expected
  [&dyn Tc; 1], got [&bool; 1]`. Building an array of trait-object references
  from concrete-typed references does not unsize the element type. Skipped.

---

## missing-stdlib

### 16. No `Iterator::cloned`; slice `.iter().cloned().sum()` does not resolve
- Upstream: `iterators/iter-cloned-type-inference.rs`.
- Trigger: `v.iter().cloned().sum()` on a `&[i64]` → `cannot find package
  'logos.lang.iterator'` / unresolved `cloned`. No `cloned` adaptor on the slice
  iterator. Skipped.

---

## intentional-divergence / out-of-scope skips

- `enum-discriminant/{discriminant_value, discriminant_size, get_discr, niche*,
  ptr_niche, repr128*, arbitrary_enum_discriminant, issue-51582 variants}.rs` —
  `discriminant_value`/`size_of` intrinsics, `#[repr(i128/u8/…)]`, `const fn`,
  unions. No discriminant-value intrinsic and no i128. The two portable
  enum-discriminant tests (issue-90038, issue-104519) landed; the rest skipped.
- `issues/issue-38942.rs` — `const A: u64 = Enum::Variant as u64;` →
  "const initializer must be a literal expression…": no const-eval (metacall is
  the comptime channel). Skipped (its whole point is the const-from-cast).
- `issues/{issue-21922, issue-24308}.rs` — trait-name UFCS (`Add::add(x, y)`,
  `<Self as Foo>::method`). Out of scope (re B108). The plain `x + y` form works.
- `issues/{issue-3556, issue-3559}.rs` — `format!("{:?}", …)` over `HashMap`/
  recursive enums; HashMap + recursive `Box` payloads out of scope. Skipped.
- `recursion/recursive-enum-box.rs` — recursive `Box<List>` linked list; without
  Box the recursive structure degenerates and loses the point. Skipped (the
  raw-pointer `recursion/instantiable` landed instead).
- `modules/*`, `imports/*`, `non_modrs_mods/*`, `shadowed/use-shadows-reexport` —
  in-file `mod{}` + `use` re-export shadowing; Logos uses packages, not in-file
  modules. Out of scope (re B111 #5).


## Resolutions (2026-05-21)
- **#1 `Option<String>` (heap payload) via let-else — FIXED.** The let-else
  VariantData extraction always LOADed the payload field as `vp->field_types[bi]`
  (a collapsed `ptr` for a struct payload), reading the inline String's first 8
  bytes as the value. Added the inline-struct binding (bind the GEP directly +
  set var_struct_) that the match path already had. Regression
  `tests/logos/pass/let_else_option_string.logos`.
- **#2 `{:?}` of a tuple containing a `str` — ROOT-CAUSED, deferred.** A `str`
  (Slice = fat {ptr,len}) element inside a TUPLE is laid out as a collapsed
  `ptr` (8 bytes) because `tuple_llvm_type` uses `logos_to_mlir(Slice)=ptr`,
  dropping `len`. So the tuple stores only the data pointer; reading the str
  element back reconstructs a slice with garbage `len` → SIGSEGV in `write_str`.
  Int-tuple Debug and standalone-str Debug both work; struct-with-str-field
  works (different field path). Fix needs a coordinated tuple-layout change
  (tuple_llvm_type uses the inline `slice_llvm_type()` for Slice elements +
  tuple-index load + tuple construction store), analogous to the B111 #2
  variant_payload_struct fix. Deferred to a focused tuple-layout pass.
- **#3 array-param indexed inside an `F: Fn([T;N])` closure — still open** (MLIR
  verify fail: getelementptr on an SSA array value). Deferred.
