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

- **G160-1** ✅ CLOSED (2026-05-23) — `Self::Variant` resolves for ALL variant
  shapes (unit `Self::Qux`, struct `Self::Baz{..}`, tuple `Self::Bar(x)`). Fix:
  lower_enum_lit + lower_enum_lit_data resolve `Self`→the enclosing enum name
  from `current_type_params_["Self"]` (the tuple form already worked via
  lower_static_call). Regression `type-alias-enum-variants-pass`.

- **G160-2** ✅ CLOSED (2026-05-23) — a non-generic type-alias of an enum is
  usable as a value-namespace path: `type A = Foo; A::Bar(1)` / `A::Qux`. Fix:
  the static-call enum-redirect + lower_enum_lit/data peel a type-alias→enum
  before the variant lookup. Covered by `type-alias-enum-variants-pass`.

- **G160-3** ✅ CLOSED (2026-05-23) — a trait with ≥2 un-overridden default
  methods on a PRIMITIVE impl now binds `Self` for every inherited default.
  Root: collect-time default registration set `Self` only for struct/datatype
  targets, so a primitive impl left `Self` unbound; the 1st inherited default
  only worked by inheriting a leaked `Self`, the 2nd+ failed "unknown type
  'Self'". Fix: bind `Self` to a SCALAR-primitive target at collect time
  (str/enum targets deliberately excluded — they rely on a TypeVar Self for
  generic eq inference). Regression
  `default-methods-multi-inherited-primitive`.

- **G160-4** ✅ CLOSED (2026-05-23) — a byte-string pattern `b"…"` over a
  `&[u8; N]` / `&mut [u8; N]` scrutinee now auto-derefs the reference (sema
  byte-string check peels the ref; the array-slice-pattern match arm peels
  `atyp`/uses the ref value as the array base). The `&[u8]` dynamic-slice form
  stays out of scope. Restored the `&[u8; 5]` arm in `byte-array-patterns`.

- **G160-5** ✅ CLOSED (2026-05-23) — user compound-assignment overloading with
  a RHS type ≠ Self (`impl ShlAssign<u8> for Int; x <<= 1u8`). The compound-
  assign resolver looked up `<Type>__<op>_assign(&mut Self, Self)` — hardcoding
  RHS = Self. Now it looks up by the actual rhs operand type (`{&mut Int, u8}`),
  falling back to the Self-RHS signature. Restored the ShlAssign<u8>/ShrAssign<u8>
  facets in `mir-augmented-assignments`.

- **G160-6** ✅ CLOSED (2026-05-23) — redundant parens around a destructuring-
  assignment place tuple `((a, b)) = …` (≡ `(a, b) = …`). bind_list unwraps a
  single nested-tuple place when the source arity isn't 1. Restored the
  `((a,b))=` facet in `tuple-destructure`.

- **G160-7** ✅ CLOSED (2026-05-23) — STRING-literal escape decoding. The
  string-literal lexer handled `\n \t \r \\ \0 \"` but not `\xNN`,
  `\u{NN}`, or `\'` — so `"\0\x00".len()` was 5, not 2. Added `\x` (1 byte),
  `\u{…}` (UTF-8 encoded), and `\'` to the string-literal escape switch
  (mirrors the char-literal decoder). Re-imported `nul-char-equivalence` (string
  facet). Full suite 4871/4871.

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

- **G160-10** ✅ CLOSED (2026-05-23) — a `!`/never-typed expression is accepted
  in if-condition and match-scrutinee positions (`if (return x){}`,
  `match return x { … }`). Fix: if-cond (sema_stmt + sema_expr) and the
  match-scrutinee pattern checks (integer/range) now accept `Never` (like
  `Error`); codegen (gen_if / gen_match) bails when the condition/scrutinee
  already emitted a terminator (the diverging operand makes the rest dead).
  Re-imported `if-ret` (+ match-bot facet). Full suite 4868/4868.

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
