# Category M — Const evaluation (audit)

v2 — re-audited 2026-06-12 (v1: 2026-05-30); spec: rust-lang/reference (local checkout `/home/victor/cxx/reference`). All v2 verdicts re-verified against code + compile probes (`/tmp/m*.logos`, 2026-06-12).

Summary: 1 feature bundle (const expressions, `const fn`, `const { … }`, `const`/`static` items, const-context positions). Verdict: **WARN — blessed §A1+§A2 divergence; replacement substantially widened since v1, residual = resolver wiring at type-position metacall sites + assoc-const path folding + 6 static (S-series) gaps.** Closed since v1: ConstResolver seam (b8012910, §6.9 — `metacall { N }` folds at expression/item positions), `static`/`static mut` split (927461fc + 18003dc5 + 7d7f2ee9, §6.2), constants-as-patterns (d207fcda, §4.4), compound const initializers (struct-lit/array/tuple now lower end-to-end), trait-default assoc const grammar (1b8ff07e), expression-valued enum discriminants (ce47ced2, G159-3). **Scoreboard inconsistency:** logos-core §6.9 row is ✅ but K10-co-06's canonical repro (`[i64; metacall { N }]`) is still red — the resolver was wired only at the two `sema_expr.cpp` metacall sites, not at the array-length / array-fill / enum-discriminant CTFE sites (probes m2/m3 fail "ctfe: expression is not a compile-time constant"). `docs/track3-gaps/consts-typeck-gaps.md` correctly still lists K10-co-06 Open.

Authoritative for "blessed": `docs/DIVERGENCES.md` §A1 (const-eval → metacall), §A2 (`const fn` → plain fn + metacall). Memory: `feedback_const_fn_via_metacall`, `project_no_const_eval`. Strict rule: a metacall REJECTION at a const position is a metacall GAP, never covered-by-divergence.

---

## 1. Const eval / `const fn` / `const { ... }`

**Rust nomenclature.** `reference/src/const_eval.md`: constant expressions (`:8-234` — allow-list incl. literals, paths-to-consts, tuple/array/struct exprs, blocks incl. `const {}`, field/index/range, closures, arithmetic, borrows w/ promotion, deref, cast, const-fn calls, loop/if/match), const context (`:236-262` — array length, repeat length, `const`/`static`/discriminant initializers, const-generic args, `const {}` blocks), const functions (`:264-290`). `items/constant-items.md` adds unnamed `const _`, trait-default omission, free-const eager evaluation.

**Logos nomenclature.** No `const fn` qualifier, no `const { … }` block; replacement per §A1/§A2:
- **`metacall { … }`** — JIT splice. Grammar `tools/peg_gen/grammars/logos.peg:559` (`metacall_item_decl`); sema `lower_metacall` at `src/compiler/sema_expr.cpp:16356`, item-position mirror at `:18390`.
- **CTFE** — AST-walking folder, `src/compiler/ctfe.cpp` (`do_eval` `:249-279`), header `src/compiler/ctfe.hpp`. Handles LIT_INT/FLOAT/BOOL/STR, PAREN, UNARY(`- !`), BINOP (full arith/logic/cmp set), **and since b8012910: VAR_REF via the `ctfe::ConstResolver` interface** (`ctfe.hpp:48-64`) — sema's `SemaConstResolver` over `module_const_values_` supplies the const's RHS node; chains (`A + B - 1`) recurse. Resolver handles bare VAR_REF only — `Type::CONST` paths still unresolved.

Const items: grammar `logos.peg:666-672` (`const_def`); **`static` is no longer an alias** — `:683-693` give `static` / `static mut` (+`pub`) their own productions; `static mut` emits `STATIC_DEF` (schema 254), collected into `module_static_muts_` (`sema_collect.cpp:1842-1848`) with unsafe-gated reads (`sema_expr.cpp:542-546`) and writes (`sema_stmt.cpp:2409-2412`). Lowering: `lower_const_def` at `src/compiler/sema_decl.cpp:1230` (initializer re-evaluated per use site — Rust const-inlining model). Shape gate: `is_const_evaluable` at `src/compiler/sema_collect.cpp:2159-2268` — since v1 widened to STRUCT_LIT (const-evaluable fields), VAR_REF→`module_consts_`, `&VAR_REF`, fn-name-as-fn-ptr-const (code present; see S12 below), ARR_LIT/TUPLE_LIT.

