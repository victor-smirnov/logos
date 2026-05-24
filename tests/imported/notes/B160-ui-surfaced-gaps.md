# B160 — rustc UI run-pass import: surfaced gaps

Batch B160 imported **24 NEW DISTINCT run-pass tests** from `tests/ui/`, mined
for FEATURE COVERAGE across FRESH / under-mined areas:
numbers-arithmetic (8), mir (2), pattern (1), destructuring-assignment (1),
char (1), cast (1), floatops (1), for-loop-while (1), match (1),
recursion (1), structs-enums (1), traits (1), type-alias-enum-variants (1),
where-clauses (1).
(`numbers-arithmetic`, `destructuring-assignment`, `char`, `type-alias-enum-
variants`, `recursion`, `where-clauses` are under-mined relative to B158's
closures/coercion/self/dst and B159's binop/generics/structs-enums/regions.)

Workflow matches B149–B159: faithful ports, `pub fn main()` → `fn main() -> i32
{ …; return 0i32; }`, isize/usize → i64/u64, integer/float literals suffixed
(float literals written with a decimal point — `2.0f64`), `assert!`/`assert_eq!`
→ early-return sentinels (distinct nonzero codes), println!/derive/Box/Rc/
RefCell/Vec/PhantomData/named-lifetimes/`#[repr]` dropped or reshaped where
incidental, nested fns/type decls hoisted to module scope, unit structs
`struct S;` → `struct S {}` (Logos needs braces), `&self`/`&mut self` →
`self: &Self` / `self: &mut Self`, by-value `self` → `self: Self`. No module
statics/consts (G153-3/G158-3): const items inlined; enum discriminant
EXPRESSIONS (`1 << 1`, `-5 >> 1`) DO evaluate via CTFE (G159-3). All 24 compile
+ link + exit 0 against the as-is `build/bin/logosc` (no compiler changes). Link
line uses `-Wl,--gc-sections` (as for B149–B159).

Coverage highlights: integer `/`/`%` with literal+variable operands (div-mod);
unsigned `< <= > >=` + div/rem across u8/u16/u32 (arith-unsigned); hex/octal/
binary literals with `_` separators + negation (integer-literal-radix); `>>`
with a shift count read out of struct fields of every integer width
(shift-various-types); i8 add/sub round-trip (i8-incr); u8 incr/decr converge
(u8-incr-decr); `0 - x` negation (i32-sub); f64 arithmetic + the full comparison
set `== < <= != >= >` (float-cmp-ops); left-associative chained `<<` + `<<`/`>>`
whose RHS is an `as`-cast value across widths (shift-various); a SIGNED
arithmetic-shift constant enum discriminant `-5 >> 1` via CTFE
(signed-shift-const-eval); value-namespace enum-variant resolution via
`Self::Bar(..)` (tuple variant) + plain `Enum::Variant` (tuple/struct/unit) +
generic `Option::Some` (enum-variant-self-and-paths); MIR call lowering across
nested-fn / inherent-method / default-trait-method / `&dyn` trait-object /
generic-callee / impl-static / UFCS trait-static `<T as Trait>::m()`
(mir-codegen-calls-dispatch); user compound-assignment operator overloading
(`+= &= |= ^= /= *= %= -=` via `*Assign` traits with Int=Self RHS)
(mir-augmented-assignments); irrefutable wildcard `if let _`/`while let _`
(irrefutable-let-patterns); `..` rest-patterns in tuple + tuple-struct match
(leading/trailing/middle/rest-then-fixed) (pat-tuple-rest); tuple
destructuring-assignment to pre-declared mutables incl. swap + rest +
longer-RHS-rest + `_` (tuple-destructure); char-literal escape decoding — NUL
three ways `\0`/`\x00`/`\u{0}`, control/quote escapes, `\xNN`/`\u{NN}`
(char-escape-equivalence); char→int, chained `as u8 as i8`, int→char,
`false as u32` (cast-char-int-chains); a FnMut driver passing a tuple to a
closure that destructures it and mutates two by-ref-captured outer counters
(foreach-put-structured); a byte-string literal pattern `b"hello"` over `[u8; 5]`
(byte-array-patterns); mutually-recursive fns threaded through an `fn(bool)->bool`
continuation (CPS) (recursion-tail-cps); an enum struct-shaped variant
`StructVariant { arg }` matched as a match expression (struct-variant-match); a
3-level supertrait chain `Baz: Bar: Foo` on a concrete i64 receiver with
overridden requireds + one inherited default per trait (supertrait-chain-methods);
a blanket `impl<T: Eq> Equal for T` + bound-constrained free fn over a primitive
via user `==` (where-clauses-blanket-eq).

## Gaps surfaced

- **G160-1** — `Self::Variant` resolves only for a TUPLE variant
  (`Self::Bar(x)`); the UNIT-variant (`Self::Qux`) and STRUCT-shaped-variant
  (`Self::Baz { .. }`) forms fail with `unknown enum 'Self'`. The plain
  `Enum::Variant` path works for all shapes. Minimal repro:
  ```
  enum Foo { Bar(i32), Baz { i: i32 }, Qux }
  impl Foo {
      fn bar() -> Foo { return Self::Bar(3i32); }   // OK
      fn baz() -> Foo { return Self::Baz { i: 1i32 }; } // unknown enum 'Self'
      fn qux() -> Foo { return Self::Qux; }             // unknown enum 'Self'
  }
  ```
  Dropped from: `type-alias-enum-variants/type-alias-enum-variants-pass.rs` (the
  `Self::Baz`/`Self::Qux` constructions; kept `Self::Bar` + explicit paths in
  `enum-variant-self-and-paths-b160`). Assessment: TRACTABLE — the `Self`→
  enclosing-enum resolution exists for the tuple-variant constructor arm in
  sema/lower; extend it to the unit + struct-variant constructor arms.

- **G160-2** — a TYPE-ALIAS of an enum cannot be used as a value-namespace path
  to construct a variant: `type FooAlias = Foo; FooAlias::Bar(1)` →
  `call to undefined static method 'FooAlias::Bar'` / `unknown enum 'FooAlias'`.
  Dropped from: `type-alias-enum-variants/type-alias-enum-variants-pass.rs` (the
  alias-construction facet). Assessment: TRACTABLE — type aliases already resolve
  in type position; the value-namespace enum-variant resolver must peel an alias
  to its target enum before looking up the variant.

- **G160-3** — a trait that leaves **two or more** default methods un-overridden
  in an impl trips a `Self`-substitution failure on the SECOND (and later)
  inherited default: `error [fn <type>__<2nd-default>]: unknown type 'Self'`.
  One un-overridden default is fine. Minimal repro:
  ```
  trait Foo {
      fn a(self: &Self) -> i64 { return 10i64; }
      fn z(self: &Self) -> i64 { return 11i64; }
      fn y(self: &Self) -> i64 { return 12i64; }   // 2nd inherited default
  }
  impl Foo for i64 { fn a(self: &Self) -> i64 { return 100i64; } }
  // i64.z() OK; i64.y() -> "unknown type 'Self'"
  ```
  Reshaped in `supertrait-chain-methods-b160` to leave ≤1 default per trait
  un-overridden. Assessment: TRACTABLE-ish — the per-impl inherited-default
  emission loop appears to lose the `Self`→concrete binding after the first
  emitted default (likely a stale/over-cleared substitution map between defaults
  in the same impl). High value: blocks faithful supertrait/default-method ports.

- **G160-4** — a byte-string pattern `b"…"` requires a `[u8; N]` scrutinee and is
  NOT auto-deref'd through a reference: `match x { b"hello" => .. }` over
  `x: &[u8; 5]` → `byte-string pattern requires `[u8; N]` scrutinee, got
  '&[u8; 5]'`. Over `[u8; 5]` (by value) it works. Dropped from:
  `match/issue-46920-byte-array-patterns.rs` (the `&[u8]` slice + `&[u8; N]`
  facets; kept the by-value `[u8; 5]` facet in `byte-array-patterns-b160`).
  Assessment: TRACTABLE — extend the default-binding-mode peeling (already done
  for struct/enum/tuple patterns, see closed `default binding modes` baghunt) to
  byte-string patterns so a `&[u8; N]` scrutinee auto-derefs.

- **G160-5** — user compound-assignment operator overloading only resolves when
  the RHS type EQUALS Self. `impl ShlAssign<u8> for Int { fn shl_assign(self:
  &mut Self, rhs: u8) … }` then `x <<= 1u8` (x: Int) →
  `compound assignment to 'x': type mismatch — expected Int, got u8`. With
  `rhs: Int` (== Self) it works. Dropped from:
  `mir/mir_augmented_assignments.rs` (the Shl/ShrAssign<u8|u16> facets; kept the
  Add/Sub/Mul/Div/Rem/BitAnd/BitOr/BitXor-Assign Int-RHS facets in
  `mir-augmented-assignments-b160`). Assessment: TRACTABLE — the compound-assign
  overload resolver hardcodes RHS==LHS; thread the trait's RHS type-arg through
  so `<lhs> op= <rhs>` selects the `*Assign<RhsTy>` impl by the rhs operand type.

- **G160-6** — extra parentheses around a destructuring-assignment place tuple
  are not seen through: `((a, b)) = (3i64, 4i64)` →
  `destructuring assignment: expected 2 places, got 1` (it treats `((a,b))` as a
  single place). The unparenthesised `(a, b) = …` works. Dropped from:
  `destructuring-assignment/tuple_destructure.rs` (the `((a,b))=` / `(((a,b)),(c))=`
  facets; kept the bare-tuple forms in `tuple-destructure-b160`). Assessment:
  TRACTABLE (minor) — the place-expression parser for assignment LHS should
  unwrap redundant parens around a tuple place.

- **G160-7** — string-literal escape sequences are NOT decoded: `"\0".len()`
  returns the SOURCE character count (e.g. `"\0\x00".len()` == 5, not 2), so a
  string literal with `\0`/`\x00`/`\u{0}` carries the literal backslash-escape
  text rather than the decoded byte. (CHARACTER-literal escapes ARE decoded
  correctly — see `char-escape-equivalence-b160`.) Dropped from:
  `str/nul-char-equivalence.rs` (all the string-length / string-equality facets;
  kept the char-literal facets as `char-escape-equivalence-b160`). Assessment:
  TRACTABLE — the lexer's string-literal scanner needs the same escape-decoding
  table the char-literal scanner already applies (`\0 \n \t \r \\ \" \xNN \u{…}`).

- **G160-8** ✅ CLOSED (2026-05-23) — matching a TUPLE OF `&Option<T>` references
  via default binding modes no longer SIGSEGVs. Root: the per-element disc test
  + payload bind passed the `&Option` REF wrapper to `resolve_tagged_enum`,
  which returned null → the C-like-enum fallback ran and under-dereferenced the
  two-level `&Enum` element (read the heap-pointer bits as the discriminant →
  wrong arm → payload deref on garbage). Fix (mlir_gen_stmt): three sites —
  pat_test Variant/VariantData, pat_bind VariantData, and the tuple-element
  nested-variant-payload bind — now resolve the tagged spec off the ENUM
  POINTEE (`TypeRef(ty).pointee()`) when the element is `&Enum`/`&mut Enum`, so
  the spec is found and the existing two-level via_ref deref applies. Re-imported
  `borrowed-ptr-pattern-option`. Full suite 4866/4866.

- **G160-9** ✅ CLOSED (2026-05-23) — a write to an outer-OUTER local from a
  2-level-nested closure no longer silently lost. Root: the capture scanner
  collected a nested closure's captures transitively (the `ClosureBox` case) but
  always via `add_capture` (BY-VALUE), so the middle closure copied `p` into its
  env; the inner closure then captured `&(middle's copy)` and wrote the copy →
  lost on middle return. Fix (sema_expr capture scanner): the `ClosureBox` case
  now checks `capture_is_mut(i)` per nested capture and routes a MUT capture
  through `mark_mut_capture` (by-ref) instead of `add_capture` — so a mutated
  variable is threaded by-ref all the way to the original alloca. Generalises to
  ≥3 nesting levels (each level re-propagates). Re-imported `foreach-nested`
  (array write + counter increment from the inner closure). Full suite green.

- **G160-10** — a `!`/never-typed expression is rejected in control-flow scrutinee
  positions: `if (return 7) { }` → `if condition must be bool, got !`;
  `match return 1 { … }` → `integer pattern requires integer scrutinee, got '!'`.
  Rust coerces `!` to any type (incl. bool) so the never-diverging condition/
  scrutinee is accepted (the body is dead). Dropped: `expr/if/if-ret.rs`,
  `binding/match-bot-2.rs`. Assessment: TRACTABLE — add `!`(never) → bool / →
  scrutinee-type coercion in the if-condition and match-scrutinee type checks
  (the operand already diverges, so the branch is provably unreachable).

## Other observations (NOT counted as new gaps — consistent with documented conventions/limits)

- **`Self::Variant` vs explicit path** — see G160-1; the explicit `Enum::Variant`
  path is the reliable form for all variant shapes today.

- **Binding-after-`@` over a struct/enum pattern with `ref`** — `ref x @ A { ref a,
  b: 20 }` → syntax error near `{`; `ref a @ Some(_)` → match-not-exhaustive
  (the `@`-bound or-arm isn't recognised as covering the variant). Bare `x @ pat`
  on simpler shapes is covered by existing `nested-patterns-at-b150`. Not ported.

- **Nested patterns inside an enum-variant payload** — `Some(&S)` / `Some(&[..])`
  give the clean diagnostic `nested patterns inside enum-variant payloads are not
  yet supported; bind to a name and match in the body`. Matches the documented
  Logos limitation (bind-then-match). Dropped: `match/struct-reference-patterns-
  12285.rs`, the nested-vec facets of `binding/match-vec-alternatives.rs`.

- **Or-pattern in a `for`-loop binding** — `for (Ok(i) | Err(i)) in v { … }` →
  syntax error near `for`. Or-patterns work in `match`/`if let`; the for-loop
  binding position doesn't accept them yet. Dropped: `or-patterns/for-loop.rs`.

- **Match binding `c2 => { … }` of a Copy aggregate** — `let mut c = (1, 7); match
  c { c2 => { c.0 = 2; assert_eq!(c2.0, 1); } }` reads `c2.0 == 2` (the binding
  aliases the scrutinee rather than copying the Copy tuple). Rust copies. This is
  a by-ref-vs-by-copy match-binding question and overlaps the default-binding-
  modes work; noted, dropped `match/issue-26996.rs` (which upstream is itself
  `ignore-test` pending #54987). Not counted as a fresh gap.

- **Enum/tuple-struct destructuring-ASSIGNMENT targets** — `TupleStruct(a, b) =
  …` / `Enum::SingleVariant(a, b) = …` → syntax error; only tuple
  `(a, b) = …` place-tuples are accepted. Dropped the tuple-struct/enum facets of
  `destructuring-assignment/tuple_struct_destructure.rs`.

- **Array length from a non-literal const expression** — `[0i64; Flopsy::Bunny as
  usize]` → syntax error near the enum path (array length must be a literal/const,
  not an `as`-cast expression). Dropped: `structs-enums/enum-vec-initializer.rs`.

- **Local type declarations inside a fn / closure body** — `fn f() { enum R { … }
  … }` and `let c = || { enum R { … } … }` → syntax error near `fn`/`{`. Matches
  the documented "hoist nested types to module scope" convention. Dropped:
  `closures/local-enums-in-closure-2074.rs`.

- **`&mut dyn Iterator<Item=T>` parameter** — `fn test(it: &mut dyn
  Iterator<Item=i32>)` → syntax error (assoc-type binding on a `dyn` parameter).
  Dropped: `iterators/for-loop-over-mut-iterator-21655.rs`.

- **String-literal patterns in `match`** — `match s { "a" => … }` → syntax error.
  Related to G160-7 (string handling). Dropped the string facets of several
  match/binding tests (`binding/match-str.rs`, `pattern/usefulness/issue-30240-
  rpass.rs`).

## Dropped tests (and the gap that caused each drop)

- `type-alias-enum-variants/type-alias-enum-variants-pass.rs` (alias-construction
  + `Self::Baz`/`Self::Qux` facets) — G160-1 + G160-2. KEPT a reshaped
  `enum-variant-self-and-paths-b160` with `Self::Bar` + explicit paths.
- `match/issue-46920-byte-array-patterns.rs` (`&[u8]`/`&[u8; N]` facets) — G160-4.
  KEPT the by-value `[u8; 5]` facet as `byte-array-patterns-b160`.
- `mir/mir_augmented_assignments.rs` (Shl/ShrAssign<u8|u16> facets) — G160-5.
  KEPT the Int-RHS compound-assign facets as `mir-augmented-assignments-b160`.
- `destructuring-assignment/tuple_destructure.rs` (`((a,b))=` nested-paren facet)
  — G160-6. KEPT the bare-tuple facets as `tuple-destructure-b160`.
- `str/nul-char-equivalence.rs` (string-length/equality facets) — G160-7. KEPT
  the char-literal facets as `char-escape-equivalence-b160`.
- `binding/borrowed-ptr-pattern-option.rs` — G160-8 ⚠️ (tuple-of-`&Option` match
  SIGSEGV). Dropped wholesale.
- `for-loop-while/foreach-nested.rs` — G160-9 ⚠️ (nested-closure outer-outer write
  lost). Dropped wholesale.
- `expr/if/if-ret.rs`, `binding/match-bot-2.rs` — G160-10 (never-type scrutinee).
  Dropped wholesale.
- `match/struct-reference-patterns-12285.rs`, `or-patterns/for-loop.rs`,
  `match/issue-26996.rs`, `structs-enums/enum-vec-initializer.rs`,
  `closures/local-enums-in-closure-2074.rs`,
  `iterators/for-loop-over-mut-iterator-21655.rs`, `binding/match-str.rs` —
  documented-limitation observations above (nested-payload patterns, for-loop
  or-bindings, Copy-match-binding alias, non-literal array length, local type
  decls, dyn-assoc-type param, string-literal match). Dropped wholesale; not
  counted as fresh gaps.

Total: **24 KEPT / passing** tests. Most surfaced gaps (G160-1..7, G160-10) are
single facets of an otherwise-portable test, so those tests were kept with the
unsupported facet reshaped; the two ⚠️ crash/miscompile gaps (G160-8, G160-9) and
a handful of documented-limitation tests were dropped wholesale.
