# B124 — UI-surfaced gaps

Batch B124 imported 30 run-pass tests distilled from `tests/ui/numbers-arithmetic`,
`tests/ui/expr`, and `tests/ui/lifetimes` (pinned rustc
`4b0c9d76ae7d387229caea55cfa73c280b08b8a7`). All 30 imported files
compile + link + exit 0. The gaps below were surfaced while probing distinct
features; the affected facets were trimmed from the imported tests (the tests
keep only the working subset), and each gap is recorded here for the grind.

All three are §B catch-up TODOs (must converge to Rust). No new §A blessed
divergences.

---

## G124-1 — nested-block `let` shadow leaks into the enclosing scope

**Symptom.** A `let` binding that *shadows* an outer name **inside a nested
block expression** overwrites the outer binding instead of being scoped to the
block.

```logos
let x: i32 = 10i32;
let y: i32 = { let x: i32 = 100i32; x + 1i32 };   // y == 101 (correct)
// BUG: after the block, `x` reads 100, not 10
```

Minimal repro returns `100` where Rust returns `10`. Sequential same-scope
shadow (`let x = 10; let x = 100;` → 100) is correct; calling a fn with its own
local `x` is correct (10). The bug is specific to a *block expression*
introducing a same-named `let`: the block's scope does not isolate the shadow
from the parent scope's slot.

**Feature / §.** §B — lexical block scoping for shadowing `let`. Block
expressions need their own variable scope so an inner `let` shadowing an outer
name restores the outer binding on block exit (Rust shadowing semantics).

**Where it bit.** `expr/block-scope-shadow-ex` (shadow facet dropped; the test
uses a distinct-named inner `let`, which works).

---

## G124-2 — `if { return v } else { e }` as a sub-expression types the
return branch as `void` (no never-type coercion)

**Symptom.** An `if`/`else` used **directly as a sub-expression** where one arm
is a bare `return` (a diverging expression) fails to type-check: the diverging
arm is typed `void` and does not coerce to the other arm's type.

```logos
let v: i32 = x + (if flag { return 99i32; } else { 1i32 });
//   error: if-expression branches have incompatible types: void vs i32
//   error: operator '+': right must be numeric, got void
```

**Working form.** Wrapping the `if` in a block whose tail supplies the value
type works (`{ if flag { return v; } tail }`), as does an `if` in statement
position (`if flag { return v; }` then a separate `let`). The `n + { return n }`
shape (existing `early-return-in-binop` import) also works. The gap is purely
the *never-type coercion of a bare-`return` if/else arm to its sibling arm's
type* when the `if` is the operand of an operator / call argument.

**Feature / §.** §B — the never (`!`) type / diverging-expression coercion at
`if`-branch granularity. A branch that diverges (`return`/`break`/`panic`)
should unify with any sibling-branch type.

**Where it bit.** `expr/early-return-binop-ex` (uses the block-wrapped working
form; the bare `if {return} else {e}` operand form was dropped).

---

## G124-3 — block nested as another block's tail expression loses its value

**Symptom.** A block expression nested as the **tail of another block** yields
`0` instead of the inner value; single-level `{ v }` works.

```logos
let z: i32 = { 7i32 };          // 7  (correct)
let z: i32 = { { 7i32 } };      // 0  (BUG)
let z: i32 = { { { 7i32 } } };  // 0  (BUG)
```

**Feature / §.** §B — block-as-value codegen must thread the tail-expression
value through an inner block that is itself the outer block's tail. Currently
only the outermost block's tail is wired to the result slot; an immediately
nested block-tail drops its value.

**Where it bit.** `expr/block-scope-shadow-ex` (deeply-nested-block-value facet
dropped; single-level block-as-value is exercised and works).

---

## Re-confirmed known-open / divergences (NOT re-reported as new)

- **Narrow-width integer METHODS** — `count_ones` / `leading_zeros` /
  `saturating_add` / `abs` / `rotate_left` / etc. resolve on `i32`/`i64`/`u32`/
  `u64` receivers but **fail on `i8`/`i16`/`u8`** (`method call: receiver is not
  a struct (got u8)`). All B124 method tests use the wide widths. This is the
  same method-resolution surface tracked since prior batches (narrow-width
  intrinsic-method binding); recorded here for completeness, not a new gap.
- **Float `.sqrt()` / `.floor()` / `.abs()` METHOD form** — not resolvable
  (`receiver is not a struct (got f64)`). The named free-fn forms
  `logos.lang.math::{sqrt_f64, floor_f64, abs_f64, …}` are the working channel
  and are used in `float-math-fns-na`. (Method-form float ops are the catch-up;
  the free fns are the present API.)
- **Integer-type associated constants `i32::MAX` / `i32::MIN` / `u8::MAX`** —
  `unknown enum 'i32'`. INT_MAX/MIN written as literals (`2147483647i32`) and
  INT_MIN as `-MAX - 1` per the standing convention. (Assoc-const projection
  through a primitive type-name; related to the assoc-const projection family
  G121-1.)
- **Lifetime turbofish `f::<'_>(…)`** — `unexpected type node code 131`. Per the
  standing convention, lifetime/`_` turbofish is dropped; elided/inferred calls
  work. Not counted as a gap.

## G124-1 follow-up (2026-05-22 investigation)
Block-EXPRESSION inner `let` shadow leaks to the outer scope at CODEGEN:
sema's lower_block_expr push/pop-scopes correctly, but mlir-gen's
`gen_expr_kind(EBlockExprView)` calls gen_block WITHOUT the scope_ save/restore
that the SBlock statement handler does (mlir_gen_stmt.cpp ~205) → an inner
`let x` rebinds `scope_["x"]` permanently, so the outer `x` reads the inner
alloca. Attempted fix: add the same save/restore (even narrowed to just scope_)
in EBlockExprView. REGRESSED `test_harness_coretest_batch_b50` (SIGSEGV) —
EBlockExpr is also emitted for many SYNTHESIZED blocks (match-arm values, FRU,
pattern-binding desugars, …) whose scope_ updates MUST persist into surrounding
lowering, and EBlockExpr carries no user-vs-synthesized provenance bit to
distinguish. Reverted. Proper fix: mark user `{ }`-block-expressions distinctly
(provenance flag on EBlockExpr) and restore scope_ only for those. Moderate.
