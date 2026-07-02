# ADR 0012 — Queue-2: the runtime interpreter (dynamic WQL + Trama)

Status: ACCEPTED (design), slices I1–I3 in flight. Parent: [0012-writ-query-language.md](0012-writ-query-language.md).
Prereq: queue-1 static engine COMPLETE (tuples/rel/semi-naïve/REdge; IR final-shaped).

## 1. What queue-2 is

Query/template TEXT arrives at RUNTIME (P2: models are the primary authors), is parsed,
type-checked, optimized and executed by a tree-walk evaluator over the SAME Writ-schema IR
(SExpr/RExpr/RQuery/TStmt) the static queue compiles. No metacall, no codegen. Queue-3 (VM JIT)
later replaces the walk, not the IR.

What is REUSED verbatim (the payoff of schemas-as-IR):
- **Parsers** — el/wql_surface/trama peg-generated parsers are ordinary Logos fns over `&Writ`
  (`parse_program(src, doc) -> WAny`); they already execute at runtime. Zero new frontend.
- **Optimizer** — `simplify_sexpr` / `simplify_rexpr_ref` are pure IR→IR fns; run them at
  query-compile time. Const-fold, filter folds, empty/identity collapse: free.
- **Plan lowering** — `lower_rquery_to_rexpr` (plan → algebra) is pure IR→IR: reused.
- **Semantics** — join cascade rules, semi-naïve/stratification, aggregate rules: same
  ALGORITHMS, re-hosted from emitters to an evaluator.

## 2. Value & data model

- **Domain = Writ data.** Rows are schema'd objects (`WAny` handles, TinyObjectMap); sources are
  Writ arrays of them. Native-struct slices stay queue-1 territory (sqlx model); a native bridge
  can come later via `logos.mem.any` TypeInfo — NOT in these slices.
