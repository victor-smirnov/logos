# Category M — Const evaluation (audit)

Generated: 2026-05-30; spec: rust-lang/reference (local checkout `/home/victor/cxx/reference`).

Summary: 1 feature audited (a single bundle covering const expressions, `const fn`, `const { … }` block, `const`/`static` item evaluation, and const-context positions). Verdict: **WARN — blessed §A1+§A2 divergence**: Logos replaces Rust's general const-evaluator with the `metacall { … }` splice plus a narrow AST-walking CTFE (`src/compiler/ctfe.cpp`). The replacement is reachable at the right Rust positions (array length, array-fill length, enum discriminant, const-item initializer when shape is literal/op/metacall), but is *narrower* than Rust's spec: no `const fn`, no `const { … }` block, no path-to-const fold inside CTFE, no const-context promotion, no compound-shape (struct/tuple/array/closure/loop/if/match/cast/borrow) evaluation. Sema gates const-item RHS to a hardcoded shape allow-list (`is_const_evaluable` at `src/compiler/sema_collect.cpp:1734-1773`) and uses `metacall` as the escape valve for anything more complex. The composition with neighbour features (const items, statics, const-generics, pattern literals, struct init, trait const) is reachable when the user writes the metacall form; the open metacall channel gap is **K10-co-06** (named const folding inside `metacall { N }`).

Authoritative for "blessed" status: `docs/DIVERGENCES.md:41` (§A1 const-eval → `metacall`), `:42` (§A2 `const fn` → plain fn + metacall rewrite). Memory rules: `feedback_const_fn_via_metacall`, `project_no_const_eval`.

---

## 1. Const eval / `const fn` / `const { ... }`

**Rust nomenclature.** "Constant evaluation" — `reference/src/const_eval.md:1-13`. The Rust spec breaks the feature into three sub-concepts:
- **Constant expressions** (`const_eval.md:7-234`) — the allow-list of expression shapes whose value is computable at compile time: literals, const params, paths to fns/consts/statics (with restrictions), tuple/array/struct exprs, block exprs incl. `unsafe` and `const` blocks (with `let`, irrefutable patterns, assignment, compound-assignment, expression-stmt), field, index, range, non-capturing closure, built-in negation/arithmetic/logical/comparison/lazy-bool on ints/floats/`bool`/`char`, all borrow forms with promotion rules, deref (incl. raw ptr deref under `unsafe`), grouped, cast (except ptr-to-addr), const-fn calls, `loop`/`while`, `if`/`match`. Errors on overflow/OOB indexing are compile errors in const context (warnings elsewhere).
- **Const context** (`const_eval.md:235-262`) — the positions where the expression *must* be a constant expression: array-type-length, array-repeat-length, initializers of `const`/`static`/enum-discriminant items, const-generic arguments, and `const { … }` blocks. Outer-generic param references are restricted to a single const-generic param or to expressions that reference no generic params.
- **Const functions** (`const_eval.md:263-290`) — fns / tuple-struct constructors / tuple-variant constructors with the `const` qualifier; callable from a const context; body restricted to constant expressions; not allowed to be `async`; param/return types restricted. `reference/src/items/constant-items.md:1-117` adds the const-*item* syntax, the inlining behavior, the `'static`-lifetime requirement, the unnamed-const form `const _: T = …;` (free items), the trait-definition omission rule, the destructor-running rule, and the "free constants are always evaluated at compile time to surface panics" rule.

