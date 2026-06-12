# Category E — Expressions and control flow (audit)

v2 — re-audited 2026-06-12 (v1: 2026-05-30); spec: rust-lang/reference (local checkout at `/home/victor/cxx/reference`)

21 features audited (13 from v1 + 8 primary-expression chapters v1 missed): 16 OK (async counted as blessed A4), 4 WARN, 1 GAP. Since v1, six headline items closed: multi-`&&` let-chains in `if`/`while` (59af9442 / 18003dc5), `?` re-based on `Try`/`FromResidual`/`ControlFlow` (77177a68), `loop {}` → `!` + Never end-to-end (eb894e80), empty/uninhabited match accepted (5bccc7fc), `DerefMut` method autoderef (5c0ad7b3), generic Range family + `Step` (654816d1). Remaining WARNs: `Eq`/`Ord` naming vs `PartialEq`/`PartialOrd` operator dispatch, `..`/`..=` operator desugar still concrete `RangeI32`/`RangeI64`, TAIL_EXPR/labeled-bare-block in blocks, expr-position fully-qualified paths. New findings: unary `!` operator overloading is **broken** (sema dispatches `not_`, trait conformance requires `not` — neither spelling compiles, probe-verified), and underscore expression `_ = expr` is absent.

All v1 file:line refs re-verified 2026-06-12; stale ones updated.

---

## 1. `let` (incl. `let-else`)

**Rust:** *let statement* (`statements.md` §statement.let); irrefutable pattern, optional ascription/init/`else` block; the else block must diverge.

**Logos:** Grammar `let_stmt` (`logos.peg:2146-2155`), `let_else_stmt` (`:2143`). AST `LET=21`, `LET_DESTRUCT=123`, `LET_ELSE=141`, `LET_PAT=217`. Sema: `lower_let` (`sema_stmt.cpp:1655`), `lower_let_else` (`:1535`), `lower_let_destruct` (`:730`).

**Verdict: OK** — keyword, structural slots, refutability split, ascription-as-coercion-site, drop-before-replace (B8), declare-uninit tracking all align with spec.

- **let-else divergence — ✅ closed.** v1 reported the check missing; it was already wired (commit 70d4a671, pre-v1) at `lower_let_else` via `block_always_diverts` (`sema_stmt.cpp:184`, check at `:1575-1576`); tests pinned by 7418de9f (logos-core §6.3). Probe: fall-through else rejects with "'let-else' else-block must diverge".
- Reborrow at let-coercion sites, mutability flag, scope-exit drops — OK (unchanged).

**Residual gaps:**
- `let ref mut x = e;` parses (pat `KW_REF KW_MUT IDENT`, `logos.peg:1974`) but sema rejects via the LET_PAT "struct patterns only" path (probe-verified). `let ref x` works.

---

## 2. Block

**Rust:** *block expression* (`expressions/block-expr.md`); statements + optional tail operand; variants plain/`unsafe`/`async`/`const`/labeled.

**Logos:** Grammar `block` (`logos.peg:1755`), `unsafe_block` (`:1759`); AST `BLOCK=20`, `UNSAFE_BLOCK=132`, `BLOCK_STMT=197`, `TAIL_EXPR=223`. Sema: `lower_block` (`sema_stmt.cpp:634`), `lower_block_expr` (`sema_expr.cpp:12955`).

**Verdict: WARN** — behaviour right (tail value = block value, scope drops, unsafe block), but `TAIL_EXPR` is a synthetic AST node with no Rust-grammar analogue, and labeled bare blocks are absent.

- Diverging tail — ✅ closed via §1.1 Never end-to-end (eb894e80): `Never`-typed tails flow up through if/match joins (probe: `if … { loop {} } else { 42 }` types and runs).
- `async` block — blessed A4 (DIVERGENCES §A4, formal since v1). `const` block — blessed A1 (metacall).

**Residual gaps:**
- Labeled bare block `'a: { … break 'a v; }` — still GAP; `labeled_loop_stmt` (`logos.peg:1830-1835`) accepts only for/while/loop (probe: syntax error). `LoopBreakFrame` machinery exists; extend with a block alt.
- `#![inner attributes]` on blocks — absent (module-only).

