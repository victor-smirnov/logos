# Consts / typeck / inference gaps surfaced by Track 3 imports

Status legend: `Open` — not started; `Partial` — partial fix landed,
notes inline; `✅ Closed` — gap closed (commit, date).

| ID | Surface | Status | Gap | Surfaced by | Repro |
|---|---|---|---|---|---|
| K10-co-01 | Arithmetic expression at array-length position | ✅ Closed (2026-05-11) — via MP-mc-01 partial: `arr_fill_lit` SIZE now admits `metacall { … }`, ctfe-evaluated to integer at sema. | `[T; 2*4]` rejected at parser; the Logos idiom is `[0i64; metacall { 2 * 4 }]`. | `arithmetic-expr-in-array-len` (un-trimmed via metacall) | `[0i64; metacall { 2i64 * 4i64 }]` |
| K10-co-02 | `null` as identifier (fn name / etc.) | ✅ Closed (`77316df`, Sprint 1.1) | `null` was KW_NULL in logos.peg; now admitted as identifier in fn/field/call positions. | `unify-return-ty` (renamed `null` → `null_p`) | `fn null<T>() -> *const T { … }` |
| K10-co-03 | `mem::transmute` | Divergence — Logos has explicit `as`/deref equivalents; no transmute planned. | Multiple typeck tests rely on transmute. | `unify-return-ty` (transmute → cast) | `mem::transmute(0_usize)` |
| K10-co-04 | Divergent (`return`-only / `panic`-only) expression usable at non-void type position | ✅ Closed (2026-05-12) — `lower_block_expr` and `lower_if_expr.lower_block_last_expr` now both recognise a tail `panic(...)` call (hand-matched by callee name, no Never-type kind needed) and adopt Error as the block-expression type. The if-expr / match-arm unifier prefers the non-divergent arm's type; codegen still emits the panic call so the unreachable dummy value never executes. Tail-RETURN treatment unchanged from the earlier partial close. Sema's `stmt_always_returns` also recognises a `panic(...)` stmt or tail-expr as terminating, so fn bodies can use `panic(msg)` as the last stmt without a dummy return. Long-term answer is still a Bot/Never type kind, but the hand-recognised list (just `panic` today) covers the corpus surface. | A block whose tail diverges keeps `void` type instead of unifying with the required type. | `expr/if-panic-all` (now faithful — no dummy after panic); `expr/expr-if-panic-pass` (new — all three rust shapes: if-arm-panic / else-arm-panic / elseif-arm-panic). | `let x: i32 = if false { panic("…") } else { 10 };` works |
| K10-co-05 | `&<int-lit-with-suffix>` temp-materialisation drops the suffix → garbage load | ✅ Closed (2026-05-11) — root cause: mlir-gen's `gen_expr_kind(ELitIntView)` width switch covered I8/I16/I32/I64 / U8/U16/U32/U64 / I24/U24/I56/U56/I128/U128 / Bool / IntLit but **not Usize/Isize**. So a `Nusize` / `Nisize` literal fell to the `width = 32` default. Result: `alloca x i64` (correctly sized for the pointee Usize) filled with `c3_i32` — only low 4 bytes set, high 4 bytes uninitialised; any subsequent `load i64` from that alloca returned garbage. Fix: add Usize/Isize cases that use `g_target_pointer_bits`. Symmetric with I64/U64. Same bug surfaced anywhere a usize/isize literal flowed through ELitInt's width logic. | `&3usize` over `&usize` param. | `binding/borrowed-ptr-pattern-3` (now works directly; let-bound local workaround in the imported test is left for clarity but no longer required). | `foo(&3usize)` stores i64 in i64 alloca, returns 3 |
| K10-co-06 | Named compile-time const at array-length position (`metacall { N }` / `metacall { Type::CONST }`) | Open — plan below ("K10-co-06 — const-name folding in the metacall channel"). | The metacall array-length channel (MP-mc-01) only folds literals + operators; a `const`-name or a concrete assoc-const projection inside `metacall { … }` errors `ctfe: expression is not a compile-time constant`. Per DIVERGENCES §A1 this is a **metacall gap to fix**, not a divergence. | Re-verify session 2026-05-26 (assoc-const + array-length probes). | `const N: i64 = 3; let a: [i64; metacall { N }] = …;` and `let a: [i64; metacall { Tri::SIDES }] = …;` |

---

## K10-co-06 — const-name folding in the metacall channel (plan, 2026-05-26)

**Blessed channel.** Per `DIVERGENCES.md` §A1, const-at-type-position is a Logos
divergence whose replacement is `metacall { … }`. So the target is NOT a general
const-evaluator in the type system — it's making the *metacall* channel fold a
named const. Bare `[i64; N]` and the `::`-path form `[i64; Tri::N]` stay separate
(see "Out of scope" below); the deliverable is `metacall { N }` / `metacall {
Tri::SIDES }` / `metacall { N * 2 }`.

