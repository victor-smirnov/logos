# ADR 0012 — WQL seam-spike: IR schemas + FE→IR contract + resource ABI

**Status:** REVIEW ROUND 1 RESOLVED (2026-07-01). Assistant's first pass; user reviewed §8 and
locked the four majors (see §9). Companion to [`0012-writ-query-language.md`](0012-writ-query-language.md).
Remaining `[?]` are minor impl details + the dir3 type-env-capture crux.

The point of the seam-spike: fix the three interfaces so dir1 (EE) and dir2 (FE) can be built in
parallel without diverging — (1) the IR as Writ schemas, (2) the FE→IR contract, (3) the resource
metacall ABI.

---

## 0. Design constants

- **IR is Writ-schema'd** (dogfood ADR 0011): every IR node is a `schema`; each family is a
  `schema enum`. Three backends consume the same tree (static metacall→LIR; interpreter `match`;
  JIT→VM). Serialization is free.
- **Two tiers**: a **scalar** algebra (`SExpr`) nested inside a **relational/graph** algebra
  (`RExpr`). A scalar-only query never instantiates the relational tier (lightweight-EL invariant,
  §4).
- **Graph-shaped**: nodes = schema'd objects; **edges = ref fields** (`WRef<S>`). The same edge
  primitive recurs at three depths: `SField` (1 step) ⊂ `REdge` (1 step over a set) ⊂ `RFix`
  (unbounded).
- **Strict typing (D4)**: every field named in the schema exists; optionality only via explicit
  `Option`-typed schema fields. No `has()`/`?.`.

### Category allocation (provisional)
WQL IR lives in its own Writ documents, so it cannot collide with the compiler's internal
`CAT_*` (0x0001–0x0007 in `schema_codes.hpp`). Still, allocate distinct numbers for clarity:

| category | meaning |
|----------|---------|
| `0x0010` | WQL scalar nodes (`SExpr`) |
| `0x0011` | WQL relational/graph nodes (`RExpr`) |
| ~~`0x0012`~~ | ~~WQL type descriptors~~ — **DROPPED, reuse compiler `CAT_TYPE`** (§3) |
| `0x0013` | Trama AST (port of `TplASTCodes`) |

**[?]** When the u56 code-narrowing (ADR 0011 future item) lands, category width may shrink;
codes here are 64-bit for now.

---

## 1. Scalar tier — `SExpr` (category 0x0010)

```logos
schema SLit   : code(0x0010_0000_0000_0000) { val: WAny = 0 }                         // literal, value verbatim (dynamic WAny field)
schema SParam : code(0x0010_0000_0000_0001) { idx: i32 = 0 }                          // bound parameter (prepared-stmt arg)
schema SVar   : code(0x0010_0000_0000_0002) { idx: i32 = 0 }                          // comprehension-bound row variable
schema SField : code(0x0010_0000_0000_0003) { base: WRef<SExpr> = 0, key: u8 = 1 }    // ONE field / edge step
schema SBin   : code(0x0010_0000_0000_0004) { op: i32 = 0, lhs: WRef<SExpr> = 1, rhs: WRef<SExpr> = 2 }
schema SUn    : code(0x0010_0000_0000_0005) { op: i32 = 0, arg: WRef<SExpr> = 1 }
schema SCall  : code(0x0010_0000_0000_0006) { fn: i32 = 0, args: WRef<SExprArr> = 1 } // function/filter call
schema SCond  : code(0x0010_0000_0000_0007) { c: WRef<SExpr> = 0, t: WRef<SExpr> = 1, e: WRef<SExpr> = 2 }  // ternary
schema SComp  : code(0x0010_0000_0000_0008) { plan: WRef<RExpr> = 0, head: WRef<SExpr> = 1 }  // comprehension → list value

schema enum SExpr : category(0x0010_0000_0000_0000) {
    Lit(SLit), Param(SParam), Var(SVar), Field(SField),
    Bin(SBin), Un(SUn), Call(SCall), Cond(SCond), Comp(SComp),
}
```

Notes:
- `SField.base` typed `WRef<SExpr>` because the base is itself an expression (a var, a param, or
  another field step) — `a.b.c` = `SField(SField(SField(Var), b), c)` … actually
  `SField(key=c, base=SField(key=b, base=Var))`. A bounded edge chain.