---

## 3. `if` / `if let` / let-chains

**Rust:** *if expression* (`expressions/if-expr.md`); condition = bool expr OR `LetChain` (`&&`-separated let/cond segments; `||` disallowed).

**Logos:** Grammar `if_expr` (`logos.peg:2253-2256`) + `if_let_chain` (`:2274-2281`; AST `IF_LET_CHAIN=250`, `LET_CHAIN_LET=251`, `LET_CHAIN_COND=252`). Sema: `lower_if` (`sema_stmt.cpp:5469`), `lower_if_let_chain` (`sema_expr.cpp:13030`) — right-to-left desugar to nested if-lets via `lower_reparsed_tail_expr`.

**Verdict: OK** — ✅ closed (commit 59af9442, logos-core §6.4): multi-`&&` chains of let-binds + bool conds land in `if` (probe: `if let Some(x) = pos(3) && let Some(y) = pos(4) && x < y` compiles + runs). Refutability, bool typing, branch unification unchanged-OK.

**Residual gaps:**
- LUB across if-arms — still ad-hoc `compat`/`unify_numeric`, no formal `coerce.least-upper-bound` (cross-cat B).
- ELSE branch duplicated per fall-through site in the chain desugar (accepted limitation; idempotent for return/panic shapes).

---

## 4. `match`

**Rust:** *match expression* (`expressions/match-expr.md`): arms, guards, guard chains (`if let` in guards), or-patterns, exhaustiveness, empty match over uninhabited scrutinee diverges.

**Logos:** Grammar `match_stmt` (`logos.peg:1858-1861`), `match_arm` (`:1863+`); AST `MATCH=82`, `MATCH_ARM=83`. Sema: `lower_match` (`sema_stmt.cpp:7636`), `check_match_exhaustiveness` (`:6991`).

**Verdict: OK** — broad pattern surface (tuple/slice/struct/variant/range/or/ref/lit), exhaustiveness diagnostics, or-patterns, guards, scrutinee-place move rules (51d2e29e).