**Symptom (all three error `ctfe: expression is not a compile-time constant`):**
```logos
const N: i64 = 3i64;
let a: [i64; metacall { N }]          = [1i64,1i64,1i64];   // module const
let b: [i64; metacall { Tri::SIDES }] = [1i64,1i64,1i64];   // concrete assoc const
```

**Root.** `ctfe::eval_expr` (`src/compiler/ctfe.cpp`) is deliberately sema-light:
it folds `LIT_*`, `PAREN_EXPR`, `UNARY`, `BINOP` and falls through to error for
everything else — including a `VAR_REF` (a bare const name) and an
`ENUM_LIT`/`ENUM_LIT_DATA` (how `Type::CONST` parses). Sema *does* own the values
(`module_const_values_`, `assoc_const_impls_`) but never hands them to ctfe.

**Fix — add a resolver seam (sema keeps all AST-shape knowledge):**

1. **`ctfe.hpp`** — add
   `using ConstResolver = std::function<std::optional<CtfeValue>(writ::TinyMapView, writ::MemHolder*)>;`
   and an optional trailing param `const ConstResolver* resolver = nullptr` on
   `eval_expr`. (Needs `#include <functional>`, `<optional>`.)
2. **`ctfe.cpp`** — thread `const ConstResolver* R` through `do_eval` /
   `eval_unary` / `eval_binop`. In the `do_eval` final fallthrough: if `R && *R`,
   call `(*R)(node, h)` and use the value if returned; else keep the existing
   error. (Resolver must not throw — `do_eval` is `noexcept`.)
3. **Sema resolver** — new member
   `std::optional<ctfe::CtfeValue> SemaChecker::resolve_ctfe_named(TinyMapView node, MemHolder* h)`:
   - `code_of(node) == la::VAR_REF`: `name = node.get(la::NAME)`. Look up
     `module_const_values_[name]`; if present, `return ctfe::eval_expr(init,
     init.holder(), &self)` (use the stored node's **own** holder — const may be
     from another module; `TinyMapView::holder()` gives it).
   - `code_of(node) == la::ENUM_LIT || la::ENUM_LIT_DATA`: `ename = NAME`,
     `vname = FIELD`. Reuse the concrete-assoc key search already in
     `lower_enum_lit` (`src/compiler/sema_expr.cpp` ~9948): try
     `inherent::<ename>::<vname>`, then for each trait with an impl on `ename`
     try `<trait>::<ename>::<vname>`. On hit, ctfe-eval `entry.value_ast`
     (`map_of(value_ast)`, its own holder).
   - Build the recursive resolver once: `ctfe::ConstResolver self =
     [this](TinyMapView n, MemHolder* hh){ return resolve_ctfe_named(n, hh); };`
     so `metacall { N * M }` and const-of-const chains fold. (Cycle-safe: sema
     already rejects self-referential const initializers — B-ca-01.)
4. **Wire it** at the two metacall-fold sites, passing `&self`:
   - array **length** — `src/compiler/sema.cpp` ~4974 (`ctfe::eval_expr(tail, holder_)`).
   - array **fill** — `src/compiler/sema_expr.cpp` ~9862 (same call, `arr_fill_lit`).

**Tests to add** (un-trimmed, metacall form): `metacall { N }`, `metacall {
Tri::SIDES }` (trait assoc const), inherent `impl S { const C }`, and arithmetic
`metacall { N * 2 + 1 }`. Verify length + element count match at runtime.

**Out of scope (separate, deeper — keep on the gap list, do NOT fold into this):**
- **Generic `metacall { T::SIDES }`** (T a bound type-param). `resolve_type` /
  ctfe run during generic-template lowering, *before* mono substitutes T, so the
  value is unknown. Needs a symbolic array-size threaded to mono (mirror the
  `__sizeof_pack:P` mechanism at `sema.cpp` ~4995) that mono resolves once T is
  concrete, reusing the g9/B121 per-impl `<Concrete>__kassoc_<C>` accessor. The
  resolver should return `nullopt` for an abstract type-param so the diag stays
  honest until this lands.
- **Bare `[i64; N]` (non-metacall)** folds the length to `0`: the grammar routes
  a non-digit SIZE to `symbolic` (`sema.cpp` ~5015), which is only resolved for
  const-generic params, never for a top-level `const`. Either route it through
  the same resolver or keep it a divergence (idiom = `metacall { N }`).
- **`::`-path in array-length** (`[i64; Tri::N]`) fails at the **parser**
  ("syntax error near 'Tri'"): the const-generic array-length production accepts
  `ident | literal`, not a qualified path. Grammar change, tracked in
  `parser-gaps.md` if pursued; the metacall form sidesteps it.