**Logos nomenclature.** Logos has no `const fn` qualifier on functions and no `const { … }` block expression. The replacement, per `docs/DIVERGENCES.md:41-42`, is two complementary mechanisms:
- **`metacall { … }`** — full Logos JIT splice, lifts an arbitrary expression to compile-time and re-inserts the resulting literal. Grammar: `tools/peg_gen/grammars/logos.peg:543-550` (`metacall_item_decl`), `:320` (`KW_METACALL = "metacall"`), `:257` (AST code `METACALL = 198`), `:268` (`METACALL_ITEM = 209`). Sema entry: `lower_metacall` at `src/compiler/sema_expr.cpp:15095-15400` (one-shot, rejects nested metacalls; only string/int/bool/float literal callee-args admitted).
- **CTFE — AST-walking const folder** — a deliberately sema-light evaluator that walks the AST: `src/compiler/ctfe.cpp:248-258` (`do_eval`), header at `src/compiler/ctfe.hpp:8-50`. Handles `LIT_INT`, `LIT_FLOAT`, `LIT_BOOL`, `LIT_STR`, `PAREN_EXPR`, `UNARY` (`-`, `!`), `BINOP` (`+ - * / % << >> & | ^ && || == != < <= > >=`) — and nothing else.

Const *items*: `tools/peg_gen/grammars/logos.peg:638-671` (`const_def`, AST code `CONST_DEF`; also admits the `pub let`, `let`, `pub static`, `static` surface forms as same-AST aliases — `:660-671`). Lowering: `lower_const_def` at `src/compiler/sema_decl.cpp:1041-1093`. Initializer shape gate: `is_const_evaluable` lambda at `src/compiler/sema_collect.cpp:1734-1773`. Generic-const factories (the `pub const X<T1,T2>: HermesStatic = @{…};` factory shape — Logos addition layered on top of HermesStatic): `src/compiler/sema_collect.cpp:1793-1820+`.

Const *generics* (the `<const N: i64>` position, **distinct** per §A1 last sentence): grammar `tools/peg_gen/grammars/logos.peg:2876-2881` (`CONST_PARAM`); sema branches at `src/compiler/sema.cpp:3414`, `:3491`, `:5454` (`__const_param:CFG` symbolic). Type kind: `LogosType::Kind::ConstVar` (`src/compiler/sema.cpp:851,979,1883,3637,3653,3944,3956,4110,4115,4408`).

Associated consts: `ASSOC_CONST_DEF` (trait, code 152 at `logos.peg:211`, grammar `:888-889`) and `ASSOC_CONST_IMPL` (impl, code 153 at `:212`, grammar `:985-986`). Sema collection: `sema_collect.cpp:1946`, `:2680`. Lowering: `sema_decl.cpp:1679`.

Const-context fold sites (where CTFE is actually invoked):
- Array type length — `src/compiler/sema.cpp:5143-5174` (`metacall { … }` block tail → `ctfe::eval_expr`).
- Array-fill length — `src/compiler/sema_expr.cpp:9942-9970` (parallel to array-type length).
- Enum discriminant — `src/compiler/sema_collect.cpp:1513-1554` (bare const-expr at `Variant = expr`, AND `Variant = metacall { … }`).
- Metacall args — `src/compiler/sema_expr.cpp:15348` (`ctfe::eval_expr(a, holder_)` for each call-arg, error message "metacall: argument N is not a compile-time constant").
- Const-item initializer — `lower_const_def` at `sema_decl.cpp:1041-1093` does *not* call CTFE — it lowers the initializer as a regular LIR expression (mlir-gen re-evaluates the AST at every use site, see `sema_decl.cpp:1047-1053`). The compile-time check is only the *shape* gate (`is_const_evaluable`).

`mono_clone.cpp` interaction: const generics travel via the `ConstVar` kind through pack expansion (`src/compiler/mono_clone.cpp:567,569,1047,1050,2516,2518`) and substitution (`src/compiler/mono_subst.cpp:28,56`). The pack expansion replaces a `<const N: i64...>` param with a runtime-scalar stream wrapped as `ConstVar` (see `mono_scan.cpp:626-631`).

**Match verdict: WARN — blessed §A1+§A2 divergence; narrowness of the replacement is a real metacall-channel debt list.**