- `SComp` is the bridge: a comprehension is a scalar-context value (a list) whose `head` is
  evaluated per row produced by the relational `plan`. `[x.name for x in items if x.active]` →
  `SComp{ plan = RFilter(RScan(items), x.active), head = x.name }`.
- `op` enums for `SBin`/`SUn` — **CEL operator set + semantics is the canon** (arith
  `+ - * / %`, compare `== != < <= > >=`, bool `&& || !`, CEL string/`in`/membership ops).
- `fn` ids for `SCall` — **CEL builtin function set is the canon** registry (size/`type`/string &
  list fns/etc.); Trama filters (`upper`, `round`, …) register as additional ids. Id stable across
  backends.
- `SExprArr` = a Writ array of `WRef<SExpr>` **[?]** — exact array type (typed array vs
  ObjectArray) TBD (minor).

---

## 2. Relational / graph tier — `RExpr` (category 0x0011)

```logos
schema RScan   : code(0x0011_0000_0000_0000) { node_code: i56 = 0, src: i32 = 1 }      // scan all nodes of a schema in a source
schema REdge   : code(0x0011_0000_0000_0001) { input: WRef<RExpr> = 0, key: u8 = 1 }   // follow ref field → target node(s): the GRAPH STEP
schema RFilter : code(0x0011_0000_0000_0002) { input: WRef<RExpr> = 0, pred: WRef<SExpr> = 1 }       // σ
schema RProj   : code(0x0011_0000_0000_0003) { input: WRef<RExpr> = 0, exprs: WRef<SExprArr> = 1 }   // π
schema RJoin   : code(0x0011_0000_0000_0004) { left: WRef<RExpr> = 0, right: WRef<RExpr> = 1, on: WRef<SExpr> = 2 }
schema RAnti   : code(0x0011_0000_0000_0005) { left: WRef<RExpr> = 0, right: WRef<RExpr> = 1, on: WRef<SExpr> = 2 }
schema RAggr   : code(0x0011_0000_0000_0006) { input: WRef<RExpr> = 0, keys: WRef<SExprArr> = 1, aggs: WRef<SExprArr> = 2 }
schema RFix    : code(0x0011_0000_0000_0007) { base: WRef<RExpr> = 0, rec: WRef<RExpr> = 1 }  // fixpoint (recursion) — DECLARED, NOT impl in queue 1

schema enum RExpr : category(0x0011_0000_0000_0000) {
    Scan(RScan), Edge(REdge), Filter(RFilter), Proj(RProj),
    Join(RJoin), Anti(RAnti), Aggr(RAggr), Fix(RFix),
}
```

- `REdge` is the **graph traversal primitive**: for each input row, follow ref field `key` to its
  target node(s). A `WRef<S>` field → one target; an array-of-refs field → many. This is how
  reachability is later built (`RFix` over `REdge`).
- `RFix` (semi-naïve recursion) is **declared now, unimplemented in the static queue-1 backend**
  (returns "not yet") — laying the Datalog base without paying for it. This is the only knowingly
  inert node in queue 1.
- `RAggr` aggregate set **[?]** — count/sum/min/max/avg + group-by; exact agg encoding TBD.

---

## 3. Type descriptors — `TDesc` (category 0x0012) **[?]**

The result type of a query/expression is computed at compile time from the bound schema and
attached to the resource (so generated rows are statically typed, sqlx-style). Options:
- **(a)** reuse the compiler's existing type mirror (`CAT_TYPE`, `writ::schema::type(kind)`) so
  WQL result types ARE Logos types directly — preferred, maximal reuse;
- **(b)** a small WQL-local `TDesc` schema enum (scalar kinds + row/relation types).

**RESOLVED → (a) reuse the compiler `CAT_TYPE` mirror.** WQL result types ARE Logos types
directly; the static backend lowers to LIR anyway, so emit the real Logos type rather than
translating a WQL-local descriptor. No WQL-local `TDesc` schema (category 0x0012 dropped). If the
queue-2 interpreter later needs a runtime tag for erased data, it reuses the same `CAT_TYPE`
encoding — no new scheme.

---

## 4. Lightweight scalar path (invariant)

