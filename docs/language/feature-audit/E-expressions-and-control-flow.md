# Category E — Expressions and control flow (audit)

Generated: 2026-05-30; spec: rust-lang/reference (local checkout at `/home/victor/cxx/reference`)

13 features audited: 6 OK, 5 WARN, 2 GAP. Logos covers the bulk of Rust expression surface — `if/match/while/for`, closures, `?`, ranges, casts, method/field/call — with mostly canonical naming. The two genuine gaps are `let-chain` / multi-`&&` if-chains (only one `&&` clause supported) and `async`/`await` (kw reserved, no productions, by-design under the blessed fibres divergence). The biggest naming-conformance issue is `Eq`/`Ord` standing in for Rust's `PartialEq`/`PartialOrd` at the primitive level. The biggest semantic gap is hard-coded `?` desugaring to `Result`/`Option`/`ControlFlow`-shaped sentinels instead of the `Try`/`FromResidual` trait surface.

---

## 1. `let` (incl. `let-else`)

**Rust nomenclature:** *let statement* (`statements.md` §statement.let); irrefutable pattern (`PatternNoTopAlt`), optional type ascription, optional `= Expr` init, optional `else BlockExpression`. `let-else` requires the else block to **diverge** to the never type.

**Logos nomenclature:** Grammar productions `let_stmt` (`tools/peg_gen/grammars/logos.peg:2015`), `let_else_stmt` (`logos.peg:2012`). AST node codes `LET = 21` (`logos.peg:108`), `LET_DESTRUCT = 123`, `LET_ELSE = 141`, `LET_PAT = 217`. Sema: `SemaChecker::lower_let` (`src/compiler/sema_stmt.cpp:1518`), `lower_let_destruct` (`sema_stmt.cpp:647`), `lower_let_else` (`sema_stmt.cpp:1398`).

**Match verdict:** OK — the user-visible keyword (`let`), the structural slot (PAT/TYPE/VALUE/BODY for the else block), and the irrefutability/refutability split are aligned with Rust spec.

**Implementation pointer:** `src/compiler/sema_stmt.cpp:1518` (`lower_let`), `:1398` (`lower_let_else`), `:923` (`lower_let_destruct_struct`), `:647` (`lower_let_destruct` tuple), `:1421-1431` (refutable-inner guard collection for let-else).

**Interactions check:**
- Patterns — OK. Irrefutable pat shapes (IDENT, tuple, struct, single-variant enum) get the dedicated alts; refutable shapes route through `LET_PAT` (`sema_stmt.cpp:923-1130`) with a refutability check.
- Type ascription (coercion site) — OK. `ann` drives `hint_call_return_type_` / `hint_enum_type_` / `hint_closure_formal_` / `hint_arr_elem_type_` / `hint_tuple_type_` (`sema_stmt.cpp:1531-1581`); int literals are widened via `widen_int_expr` (`sema_expr.cpp:1010`).
- Inference — OK; falls through to RHS type when no annotation.
- Move / Borrow (RHS consumed/borrowed) — OK. Drop-before-replace for re-let lands via B8 (commit referenced in DIVERGENCES); `decl_uninit_vars_` (`sema_stmt.cpp:1664,1687`) tracks the `let x: T;` declare-without-init case.
- Drop scope — OK. `lower_block` (`sema_stmt.cpp:551-645`) emits scope-exit drops in declaration-reverse order.
- `let-else` (diverging else) — partial. The else block is parsed (`LET_ELSE`) and the body is type-checked, but I grepped for an explicit "else block must diverge" check and found only refutability checks (`sema_stmt.cpp:1398-1440`). The spec requires the else block to evaluate to `!` (`statement.let.behavior`). WARN-leaning.
- Reborrow (let-coerce site) — OK. Documented at `sema_stmt.cpp:1722-1731` ("Rust auto-reborrows `&mut T` at COERCION sites in `let _: T = rhs`").
- Variables (mutability) — OK. `IS_MUT` flag carried via `define(name, ty, is_mut=true)`.

**Gaps / debt:**
- No explicit divergence check on the `let-else` else block — a non-diverging else compiles silently. One-line fix: walk the lowered block and assert the tail is `Never` / contains a SReturn/SBreak/SContinue/SPanic terminator.
- `let CHAIN` (let-chains in `if`/`while`) capped at exactly one `&&` clause — see `if`/`while` below.
- `let ref mut x = e;` — the `KW_REF KW_MUT` grammar alt isn't present (`logos.peg:2019-2025` only has `KW_REF IDENT`). WARN.

---

## 2. Block

**Rust nomenclature:** *block expression* (`expressions/block-expr.md`); statements followed by an optional *final operand* (tail expression). Block is a value expression evaluating to the tail's value, or unit if absent. Variants: plain block, `unsafe`, `async`, `const`, labeled.

**Logos nomenclature:** Grammar production `block` (`logos.peg:1624`); AST `BLOCK = 20`. `unsafe_block` (`logos.peg:1628`); AST `UNSAFE_BLOCK = 132`. Bare scoping at stmt position: `BLOCK_STMT = 197` (`logos.peg:1663`). `TAIL_EXPR = 223` (`logos.peg:282`) is the "trailing expression at stmt position without SEMI". Sema: `lower_block` (`sema_stmt.cpp:551`), `lower_block_expr` (`sema_expr.cpp:1150-1188`), `UNSAFE_BLOCK` expr-position handler (`sema_expr.cpp:1191-1225`).

**Match verdict:** WARN — Logos's `TAIL_EXPR` AST node has no analogue in Rust's grammar (Rust models the tail as `Statements -> Statement+ ExpressionWithoutBlock | ExpressionWithoutBlock`, i.e. an ordinary trailing `Expression`). The behaviour is right (tail value is the block value); the naming/AST shape differs.

**Implementation pointer:**
- Grammar: `tools/peg_gen/grammars/logos.peg:1624` (block), `:1663` (BLOCK_STMT for bare `{…}` at stmt position), `:1665-1668` (EXPR_STMT vs TAIL_EXPR routing).
- Sema block (stmt body): `src/compiler/sema_stmt.cpp:551-645`.
- Sema block-as-expression: `src/compiler/sema_expr.cpp:1150-1188`.
- Sema unsafe-block-as-expression: `src/compiler/sema_expr.cpp:1191-1225`.
- TAIL_EXPR -> implicit return logic: `src/compiler/sema_stmt.cpp:311-367`.

