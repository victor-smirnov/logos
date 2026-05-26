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

## Remaining passes (top-down)

- [ ] Lexical layer (tokens / keywords / literals / identifiers).
- [ ] Items (struct / enum / trait / impl / fn / use / mod / const / static / type-alias / generics).
- [ ] Expressions + operators (precedence, all expression forms).
- [ ] Patterns (element generality, ranges, rest, bindings).
- [ ] Types (paths, refs, slices, fn ptrs, dyn, impl Trait, qualified projection).