Const generics `<const N: i64>`: unchanged, conformant (`Kind::ConstVar`, mono pack-expansion) — §A1 carve-out "they work".

Assoc consts: trait-side `const N: T;` AND **`const N: T = default;`** (`logos.peg:938-940`, default landed 1b8ff07e — `SemaAssocConstInfo.has_default`, impl-completeness skips the missing-item error); impl-side `logos.peg:1037`.

CTFE invocation sites (resolver-wiring status — the load-bearing v2 finding):
- Metacall expr-position args — `sema_expr.cpp:16590-16616` — **resolver wired** (§6.9).
- Metacall item-position args — `sema_expr.cpp:18710-18715` — **resolver wired**.
- Array type length `[T; metacall {…}]` — `src/compiler/sema.cpp:5630-5661` — **resolver NOT wired** (`eval_expr(tail, holder_)`, null resolver).
- Array-fill length — `sema_expr.cpp:10868` — **NOT wired**.
- Enum discriminant (bare expr `= 1 << 3` G159-3, and `= metacall {…}`) — `sema_collect.cpp:1938-1975` — **NOT wired**.
- Pattern const-name (§4.4) — `sema_stmt.cpp:4773-4775` folds `module_const_values_` entry directly (no resolver needed for the entry itself; a const-referencing-const pattern would need one).

**Match verdict: WARN — blessed §A1+§A2; the replacement now covers most Rust const-init shapes; residual debt is the type-position resolver wiring + path projection.**