The high-level shape ("Rust has const-eval ⇒ Logos has metacall + CTFE") is the spec-blessed replacement. The unblessed deltas are:
- (A) `is_const_evaluable` (`sema_collect.cpp:1734-1773`) is a hardcoded shape allow-list (literal codes + BINOP/UNARY/PAREN/CAST/METACALL + `ARR_LIT`/`TUPLE_LIT` admitted only so a more-specific diagnostic at lowering time can win). Spec const expression list (`const_eval.md:25-234`) is far broader: struct exprs, field/index/range/closure/loop/if/match/borrow/deref/group/cast/call-const-fn. Several Rust-conformant const-init shapes are rejected by Logos with a generic *"initializer must be a literal expression, simple arithmetic over literals, or an explicit `metacall <fn>()`"* (`sema_collect.cpp:1775-1780`). The escape hatch (`metacall { … }`) covers the capability — but only when the user writes it; an unmodified Rust import fails the gate without redirecting hint.
- (B) CTFE's `do_eval` (`ctfe.cpp:248-258`) does not resolve names. A `VAR_REF` to a `const N: i64 = 3;` inside `metacall { N }` falls through to "ctfe: expression is not a compile-time constant" (`ctfe.cpp:257`). This is exactly **K10-co-06** — open gap with plan in `docs/track3-gaps/consts-typeck-gaps.md:13-88`.
- (C) No `const { … }` block expression. Grammar grep for `KW_CONST LBRACE` returns zero matches in `tools/peg_gen/grammars/logos.peg` (I greppped); spec `const_eval.md:55-59` and `:257-258` list it as both a constant-expr and a const-context. Replacement is `metacall { … }` (which IS implemented as a block-tail form) but the keyword/shape differs.
- (D) `const fn` qualifier is absent. Grammar grep for `const fn`/`const_fn` in `src/compiler/` returns zero matches. Per §A2, callers must wrap call-sites in `metacall <fn>(…)` to force compile-time evaluation. The callee fn body has no compile-time-only restriction — it can call any code; metacall JITs it.
- (E) Static items (`static FOO: T = …;` and `pub static`) collapse to the same `CONST_DEF` AST as `const` (`logos.peg:668-671`). `static mut` is rejected by grammar omission. Rust distinguishes (statics have addresses; consts are inlined; statics permit interior-mutable types). Logos behaves "as `const` for now" — undocumented row in DIVERGENCES; treat as B-style debt for the static/const distinction.
- (F) Unnamed const (`const _: T = …;`) is grammatically absent: `const_def` requires `IDENT` not `IDENT | "_"` (compare Rust `ConstantItem` syntax at `reference/src/items/constant-items.md:6`). No `_SameNameTwice`-style scoped duplicate-OK form.
- (G) Trait-definition const without initializer (`const NAME: T;`) is supported (`logos.peg:888-889` → `ASSOC_CONST_DEF`). Trait-definition `const NAME: T = default_expr;` (default value) is grammatically absent (no `ASSIGN expr` alt). Rust permits it (`reference/src/items/associated-items.md` §Associated constants).
- (H) Const-context "loop"/"while"/"if"/"match"/"closure" in initializer — rejected by `is_const_evaluable`. Borrow/deref/struct-expr likewise. The metacall channel CAN host these (mlir-gen JITs the fn) but only as `pub const X: T = metacall <fn>();`, not inline.

The replacement IS sufficient for the metacall positions Logos defines as const contexts; the underlying CTFE walker is the actual feature gap (not `const fn`, not `const { }`). Most expansions converge to "thread the ConstResolver seam through `ctfe::do_eval`" (K10-co-06 plan).

**Implementation pointer.**
- CTFE walker: `src/compiler/ctfe.cpp:248-258` (do_eval), `:67-244` (per-shape eval), header at `src/compiler/ctfe.hpp:8-50`.
- Metacall fold sites (call into CTFE):
  - Array length: `src/compiler/sema.cpp:5143-5174`.
  - Array-fill length: `src/compiler/sema_expr.cpp:9942-9970`.
  - Enum discriminant (bare expr + metacall): `src/compiler/sema_collect.cpp:1513-1554`.
  - Metacall call-arg: `src/compiler/sema_expr.cpp:15348-15355`.
