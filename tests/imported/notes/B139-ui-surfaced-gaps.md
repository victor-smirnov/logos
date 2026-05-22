# B139 — UI-surfaced gaps

Batch B139 imported 24 DISTINCT rustc UI run-pass tests (pinned SHA
`4b0c9d76ae7d387229caea55cfa73c280b08b8a7`) across: tuple (1), array-slice-vec
(1), binop (1), numbers-arithmetic (1), range (1), structs-enums (3), enum (1),
closures (1), pattern (2), traits (1), coercion (1), associated-types (1),
generics (1), unboxed-closures (1), for-loop-while (1), structs (2), binding
(2), or-patterns (1). Do NOT modify the compiler/stdlib. All 24 compile + link
+ exit 0.

Suffix `-b139` on every file (global ctest-name uniqueness).

## NEW gaps surfaced

### G139-1 — labeled `break 'label <value>` does NOT carry the value (TRACTABLE)

`break 'a;` (no value) from a labeled loop works, and UNLABELED `break <value>`
from a `loop` used as an expression works (B124 loop-break-value-ex confirms).
But a LABELED break carrying a value, `'a: loop { break 'a 42i64 }` bound to a
`let v: i64`, yields GARBAGE (observed v≈256, not 42) — both at a single level
and from a nested inner `loop` breaking to the outer label. So the value operand
of a *labeled* break is dropped/mis-routed while the loop-break-value path for
the *unlabeled* form is correct.

Tractability: TRACTABLE — parallel-mapping / missing-case. The unlabeled
break-with-value codegen already produces the loop's result correctly; the
labeled-break lowering needs the same value-operand wiring threaded to the
labeled target (the label resolution path likely treats labeled break as the
no-value control-flow-only form). A labeled-break-with-value candidate
(for-loop-while/loop-labeled-break-value) was DROPPED on this. NOT a deep
(region/representation/calling-convention) gap.

### G139-2 — or-pattern NESTED inside a variant payload is a parse error (TRACTABLE)

TOP-LEVEL or-patterns work (`Test::Bar | Test::Qux => ..`, imported as
or-patterns/basic-switch-b139). But an or-pattern nested INSIDE a variant
payload — `Some(Test::Bar | Test::Qux) => ..` (the literal upstream
basic-switch.rs shape) — is a `syntax error near 'Bar'`. The or-pattern
production is only reachable at match-arm top level, not inside a constructor
sub-pattern.