> **A scalar-only query (root `SExpr`, no `SComp`/relational descent) incurs ZERO relational
> overhead** — no iterator, no row buffer, no source init.

In the static backend this falls out of lowering `SExpr` straight to LIR + inlining. The
interpreter must special-case a bare-`SExpr` root to a direct eval (no plan executor). Trama
`{{ expr }}` and `{% if expr %}` are exactly this path.

---

## 5. FE → IR contract

A frontend (EL parser, Trama parser) produces, per resource/expression:

```
struct FeOutput {
    root:        WRef<RExpr> | WRef<SExpr>,   // relational plan, or a bare scalar (lightweight path)
    params:      [ParamDecl],                 // ordered; idx = SParam.idx
    sources:     [SourceDecl],                // idx = RScan.src; binds a name to a schema + a Source
    result_type: TDesc,                       // computed against the bound schema(s)
}
struct ParamDecl  { name: str, ty: TDesc }
struct SourceDecl { name: str, node_schema: i56 /*schema code*/, /* capabilities [?] */ }
```

- Trama emits a Trama AST (category 0x0013, port of `TplASTCodes`) whose `EXPRESSION` attributes
  hold `WRef<SExpr>` (the only coupling, per ADR 0012 §seam).
- The FE resolves field keys (`SField.key`, `REdge.key`) against the bound schema at parse time —
  unknown field = compile error here (D4 strict). So the IR handed downstream is already
  schema-checked; backends trust it.
- **[?]** Source binding: how a `wql!{}` names its inputs (in-scope Logos variables of schema /
  collection type vs explicit `from` clause). See ABI §6.

---

## 6. Resource ABI (metacall integration)

`resource` = a compile-time WQL/Trama artifact. Two surface forms:

```logos
// (a) named, reusable (a "prepared statement" item)
resource active_users = wql! {
    [ u.name for u in Users if u.active && u.age >= $min_age ]
};

// (b) inline expression position
let html = trama! { "<ul>{% for u in users %}<li>{{ u.name }}</li>{% endfor %}</ul>" };
```

**Metacall flow** (queue 1, static):
1. Compiler lexes the block body as opaque text + captures the **lexical type environment**
   (in-scope schema/collection bindings the block may reference — `Users`, `users`, `$min_age`).
2. metacall entry: `parse (FE) → schema-typecheck → IR (schemas above) → lower to LIR → emit`
   a function `fn(params…) -> result_type` + any static data tables.
3. The emitted function is statically linked; the `resource`/`trama!` site becomes a typed
   callable / value. Errors (unknown field, type mismatch, unknown function) surface as **Logos
   compile errors at the block site**.

**Open ABI questions [?]:**
- **Param syntax** — `$name` (shown) vs `:name` vs typed `$min_age: i64`. Lean: `$name`, type
  inferred from use, overridable.
- **Source binding** — **RESOLVED:** explicit sources for `wql!` (`from Users u`; clarity,
  models-as-authors P2) + implicit data context for `trama!` (jinja-style, the render `data`). FE
  emits `SourceDecl{name}` symbolically; resolving the name → concrete schema code happens at the
  metacall (dir3), so dir2 can produce IR without the type-env.
- **Result shape** — scalar / typed-row iterator / single row / `Option<row>`. Computed from the
  query shape (comprehension → iterator; aggregate → scalar; …).
- **How metacall captures the type environment** — needs the compiler to expose in-scope schema
  bindings to the metacall. **[?]** the deepest integration question; the dir3 design-intensive core.
- **Where generated data tables live** — same as other metacall-emitted statics (ADR 0011 `make`
  path precedent).

---

## 7. What queue 1 (static) implements vs declares-only

| node | queue-1 static |
|------|----------------|
| all `SExpr` | implemented (scalar → LIR) |
| `RScan`, `REdge`, `RFilter`, `RProj`, `SComp` | implemented (the EL+comprehension core) |
| `RJoin`, `RAnti`, `RAggr` | **implemented — MVP includes ALL relational ops** (user 2026-07-01) |
| `RFix` | **declared only** (Datalog base; "not yet" in backend) — the sole inert node in queue 1 |

---

## 8. Review checklist (for the user)

1. Category numbers (§0) — OK, or align with a scheme you have in mind?
2. Scalar/relational node sets (§1/§2) — anything missing for EL+comprehension MVP? `SLet`?
   string interpolation node, or does Trama own that?