- Empty/uninhabited match — ✅ closed (commit 5bccc7fc, logos-core §4.2): `Never`/empty-enum scrutinee is trivially exhaustive (`sema_stmt.cpp:6996-7005`); pairs with Never tightening so the join types correctly.
- Plain `&&` in guards — OK (v1 mis-stated this as a gap: the guard slot is a full `expr`, so boolean `&&` chains parse; spec's "guard chains" means *let*-chains).

**Residual gaps:**
- `if let` in match-arm guards (spec §expr.match.guard.let / .chains) — GAP, probe-verified parse error. Explicitly deferred in logos-core §6.4 (binding-propagation into the arm body is the blocker, not the grammar).
- Default binding modes (RFC 2005 match ergonomics) — absent; stdlib itself works around with explicit `ref` (`stdlib/lang/cmp/cmp.logos:87-93` note). Cross-cat F.
- LUB across arms — same cross-cat B gap as `if`.

---

## 5. Loops (`loop`/`while`/`for`)

**Rust:** *loop expression* (`expressions/loop-expr.md`): four flavours, labels, `break` w/ value, `IntoIterator` desugar, `while let` (+ chains), diverging `loop {}`.

**Logos:** Grammar `loop_stmt`/`loop_expr` (`logos.peg:1821-1828`), `while_stmt` (`:2180`, first alt = `KW_WHILE if_let_chain block`), `for_stmt` (`:1807-1820`), `labeled_loop_stmt` (`:1830-1835`). AST `LOOP=68`, `WHILE=58`, `FOR=70` (int-range fast path), `FOR_EACH=99` (IntoIterator desugar), `LABELED_LOOP=142`, `BREAK=66`/`CONTINUE=67` (+`BREAK_EXPR=231`/`CONTINUE_EXPR=232`).

**Verdict: OK.**

- `loop {}` types as `!` — ✅ closed (commit eb894e80): `lower_loop` sets `last_loop_diverged_` (`sema_stmt.cpp:6483`), consumed at `sema_expr.cpp:1381` (`never_t()` tail). Probe-verified through an if-join.
- Multi-`&&` `while let` chains — ✅ closed (commit 18003dc5, logos-core §6.4 Wave 8): while-form reuses `if_let_chain`, desugars to `loop { chain { body } else { break } }`; test `core_6_4_while_let_chain`.
- break-with-value type collection (`LoopBreakFrame`), labels, `continue`-in-loop-only, loop-boundary drops, `for` pattern + `IntoIterator`/`iter()` fallback — OK (unchanged).

**Residual gaps:**
- Labeled bare block — see §2.
- FOR vs FOR_EACH AST split = implementation fast-path, not a divergence (documented).

---

## 6. Closure

**Rust:** *closure expression* (`expressions/closure-expr.md`): optional `async`/`move`, pipe params, `-> Ret`, capture inference, `Fn`/`FnMut`/`FnOnce`, no-capture → fn-ptr coercion.

**Logos:** Grammar `closure_expr` (`logos.peg:2884+`); AST `CLOSURE_EXPR=109`. Sema `lower_closure_expr` (`sema_expr.cpp:13402`); `Kind::Closure`; `is_fn_family` Fn-trait dispatch.

**Verdict: OK** — `move`, typed/untyped params, `|mut x|`, RFC-2229 narrow capture, no-capture → fn-ptr coercion, `Box<dyn Fn>`, Fn/FnMut bounds.

- move-closure + droppable-capture double-free — ✅ closed (commit 23f5b86b, logos-core §7.1/§7.4): fn-param epilogue drops + `closure_owned_drop_` skip for body-moved captures.

**Residual gaps:**
- Async closures — blessed A4.
- Capturing closure as fn return (`-> impl Fn` / `-> Box<dyn Fn>` at return position) — open (logos-core §7.3).
- Closure-type rendering in diagnostics uses literal pipe form (logos-core §7.9, cosmetic); `move` flag through generic Fn-bound mono untested (§7.10).

---

## 7. Try `?`

**Rust:** `expr.try` desugars via `Try::branch` / `FromResidual::from_residual`; restricted types: `Result`, `Option`, `ControlFlow`, `Poll<…>`.

**Logos:** postfix `QUESTION` (`logos.peg:2408,2581`); AST `TRY_EXPR=122`; sema at `sema_expr.cpp:1014-1060`.

**Verdict: OK** — ✅ closed (commit 77177a68, logos-core §6.5): stdlib gains `ControlFlow<B,C>` (`stdlib/lang/control_flow/control_flow.logos`) and `Try<Continue,Residual>`/`FromResidual<R>` (`stdlib/lang/try_trait/try_trait.logos`); non-Result/Option inner types lower to `match (e).branch() { Continue(c) => c, Break(r) => return Ret::from_residual(r) }`. User types opt in; verified by compiling+running `core_6_5_try_on_user_type`. Result/Option keep the legacy name-match fast-path incl. heterogeneous-`E` `From` conversion.

**Residual gaps:**
- `Try` carries free generic params, not associated types (P-trait-04 follow-up) — shape divergence from Rust's trait, user-visible in impl headers.
- No `impl Try for ControlFlow` in stdlib → `?` directly on a `ControlFlow` value (spec-supported) doesn't work yet.
- `try { … }` block — absent. `Poll` shapes — N/A under A4.

---

## 8. Async / await

**Rust:** *await expression*, async blocks/fns/closures.

**Logos:** `KW_ASYNC`/`KW_AWAIT` reserved tokens only (`logos.peg:370-371`); no productions.

**Verdict: BLESSED (A4)** — formally registered in `docs/DIVERGENCES.md` §A4 (fibres; capability preserved, colour absent) — v1's "document it" item is closed. A8 update: the Rust `Pin<P>`/`Unpin`/`PhantomPinned` API landed 2026-06-12 (commit 6dabfe99), removing the main type-surface hole that an eventual wasm stackless path would have needed.

---

## 9. `return`

**Rust:** *return expression*; diverging, type `!`.

**Logos:** Grammar `return_stmt` (`logos.peg:2189`), expr-position `RETURN_EXPR=233`. Sema `lower_return` (`sema_stmt.cpp:2620`), expr-position diverging handler (`sema_expr.cpp:1238+`, `never_t()` wrap).

**Verdict: OK** — coercion to fn return type, scope-unwind drops with value hoist, closure-local `ret_type_` save/restore, implicit tail-as-return. Never-typing now end-to-end per §1.1. Residual: TAIL_EXPR synthetic node (see §2).

---

## 10. Field access / Method call / Call

**Rust:** field/method-call/call expressions; receiver candidate ladder (autoref + autoderef), `Fn*` dispatch for callable values.

**Logos:** AST `FIELD_READ=54`, `METHOD_CALL=56`, `CALL=30`, `STATIC_CALL=98`, `TUPLE_INDEX=102`, etc. Sema: `lower_call` (`sema_expr.cpp:2420`), `lower_method_call` (`:6568`).

**Verdict: OK.**

- `DerefMut` autoderef for `&mut self` methods — ✅ closed (commit 5c0ad7b3, logos-core §6.13): per-step probe at `sema_expr.cpp:6602-6620` routes mutating methods through `deref_mut` (`Box<Vec<T>>::push` dispatches `Box.deref_mut` then `Vec.push`).
- User-`Deref` auto-invoke for `&*x` / `&mut *x` reborrow — ✅ closed (commit dcc2f4e8): `emit_generic_deref_call` at `sema_expr.cpp:997` (mut) / `:2284` (shared).
- Multi-impl `Deref` selection by self-type shape — ✅ closed (commit 8c10eb4e): impl picked by receiver shape, not first-wins (`emit_generic_deref_step`, `sema_expr.cpp:64-176`).
- Autoref ladder, inherent→trait order, turbofish, Fn-family call of closure values — OK (unchanged).

**Residual gaps:**
- UFCS `<T as Trait>::method(…)` still drops the trait qualifier ("consumed and dropped", `logos.peg:3083-3086`) — overlapping trait methods can't be disambiguated.
- `union` field access (cross-cat C: `union` item landed per logos-core §6.1; E-side field-expr path not re-audited here), `&raw const/mut` (cross-cat A) — open.

---

## 11. Operator overloading

**Rust:** ops desugar to `core::ops`/`core::cmp` traits; `==`→`PartialEq::eq`, `<`→`PartialOrd::lt`, unary `-`/`!`→`Neg::neg`/`Not::not`, compound `*Assign`, `Index`/`IndexMut`.

**Logos:** Arithmetic/bitwise/compound trait set canonical (`stdlib/lang/ops/ops.logos`); `Index<Idx,Output>`/`IndexMut` confirmed present (`ops.logos:185-189` — v1's "verify" resolved; generic params, not assoc type). Binop dispatch `lower_binop` (`sema_expr.cpp:1548`, cmp map `:1759-1764`); compound map `op_assign_trait_method` (`sema_stmt.cpp:2126-2141`); unary at `sema_expr.cpp:2340-2354`; `IndexMut` desugars `try_index_mut_assign` (`sema_stmt.cpp:6702`) + compound `a[i] op= v` (`:2266-2280`).

**Verdict: WARN** — two issues:

1. **Comparison naming divergence persists (v1 finding #5 confirmed open).** `==`/`!=` dispatch to `Eq::eq/ne`, `<`/`<=`/`>`/`>=` to `Ord::lt/le/gt/ge` (`sema_expr.cpp:1759-1764`); Rust maps these to `PartialEq`/`PartialOrd`. Mitigations since: `PartialEq` now carries real `eq`/`ne` (f32/f64 impls, `cmp.logos:30-50`), and bound-checks alias `T: PartialEq`→Eq-impls / `T: PartialOrd`→Ord-impls (`sema_collect.cpp:1051-1061`). But `impl PartialOrd for X` still cannot enable `<` (PartialOrd is an empty marker, `cmp.logos:41`; no `partial_cmp`). Not a blessed divergence → catch-up TODO.
2. **NEW BUG: unary `!` overloading is unusable.** Dispatch looks up `<T>__not_` (`sema_expr.cpp:2343`, `method_name = "not_"`) while trait conformance demands `fn not` (`ops.logos:60-62`). Probe: `impl Not for Flag { fn not }` → operator not found; `fn not_` → "impl Not for Flag: missing method 'not'". Neither spelling compiles; no test covers `impl Not` (only `impl Not for bool` in stdlib, reached via the primitive `!` path). `Neg` works (probe EXIT ok). One-line fix: `method_name = "not"`.

Compound-assign trait set, `Neg`, enum/TypeVar `==` desugars, widen+autoref around dispatch — OK.

**Residual gaps:** `partial_cmp` shape absent; `&raw` ops absent (cross-cat A).

---

## 12. Range

**Rust:** six range productions → six generic `std::ops::Range*<Idx>` types; iterable via `Step`.

**Logos:** Grammar `range_expr` (`logos.peg:2297+`, all six forms); AST `RANGE_EXPR=112`. Sema `sema_expr.cpp:1171-1232`. Stdlib (`stdlib/lang/range/range.logos`): `Step` trait (`:21`) + 6 generic types `RangeOf`/`RangeOfIncl`/`RangeOfFrom`/`RangeOfTo`/`RangeOfToIncl`/`RangeOfFull` (`:56-149`) with factory fns — ✅ landed (commit 654816d1, logos-core §6.12).

**Verdict: WARN** — type family now exists generically, but the **operator desugar is still concrete**: value-position `a..b`/`a..=b` constructs `RangeI32`/`RangeI64` (`sema_expr.cpp:1209`), restricting operand types to integers (`:1183-1186`); generic `RangeOf<T>` reachable only via factory fns. Known-deferred (logos-core §6.12 "Wave 9"). Also:

- Names diverge (`RangeOf*` vs Rust `Range*`) — mono base-name collision workaround, revisit with per-package qualification.
- Inclusive `a..=b` rewrites `end` to `end+1` at construction (`sema_expr.cpp:1205-1208`) — right for iteration, wrong for a user-visible `.end` read or `contains`.
- Open-ended forms (`a..`, `..b`, `..`) in *value* position error out (LHS/RHS required); they work as slice indices (`sema_expr.cpp:9739-9742` RANGE_EXPR-as-index path).

---

## 13. Cast `as`

**Rust:** `expr.as` — numeric/enum/bool/char/ptr/fn-ptr cast table.

**Logos:** Grammar `cast_expr` (`logos.peg:2532`; ns `:2366`); AST `CAST=69`. Sema `lower_cast` (`sema_expr.cpp:619+`). No behavioural changes since v1 (no commits touching `lower_cast`).

**Verdict: OK** — numeric/enum-to-int/bool/char/fn-item/no-capture-closure→fn-ptr paths present; `slice as <T>[]` is a Logos Hermes extension (A6).

**Residual gaps (unchanged from v1):** trait-object ptr-cast rules (principal trait/auto-trait/lifetime, §expr.as.pointer.unsized.trait); `enum Drop` cast rejection; `u8 as char` validity; `&[T;n]→*T` mut-tightening footnote.

---

## 14. Primary expressions (missed in v1)

Spec chapters `literal-expr.md`, `path-expr.md`, `grouped-expr.md`, `array-expr.md`, `tuple-expr.md`, `struct-expr.md`, `underscore-expr.md` — not audited in v1; swept 2026-06-12.

| Feature | Verdict | Evidence |
|---|---|---|
| Literal expr | OK | int/float/str/char/bool literal kinds + suffixes; covered at depth in cat-B audit; `-128i8` range edge tracked under DIVERGENCES B4. |
| Path expr | WARN | `pkg.path::Item` model is a blessed-adjacent Logos shape, but fully-qualified dotted+`::` paths in *expression* position are a parse error (use+short-name required) — a Logos-model conformance item ([[ref_logos_path_model]]). UFCS qualifier drop → §10. |
| Grouped (paren) expr | OK | probe `(1 + 2) * 3` ✓. |
| Array expr + repeat | OK | `ARR_LIT=65` (`logos.peg:2847`); probe `[2; 3]` + `arr[0]` ✓. |
| Index expr | OK | builtin indexing + `Index`/`IndexMut` trait dispatch (§11); slice-mutability enforced (B6). |
| Tuple expr + index | OK | `TUPLE_LIT=101`, unit `()` (`logos.peg:2421,2594`); probe `(5,).0` (1-tuple, ex-B4 gap) + `tup.1` ✓. |
| Struct expr (incl. FRU) | OK | `STRUCT_LIT=53` with `BASE` slot (`logos.peg:2701-2714`); probe `P { x: 10, ..a }` ✓. |
| Underscore expr | GAP | `_ = expr;` rejects with "assignment to undefined variable '_'" (probe). Spec `underscore-expr.md`: `_` is a valid assignee place. Small fix in the place-assign path (gen_lvalue_addr discard case). |

---

## Cross-category gaps

- **B — LUB (`coerce.least-upper-bound`)** for if/match/loop-break joins: still ad-hoc `compat`/`unify_numeric`. (Never-divergence half of v1's entry is closed by §1.1.)
- **F — match ergonomics**: default binding modes (RFC 2005); `if let` in match guards (the one remaining let-chain site).
- **C/A — `&raw const`/`&raw mut`**, union field-expr re-check.
- **B/C — `Try` associated-types shape** (P-trait-04) and `partial_cmp`/`Option<Ordering>` through bound-check — both blocked on the same assoc-type wiring.
- **A4/A1 (blessed)** — async blocks/closures/await; `const {}` blocks via metacall.

## Scoreboard cross-check (logos-core.md / DIVERGENCES.md)

Verified against code + probes: §1.1 ✅ real (probe), §6.3 ✅ real (probe; note: assertion pre-dated v1 — v1 missed it), §6.4 ✅ honest (if+while real, match-guard form explicitly deferred and confirmed still a parse error), §6.5 ✅ real with honest residuals (legacy Result/Option path; free-generics shape), §6.12 ✅ honest (operator desugar deferral is real: `..` still → `RangeI32`), §6.13 ✅ real (code at `sema_expr.cpp:6602`). No scoreboard-vs-reality lies found. Not in any catalog: the §11 `Not`-dispatch bug (new).

## Recommended next moves

1. **Fix unary `!` `Not` dispatch** — `sema_expr.cpp:2343` `"not_"`→`"not"`; add an `impl Not` test (currently zero coverage). One line + test.
2. **`_ = expr` underscore assignee** — accept `_` in the place-assign path; closes `underscore-expr.md`.
3. **Range operator desugar to `RangeOf<T>`** (logos-core §6.12 Wave 9) — route `..`/`..=` through the generic family, fix the inclusive `end+1` observable, then alias `RangeI32 = RangeOf<i32>`.
4. **`PartialOrd`/`partial_cmp` real shape + operator dispatch rename** — `<` family → `PartialOrd::lt`, keeping Ord⇒PartialOrd fallback; unblocks `impl PartialOrd for X` ports (needs `Option<Ordering>` through bound-check).
5. **`if let` in match guards** — the last let-chain site; binding-propagation into the arm body.
6. **Labeled bare block** — add `LIFETIME COLON block` alt to `labeled_loop_stmt`; `LoopBreakFrame` already supports break-with-value.
7. **UFCS trait-qualifier honoring** — stop dropping the `<T as Trait>` qualifier (`logos.peg:3083-3086`); needed for overlapping trait methods.
8. **`impl Try for ControlFlow` + `try {}` block** — finish the spec's restricted-types set on the now-real `Try` surface.
9. **LUB procedure** for if/match/break joins (cross-cat B).
10. **`let ref mut x = e`** — route through the general pattern binder instead of the "struct patterns only" reject.