- Const-item lowering & shape gate: `src/compiler/sema_decl.cpp:1041-1093` + `src/compiler/sema_collect.cpp:1734-1781`.
- Metacall site: `src/compiler/sema_expr.cpp:15095-15400` (`lower_metacall`).
- Const-generic param machinery (separate from const-eval per §A1): `src/compiler/sema.cpp:3414, 3491, 5454` and `LogosType::Kind::ConstVar` (numerous; sema/mono_subst/mono_clone).
- Assoc const: `src/compiler/sema_collect.cpp:1946, 2680`, `src/compiler/sema_decl.cpp:1679`.
- `mono_clone.cpp` carries ConstVar through pack expansion at `:567,569,1047,1050,2516,2518`.
- Grammar: `tools/peg_gen/grammars/logos.peg:638-671` (const item), `:888-889` + `:985-986` (assoc const), `:2876-2881` (const-generic param), `:320` (`KW_METACALL`), `:543-550` (metacall_item_decl).

**Interactions check** (vs `docs/language/feature-interactions.md:451-457` edges for "Const eval / `const fn` / `const { ... }`"):

- **Const items / static items** — *OK with caveats.* `const X: T = expr;` parses, lowers, and the inlining model matches Rust (mlir-gen re-evaluates the initializer at each use site — `sema_decl.cpp:1047-1053`). `static`/`pub static` collapses to the same AST node (`logos.peg:668-671`) — divergent (Rust has distinct semantics; Logos treats them identically). `static mut` rejected by grammar omission (matches Rust safety stance under different mechanism — no `unsafe { static mut S: u8 = 0; &mut S }` form). **WARN** — static/const collapse is undocumented in DIVERGENCES; should be either separated (Rust-conformant) or recorded as a §B catch-up row.
- **Const generics (`[T; N]`)** — *OK conformant.* `<const N: i64>` parses (`logos.peg:2876-2881`), travels through sema/mono as `ConstVar`, and the `[T; N]` use is the standard array-type slot. The interplay with the *constant-expression* check is what §A1's "Const-**generics** `<const N: i64>` are NOT this (they work)" line carves out. The array-length sema branch at `sema.cpp:5193+` accepts a `CONST_PARAM` reference (the `symbolic` path). One sub-gap: `metacall { T::SIDES }` for a trait-bound assoc-const is open per K10-co-06 plan note (`consts-typeck-gaps.md:73-79`).
- **`const fn` (restricted ops)** — *replaced (§A2 blessed) but with metacall-channel narrowness debt.* The `const fn` qualifier is grammatically and semantically absent (greppd `const fn`/`const_fn` in `src/compiler/` — zero matches). Replacement: write a plain `fn` and wrap calls as `metacall fn(…)`. The narrowness gap: a metacall arg that's an `ENUM_LIT`, `FIELD_READ`, or named const VAR_REF is rejected by `ctfe::do_eval`. K10-co-06 is the planned fix.
- **Const-context expressions** — *partial.* Recognised const contexts: array type length, array repeat length, enum discriminant, metacall args. Not recognised:
  - Const-generic argument *position* — works as a const-param reference but cannot host an arithmetic expression (`Foo::<N + 1>` — I greppd the grammar's `type_arg_list` at `logos.peg:2900+`; only `type_or_lt_arg` which is identifier/literal-shaped, not a general const-expr).
  - `const { … }` block — absent; replacement is `metacall { … }` (different keyword).
  - Static initializer — runs through `lower_const_def` so the same gate applies.
  - **GAP** for free-standing `const _: () = assert!(…);`-style compile-time panic surfacer (the Rust pattern at `items/constant-items.md:97-109`) — Logos has no `_`-named const, no const-`assert!`, no panic-at-CTFE channel.
- **Pattern literals** — *OK with constraint.* Pattern-literal match (e.g. `match x { 1 => …, 2 => … }`) works for the same set of literal shapes the parser admits. Const-name patterns (`match x { N => … }` where `N` is a `const`) — I greppd for VAR_REF-as-pattern handling in `sema_stmt.cpp` and didn't find a dedicated path; named-const-in-pattern likely binds `N` to the scrutinee rather than matching it. **WARN — gap.** The Rust spec says pattern literals must be const expressions; if Logos pattern lowering doesn't lift a path-to-const into a literal match, that's a real intersection failure. Worth a probe test (see "Recommended next moves").
- **Static init** — *same as const items above.* The `static FOO: T = expr;` form lowers identically to `const`. WARN — collapse is not in DIVERGENCES.
- **Trait `const` (limited)** — *grammatical OK; missing trait-default-value form.* `trait T { const NAME: T; }` parses (`logos.peg:888-889`); `impl T for X { const NAME: T = expr; }` parses (`logos.peg:985-986`). The Rust shape `trait T { const NAME: T = default_expr; }` (default value) is absent — grep `KW_CONST IDENT COLON type_ref ASSIGN expr SEMI` inside `impl_item` shows only impl-side; the trait-side has no `ASSIGN expr` alt. **WARN gap.**

**Gaps / debt** (concrete, falsifiable):

1. **K10-co-06 — CTFE doesn't fold path-to-const inside `metacall { N }`.** Plan in `docs/track3-gaps/consts-typeck-gaps.md:17-88`. Touches `ctfe.hpp`/`ctfe.cpp` (add `ConstResolver` seam) + `sema.cpp` ~4974 + `sema_expr.cpp` ~9862. Open since 2026-05-26.
2. **Const-item shape allow-list is narrow.** `is_const_evaluable` (`sema_collect.cpp:1734-1773`) rejects struct/tuple-with-bindings/if/match/closure/borrow init shapes that Rust admits. Pragmatic refactor: lift to a recursive walker mirroring `ctfe::do_eval` so the allow-list and the evaluator share a single source of truth. Until then, any Rust import with a compound const initializer (`const POINT: Point = Point { x: 1, y: 2 };`) is rejected with the literal/arith error.
3. **`const { … }` block expression — absent.** Replacement is `metacall { … }` (different keyword, different precedence — `metacall` is one-shot, not nestable; `const` blocks in Rust can compose). Document in DIVERGENCES §A1 explicitly that the *block-form const context* is also subsumed by `metacall { … }`.
4. **`static` / `pub static` aliased to `const` AST.** `logos.peg:668-671` collapses them. Real Rust semantics differ (statics have stable addresses; consts are inlined). Either split the AST and implement statics as addressable global storage, or record in DIVERGENCES as a §B row. Not currently tracked.
5. **Unnamed const `const _: T = …;` — absent.** `logos.peg:652-663` requires `IDENT` not `IDENT | "_"`. Spec at `items/constant-items.md:70-94`. Used by Rust macros that emit duplicate scoped compile-time assertions.
6. **Trait-default assoc-const (`trait T { const N: T = default_expr; }`) — absent.** Add an `KW_CONST IDENT COLON type_ref ASSIGN expr SEMI` alt to `trait_item`. Sema scaffolding (`ASSOC_CONST_IMPL` path) reusable.
7. **Pattern-literal const-name match.** Probe-test: `const N: i64 = 7; match x { N => "hit", _ => "miss" }`. If the binding-vs-literal arbitration silently binds `N`, the intersection between const-items and pattern literals is broken. Probable Rust-side test source: `tests/imported/pass/patterns/`. **Probe only — not yet confirmed by repro in this audit.**
8. **`assert!` / panic at CTFE.** Rust uses `const _: () = assert!(usize::BITS == 0);` for compile-time assertions (`items/constant-items.md:100-108`). Logos CTFE has no `assert!` recognition and no panic plumbing; replacement could be metacall returning Result but it's surface-divergent.
9. **Const-generic argument as an arbitrary const expression** (`Foo::<N + 1>`) — `type_arg_list` doesn't admit a BINOP. Spec at `const_eval.md:259-261` constrains it but does admit non-trivial const expressions. Logos requires the user to bind to a const first.
10. **Test gaps** — no dedicated test exercising the const-eval × const-generic × array-length intersection (the K10-co-06 plan lists adds; not yet implemented). No test exercising static-vs-const distinction (correctly given they're aliased). No test for compile-time assertion patterns.

---

## Cross-category gaps

- **C-items × M-const-eval** — `static` items live both in category C (items list) and M (const-evaluated initializer). The static-vs-const collapse should be tracked in BOTH category audits; currently neither row exists. Recommend adding a one-line row in `C-items.md` summary and a §B catch-up in `docs/DIVERGENCES.md`.
- **D-generics-and-bounds × M-const-eval** — Const-generic *expression* arguments (`Foo::<N + 1>`) require both the const-eval expression machinery and the type-arg grammar to widen. Lives at the intersection — either category can host the gap row.
- **F-patterns × M-const-eval** — Pattern-literal-as-const-name (`const N: i64 = 7; match x { N => … }`) is a pattern-side question that needs M's CTFE to fold the path. Single intersection probe-test would resolve both.
- **L-attributes × M-const-eval** — Rust `#[cfg(version("…"))]` and `#[cfg(target_pointer_width = "…")]` are const-eval-flavour predicates evaluated at sema time. Logos cfg handling is in L-audit; no new gap here, but note the overlap.
- **K-unsafe × M-const-eval** — Rust permits raw-ptr deref in const context under `unsafe` (`const_eval.md:198-215`). Logos has no unsafe-block-in-const channel either way; metacall does its own JIT context.

## Recommended next moves

Single-session sized work items, ordered by leverage:

1. **Close K10-co-06 — add `ConstResolver` to `ctfe::eval_expr`.** Plan already written (`docs/track3-gaps/consts-typeck-gaps.md:39-67`). Edits: `src/compiler/ctfe.hpp` (function signature + typedef), `src/compiler/ctfe.cpp` (thread resolver through `do_eval`/`eval_unary`/`eval_binop`), `src/compiler/sema.cpp` (resolver impl + wire at array-length call), `src/compiler/sema_expr.cpp` (wire at array-fill call). Tests at `tests/imported/pass/array-len/` for `metacall { N }`, `metacall { Tri::SIDES }`, `metacall { N * 2 + 1 }`. Single sitting; clear acceptance.
2. **Unify `is_const_evaluable` with `ctfe::do_eval`.** Lift the allow-list (`sema_collect.cpp:1734-1773`) to call CTFE directly (or share the same walker), so the shape gate and the evaluator agree. Removes the narrow "literal-or-arith-or-metacall" error and accepts whatever CTFE accepts (with the K10-co-06 extension, that's literals + ops + path-to-const). Lower complexity than (1) once (1) is in.
3. **Probe pattern-literal × const-name intersection.** Write a single test `tests/imported/pass/patterns/match-const-name.logos` that uses `const N: i64 = 7;` then `match x { N => … }`. If it fails (binding vs. literal arbitration), file a sema_stmt gap row; if it works, document the intersection as covered.
4. **Document the static-vs-const collapse** in `docs/DIVERGENCES.md` as a §B row (catch-up — Rust distinguishes; Logos doesn't). Keep the actual split for a separate session once a real test requires the distinction (mutable static, or `&CONST` vs `&STATIC` address-equality).
5. **Add the trait-default assoc-const grammar alt** in `logos.peg` `impl_item` (currently `:984-986`) → add a parallel rule under the trait-side `KW_CONST IDENT COLON type_ref ASSIGN expr SEMI`. Sema slot already exists for `ASSOC_CONST_DEF`; the new alt just supplies VALUE.