3. Edge model (`SField`/`REdge`/`RFix` unification) — agree this is the one graph primitive?
4. ~~Type descriptors~~ — **RESOLVED §9: reuse `CAT_TYPE`.**
5. FE→IR contract (§5) — fields right? Source/capability model?
6. ~~Resource ABI source binding~~ — **RESOLVED §9.** Param syntax + type-env-capture still open.
7. ~~MVP relational ops~~ — **RESOLVED §9: all in.**

## 9. Review round 1 — locked (user 2026-07-01)

- **Functions/operators → CEL is the canon** (operator set + builtin function registry; Trama
  filters register as extra ids). §1.
- **Result type → reuse compiler `CAT_TYPE`**; no WQL-local `TDesc`; category 0x0012 dropped. §3.
- **Source binding → explicit `from` for `wql!`, implicit data context for `trama!`.** §6.
- **MVP relational ops → ALL in** (`Join`/`Anti`/`Aggr` implemented in queue 1; only `RFix`
  declared-only). §7.

**Still open (do not block dir1/dir2 start):**
- (minor) `SExprArr` exact Writ array type; `op`/`fn` enum value tables; `SLet` / string-interp
  node (likely Trama-owned).
- (**dir3 crux**) how the metacall captures the in-scope **type environment** to resolve
  `SourceDecl` names → schema codes + type the params/result. This is the design-intensive core of
  direction 3; FE (dir2) emits symbolic source names so it is NOT blocked.

**Seam is stable enough to parallelize.** dir1 (EE over the IR + data-source abstraction) and dir2
(FE: EL CEL+comprehension parser + Trama parser, both via [[project_peg_gen_logos]]) can start;
dir3 type-env-capture is **DEMOTED out of queue 1** — see §10.3.

## 10. Grounding round — real-code findings (2026-07-01)

Three read-only research passes over the actual compiler/stdlib. Net: the static path is MORE
tractable than the skeleton assumed; the "dir3 crux" is demoted.

### 10.1 Surface already exists
`wql! { … }` / `trama! { … }` = the grammar's `FN_MACRO_CALL` (code 225, `name! { raw_text }`,
balanced-delim RAW_TEXT) — already PARSED (raw text captured), just unused in-tree. Item-position
variant `FN_MACRO_CALL_ITEM` (226). Resource surface is free; no new grammar needed (a dedicated
`quote_raw_text!` is optional, ~20 LOC). Anchors: logos.peg:2593-2595 / :578; peg_gen_cpp/codegen.cpp:485.

### 10.2 Emission path — generate `quote_item!`, don't hand-build LIR
Handler `#[metaprog_handler("wql")]` (registered collect_fn sema_collect.cpp:1837; dispatched
run_metaprog_dispatch main.cpp:2220) → parse block text → inspect schema via OView / `type_of::<T>()`
→ emit `quote_item! { fn __wql_N(...) -> Row { … } }` → returns `QuoteItemBlob` →
`logos_emit_item_blob_subst` main.cpp:335 splices into AST → normal sema/mono/mlir_gen/JIT-link.
Hand-built LIR (lir_builder.hpp) is the fallback only. Quote forms: quote_item!(200)/quote_expr!(201)/
quote_ty!(204), lowered sema_expr.cpp:15775/16698/16484. Static data tables → `HermesStatic` parametric consts.

### 10.3 dir3 "type-env capture" — DEMOTED out of queue 1
metacall runs at COMPILE TIME in JIT and is VALIDATED to NOT capture runtime locals (lower_metacall
sema_expr.cpp:17391; capture-check :17493-17615). Correct model (= sqlx):
- TYPES (schemas, source types) read at metaprog time via reflection / **explicit block decls**;
- RUNTIME VALUES (data sources + scalar params `$x`) flow as ARGUMENTS to the generated function,
  not captures. `wql!{ from users:[User] u where u.age >= $min:i64 select u.name }` →
  `fn __wql_N(users:&[User], min:i64) -> impl Source<str>`, called with runtime values.
- ⇒ With **explicit source/param types in the block**, the general lexical-type-environment hook
  (Agent-2's highest-risk gap) is NOT NEEDED for the static path → deferred ergonomics (implicit
  binding) for a later queue. The hook = extend lower_metacall to snapshot `scope_`
  (sema_impl.hpp:1867) + module_consts_ — known mechanism, just deferred.