Tractability: TRACTABLE — missing-case in the grammar (the sub-pattern
production inside `Variant(...)` doesn't recurse into the or-pattern rule).
Parallel-mapping to the top-level arm rule. basic-switch distilled to the
top-level or-pattern form.

**✅ FIXED 2026-05-22.** Grammar: new `pat_variant_arg` wraps a payload arg in
`PAT_OR` only when a PIPE is present (single-arg cases unchanged). Sema:
`synth_refutable_inner` now routes a bindingless `PAT_OR` inner through the same
`match synth { A | B => true, _ => false }` guard it already builds for a
bindingless inner variant (each alt must bind nothing). Regression:
or_pattern_in_variant_payload. NOTE: guarded payload arms still don't count
toward exhaustiveness (shared limitation with nested-variant-in-payload — needs
the catch-all), so the regression keeps a `_` arm; the or-pattern lowering
itself is correct (values route right).

### G139-3 — empty struct-like enum variant `Variant {}` is a parse error (TRACTABLE)

An empty-braces struct VARIANT in a declaration — `enum E { Empty4 {}, .. }`
(the upstream empty-struct-braces.rs shape) — is a `syntax error near 'enum'`
(the `{}` empty variant body fails to parse). Note the contrast: an empty
struct ITEM `struct Empty1 {}` parses + constructs fine (B137 unit-like-struct).

Tractability: TRACTABLE — missing-case in the enum-variant grammar (the
struct-variant body production rejects an empty field list, while the
struct-item production accepts it — parallel-mapping to the struct-item rule).
The empty-struct-braces candidate was DROPPED on this.

**✅ FIXED 2026-05-22.** Grammar: `variant_def` += `IDENT LBRACE RBRACE`
(struct-shape, zero payload). Sema: the construction-side struct-shape check
(`sema_expr.cpp`) gated on `payload_field_names.empty()`, conflating "no fields"
with "not struct-shape" — now gates on the declared `is_struct_shape` flag, so
`E::Empty {}` constructs + matches. Regression: empty_struct_enum_variant.

### G139-4 — `char` does not implement `Copy` (catch-up, §B)

`fn f<T: Copy>(v: T)` instantiated at `char` errors: *"type 'char' does not
implement trait 'Copy' required by parameter 'T'"*, while i64/u32/bool satisfy
`Copy`. In Rust `char: Copy`. copy-trait-implicit-copy was instantiated at
i64/u32/bool instead.

Tractability: TRACTABLE — missing `impl Copy for char` (or the auto-Copy
classification for the char primitive) in the stdlib/prelude. Same family as
the §B1 T:Copy auto-copy work; a one-impl add, not a deep gap.

**✅ FIXED 2026-05-22.** Added `impl Clone for char` + `impl Copy for char` to
stdlib/lang/clone/clone.logos (char was the lone missing primitive).
Regression: char_copy_bound.

## Re-confirmed known-open / blessed-divergence (NOT re-reported)

- **1-tuple type/literal/pattern with trailing comma** (`('c',)`, `(char,)`,
  `let (y,) = ..`) is a parse error (`syntax error near ','`). tuple/one-tuple.rs
  was DROPPED on this; tuple-index-mut-b139 (plain anonymous 2-tuple field
  mutation + `&mut x.1`) imported instead. (New observation but it's the
  1-tuple-surface family; recorded here as known-open going forward.)
- **`&x`/`&y` ref-destructure in a `let` tuple pattern** (`let (&x, &y) = ..`)
  is rejected: *"'let <pattern> = expr;' currently supports struct patterns
  only (other shapes are refutable)"*. binding/borrowed-ptr-pattern-infallible
  DROPPED; the `&ref a` MATCH-arm form (binding/borrowed-ptr-pattern-b139) works
  and was imported instead.
- **`Variant(..)` payload-ignore pattern** in a match arm (`Animal::Cat(..)`) is
  a parse error; a named `_x` binding is the working form (used in
  nested-enum-payload-b139). Same family as B135/B137 nested-payload notes.
- **turbofish-in-pattern** (`noption::some::<isize>(n)`) dropped per port
  conventions (generic-tag-values-b139 uses bare patterns).
- **type-alias name in a struct pattern** (`S2 { .. }` where `type S2 = S`) is
  rejected ("struct pattern: unknown struct 'S2'"); the CONSTRUCTION through the
  alias works, so struct-aliases-b139 matches via the base name `S` (B137
  known-open).
- **nested FnMut closures capturing an enclosing closure's environment** abort /
  fail codegen (`mlir_gen: index write: undefined 'a'`) — B107 known-open;
  for-loop-while/foreach-nested DROPPED.
- **chained postfix call on a field/expr** (`(data.compute)(arg)`) — the
  `f()(args)` family (B136); basic-newtype-pattern-b139 binds the fn-ptr to a
  local first.
- **`where F::T: Copy`** (a where-clause bounding an associated-type projection)
  is a parse error; dropped from associated-types-basic-b139 (the projection
  RETURN `-> F::T` works).
- **`Box<dyn Trait>`** → `&dyn Trait` (stack); **`==` on `Option<T>`** avoided
  (match / nonzero-ret guards) — B111/B135 known-open.

## Mechanical port rules applied (per batch conventions, not gaps)

- `package <name>;` header; `pub fn main()` → `fn main() -> i32 { …; return 0i32; }`;
  `assert!`/`assert_eq!`/`panic!`/`println!` → distinct nonzero return codes.
- `isize`/`usize` → `i64`/`u64`; all integer literals suffixed; negatives as
  `0 - n` (incl. radix-literal negation).
- `&self` → `self: &Self` / `&mut self` → `self: &mut <Type>`; `match self` →
  `match *self`.
- `#[repr]`/`#[derive]`/`Box`/`Rc`/`RefCell`/`format!`/`mem::size_of`/`static`
  facets dropped where incidental.
- closures given BLOCK bodies (`|a| { a }`); `Vec` for-loop halves rewritten to
  fixed `&[T;N]` arrays to avoid an import where the control-flow was the point.
- range-bound-to-a-local (`let r = 0..=10`) dropped (needs `use std.lang.range`);
  direct `for i in lo..=hi` / `lo..hi` loop heads kept.

## Source dups caught (checked by exact basename vs RUSTC-PROVENANCE.md)

Initially-considered sources found already-imported in earlier batches and
dropped: destructure-array-1, fixed_length_copy,
mutability-inherits-through-fixed-length-vec (the existing import covers the
`for i in &mut ints` half — array-mut-inherit-b139 tests the indexed `+=` +
`[v;N]` repeat-init core only), vec-matching, integer-literal-radix (the basic
radix import exists — integer-literal-radix-neg-b139 adds the NEGATED-literal
facet), early-return-in-binop (B12/expr-xpr4), if-generic (B136), i32-sub
(≈ i32-negate B137), arith-unsigned (B136), generic-fn (≈ generic-fn-id-gen4
B134), issue-3683 trait-default-method (B136 trait-default-method-on-primitive),
&dyn Trait dispatch (B138 — coercion-generic-dyn-param-b139 made distinct via
the GENERIC trait type-param `Trait<i64>` on the object).

## Final test set (24)

tuple: tuple-index-mut. array-slice-vec: array-mut-inherit. binop:
binops-corner. numbers-arithmetic: integer-literal-radix-neg. range:
range-inclusive-forloop. structs-enums: struct-aliases, nested-enum-payload,
class-poly-methods. enum: struct-like-variant-match. closures:
basic-closure-syntax. pattern: ignore-rest-patterns, integer-range-binding.
traits: copy-trait-implicit-copy. coercion: coercion-generic-dyn-param.
associated-types: associated-types-basic. generics: generic-tag-values.
unboxed-closures: unboxed-closures-by-ref. for-loop-while: break-continue-all.
structs: struct-fru-from-literal-base, basic-newtype-pattern. binding:
exhaustive-bool-match, borrowed-ptr-pattern. or-patterns: basic-switch.
cast: supported-numeric-cast.
