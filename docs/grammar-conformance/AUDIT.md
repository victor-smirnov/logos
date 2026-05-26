# Logos ↔ Rust grammar conformance audit

> Strategy (Victor 2026-05-25): stop point-fix bug-hunting; do a **cascade of
> refactoring + generalization**, working through the Rust Language Reference
> (`/home/victor/cxx/reference/src/*.md`) top-down. Bring the Logos grammar
> (`tools/peg_gen/grammars/logos.peg`) into conformance with the Rust grammar,
> **except** where `docs/DIVERGENCES.md` records a blessed divergence.

## Method

For each Rust grammar production (read top-down: lexical → items → statements →
expressions → patterns → types), classify the Logos counterpart:

- ✅ **conformant** — accepts the same forms.
- ⚠️ **divergence** — intentional, recorded in DIVERGENCES.md (§A) or a Logos
  addition. No action.
- ❌ **gap** — Logos is narrower / differently-shaped than Rust with no blessed
  reason. **Fix, generalizing the whole class** (not the one shape).
- 🔧 **over-enumerated** — Logos splits into many variant productions what Rust
  expresses with one general rule. Collapse + route through one sema path.

Each grammar change gates 100% (full ctest) and ships with a regression test.

## Findings

### Statements (`reference/src/statements.md`)

Rust: `Statement -> ; | Item | LetStatement | ExpressionStatement | MacroInvocationSemi`.
- `LetStatement -> let PatternNoTopAlt (: Type)? (= Expr | = Expr else Block)? ;`
- Assignment is an **expression** (`AssignmentExpression : Expr = Expr`,
  `CompoundAssignmentExpression : Expr op= Expr`) with a **place expression** LHS
  — there is exactly ONE assignment form, place generality comes free.

| Construct | Logos | Status |
|---|---|---|
| `let pat (: T)? (= e)?` | works incl. no-annotation + tuple pat (verified) | ✅ |
| `let pat = e else { }` | `let_else_stmt` | ✅ |
| assignment `place = value` | **~17 productions** (`assign_stmt`, `field_write_stmt`, `index_write_stmt`, `deref_write_stmt`, `chain_field_write_stmt`, `tuple_field_write_stmt` + `*_compound_assign_stmt` + `destructure_assign_stmt`) ending in a general `place_assign_stmt <- atom ASSIGN expr` catch-all | 🔧 over-enumerated — grammar parses via the catch-all, but the 16 specialized rules each route to a specialized sema path. Target: collapse to Rust's 2 forms over a place expr + one general place-write sema path (`gen_lvalue_addr`). Higher-risk (sema codegen must cover all place shapes); sequence after cleaner items. |
| expr-stmt with/without block | works | ✅ |

### Loops (`reference/src/expressions/loop-expr.md`)

| Construct | Logos | Status |
|---|---|---|
| `for PATTERN in iter { }` | was `IDENT`-only; **G-CONF-1 DONE** — for-each grammar gained a `pattern` alt (after the IDENT fast-path); `lower_for_each` binds a synth element var + destructures the pattern as a body prologue across all 4 paths (array/slice/&Vec/iterator). Tuple patterns by-value/by-ref/nested supported; richer forms reject cleanly. | ✅ (tuple); struct/tuple-struct pattern loop-vars are a follow-up |
| `loop` / `while` / `while let` / labeled | present | ✅ (verify details later) |

### Match (`reference/src/expressions/match-expr.md`)

| Construct | Logos | Status |
|---|---|---|
| `match EXPR { arms }` (any scrutinee) | `match p.x { }` works (verified) — the `match_head_var <- IDENT` is one alt; expression scrutinees route via the match-expression path | ✅ |

### Pattern element generality (`reference/src/patterns.md`)

- Tuple / slice / variant-payload pattern *elements* historically used
  `pat_binding` (bindings only) rather than full sub-patterns — the root behind
  the recurring "nested pattern" point-fixes (K4 / B170 / G172-11). Much has been
  widened incrementally; **audit pending** — confirm element productions allow
  arbitrary `pat_single` uniformly. (To do in the patterns pass.)

---

# Full grammar catalog (2026-05-25)