- Schema lookup + field/type enumeration: fully mapped, low risk — find_struct_by_name + is_schema
  + schema_fields/schema_keys/schema_type_code (SemaStructInfo, sema_collect.cpp:4099). Result type
  via make_tuple_type/make_struct_type + pool_->alloc (sema_impl.hpp:299/329).

### 10.4 Data-source abstraction (dir1) — `Source` trait, built fresh
Comprehensions (lower_list_comp sema_expr.cpp:11191; LIR SForEach lir.hpp:585) desugar EAGERLY to
array/slice loops ONLY — they do NOT iterate trait `Iterator`s yet. So WQL must NOT piggyback the
stdlib comprehension desugaring for general/Writ sources. Instead:
- Define `Source<Row>` / `IntoSource<Row>` mirroring `Iterator`/`IntoIterator` (iter.logos:146/570;
  Vec/Slice iters vec.logos:285-425). Monomorphized zero-cost (spec/monomorphization.md:21-34).
- `ArraySource<T>` (collections, via existing IntoIterator), `WritObjectSource<S>` (Writ map/array,
  slot-loop wmap.logos:242 / array.logos:164), `RefEdgeSource<From,To>` (graph edge).
- Codegen emits IntoIterator-based `for` loops (which DO work) — not comprehension sugar.

### 10.5 Graph edges (REdge) — feasible NOW via resolve()+view, no new WRef type required
No typed `WRef<S>` wrapper / traversal API in stdlib yet; refs are erased `WAny`. BUT the mechanism
exists: ref field → `WAny` → `WAny.resolve()` (static_view.logos) → `view::<S>()` (ADR 0011 typed
bind). So `REdge` builds on resolve()+view (a typed `WRef<S>` is optional later sugar).
**[verify]** ADR-0011's claimed `WRef<schema>` field-type support vs Agent-3's "no WRef in stdlib" —
reconcile before relying on it; resolve()+view is the safe baseline.

### 10.6 Net impact
- Queue-1 static MVP is buildable on existing machinery: surface (10.1) + quote_item! emission
  (10.2) + explicit-types model (10.3) + `Source` trait (10.4) + REdge via resolve+view (10.5).
- dir3 type-env hook DEFERRED (not a blocker). dir1/dir2 unblocked.
- **First de-risking slice:** see §11.6 (revised after the Writ-substrate round).

## 11. Writ substrate — what EXISTS vs what to BUILD (2026-07-01, 2 more research agents)

User's steer: we can embed Writ data of arbitrary complexity in .rodata; Writ has parameter
(named-placeholder) support; copy rodata→heap + substitute = the resource. Lean on it. Findings:

### 11.1 EXISTS in Logos+Writ today (anchors)
- **Embed arbitrary Writ in rodata**: `@{…}`/`@[…]` literals → LLVM internal globals
  `[u64 size][blob]`, content-keyed dedup. sema_expr.cpp:1490 / mlir_gen_expr.cpp:6183. Any depth.
- **Read-only zero-copy view**: `WritStatic{ptr}` + `WView2`/`wview2_from_ptr` (root/map_get/
  tiny_map_get/array_get; self-relative resolve). wstatic.logos / static_view.logos:35.
- **Deep-copy rodata→heap + re-anchor**: `compactify(dst,v)` / `clone()` — position-independent.
  compactify.logos:30; C++ clone.hpp:45.
- **Heap mutation**: `Writ` + `WMap::set` (auto-grow), schema field writes. container.logos /
  wmap.logos:217.
- **Positional capture / template-patch**: `@{… $x …}` → blob with PARAM SLOTS + `__writ_slots_<i>`
  table; `writ_template_install()` copies blob + patches slots with runtime WAny + re-anchors.
  tmpl.logos:36. ← this is already "embed-with-holes → copy → substitute", but POSITIONAL.
- **`WParameter` node EXISTS** (named placeholder data structure): `{name: WAny→WString,
  value: WAny|null}`, type code 127. compound_types.hpp:62 / stdlib parameter.logos /
  clone.cpp handles it. Created via `Writ::parameter(name, value)`.