- **RtVal** — the runtime scalar: `schema enum RtVal { I(i64) | F(f64) | B(bool) | S(str-view) |
  Node(WAny) | Null }` (exact repr = implementor's call; must be cheap to copy). The EL lattice
  maps INT/FLT/BOOL/STR onto it; `Node` carries row/object handles; `Null` exists ONLY in lenient
  mode (§4).
- Rel rows / tuple projections: small fixed-arity RtVal groups (implementor picks repr; set
  semantics need Hash+Eq over them — mirror the tuple rules: f64 kills keyed use).

## 3. Compile-then-run API (package `logos.std.query` — the PUBLIC, ABI-carried surface)

`logos.std.wql.*` stays ABI-excluded engine internals; the runtime API is a NEW package that
wraps it and IS the stability surface:

```
let cat: SchemaCatalog = schema_catalog!(Emp, Dept);        // queue-1 macro (§5)
let q: QueryPlan = Query::compile(text, &cat)?;             // parse→typecheck→optimize, Result
let rows: QRows = q.run(&env)?;                             // env: name→source/param bindings
let t: Tpl = Tpl::compile(tpl_text, &cat)?;  t.render(&env)? -> String
```

- Errors are VALUES (`Result` + positioned message), never compile diagnostics — the caller is a
  running program (likely a model-driven loop; the error text is the model's feedback signal).
- compile-once/run-many is the contract; `run` is re-entrant over different envs.
- Env binds sources (name → Writ array handle), scalar params, and registered UDFs/UDAs (§6).

## 4. Typing: strict-on-schema, lenient-on-erased (D4 resolution for queue-2)

- **Strict mode (default):** every source is declared in the env with a schema code; the
  checker resolves `e.field` against the catalog exactly like the static queue resolves against
  the module AST. Unknown field/fn/type mismatch → compile-time (i.e. `Query::compile`) error.
- **Lenient mode (opt-in, per-source `WAny` erased):** field access on an erased source yields
  `RtVal::Null` when missing; `null` propagates CEL-style through operators; comparisons with
  null are false, `??`-style defaulting deferred. This is the D4 "lenient → queue-2" half.
- The checker is a new runtime pass (SExpr/RQuery walk over the catalog) — it REPLACES the
  reflection stamping of the static queue; EL_TY lattice + ElTypes shape reused.

### 4a. AMENDMENT (I3, as shipped) — API surface + exact Null semantics

**API** — lenient-ness is a BIND-time property (per binding, not per compile):
`env.bind_node_erased(name, node)` / `env.bind_source_erased(name, arr)`. The checker types
such roots/rows `dyn` (runtime-typed); everything else stays strict. Additionally, a
`WAny`-typed field on a STRICT schema (catalog kind `any`, e.g. `meta: WAny`) resolves
leniently — I1's check-time rejection of `FK_ANY` is lifted.

**Field resolution on a lenient value** (ONE dynamic read, shared with strict eval):
- string-keyed Writ map (`is_map`) → get by NAME; missing key → `Null`; hit → value BY SHAPE
  (int/float/bool/string → scalar, anything else → `Node` handle — "type surprise" keeps the
  runtime shape, CEL-style);
- schema-stamped TOM whose schema IS in the catalog → the cataloged key read, WritField
  DEFAULTS included (schema'd data behaves schema'd even under an erased binding); `FK_ANY`
  fields convert by shape, unset → `Null`;
- unknown schema / unknown field / non-object base / `Null` base → `Null`.

**Null propagation table** (`dyn` = check-time type of any lenient expression):

| construct | rule |
|---|---|
| `a && b`, `a \|\| b`, `!a` | truthiness; `Null` is falsy (`!Null` → `true`) |
| `==`, `!=` | `Null == Null` → `true`; `Null == x` → `false` (`!=` negates) |
| `<` `<=` `>` `>=` | any `Null` operand → `false` (Null never orders) |
| `+ - * / %`, unary `-` | any `Null` operand → `Null` (checked type `dyn`) |
| `?:` | `Null` condition takes the else branch |
| builtins (len/upper/…) | any non-STRING argument (incl. `Null`) → `Null` |
| UDF args | `dyn` args pass UNCHECKED; the `RtVal` (incl. `Null`) reaches the fn as-is |
| `where` / `{% if %}` | `Null` predicate → row dropped / branch not taken |
| `{% for %}` / scans | non-array lenient value iterates as EMPTY |
| `select` | `Null` cells are legal; column type reports `"dyn"`; `QRows::is_null(r,c)` probes, typed getters return zero-values |
| `{{ x }}` render | `Null` renders as the EMPTY string |
| `order by` | `Null` keys sort as 0 (numeric tier) — documented, not an error |
| `group by` key | `Null` keys group together (`rt_eq`) |
| aggregate args | REJECTED at check time (accumulator layout needs a checked type) |
| join keys | a `dyn` side never qualifies as a HASH key (type unknown at compile) — such joins take the LOOP tier over the full predicate |
| rel columns | a `dyn` select component cannot feed a typed rel column — check-time error |

One consequence: the I1 compile-phase "no catalog schema declares this field" rejection is
GONE — with lenient bindings legal, that field may resolve at run; the strict phase (which
sees the env's binding kinds) owns the rejection now.

## 5. SchemaCatalog — queue-1 serving queue-2

Runtime needs schema code → {field name → (key code, EL type, edge-target schema)}. Schema decls
compile away, so the catalog is GENERATED: `schema_catalog!(S1, S2, …)` is a queue-1 metacall
macro that reflects the schema decls (module AST, same machinery as wql!) and emits a static
Writ blob in .rodata (the long-planned Writ-in-rodata embedding) + a `SchemaCatalog` view over
it. Explicit-metacall house style; no global registry, no link-time magic.

**RESOLVED FORK (user, 2026-07-03) — metadata emission strategy.** Interpreter type metadata
(Datalog IR + Trama; TOM-schema metadata REQUIRED, not optional) is emitted by METAPROGRAMS:
- primary: `schema_catalog!` callsite reflection (above);
- where callsite reflection can't see the decl (cross-module — the module-AST boundary), the
  sanctioned mechanism is an ANNOTATION ON THE TYPE: a derive-style hook on the `schema` decl
  (the existing `#[metaprog_handler]`/derive_* machinery) emitting that schema's catalog entry
  AT THE DECL SITE; the callsite catalog aggregates entries.
- Explicitly rejected: global link-time registries; hand-written catalogs.
Catalog-entry representation must be IDENTICAL for both emission routes (one entry shape, two
emitters).

**GENERAL PRINCIPLE (user, 2026-07-03) — reflection parity, on-demand.** The interpreter (now)
and the VM (queue-3) must have THE SAME access to program information that metacall has — via
ON-DEMAND emission, never an always-on runtime reflection system: annotations mark WHAT the
runtime needs; metaprograms emit exactly that into .rodata as Writ blobs; runtime tiers read
views over them. ONE channel — `annotation → metaprog hook → rodata Writ blob → runtime view` —
of which the schema catalog is merely the first client. Anticipated clients: fn-signature
metadata for dynamic UDF resolution (I2, e.g. `#[query_fn]`), rel/program metadata for the VM.
rodata is the DESIGNATED channel (compile-once, shared, mmap-friendly), not a later
optimization; a heap-built catalog is acceptable only as a slice-local stopgap with the blob
shape preserved.

## 6. UDF/UDA at runtime

Registry on the env: `env.register_fn("score", <fn ptr>)` with an RtVal-based signature
(`fn(&[RtVal]) -> RtVal` MVP); UDA = init/step/fin triple of fn ptrs. Names resolve at
`Query::compile` against builtin table first, then registry — same precedence as queue-1.
**Aggregates get the GENERIC rule table here** (user requirement): one
`agg_result_ty(fn, arg_ty) -> ty` map (count:()→INT; sum/min/max:T→T; avg:T→Quot(T), Quot(INT)=
Quot(FLT)=FLT under the current tower) — the static emitter migrates to consult the same table
later.

## 7. Execution engine

- `eval_sexpr(node, env, row_bindings) -> RtVal` — tree walk; builtins + registry dispatch;
  runtime cascade = tag dispatch on RtVal (strong-typing-as-selector, runtime edition).
- Relational: materialized Vec<row> pipeline mirroring the static shapes — scan (Writ array) →
  filter → joins (hash via RtVal key when hashable: I/S/B; loop tier for F — SAME cascade rules,
  decided at compile step from checked types, not per-row) → traversal (REdge: `base_var`+`path`
  walk, `has_on`, `is_anti` NOT-EXISTS; use the semantic fields, never `src`) → sort/distinct/
  limit/first/find → aggregates (rule table §6) → project.
- rel/fixpoint: same semi-naïve algorithm (total + shadow set + delta, k-variant body rewrite
  is an INTERPRETER LOOP variable here, not code expansion); stratification checked at
  `Query::compile`.
- Trama: TStmt walk appending to a String; embedded SExprs via `eval_sexpr`; loop vars bind
  rows/elements; `{% set %}` env-scoped.

## 8. Slices

- **I1** — RtVal + eval_sexpr + runtime checker (strict) + SchemaCatalog macro + **dynamic
  Trama** (`Tpl::compile/render`): first user-visible dynamic surface. DONE.
- **I2** — relational executor (scan/filter/join cascade/REdge/mods/aggregates+rule table) +
  `Query::compile/run` for non-recursive queries; UDF/UDA registry. DONE.
- **I3** — rel blocks + semi-naïve fixpoint + stratification at runtime; lenient mode (§4a);
  registry unification (templates resolve UDFs at render against the SAME env registry —
  unknown-fn defers at compile for BOTH surfaces); `register_fn`/`register_agg` return `bool`
  (false = bad type name / capacity — no silent no-op). DONE — queue-2 COMPLETE.
  Implementation notes: the rel dependency-graph half (RelDeps + compute_rel_scc) moved to
  the metaprog-free `logos.std.wql.params` and is shared by the static walker and the
  interpreter; rel column reads compile to DOTTED idents ("p.b") over RtVal column groups;
  the delta variant is a LOOP VARIABLE (which scan node reads the delta region), no IR
  rewriting; termination = the standard Datalog contract, generative recursion deliberately
  NOT capped (static parity).
- Later (not scheduled): native-struct bridge via TypeInfo; queue-3 VM.

## 9. Non-goals (queue-2)

No codegen, no JIT, no cost-based optimization (heuristic cascade only), no incremental
view maintenance, no query cache keyed on text (caller owns compile-once), no cross-arena
source mixing in one query (single doc + params MVP).