Compared the complete set of Rust Reference grammar blocks (`reference/src/**`,
the ` ```grammar ` fences) against the Logos PEG. Legend: ✅ conformant ·
⚠️ blessed divergence (DIVERGENCES.md / Logos model) · ❌ gap (fix) ·
🔧 over-enumerated (collapse).

## Paths (`paths.md`) — pervasive divergence

Rust separates segments with `::` (`a::b::c`, `Vec::<T>`, `Trait::method`,
`<T as Trait>::X`). Logos uses **`.`** for package/module paths
(`logos.mem.collections.vec`) and **`::`** only for turbofish / type-qualified
calls (`Vec::<i64>`, `Type::method`, `<T as Trait>::Item`). ⚠️ **Divergence**
(Logos path model). No `super`/`self`/`crate`/`$crate` segments (no module tree;
package system instead). Not a catch-up item.

## Items

| Rust production | Logos | Status |
|---|---|---|
| `Module` (`mod` / `mod {}`) | packages (`package x;` + `use`) | ⚠️ divergence |
| `ExternCrate` | n/a | ⚠️ divergence |
| `UseDeclaration` (`use Tree;`, `::`-paths, `*`, `{}`, `as`) | `use a.b.c;`, `use a.b.{V1,V2};`; **no `as` rename, no `*` glob, no nested `{}` groups beyond variant-import** | ⚠️ path-model divergence + ❌ minor gaps (`use … as Alias`, glob) — low priority |
| `Visibility` `pub`, `pub(crate)`, `pub(in path)`, `pub(self/super)` | `pub` only — `pub(crate)` is a **parse error** (verified) | ❌ GAP (restricted visibility) — but ties to the module-model divergence; likely map `pub(crate)`→package-private. Low priority. |
| `StructStruct` / `TupleStruct` / unit | all present incl. `struct Foo;` (G172-14) | ✅ |
| `Enumeration` + tuple/struct variants + discriminant | present; discriminant via `= Expr`/metacall/xref | ✅ (discriminant-from-arbitrary-const is const-eval ⚠️ A1) |
| `Union` | none | ⚠️/❌ — unions absent; rare, defer |
| `Trait` (`unsafe`/`auto`, supertraits, assoc items) | present | ✅ |
| `Implementation` inherent/trait/negative/unsafe | present | ✅ |
| `Function` qualifiers `const async safe unsafe extern` | `unsafe`/`extern`; `async`⚠️A4, `const`⚠️A2; **`safe` keyword** absent (extern-block niche) | ✅ for the non-divergent subset |
| `SelfParam` shorthand `&self`/`&mut self`/`self` + `self: Type` | BOTH `&self` shorthand (verified ✅) and explicit `self: &T` accepted | ✅ |
| `GenericParams`: lifetime / type / **const** params | present | ✅ |
| └ **default type param** `<T = Type>` | **parse error** (verified) | ❌ GAP → candidate fix |
| `WhereClause` incl. `for<…>` HRTB, lifetime preds | present (`where_pred`, HRTB) | ✅ |
| `TypeAlias` (+ bounds, where) | present | ✅ |
| `ConstantItem` / `StaticItem` (`static mut`) | `const`/`static`/`let` module-level; **`static mut`** (mutable global) absent | ✅ const/static; ❌ `static mut` gap (niche) |
| `ExternBlock` `extern { }` | `extern fn …;` decls only (no block grouping) | ⚠️ minor divergence |
| `MacroItem` (`macro_rules!`) | metaprog / `quote_*!` | ⚠️ divergence A3 |

## Statements (`statements.md`)

| Rust | Logos | Status |
|---|---|---|
| `LetStatement` `let pat (:T)? (= e (else block)?)? ;` | present, incl. no-annotation + tuple pat + let-else | ✅ |
| `ExpressionStatement` | present | ✅ |
| **assignment** (an *expression*: `Expr = Expr`, `Expr op= Expr`, place LHS) | **~17 statement productions** + general `place_assign_stmt <- atom ASSIGN expr` catch-all | 🔧 **over-enumerated** — biggest collapse target. Rust: 2 forms over a place expr. Logos parses via the catch-all but routes 16 specialized shapes to specialized sema. Collapse → one place-write sema path. Higher-risk; flagship generalization. |
| item-as-statement | present (nested fn, etc.) | ✅ |

## Expressions

Rust models all of these as one recursive `Expression`; Logos uses a
precedence tower (range→log→cmp→bit→shift→add→mul→cast→unary→postfix→atom) —
✅ equivalent. Form-by-form:

| Rust | Logos | Status |
|---|---|---|
| literals (char/str/raw-str/byte/byte-str/**c-str**/int/float/bool) | int/float/str/raw-str/char/byte-str/bool; **no C-string `c"…"`** | ✅ (C-string ⚠️ niche/divergence) |
| path / qualified-path expr | present (`::`-divergent) | ⚠️ |
| call / method-call / field / tuple-index / index | present | ✅ |
| **closure** `\|x\| expr` (expr body) AND `\|x\| -> T { }` | both work (verified) | ✅ |
| struct expr + `..base` | present | ✅ |
| array `[a,b]` / `[v; n]` | present | ✅ |
| tuple `(a, b)` / `(a,)` 1-tuple | present incl. `(a,)` (verified) | ✅ |
| **range exprs** `a..b`, `a..`, `..b`, `..`, `a..=b`, `..=b` (as VALUES) | only in for-loop head; `let r = 0..10` is a **parse error** (verified) | ❌ GAP — ranges aren't first-class value expressions. Needs Range/RangeInclusive/RangeFrom/… types + expr productions. Medium (stdlib + grammar). |
| if / if-let / **let-chains** (`if a && let P = e`) | if / if-let present; **let-chains** absent | ✅ core; ❌ let-chains gap (medium) |
| match (+ guard, + let-guard-chains) | present; let-guard-chains absent | ✅ core; ❌ guard let-chains (medium) |
| loop / while / while-let / for / labeled | present; `for PATTERN` ✅ (G-CONF-1) | ✅ |
| block / unsafe block / **const block** / **async block** | block, unsafe; const-block⚠️A2, async-block⚠️A4 | ✅ |
| await | ⚠️ A4 (fibres) | ⚠️ |
| break/continue/return (+ label/value) | present | ✅ |
| try `?` | present | ✅ |
| borrow `&`/`&mut`/`&&`/`&raw const/mut` | `&`/`&mut`/`&&`; `&raw` absent | ✅ core (raw-borrow niche) |
| underscore expr `_` (in assign LHS) | partial | ⚠️ verify |

## Patterns (`patterns.md`)

Rust uses full `Pattern` recursively in tuple / tuple-struct / slice / struct
element positions. Logos historically used `pat_binding` (bindings only) in some
element slots — the root of the recurring nested-pattern fixes — but the
element productions now route through `pat_single` with synth-guard machinery.
Verified working: `match p.x {1=>…}`, `W(a,b)`, nested `Some(Some(v))`,
`(a,(b,c))`, slice patterns. 

| Rust | Logos | Status |
|---|---|---|
| literal (incl. `-`) / ident (`ref`/`mut`/`@`) / wildcard / rest `..` | present | ✅ |
| reference `&`/`&&` `mut`? | PAT_REF present | ✅ |
| struct / tuple-struct / tuple / slice (full sub-patterns) | present (verified) | ✅ |
| grouped `( P )` | present (verified) | ✅ |
| **range patterns**: closed `a..=b` ✅; half-open `a..`, `..=b`, `..b` | closed inclusive ✅; half-open `a..` and `..=b` are **parse errors** (verified) | ❌ GAP — half-open range patterns |
| path pattern (const/enum unit by path) | present | ✅ |
| or-pattern at any depth | present (PAT_OR; fanned) | ✅ |

## Types

| Rust | Logos | Status |
|---|---|---|
| path type `Foo<…>` (`::`) | `.`/`::`-divergent | ⚠️ |
| ref `&'a mut T` / raw ptr `*const/*mut T` | present (verified `*mut T`/`*const T`) | ✅ |
| array `[T; N]` / slice `[T]` / `&[T]` | present | ✅ |
| tuple `()` / `(T,)` / `(A,B)` | present incl. `(T,)` (verified) | ✅ |
| never `!` / inferred `_` | present | ✅ |
| `dyn Bounds` (multi `dyn A + B`) + bare `dyn` | present (verified `&dyn A`, multi-bound parses) | ✅ |
| `impl Bounds` (multi) | `impl_type` single-bound `impl Trait<…>` | ⚠️ verify multi-bound `impl A + B` |
| bare fn `fn(A)->R` (+`unsafe`/`extern`/variadic/named params) | `fn_ptr_type` present | ✅ core |
| qualified `<T as Trait>::X` | present (G172-6) | ✅ |
| `ForLifetimes` HRTB on types | present | ✅ |

## Prioritized fix queue (gaps, generalize each)

1. **🔧 Assignment over-enumeration → place-expression collapse** (flagship; ~17→2; high payoff, higher risk).
2. **❌ Range value expressions** `a..b` etc. (medium; needs Range types + grammar).
3. **❌ Default type parameters** `<T = Type>` (small-medium).
4. **❌ Let-chains** `if a && let P = e` / match-guard let-chains (medium).
5. **❌ Half-open range patterns** `a..`, `..b`, `..=b` (small; confirmed gap).
6. Lower priority / divergence-adjacent: `pub(crate)`, `use … as Alias`, glob `use *`, `_ = expr` underscore-assign, `static mut`, unions, C-strings, `&raw`, default type params.

Done: **G-CONF-1** `for PATTERN in iter` ✅.