Probe matrix (2026-06-12, `bin/logosc` @ 00355c52):
- ✅ `[i32; metacall { 2 + 2 }]` compiles+runs (m1).
- ❌ `[i32; metacall { N }]` w/ module const N → "metacall in array length: ctfe: expression is not a compile-time constant" (m2). **K10-co-06 residual.**
- ❌ `enum E { A = metacall { N } }` → same error (m3); ✅ `A = 1 << 3` works (m3b, G159-3).
- ❌ `get::<metacall { 2 + 2 }>()` → parse error (m4) — `type_or_lt_arg` (`logos.peg:1372+`) admits only INT/−INT/hermes_lit/type_ref; no metacall, no const-expr. Metacall gap per §A1 strict rule.
- ✅ `const N = 7; match x { N => …, _ => … }` runs (m5) — v1 gap #7 closed (d207fcda).
- ✅ `const P: Point = Point { x: 1, y: 2 }; P.x` runs (m6) — v1 gap #2 (compound shapes) closed for struct-lit.
- ✅ `const A: [i32; 2] = [10, 20];` + `const T: (i32, i32) = (3, 4);` run (m11) — array/tuple const init now end-to-end (v1's "B-ca-05 reject" is gone).
- ❌ trait-default assoc const *projection*: `trait Shape { const SIDES: i64 = 3; } impl Shape for Tri {}` then `Tri::SIDES` → "unknown enum 'Tri'" (m7). Grammar+completeness landed (1b8ff07e); qualified access through the default is the pinned follow-up. ✅ impl-side override `Tri::SIDES` works (m7b).
- ✅ `const _: i64 = 5;` parses (`_` lexes as IDENT) (m9); ❌ duplicate `const _` rejected "duplicate const '_'" (m9b) — Rust treats each `_` item as distinct.
- ❌ `static F: fn() -> i32 = answer;` still rejected by the shape gate (m10) — the `find_func_candidates` acceptance (`sema_collect.cpp:2233`) doesn't fire (fns not yet registered at phase-2 const collect; S12, needs pass-0 fn pre-registration).
- ❌ **S25 (critical):** cross-fn `static mut` read **segfaults at runtime** (m8 → SIGSEGV). `STATIC_DEF` reuses const-inlining storage; each use site materialises a fresh alloca instead of one `llvm.mlir.global` + `addressof`. Known-deferred in logos-core §6.2 Wave 9 notes; confirmed still live.

**Interactions check** (delta from v1):
- **Const/static items** — static/const split landed (§6.2): immutable `static` types `&STATIC: &'static T` (test `core_6_2_static_lifetime`); `static mut` unsafe-gated (tests `core_6_2_static_mut*`); static-refs-static + `&X` init accepted (S3/S11/S6 closed, 7d7f2ee9). v1's "collapse undocumented" WARN is resolved by the split. Open: S2 (static/fn namespace clash accepted), S12, S15 (`static mut ARR[i] = …` → "immutable variable"), S17 (no fn-local `static`), S20 (`static MSG: &str = "lit"` double-`&` type error), **S25 (segfault)**.
- **Const generics** — OK, unchanged. `metacall { T::SIDES }` for trait-bound assoc const still open (resolver is name-only).
- **`const fn`** — §A2 blessed, unchanged; metacall channel narrowness now = path-projection + type-position wiring only.
- **Const-context positions** — array length/array-fill/discriminant accept metacall-with-literals; named consts only at expr/item positions (table above). Const-generic argument position accepts a literal or a bound const-param, not an expression/metacall.
- **Pattern literals** — const-name patterns work (§4.4); v1's probe-question resolved ✅.
- **Trait const** — default-value grammar ✅; default projection ❌ (m7).

**Gaps / debt** (falsifiable, current):
1. **K10-co-06 residual — wire `SemaConstResolver` at the 3 remaining CTFE sites** (`sema.cpp:5654`, `sema_expr.cpp:10868`, `sema_collect.cpp:1943/1974`). The resolver type + sema impl already exist; this is plumbing. Closes probes m2/m3 and the catalog's canonical repro. Update logos-core §6.9 row to note the partial scope until then.
2. **Path projection in CTFE** — `metacall { Tri::SIDES }` / `Type::CONST`: `ConstResolver::lookup_const` is name-keyed; no PATH-node handling in `do_eval`.
3. **Trait-default assoc-const projection** — `Tri::SIDES` via default → "unknown enum" (m7); pinned follow-up of 1b8ff07e.
4. **S25 static-mut cross-fn segfault** — needs real `llvm.mlir.global` storage. Critical (runtime UB shipped as ✅ scoreboard row caveat).
5. **S12 static fn-ptr init** — acceptance code dead due to collect-phase ordering.
6. **Const-generic argument as expression** (`Foo::<N + 1>`, `get::<metacall {…}>()`) — `type_or_lt_arg` grammar gap (m4).
7. **`const { … }` block expression absent** — replacement `metacall { … }`; should be named explicitly in DIVERGENCES §A1 (block-form const context subsumed).
8. **Unnamed const semantics** — single `const _` parses but duplicates rejected; Rust requires each `_` distinct + evaluated (m9b).
9. **S2/S15/S17/S20** — remaining §6.2 Wave-9 documented statics gaps.
10. **Compile-time assertion channel** (`const _: () = assert!(…)`) — still no CTFE panic plumbing.

---

## Cross-category gaps

- **C-items × M** — static/const split landed; remaining S-series rows live in logos-core §6.2. S25 also touches mlir-gen (global storage).
- **D-generics × M** — const-generic *expression* arguments (gap 6) sit at the grammar/type-arg intersection.
- **F-patterns × M** — const-name patterns closed (d207fcda); no open intersection.
- **K-unsafe × M** — `static mut` unsafe gating landed (§6.2); raw-ptr-deref-in-const has no channel either way (metacall JITs its own context).

## Recommended next moves

1. **Wire the ConstResolver at the 3 type-position CTFE sites** (gap 1) — mechanical; closes K10-co-06's repro; ½ session.
2. **Fix S25** — emit `llvm.mlir.global` for `STATIC_DEF`, route reads/writes through `addressof`; removes a shipped runtime segfault. 1 session.
3. **PATH handling in `do_eval` + resolver** (gap 2) — unlocks `metacall { Tri::SIDES }`; pairs with fixing trait-default projection (gap 3) at the same lookup seam.
4. **Const-generic-arg expression surface** (gap 6) — add `metacall`/BINOP alt to `type_or_lt_arg`, fold via CTFE.
5. **S12 pass-0 fn pre-registration** — mirrors the union pass-0 fix.