**Interactions check:**
- Statements — OK (grammar `stmt*`).
- Tail expression — OK (TAIL_EXPR + block_expr produce a value).
- Drop scope (block end) — OK. `lower_block` calls `collect_drops()` at `sema_stmt.cpp:640-642`.
- `unsafe` block — OK (`UNSAFE_BLOCK` AST + dedicated grammar + sema handler).
- `async` block — GAP. KW_ASYNC reserved (`logos.peg:358`) but no `async_block_expr` production; blessed under A4 (fibres divergence in [[ref_divergences_register]]).
- `const` block — GAP. No `const_block_expr` production; const-context computation routes through metacall (the const-fn replacement) per [[project_no_const_eval]]; document as a blessed divergence.
- Diverging tail (`!`) — partial. The TAIL_EXPR path doesn't explicitly type the block as `Never` when the tail diverges — `lower_block_expr` (`sema_expr.cpp:1150-1188`) carries the result type from the tail expression. The spec rule `expr.block.diverging` ("a block diverges if all reachable paths contain a diverging expression") is approximated via `Never`-typed expressions flowing up, but I grepped and found no formal CFG-divergence pass.
- Labeled block (`'a: { ... break 'a; ... }`) — partial. `LABELED_LOOP = 142` covers labeled loops; `expr.loop.block-labels` (Rust's labeled non-loop block) is NOT separately handled — the grammar `labeled_loop_stmt` (`logos.peg:1699-1704`) only accepts `for/while/loop`, not plain block. WARN.

**Gaps / debt:**
- TAIL_EXPR is a synthetic AST node; consider documenting that it corresponds to Rust's `Statements -> ExpressionWithoutBlock` tail (the user-visible behaviour matches Rust).
- No labeled-bare-block: `'block: { break 'block v; }` (spec §expr.loop.block-labels). Sized work item.
- No async/const blocks (blessed divergence A4 + no-const-eval; should be noted in DIVERGENCES).
- No `#![inner attributes]` on block bodies (spec §expr.block.inner-attributes) — Logos inner-annotations are module-only (`logos.peg:597,242`).

---

## 3. `if` / `if let`

**Rust nomenclature:** *if expression* (`expressions/if-expr.md`). Conditions = boolean expression OR `let CHAIN`; `LetChain -> LetChainCondition ( && LetChainCondition )*`. Multiple `&&`-separated let-chain conditions, but `||` is disallowed alongside `let`.

**Logos nomenclature:** Grammar `if_expr` (`logos.peg:2112-2123`); AST `IF = 23`. The let-chain shape parses ONE `&& <expr>` segment after the `let pat = scrut`, stored under `GUARD`. Sema: `SemaChecker::lower_if` (`sema_stmt.cpp:5201`).

**Match verdict:** WARN — supports `if let pat = e && cond` (single-`&&` chain) but NOT 2+ chained `&&` conditions (`let a = e1 && let b = e2 && cond`). Plain boolean `if a && b && c` works via `log_expr_ns`'s `(AND / OR cmp)*` fold (`logos.peg:2161-2164`).

**Implementation pointer:**
- Grammar: `tools/peg_gen/grammars/logos.peg:2112-2123`.
- Sema `lower_if`: `src/compiler/sema_stmt.cpp:5201-5345`.
- Refutable-inner guard collection (let-chain): `src/compiler/sema_stmt.cpp:5208-5266`.
- mlir-gen: `src/compiler/mlir_gen_stmt.cpp:1874-1905` (`gen_if`).
- if-as-expression: handled implicitly via stmt-to-expression promotion (`logos.peg:2269` in `primary_expr_ns`).

**Interactions check:**
- Boolean type — OK. Condition is `expr_ns` (no-struct-lit chain) at `logos.peg:2120`; sema type-checks against `bool`.
- Patterns (`if let`) — OK (`KW_IF KW_LET pattern …` at `logos.peg:2114,2116`).
- Refutability — OK. Refutable inner subpats route through `current_pat_refutable_guards_` (`sema_stmt.cpp:5208-5266`).
- Block — OK (THEN/ELSE are `block`s).
- Type unification (both branches) — OK. `lower_if` returns a block-expr whose type is unified across both branches; coercion via `compat`. Spec §expr.if.type ("must have the same type").
- Coercion to common type — partial. Logos has no formal LUB (`least upper bound`) procedure; it uses `compat`/`unify_numeric` which is asymmetric. Cross-category gap with B (type coercions §coerce.least-upper-bound).
- `else` branch — OK (recursive `if_expr / block`).
- CFG divergence (early-return) — partial. Like blocks, divergence flows through `Never` typing but there's no formal divergence pass.

**Gaps / debt:**
- Multi-`&&` let-chains beyond one segment unsupported (spec §expr.if.chains.intro). Rewrite the grammar to a `LetChain` non-terminal — sized work item, since the lowering scheme generalises.
- LUB for if-arm types (spec §expr.if.type + `coerce.least-upper-bound`). Currently first-wins / direct compat. Cross-cat with B.
- `||` mixed with let-let-chains is correctly disallowed in spec; Logos doesn't explicitly diagnose, just doesn't parse — same outcome, OK by accident.

---

## 4. `match`

**Rust nomenclature:** *match expression* (`expressions/match-expr.md`). Scrutinee + arms (`Pattern => Expr` with optional `if Guard`); spec covers exhaustiveness, refutability, or-patterns, guards, guard chains, `let`-in-guard, scrutinee place/value distinction, LUB across arms, empty match diverges to `!`.

**Logos nomenclature:** Grammar `match_stmt` (`logos.peg:1727-1730`); AST `MATCH = 82`, `MATCH_ARM = 83`. Sema: `lower_match` (`sema_stmt.cpp:7705+`), `lower_match_expr` (used through `lower_match`), exhaustiveness in `check_match_exhaustiveness` (`sema_stmt.cpp:6608`) and AST-level `ast_patterns_exhaustive` (`sema_stmt.cpp:6934`).

**Match verdict:** OK — names align (`match`, MATCH_ARM, scrutinee, exhaustiveness). Coverage is broad: enums (variant + payload), tuples, structs, slices, ranges, char ranges, or-patterns, guards (incl. let-in-guard at `LHS+GUARD+EXPR/BODY`), Hermes-literal patterns.

**Implementation pointer:**
- Grammar: `tools/peg_gen/grammars/logos.peg:1727-1743`.
- Sema: `src/compiler/sema_stmt.cpp:7705-7845` (`lower_match`), `:6608-6700` (`check_match_exhaustiveness`), `:6934-7060` (`ast_patterns_exhaustive`).
- mlir-gen: `src/compiler/mlir_gen_stmt.cpp:3429-4280` (`gen_match`).
- Scrutinee-move marker: `mark_match_scrutinee_moved` (referenced in MEMORY.md B7).

**Interactions check:**
- Patterns — OK (large surface: tuple, slice, struct, variant, range, or, ref, char/string lit).
- Exhaustiveness checking — OK. `check_match_exhaustiveness` (`sema_stmt.cpp:6608`) emits `"match is not exhaustive — missing variant(s): {}"` and similar for bool.
- Refutability — OK. Match patterns are inherently refutable; `lower_match` doesn't reject them. `let` paths route refutable shapes to LET_ELSE / LET_PAT.
- Or-patterns — OK (`PAT_OR = 139`; `pat_single PIPE pat_single` at `logos.peg:1779`).
- Guards (`if ...`) — OK (`KW_IF expr FATARROW …` at `logos.peg:1732,1734`).
- Scrutinee place (move/borrow rules) — OK (mark_match_scrutinee_moved widened to PLACE scrutinees — 51d2e29e).
- Binding modes (default `ref`/`ref mut`) — partial. PAT_REF exists (`PAT_REF = 158`) but Rust 2018+ "default binding modes" (auto-`ref` on `&Pat` against an `&T` scrutinee) need to be cross-checked under category F.
- Never type (uninhabited arms) — partial. Empty enum match (spec §expr.match.empty) — couldn't find a dedicated path that types `match e {}` as `!`; relies on the exhaustiveness check to reject. WARN.
- Drop (scrutinee/match-temp) — OK (scope drops apply).
- Enum (variant patterns) — OK.
- Guard chains (`&&`-separated guard ops) — GAP. Grammar `match_arm` only accepts a single `KW_IF expr` (no `&&` continuation, `logos.peg:1732-1737`). Spec §expr.match.guard.chains.
- `let`-in-guard (spec §expr.match.guard.let) — GAP. Single guard expr only; no `if let Pat = e` guard form.

**Gaps / debt:**
- Match guard chains (`if a && let Some(b) = c && b > 0`) absent — spec §expr.match.guard.chains.
- `let`-in-guard (`if let Some(c) = name.chars().next()`) absent.
- Empty-match → `!` isn't a special case in `lower_match`; surfaces as "non-exhaustive" instead of a divergence acceptance for known-uninhabited scrutinees.
- LUB across match arms (spec §expr.match.type) — same gap as `if`; uses `compat` rather than a formal LUB.
- Inner attributes on match (spec §expr.match.attributes.inner) — not handled at expression position.

---

## 5. Loops (`loop`/`while`/`for`)

**Rust nomenclature:** *loop expression* (`expressions/loop-expr.md`). Four flavours: `loop`, `while`, `for` (iter loop), labeled block expression. Supports labels, `break` w/ value (only `loop` + labeled block), `continue`, `IntoIterator` desugaring for `for`, `while let`, `while`-`&&` chains.

**Logos nomenclature:** Grammar `loop_stmt`/`loop_expr` (`logos.peg:1690-1696`), `while_stmt` (`:2046-2051`), `for_stmt` (`:1676-1688`), `labeled_loop_stmt` (`:1699-1704`). AST: `LOOP = 68`, `WHILE = 58`, `FOR = 70` (integer range), `FOR_EACH = 99` (collection-driven, where the IntoIterator desugar lives), `LABELED_LOOP = 142`, `BREAK = 66`, `CONTINUE = 67`. `BREAK_EXPR = 231`/`CONTINUE_EXPR = 232` for diverging-expr position. Sema: `lower_loop`, `lower_while`, `lower_for` (`sema_stmt.cpp:5790-5910` for the IntoIterator desugar).

**Match verdict:** OK — names line up (`loop`, `while`, `for`, `break`, `continue`, labels). Two AST splits — FOR (integer range) vs FOR_EACH (everything else) — are an implementation optimisation, not a divergence (Logos folds the simple `for i in lo..hi` directly into a counter loop without round-tripping through Range struct ctor).

**Implementation pointer:**
- Grammar: `tools/peg_gen/grammars/logos.peg:1676-1696,1699-1718,2046-2051`.
- Sema `lower_for` desugar: `src/compiler/sema_stmt.cpp:5790-5910` (IntoIterator → `next()`-based while-let).
- mlir-gen: `src/compiler/mlir_gen_stmt.cpp:1913-1944` (`gen_while`), `:1946-2063` (`gen_for`), `:2067-2110` (`gen_loop`).
- Loop labels: `LABELED_LOOP = 142`, label resolved through `active_loop_labels_` and `LoopBreakFrame`.

**Interactions check:**
- `break` with value (in `loop`) — OK. `BREAK` carries `LABEL`+`VALUE`; `LoopBreakFrame` (`sema_expr.cpp:1098-1110`) collects break-value types and unifies.
- `continue` — OK (`CONTINUE` w/ label).
- Patterns (`while let`, `for x in ...`) — OK. `while_stmt` first alt accepts `KW_WHILE KW_LET pattern ASSIGN cmp_expr_ns AND expr_ns block` (so `while let Some(x) = e && cond {…}` works for ONE `&&`); plain `while let` second alt. `for_stmt` last alt accepts a non-IDENT pattern (`KW_FOR pattern KW_IN …`).
- `IntoIterator` (for desugaring) — OK. `sema_stmt.cpp:5807` looks up `base + "__into_iter"`; fallback to `iter()`/`iter_mut()` (`sema_stmt.cpp:5896-5901`).
- Diverging `loop {}` (`!`) — partial. A `loop` body with no `break` should type as `!` per spec §expr.loop.infinite.diverging; Logos returns `Void` if no break. WARN-leaning; the typing affects `let x = loop {};` (impossible to write in Logos in tail position without an explicit type).
- Labels (`'label:`) — OK (`LIFETIME COLON loop_stmt/for_stmt/while_stmt` at `logos.peg:1699-1704`).
- Drop scope — OK (loop boundary marked `loop_boundary` at `sema_stmt.cpp:557`; `collect_drops_to_loop` for break/continue).
- Block — OK.
- CFG divergence — partial (same general gap).
- Multi-`&&` `while let` chains — GAP (one `&&` segment only — same restriction as `if`).
- `'block: { break 'block v; }` labeled bare block — GAP (see §2).

**Gaps / debt:**
- `loop {}` should type as `Never`/`!` — currently `Void`. One-line fix: in `lower_loop`, if no `break` recorded, set the result type to `never_t()`.
- Multi-`&&` `while let` chains.
- Spec `expr.loop.continue.in-loop-only` enforced (`sema_expr.cpp:1086`).
- `for` integer-range fast-path (FOR vs FOR_EACH) is an implementation choice — fine; document so the AST shape doesn't surprise audit readers.

---

## 6. Closure

**Rust nomenclature:** *closure expression* (`expressions/closure-expr.md`). Optional `async`/`move`, `|params|` (with patterns + optional types), optional `-> Ret`, body expr or block. Captures inferred (or forced by `move`). Type implements `Fn`/`FnMut`/`FnOnce`; non-capturing closures coerce to `fn` pointer; async closures return `Future`.

**Logos nomenclature:** Grammar `closure_expr` (`logos.peg:2720-2755`); AST `CLOSURE_EXPR = 109`, `CLOSURE_TYPE = 125`. `IS_MOVE` flag on the AST. Sema: `lower_closure_expr` (`sema_expr.cpp:12200-12471`); type kind `LogosType::Kind::Closure` (`sema.hpp:61`); FnPtr at `sema.hpp:69`. Fn-trait family: `Fn`/`FnMut`/`FnOnce` recognised via `is_fn_family` (`sema.cpp:3316-3320`).

**Match verdict:** OK — keyword (`move`), pipe-syntax, `-> Type`, expression-body / block-body variants all aligned with Rust's grammar. Closure type kind named `Closure` matches Rust's "closure type".

**Implementation pointer:**
- Grammar: `tools/peg_gen/grammars/logos.peg:2720-2755`.
- Sema: `src/compiler/sema_expr.cpp:12200-12471` (`lower_closure_expr`), `:12473-12700` (capture analysis incl. RFC-2229 phase-2 narrow capture).
- LIR: `EClosure` at `include/logos/compiler/lir.hpp:653`, `EClosureCall` at `:361`, `EFnPtrCall` at `:367`.
- mlir-gen: `src/compiler/mlir_gen_expr.cpp:4282` (`gen_closure`).
- Fn-trait dispatch: `is_fn_family` at `src/compiler/sema.cpp:3316-3320`, used in `sema_collect.cpp:1043-1051` for bound resolution.

**Interactions check:**
- Closure types — OK (`Kind::Closure`).
- `Fn`/`FnMut`/`FnOnce` — OK by name.
- `move` keyword — OK (`KW_MOVE` token + `IS_MOVE` flag).
- Capture analysis (RFC-2229) — OK; phase-2 narrow-field capture landed (MEMORY.md `cda40eb2` / `7bf16f60`).
- Lifetimes — partial. Captures borrow enclosing locals; lifetime tracking through closure env is approximated (see Lifetimes §5 of A audit).
- Generics (closures-as-impl-Trait) — partial. `impl Fn(…)` parses (`impl_type` at `logos.peg:1239`); routed through `is_fn_family`.
- Trait objects (`dyn FnMut`) — OK. Box<dyn Fn(…)> works (G167-3 hint-peel at `sema_expr.cpp:12242`).
- Send/Sync — N/A (Logos has no Send/Sync auto-trait; cross-cat H).
- Async closures — GAP. Grammar has no `KW_ASYNC` in `closure_expr` (only `KW_MOVE`); KW_ASYNC reserved (`logos.peg:358`).
- Function pointer coercion (non-capturing only) — OK. `try_coerce_closure_to_fnptr` (`sema_expr.cpp:3472,3500,7680`) handles the coercion.

**Gaps / debt:**
- Async closures (blessed divergence A4).
- `impl FnOnce` -> `dyn FnOnce` for "called once" callers — coverage of `FnOnce` is shallower than `Fn`/`FnMut` (search the test suite).
- The `FnMut`/`FnOnce` vs `Fn` selection rule (spec `expr.call.trait`) is implemented via `is_fn_family` but doesn't formally choose the most-specific trait based on capture mutability — relies on the user's `dyn Fn*` annotation.

---

## 7. Try `?`

**Rust nomenclature:** *try propagation expression* (`expressions/operator-expr.md` §expr.try). Desugars to `match Try::branch(expr) { Continue(v) => v, Break(r) => return FromResidual::from_residual(r) }`. Supported for `Result`, `Option`, `ControlFlow`, `Poll<Result<…>>`, `Poll<Option<Result<…>>>` and any user type impl'ing `Try` (unstable).

**Logos nomenclature:** Grammar production: postfix `QUESTION` in `atom`/`atom_ns` (`logos.peg:2250,2421`). AST `TRY_EXPR = 122`. Sema lowering: `case la::TRY_EXPR` (`sema_expr.cpp:847-977`). LIR `ETry` (`lir.hpp:459`).

**Match verdict:** WARN — keyword/operator surface matches Rust, but the lowering is **hard-coded** to `Result`/`Option` (by name match on enum variants `Ok`/`Err`/`Some`/`None`). No `Try` / `FromResidual` trait surface; no `ControlFlow` support; no user-type extensibility.

**Implementation pointer:**
- Grammar: `tools/peg_gen/grammars/logos.peg:2250-2251,2421-2422`.
- Sema: `src/compiler/sema_expr.cpp:847-977`.
- Heterogeneous-E desugar (`Result<T,Ei> → Result<U,Eo>`): `sema_expr.cpp:888-975` (synthesises `From::from(e)` via `<base_target>__from` symbol).
- LIR: `include/logos/compiler/lir.hpp:459`.

**Interactions check:**
- `Try` trait (`Try::branch`, `FromResidual`) — GAP. No `Try` trait in stdlib; hard-coded name match.
- Functions returning `Result`/`Option` — OK.
- Type inference — partial; the function return type drives ok/err disc lookup.
- Error conversion (`From`) — OK for the heterogeneous case (sema_expr.cpp:909+). Looks up `<E_outer>__from` and emits the call.
- Diverging on `Err`/`None` — OK (the `return Err(…)`/`return None` synthesis at `sema_expr.cpp:962-963`).
- `try` blocks — GAP. No `try { … }` block syntax.
- `ControlFlow` — GAP. No support; spec §expr.try.restricted-types lists it as supported.
- `Poll<Result<...>>` shape — GAP.

**Gaps / debt:**
- Re-base `?` on a `Try` trait in stdlib (would replace the name-match-on-Ok/Err/Some/None with a trait-method dispatch).
- Add `ControlFlow<B, C>` to the restricted-types set.
- Add `try { … }` block.

---

## 8. Async / await

**Rust nomenclature:** *await expression* (`expressions/await-expr.md`), *async block* / *async fn*. `expr.await` desugars to a `match … { Ready(r) => r, Pending => yield }` loop over `IntoFuture::into_future(expr)`.

**Logos nomenclature:** `KW_ASYNC` and `KW_AWAIT` are reserved tokens (`tools/peg_gen/grammars/logos.peg:358-359`); the comment explicitly says "Reserved (no grammar use yet) — kept for stackless-coroutine path on wasm32/64 where threads/context-switch aren't available."

**Match verdict:** GAP — feature absent by design. Blessed divergence A4 (fibres replace async); the Logos runtime model is green-stack fibres at language level (per `feedback_think_in_rust.md`).

**Implementation pointer:** n/a — no productions, no AST nodes, no sema handlers.

**Interactions check:** n/a — feature absent. Cross-references in MEMORY.md ([[project_box_unsized_customdst]], [[ref_divergences_register]]) consistently treat async as the blessed divergence.

**Gaps / debt:**
- The "no async/await" status should be a formal entry in `docs/DIVERGENCES.md` under A4 — currently only referenced in scattered MEMORY entries.
- WASM stackless-coroutine path (the rationale for keeping the keywords reserved) is not yet specified; if/when revisited, the `Future` / `Pin` / `IntoFuture` traits would all need landing too.

---

## 9. `return`

**Rust nomenclature:** *return expression* (`expressions/return-expr.md`). `return Expr?`. Diverging; type `!`.

**Logos nomenclature:** Grammar `return_stmt` (`logos.peg:2053-2054`) for stmt-position, and `RETURN_EXPR` in `primary_expr_ns` / `primary_expr` (`logos.peg:2450-2451`) for expr-position. AST `RETURN = 22`, `RETURN_EXPR = 233`. Sema: `lower_return` (`sema_stmt.cpp:2422`); expr-position diverging handler at `sema_expr.cpp:1045-1116`.

**Match verdict:** OK — keyword matches; expression-position return supported; diverging type handled via `never_t()` block_expr wrap (`sema_expr.cpp:1113`).

**Implementation pointer:**
- Grammar: `tools/peg_gen/grammars/logos.peg:2053,2450-2451`.
- Sema: `src/compiler/sema_stmt.cpp:2422` (`lower_return`), `:311-367` (TAIL_EXPR-as-implicit-return at fn-body level).
- Sema expr-position: `src/compiler/sema_expr.cpp:1045-1116`.
- mlir-gen: `src/compiler/mlir_gen_stmt.cpp:1726` (`gen_return`).

**Interactions check:**
- Function return type (coercion site) — OK. `compat(rval->type, ret_type_)` check at `sema_expr.cpp:1076-1082` + `sema_stmt.cpp:354-358`.
- Diverging (`!`) — OK (`never_t()` typed block_expr wrapper).
- Drop (scope unwind) — OK. `collect_all_drops()` at `sema_stmt.cpp:595` emits the drops before the SReturn, with the return value hoisted into a temporary first (`:599-609`).
- Closures (closure return) — OK. `lower_closure_expr` saves/restores `ret_type_` (`sema_expr.cpp:12364-12421`); G156-7 drop-boundary at closure body so a `return` only drops the closure frames.
- CFG divergence (if-merge) — partial (general divergence-tracking gap).
- Implicit tail-as-return — OK at fn-body level (`tail_as_return_` gate at `sema_stmt.cpp:330-361`).

**Gaps / debt:**
- Implicit-return via TAIL_EXPR is a Logos-only AST node (Rust uses the block-expr-tail); behaviour matches but parsers / external tooling should be aware.
- Return-type elaboration with `impl Trait` / RPIT not exercised here (cross-cat with B).

---

## 10. Field access / Method call / Call

**Rust nomenclature:** *field expression* (`expressions/field-expr.md`), *method-call expression* (`expressions/method-call-expr.md`), *call expression* (`expressions/call-expr.md`). Field is a place expression; method call has the receiver-deref + autoref candidate ladder; call uses `Fn`/`FnMut`/`FnOnce` for non-function types.

**Logos nomenclature:** AST `FIELD_READ = 54`, `FIELD_WRITE = 55`, `METHOD_CALL = 56`, `CALL = 30`, `STATIC_CALL = 98`, `GENERIC_CALL = 91`, `INVOKE_EXPR = 230`, `TUPLE_INDEX = 102`. Grammar: postfix chains in `atom`/`atom_ns` (`logos.peg:2391-2423`, `:2221-2252`). Sema: `lower_call` (`sema_expr.cpp:2172`), `lower_method_call` (`:6051`), `lower_generic_call`, `lower_static_call`, `lower_invoke_expr`.

**Match verdict:** OK — names align (METHOD_CALL, CALL, FIELD_READ/WRITE). The method-call ladder approximates Rust's "candidate receivers" (`expr.method.candidate-receivers`).

**Implementation pointer:**
- Grammar: `tools/peg_gen/grammars/logos.peg:2391-2511`.
- Sema field: lower_field via `case la::FIELD_READ` (referenced throughout `sema_expr.cpp`).
- Sema method: `src/compiler/sema_expr.cpp:6051-6107` (entry incl. user-Deref autoderef and `Vec::get` move-out diagnostic), `:7185-7330` (autoref ladder).
- Sema call: `src/compiler/sema_expr.cpp:2172+` (`lower_call`), `:2246-2280` (Fn-family dispatch for callable values).
- LIR: `EMethodCall`, `ECall`, `EClosureCall`, `EFnPtrCall` (`include/logos/compiler/lir.hpp:117,361,367`).

**Interactions check:**
- Struct/Enum/Tuple/Union (field-access kinds) — partial. Struct/Tuple OK (FIELD_READ + TUPLE_INDEX). Enum field access via pattern only (no `e.field` on enums — Rust same). Union — Logos has no `union` keyword (cross-cat with C); GAP.
- Trait methods — OK.
- Autoref (`self`/`&self`/`&mut self`) — OK (`sema_expr.cpp:7219-7240` builds {direct, &recv, &mut recv} types).
- Auto-deref (Deref/DerefMut) — partial. `Deref` autoderef in method resolution at `sema_expr.cpp:6093-6107` (bounded loop, immutable Deref). `DerefMut`-driven autoderef for `&mut self` methods is "a separate follow-up" (comment at `sema_expr.cpp:6092`). WARN.
- Method resolution (inherent → trait) — OK.
- UFCS (`Trait::method`) — partial. `<T as Trait>::method(...)` UFCS grammar at `logos.peg:2923-2924` lowers to STATIC_CALL but the trait qualifier is "consumed and dropped" (comment at `logos.peg:2922`) — type dispatch already resolves the method. WARN: the Rust UFCS distinguishes overlapping trait methods; Logos can't because it doesn't consume the trait disambig.
- Generics (turbofish) — OK (`call_expr` and `atom` accept `IDENT::<T>(args)` and `.method::<T>(args)`).
- Visibility — OK (cross-cat I).
- Closures (call as Fn) — OK (`sema_expr.cpp:2246-2280`).
- Operator overloading (`+` → `Add::add`) — OK (see §11).
- Implicit borrow in `==`, method receivers — OK (`expr.cmp.place`).

**Gaps / debt:**
- `DerefMut` autoderef in method resolution for `&mut self` methods through a `DerefMut`-impl smart pointer.
- UFCS dispatch on the trait qualifier is dropped — overlapping trait methods on the same type can't be disambiguated via UFCS today.
- Union field-access (cross-cat C).
- `&raw const` / `&raw mut` operators (spec §expr.borrow.raw) — absent; tracked under category A audit gaps too.

---

## 11. Operator overloading

**Rust nomenclature:** Operators desugar to traits in `core::ops` and `core::cmp` (`expressions/operator-expr.md`). Arithmetic: `Add/Sub/Mul/Div/Rem`. Bitwise: `BitAnd/BitOr/BitXor/Shl/Shr`. Unary: `Neg/Not`. Compound: `*Assign` siblings. Comparison: `PartialEq` (`eq`/`ne`), `PartialOrd` (`lt`/`le`/`gt`/`ge`). Index: `Index`/`IndexMut`. Deref: `Deref`/`DerefMut`. Drop. `&`/`&&`/`!`/`-`/`*` borrow/deref — NOT overloadable for `&`/`&mut`.

**Logos nomenclature:**
- Arithmetic traits: `Add`/`Sub`/`Mul`/`Div`/`Rem` (`stdlib/lang/ops/ops.logos:24-38`).
- Bitwise: `BitAnd`/`BitOr`/`BitXor`/`Shl`/`Shr`/`Not` (`ops.logos:51-62`).
- Compound: `AddAssign`/`SubAssign`/`MulAssign`/`DivAssign`/`RemAssign`/`BitAndAssign`/`BitOrAssign`/`BitXorAssign`/`ShlAssign`/`ShrAssign` (`ops.logos:79-88`).
- Comparison: `Eq` (with `eq`+`ne` defaults; `stdlib/lang/cmp/cmp.logos:13-18`); `PartialEq` (`eq`+`ne` defaults; `cmp.logos:29-32`); `Ord` (provides `cmp`); `PartialOrd` is "kept as an empty marker" (`cmp.logos:33-40`).
- Sema operator dispatch: `lower_binop` at `sema_expr.cpp:1346`, with the mapping table at `:1544-1562` (`Add`/`Sub`/`Mul`/`Div`/`Rem`/`BitAnd`/`BitOr`/`BitXor`/`Shl`/`Shr` + `Eq`/`Ord`).
- Compound dispatch: `lower_compound_assign` at `sema_stmt.cpp:1972`; mapping at `:1958-1960` (`AddAssign`+`add_assign`, …).
- Unary operator overloading: `sema_expr.cpp:2091-2136` (`Neg`/`Not`).

**Match verdict:** WARN — naming mostly aligned for ops, but the comparison-trait hierarchy diverges: Logos uses `Eq` for `<`/`<=`/`>`/`>=` (mapping at `sema_expr.cpp:1559-1562` → `trait_name = "Ord"; method_name = "lt"`), and the primitive impls (`impl Eq for i32` at `cmp.logos:50-61`) match what Rust calls `PartialEq`. Rust spec maps `<` to `PartialOrd::lt`. The result is that user code writing `impl Ord for X` to enable `<` is correct in Logos but would be `impl PartialOrd for X` in Rust.

**Implementation pointer:**
- Sema binop trait dispatch: `src/compiler/sema_expr.cpp:1540-1602`.
- Sema unary: `src/compiler/sema_expr.cpp:2091-2136`.
- Sema compound: `src/compiler/sema_stmt.cpp:1956-1970,2094-…` (`lower_place_compound_assign`).
- TypeVar-`Eq` desugar for generic `==` / `!=`: `sema_expr.cpp:1614-1656`.
- Enum-`Eq` desugar: `sema_expr.cpp:1665+`.
- Stdlib traits: `stdlib/lang/ops/ops.logos`, `stdlib/lang/cmp/cmp.logos`, `stdlib/lang/cmp/ord.logos`.

**Interactions check:**
- Operator traits (`Add`, `Index`, `Deref`, `Drop`, `PartialEq`, `Ord`, …) — partial. `Index`/`IndexMut` are referenced indirectly through `try_index_mut_assign` (MEMORY.md place-writer flagship), `Deref`/`DerefMut` via auto-deref. `Drop` covered under A audit. WARN: the `PartialEq` ↔ `Eq` name shuffle described above.
- Auto-deref — OK.
- Coercions — OK; widen + autoref applied around the dispatch.
- Patterns (`==` in match guards) — OK indirectly (guards reuse `==`).
- Const eval (some ops in const) — N/A (no-const-eval divergence; metacall instead).

**Gaps / debt:**
- **`Eq` covers Rust's `PartialEq` semantics**, `Ord` covers `PartialOrd` for `<`/`<=`/`>`/`>=`. Rename or alias the stdlib traits so that ported Rust code's `impl PartialOrd for X` finds a binding. SL-sl-02 (`cmp.logos:20-28`) already notes this. WARN.
- `PartialOrd` is an empty marker (`cmp.logos:40`); float `<`/`<=` therefore can't go through a real partial-cmp trait. Documented in stdlib.
- The `partial_cmp(&self, other: &Self) -> Option<Ordering>` shape isn't implemented (`cmp.logos:34-39`).
- `Index`/`IndexMut` — verify the trait names match Rust (couldn't find a clear `pub trait Index` declaration; if absent, it's a naming gap). Cross-cat C / D.
- Unary `&`/`&&` overloading attempted via the `AND  unary_expr_ns` alt (`logos.peg:2218`) — Rust forbids overloading borrow/raw-borrow.
- `&raw const` / `&raw mut` operators (spec §expr.borrow.raw) — absent.

---

## 12. Range

**Rust nomenclature:** *range expression* (`expressions/range-expr.md`). Six productions: `a..b` (Range), `a..` (RangeFrom), `..b` (RangeTo), `..` (RangeFull), `a..=b` (RangeInclusive), `..=b` (RangeToInclusive). Types in `std::ops::Range*`. Used as iterators (`for i in 1..11`) and slice indices.

**Logos nomenclature:** Grammar `range_expr` (`logos.peg:2139-2151`); AST `RANGE_EXPR = 112`. INCLUSIVE flag. Sema: `case la::RANGE_EXPR` at `sema_expr.cpp:979-1038`. Stdlib: `RangeI32`/`RangeI64` (`stdlib/lang/range/range.logos:15-22`). `for i in lo..hi` uses the dedicated FOR AST node (`logos.peg:1676-1681`) for the integer-range fast path; value-position ranges go through the stdlib struct.

**Match verdict:** WARN — all six grammar productions present, BUT the value-position type is `RangeI32` / `RangeI64` (not `Range<T>` over a generic `T`). Spec uses a single generic `std::ops::Range<Idx>`; Logos has only i32/i64-specialised forms.

**Implementation pointer:**
- Grammar: `tools/peg_gen/grammars/logos.peg:2139-2151`.
- Sema: `src/compiler/sema_expr.cpp:979-1038`.
- Stdlib: `stdlib/lang/range/range.logos:15-80+`.

**Interactions check:**
- `Range`/`RangeInclusive`/`RangeFrom`/`RangeTo`/`RangeFull` types — partial. `RangeI32` / `RangeI64` only (no `RangeFrom<T>`, `RangeTo<T>`, `RangeFull`, `RangeInclusive<T>` stdlib types). Spec table at `expr.range.behavior` lists six distinct types. GAP for `RangeFrom` / `RangeTo` / `RangeFull` / `RangeInclusive` / `RangeToInclusive`.
- `for` loop (IntoIterator) — OK for the `for i in a..b` fast-path; the value-position range lowers to `RangeI32::next()` / `RangeI64::next()` via the IntoIterator desugar (`stdlib/lang/range/range.logos:24-31`).
- Slice indexing (`&v[a..b]`) — OK via the RANGE_EXPR-as-index path in `sema_expr.cpp:8882-9003`.
- Patterns (range-pat) — OK (`PAT_RANGE = 156`, `PAT_CHAR_RANGE = 229`). Cross-cat F.

**Gaps / debt:**
- The full range-type family (`RangeFrom<T>` etc.) — Logos has only `RangeI32`/`RangeI64`. Open-ended forms (`a..`, `..`, `..b`) parse but only become useful in slice indexing.
- Generic `Range<T: Step>` (Rust's actual shape) would let user types use range expressions.
- Inclusive value-position ranges (`(1..=5).collect()` style) — sema rewrites `hi` to `hi+1` (`sema_expr.cpp:1013-1016`), which is correct for iteration but wouldn't survive a `Range::contains` or `Range::end` user-visible read.

---

## 13. Cast `as`

**Rust nomenclature:** *type cast expression* (`expressions/operator-expr.md` §expr.as). Numeric, enum (discriminant), bool/char-to-int, u8-to-char, ptr-to-ptr (compat rules), ptr-to-int, int-to-ptr, fn-item-to-fn-ptr, closure-to-fn-ptr (no-capture only), array/ref-to-ptr.

**Logos nomenclature:** Grammar `cast_expr` (`logos.peg:2372-2375`); AST `CAST = 69`. Sema: `lower_cast` (`sema_expr.cpp:469-695`).

**Match verdict:** OK — `as` keyword and CAST node match Rust; numeric/enum/bool/char/ptr cast paths are present. Logos additionally handles `slice as <T>[]` for Hermes typed containers — a Logos extension, blessed divergence territory.

**Implementation pointer:**
- Grammar: `tools/peg_gen/grammars/logos.peg:2372-2375` (full expr chain), `:2208-2211` (no-struct-lit chain).
- Sema: `src/compiler/sema_expr.cpp:469-695`.
- Helpers: `widen_int_expr` (`sema_expr.cpp:1010`), enum c-style cast block (`:644-657`).

**Interactions check:**
- Numeric coercions (truncation/extension) — OK (`tgt_scalar` switch table at `sema_expr.cpp:623-643` covers `i8..i128`, `u8..u128`, `usize`/`isize`, `f32`/`f64`, `char`, `Bool`, `Ptr`).
- Pointer casts — partial. Sized-to-sized OK; unsized-to-sized (slice → thin ptr per `expr.as.pointer.discard-metadata`) not specifically tested; trait-object metadata compatibility rules (`expr.as.pointer.unsized.trait`) — would require principal-trait match + auto-trait/lifetime rules; I grepped and found no implementation.
- `unsafe` (some ptr casts) — partial. The `unsafe` block makes ptr deref unsafe; cast itself is not gated.
- Coercions (interplay) — OK at the call/arg level via `coerce_arg_to_param`.
- Const eval — N/A.
- Enum-to-int — OK (`src_is_cstyle_enum` predicate at `:645-657`).
- bool/char → int — OK.
- u8 → char — partial. Truncating cast through the `tgt_scalar` table handles it numerically; spec `expr.as.u8-as-char` requires producing a valid char (not just numeric value).
- fn-item → fn-ptr — OK (via `try_coerce_closure_to_fnptr` and the `K::FnPtr` target arm).
- closure → fn-ptr (no-capture) — OK; verified by `try_coerce_closure_to_fnptr`.
- Array/ref → ptr — partial. Verify via test.

**Gaps / debt:**
- Trait-object ptr-to-ptr cast rules (principal-trait identity, auto-trait removal/super-only addition, lifetime shortening) — absent. Spec §expr.as.pointer.unsized.trait.
- Cast forbidding rule for `enum Drop` (spec §expr.as.enum.no-drop) — couldn't grep an explicit reject.
- `u8 as char` invalid scalar-value rejection (`expr.as.u8-as-char` requires a *valid* char code-point).
- `&m₁ [T; n] → *m₂ T` array-to-pointer mut-only-tightening rule (footnote `[^lessmut]`).

---

## Cross-category gaps

- **B (Type system primitives) — `!` / Never type integration with `loop {}`, empty `match`, blocks.** `Never` exists as `Kind::Never`; flow-typing through control-flow isn't a separate pass. Cross-cats with B.
- **B / F — LUB (`coerce.least-upper-bound`).** `if`-arms, `match`-arms, `loop` break-values, labeled-block break-values all use ad-hoc `compat`/`unify_numeric` instead of a formal LUB procedure. Needed for the spec rules `expr.if.type`, `expr.match.type`, `expr.loop.break-value.type`, `expr.loop.block-labels.type`.
- **C (Items) — `union` field access.** No `union` keyword / `UNION` AST node; field expression spec mentions union.
- **C — `&raw const` / `&raw mut` operators.** Cross-cat with A (Borrow) and B (Raw pointer). Spec §expr.borrow.raw not implemented.
- **F (Patterns) — let-chain support, match guard chains, match guard `let`.** Surfaces in `if let` (§3), `while let` (§5), `match` guards (§4); the underlying grammar capacity for one `&&` segment is the same bottleneck.
- **G (Memory / safety) — `DerefMut`-driven method autoderef** (§10).
- **M (Const evaluation) — `const { ... }` block** (§2). Blessed divergence (metacall replaces).
- **O (Async)**: blessed divergence A4 (§8). Document explicitly in DIVERGENCES.md.

---

## Recommended next moves

Single-session work items, ordered by impact:

1. **Add `let-chain` multi-`&&` support to `if_expr` / `while_stmt` / `match_arm`** — grammar refactor to a shared `LetChain` non-terminal followed by per-shape lowering reuse. Closes the biggest user-visible parity gap in if/while/match (`expr.if.chains`, `expr.loop.while.chains`, `expr.match.guard.chains`).
2. **`loop {}` should type as `!`/Never** — one-line fix in `lower_loop`: if no `break` value recorded, set the result type to `never_t()`. Closes `expr.loop.infinite.diverging`.
3. **Re-base `?` on a real `Try` trait surface** — add `pub trait Try { type Output; type Residual; fn branch(self) -> ControlFlow<Self::Residual, Self::Output>; }` and `FromResidual` in stdlib; rewrite `sema_expr.cpp:847-977` to dispatch via the trait. Closes the Ok/Err/Some/None name-match hack and unblocks user-defined `?`.
4. **Verify `let-else` diverging-else check** — walk the lowered else block and assert its tail is `Never` / contains a hard terminator. Closes `statement.let.behavior`.
5. **Implement `DerefMut`-driven method autoderef for `&mut self` methods on smart-pointer receivers** — comment at `sema_expr.cpp:6092` flags this as deferred; mirror `emit_generic_deref_step(want_mut=true)` in the method autoderef loop.
6. **Add full Range type family (RangeFrom/RangeTo/RangeFull/RangeInclusive/RangeToInclusive) to stdlib + sema** — Logos has the grammar but only the closed integer forms get a stdlib type.
7. **Add labeled bare-block (`'block: { break 'block v; }`)** — extend `labeled_loop_stmt` to also accept `block`; corresponding LIR/sema for break-value frames already exists (LoopBreakFrame).
8. **Rename or alias `Eq`/`Ord` ↔ `PartialEq`/`PartialOrd`** in stdlib so ported Rust code's `impl PartialOrd for X` finds a binding without rewrite. SL-sl-02 already notes this; ship the renamed traits + bound-fallback.
9. **Trait-object ptr-to-ptr cast rules** (`expr.as.pointer.unsized.trait`) — current `lower_cast` lacks principal-trait identity / auto-trait / lifetime rules for `*const dyn Foo as *const dyn Bar`.
10. **Document async / Pin absence in DIVERGENCES.md** — currently scattered across MEMORY.md feedback files; consolidate as an A4 entry naming all the missing types (`Future`, `Pin`, `IntoFuture`, `Poll`, `Context`, `Waker`).