### 11.2 Parameters are NUMERIC (user 2026-07-01) — resolution layer missing
Writ identifies parameters by a **NUMERIC id**, not a string name — faster (O(1) index vs string
hash) and it lines up with both the existing positional `$x` capture slots AND the IR's
`SParam{idx}`. The Memoria string-name `IParameterResolver` is a HISTORICAL example, **NOT** our
model — do not copy it.
- **Positional `$x` captures** — EXISTS end-to-end; slots are already NUMERIC (`__writ_slots_<i>`,
  tmpl.logos:36). This is the fast/convenient path the user flagged.
- **`WParameter` node** — EXISTS (type code 127) but the current port stores a **WString name**
  (parameter.logos:22-25 — the LEGACY `?name` port). For WQL the id must be numeric → §11.3a.
- **Numeric capture + substitution ALREADY EXISTS** (user 2026-07-01) via the template-patch path
  (`writ_template_install`, numeric `__writ_slots_<i>`). NOT missing. The only thing absent is a
  thin abstracting interface (a resolver), and even that is likely unneeded — the resource just
  REUSES the existing template-patch. ⇒ no "build the substitution keystone" step; reuse it.

### 11.3 TO BUILD in Logos+Writ (the keystone — additive, well-scoped, NUMERIC)
0. **(a) [decision] numeric param id on the node** — the parameter carries a numeric id, not a
   WString name. Lean: a numeric-id parameter representation (revise `WParameter.name`→`id: u32`,
   OR a WQL-specific numeric param node, OR reuse the positional `__writ_slots_` path directly).
   Keep the legacy string `WParameter` only if SDN `?name` debug forms still need it.
1. (trivial) `writ_from_static(WritStatic) -> Rc<Writ>` one-step wrapper.
2. **Numeric resolver**: `resolve(id: u32) -> WAny` over a DENSE table (a `[WAny]` slice / small
   struct), NOT a string map → O(1) index. Optional `is_bound(id)`.
3. **`substitute_parameters(src, resolver) -> Rc<Writ>`** — deep-copy the tree (reuse compactify +
   the parameter clone case) and fill each parameter's value from `resolver[id]` (functional,
   materialize to heap; unbound → null/error per policy).
4. eval integration: the interpreter reads `resolver[id]` on parameter nodes (queue 2).
5. (small) literal grammar to embed a numeric-id parameter in `@{…}`, or just keep positional `$x`
   (already numeric); until then, build parameterized containers programmatically.
6. (later) schema-typed parameters (ADR 0011): the param node carries an expected-type assertion;
   bind/resolve is type-checked. Ties into D4 strict typing.

### 11.4 Resource emission — the two strategies UNIFY
The IR/plan and the Trama AST are Writ-schema'd data, so they are ALWAYS emittable as a **Writ blob
in rodata** (11.1, dogfood). Backends differ only in how they CONSUME that one artifact:
- **Generic executor + resolver** — walks the blob, binds runtime values via the named-parameter
  resolver (11.3). **Shared by queue-1's generic path AND queue-2 interpreter** (same executor).
- **Specialized native codegen** — metaprog emits a typed `fn(params)->Row` via `quote_item!`
  (§10.2); params are function args. queue-1 FAST path; an OPTIMIZATION over the generic executor,
  added later.
- **VM JIT** — blob → bytecode. queue 3.
So agent-1's codegen strategy and the user's data-driven strategy are not rivals: **plan-as-blob is
the canonical artifact; generic-executor first (cheap, validates the whole substrate), native
specialization later.** The named-parameter resolver is the shared runtime-value binding for all.

### 11.5 Gaps that are NOT blockers (polish)
one-step wrapper (trivial); `?name` literal sugar (build programmatically meanwhile); conditional/
computed literal fields (metaprog); schema-sugar over a materialized blob (wiring); compression;
deserialize validation.

### 11.6 Revised first de-risking slice
**CORRECTED (user 2026-07-01):** the substitution substrate already exists (§11.2) — no keystone to
build. First real slice = the **toolchain slice** (§10.2): `wql!{}`/`trama!{}` → metaprog handler →
`quote_item!` emission → linked fn, REUSING the existing template-patch path for numeric params.
Validate end-to-end with one trivial case + test. Then build out the IR executor + frontends.
