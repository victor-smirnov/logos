# Deem

> Scope: Deem — Logos's native query facility over Writ data. Two surfaces share one Writ-schema IR (SExpr scalar tier + RExpr relational/graph tier): the STATIC `deem` LANGUAGE ITEM (`pub? deem q(params) { query }` — metacall → native fn, sqlx-style prepared statement; the historical `deem!` macro is RETIRED, its spelling errors with the replacement written out), which since P5 is the ONLY query surface, and the runtime TEMPLATE engine (`Tpl`, specced in `docs/spec/trama.md`). The DYNAMIC `Query::compile(text,&cat)?.run(&env)?` query API was deleted at P5 with the interpreter (`deem.exec.dynamic-api`). This spec is ALSO the canonical home of EL (rule domain `el.*`), the CEL-class expression sublanguage embedded by both Deem clauses and Trama (`docs/spec/trama.md` links these `el.*` ids). Deem ships/versions with the language but is a metaprogramming/stdlib surface, so it has its own spec. Source layers: `stdlib/mem/wql/grammars/{wql,el}.peg` (PEG surfaces, schema-emission mode), `stdlib/mem/wql/*.logos` (engine — ABI-excluded internals), `stdlib/mem/deem/deem.logos` (package `logos.mem.deem`, ABI-carried: `SchemaCatalog`, `QEnv`, `RtVal`, `QError` — the runtime binding types the template engine `stdlib/mem/deem/tpl.logos` reads; no query API), ADR 0012 (`docs/adr/0012-writ-query-language.md`) + ADR 0012-queue2 (`docs/adr/0012-queue2-interpreter.md`; the design of the deleted interpreter, whose template half survives). Each rule's `id` is its permanent linkable address; the domain is `deem` for the query surface and `el` for the shared expression language.

## Surfaces and execution model

### `deem.surface.static-item` — `pub? deem q(params) { query }` language item

The static surface is the `deem` ITEM (grammar/Sema-owned head; contextual lead ident — see `item.deem.*` in `items.md`): the COMPILER parses the body with the C++ parser generated from the same `wql.peg` the runtime parser comes from and hands the stdlib handler a zero-copy pointer into its arena; the handler walks the plan and emits `fn <name>(<params>) -> <Ret>` — a prepared statement compiled to native code. Item visibility is real (`pub`/none); the historical `resource <name> = deem!(…){…};` macro spelling is RETIRED and errors with the item replacement written out (name and params substituted).

*Divergence:* the compile-time-typed-query model is `sqlx::query!` for Writ; unlike SQL there is no runtime query planner in this surface.

*Evidence:* `src/compiler/sema_expr.cpp` (lower_deem_def), `stdlib/mem/wql/wql.logos`, `tests/logos/pass/wql_deem_item_e2e.logos`, `tests/logos/fail/wql_deem_macro_retired_fail.logos`

### `deem.surface.params` — parenthesized parameter list

The parens carry a genuine Logos fn parameter list, re-emitted VERBATIM into the generated signature (param order preserved) and thus type-checked by the compiler; the handler also parses it locally to bind sources and type scalars — there is no `$` sigil and no `with` clause (both retired).

*Divergence:* EXTENSION over SQL/LINQ — query inputs are ordinary strongly-typed Logos function parameters, not bind markers.

*Evidence:* `stdlib/mem/wql/wql.logos` (`deem`); parser `stdlib/mem/wql/params.logos` (`parse_macro_params`, `MacroParams`)

### `deem.surface.source-param` — slice params are sources

A slice param (`emps: &[Emp]`) is a query SOURCE named by a `from`/`join` clause by its param name; the row type is the slice ELEMENT type, its fields reflected automatically; a source ident matching no slice param is a compile error.

*Divergence:* EXTENSION — sources are typed Rust-style slices, giving static row-field resolution (P3 schema-typing-as-selector).

*Evidence:* `stdlib/mem/wql/wql.logos` (`deem`); source resolution `stdlib/mem/wql/plan_walker.logos` (`resolve_source`); reflection `stdlib/mem/wql/reflect.logos`

### `deem.surface.scalar-param` — scalar params referenced bare in EL

A scalar param (`i64`/`f64`/`str`/`bool`) is referenced BARE (no sigil) inside EL clause bodies and by `limit`; its EL value-type is seeded from the declared param type (`el_ty_of_name`).

*Divergence:* differs from CEL/SQL bind variables — a scalar param is a plain in-scope name, not a `$`-prefixed or `?` positional bind.

*Evidence:* `stdlib/mem/wql/wql.logos` (`deem`); seeding `stdlib/mem/wql/rexpr_walk.logos` (`apply_scalar_params`); `stdlib/mem/wql/el.logos` (`ElTypes::set_ty_named`, `ElTypes::class_of_name`, `el_ty_of_name`)

### `deem.surface.pipeline` — clause pipeline order

Evaluation order is `from → [join…] → where → group/aggregate → having → order → project(select) → distinct → limit`; `having` exists only on the aggregate shape.

*Divergence:* matches SQL logical clause ordering (WHERE before GROUP BY before HAVING before ORDER BY before the projection's DISTINCT/LIMIT).

*Evidence:* `stdlib/mem/wql/grammars/wql.peg` (`simple_query`, `join_query`, `aggr_query`); lowering `stdlib/mem/wql/lower.logos` (`lower_simple`: RQSimple pipeline where→order→project→distinct→limit; `lower_join`, `lower_aggr`)

### `deem.surface.program-envelope` — rel blocks + one entry query

The macro body is an `RQProgram` envelope: zero or more `rel NAME(cols){ bodies }` blocks followed by exactly one entry query; a rel-less body still parses as a program (the `rels` edge is NULL).

*Divergence:* the rel/entry split mirrors Datalog's rules + goal; a bare entry query is the degenerate zero-rule program.

*Evidence:* `stdlib/mem/wql/grammars/wql.peg` (`program`, `rel_list`, `rel_block`); `stdlib/mem/wql/wql.logos` (`deem`), `stdlib/mem/wql/plan_walker.logos` (`walk_program_params`)

## Query shapes

### `deem.query.from` — `from src var`

Every query opens with `from <src> <var>`: `src` names a source (slice param or rel), `var` binds the row loop variable used by all downstream EL clause bodies.

*Divergence:* the range-variable binding is Datalog/comprehension style (`from src var`) rather than SQL's post-hoc `FROM t` with column-scoped names.

*Evidence:* `stdlib/mem/wql/grammars/wql.peg` (`simple_query`, `join_query`, `aggr_query`, `find_query`)

### `deem.query.simple` — RQSimple scan/filter/project

`from src v [where P] select [first] [distinct] S [order by O [desc]] [limit N|p] [: RTy]` — a single-source scan with optional filter, projection, and select-tail modifiers.

*Divergence:* the SQL `SELECT … FROM … WHERE …` single-table query, with the clause keywords reordered to source-first.

*Evidence:* `stdlib/mem/wql/grammars/wql.peg` (`simple_query`)

### `deem.query.join` — RQJoin N-way join chain

`from a x ([anti] join b y on ON)+ [where P] select …` — one or more join steps chained after the source; each step's `on` predicate may reference every var bound so far plus scalar params; the chain lowers left-deep.

*Divergence:* SQL `INNER JOIN … ON` / `WHERE NOT EXISTS` (anti), generalized to an N-way left-deep chain; `LEFT/RIGHT/FULL OUTER` joins are NOT provided (RESTRICTION).

*Evidence:* `stdlib/mem/wql/grammars/wql.peg` (`join_query`, `join_steps`, `join_step`); lowering to left-deep `RJoin`/`RAnti`/`REdge` `stdlib/mem/wql/lower.logos` (`fold_join_steps` + `lower_join`)

### `deem.query.aggregate` — RQAggr group-by + aggregate

`from a x ([anti] join b y on ON)* [where P] group by K aggregate name=fn(arg?),… [having H] select …` — join steps are OPTIONAL here (aggregate over the joined or single-source stream); `having` is a predicate over the group key + aggregate output names.

*Divergence:* SQL `GROUP BY … HAVING …`, restricted to a SINGLE group key expression `K` (RESTRICTION; no multi-column `GROUP BY a,b` — use a tuple key expression).

*Evidence:* `stdlib/mem/wql/grammars/wql.peg` (`aggr_query`, `agg_list`, `having_clause`); lowering `stdlib/mem/wql/lower.logos` (`lower_aggr`: where→RAggr→having-as-RFilter→order→project→distinct→limit)

### `deem.query.find` — RQFind single-row borrow

`from src var find P` — REPLACES where+select: the generated fn returns `Option<&Ty>`, a borrow of the FIRST row matching `P` (early-exit scan), `None` when none match; no other clause may follow.

*Divergence:* EXTENSION — like Rust `Iterator::find` returning a borrow, not a SQL construct; `P` may reference scalar params bare.

*Evidence:* `stdlib/mem/wql/grammars/wql.peg` (`find_query`, `find_body`); lowering `stdlib/mem/wql/lower.logos` (`lower_find`: RProj(identity) over RFilter over RScan); emission `stdlib/mem/wql/rexpr_walk.logos` (`emit_find`)

### `deem.query.shape-dispatch` — ordered-choice shape selection

The four shapes are distinguished by PEG ordered choice — join, then aggregate, then find, then the simple fallback — each re-parsing the shared `from src var` prefix (packrat-memoized), disambiguated by the structural keyword after the source.

*Divergence:* no analogue; a grammar/parsing detail.

*Evidence:* `stdlib/mem/wql/grammars/wql.peg` (`query`)

## Clauses and modifiers

### `deem.clause.where` — `where P`

`where <P>` filters the row stream to rows for which the EL predicate `P` (a `bool`) holds; lowers to an `RFilter` (σ).

*Divergence:* SQL/LINQ `WHERE` / `.filter(…)`.

*Evidence:* `stdlib/mem/wql/grammars/wql.peg` (`where_clause`, `where_body`); `stdlib/mem/wql/ir.logos` (`RFilter`)

### `deem.clause.group-by` — `group by K`

`group by <K>` partitions rows by the EL key expression `K`; groups feed the `aggregate` specs; lowers to an `RAggr` (γ) carrying one key + the aggregate-spec array.

*Divergence:* SQL `GROUP BY`, single-key only (see `deem.query.aggregate`).

*Evidence:* `stdlib/mem/wql/grammars/wql.peg` (`aggr_query`, `key_body`); `stdlib/mem/wql/ir.logos` (`RAggr`)

### `deem.clause.aggregate` — `aggregate name=fn(arg?),…`

`aggregate <name>=<fn>(<arg>?),…` binds each aggregate output to a column `name` computed by `fn` over the group; `count()` is the sole nullary form (matched first in ordered choice), the arg-bearing form `fn(e)` carries an EL argument.

*Divergence:* SQL aggregate list with explicit output aliasing (`name=fn(arg)` vs `fn(arg) AS name`).

*Evidence:* `stdlib/mem/wql/grammars/wql.peg` (`agg_list`, `agg_item`); `RQAgg`/`RAgg` `stdlib/mem/wql/grammars/wql.peg` (`%schema` entry `RQAgg`), `stdlib/mem/wql/ir.logos` (`RAgg`)

### `deem.clause.having` — `having H`

`having <H>` filters GROUPS by an EL predicate over the group key + aggregate output names; it has no IR node of its own — it lowers to an `RFilter` over the `RAggr` output (one filter mechanism).

*Divergence:* SQL `HAVING`; exists only on the aggregate shape.

*Evidence:* `stdlib/mem/wql/grammars/wql.peg` (`having_clause`, `having_body`); lowering note `stdlib/mem/wql/ir.logos` (the ORDER/LIMIT/DISTINCT tier comment above `RSort`), `stdlib/mem/wql/lower.logos` (`lower_aggr`)

### `deem.clause.select` — `select S`

`select <S>` projects each surviving row to the EL expression `S`; lowers to an `RProj` (π); the projected value's EL type determines the row element type of the result `Vec`.

*Divergence:* SQL `SELECT`; a single projection expression (scalar or tuple), NOT a comma-separated column list — multiple columns are a `select (a,b,…)` tuple (see `deem.project.tuple`).

*Evidence:* `stdlib/mem/wql/grammars/wql.peg` (`sel_body`); `stdlib/mem/wql/ir.logos` (`RProj`); emission `stdlib/mem/wql/rexpr_walk.logos` (`emit_rexpr`, `emit_simple`)

### `deem.select.distinct` — `select distinct S`

`select distinct <S>` dedups the PROJECTED values; lowers to an `RDistinct` (δ) ABOVE the projection; the MVP dedup is a linear `__out` scan on native `==` (O(n²)).

*Divergence:* SQL `SELECT DISTINCT`.

*Evidence:* `stdlib/mem/wql/grammars/wql.peg` (`KW_DISTINCT?` in `simple_query`/`join_query`/`aggr_query`); `stdlib/mem/wql/ir.logos` (`RDistinct`); dedup `stdlib/mem/wql/rexpr_walk.logos` (`push_guarded_frag`)

### `deem.select.first` — `select first S`

`select first <S>` makes the query SINGLE-ROW: the fn returns `Option<ElemTy>` — the first projected value in scan order (or in `order by` order when present) — emitted with early-return on the first match (no `Vec`); `first` excludes `distinct` and `limit`.

*Divergence:* EXTENSION — like `SELECT … LIMIT 1` returning an `Option` rather than a one-row set.

*Evidence:* `stdlib/mem/wql/grammars/wql.peg` (`KW_FIRST?` in `simple_query`/`join_query`/`aggr_query`); emission `stdlib/mem/wql/rexpr_walk.logos` (`SELM_FIRST`, `emit_simple`, `emit_join_chain`); exclusion diagnostics `stdlib/mem/wql/plan_walker.logos` (`walk_query`)

### `deem.select.order-by` — `order by O [desc]`

`order by <O> [desc]` sorts by ONE key expression `O` (ascending default, `desc` for descending); lowers to an `RSort` (τ) that sits UNDER the projection (its key ranges over the input rows / the group key + aggregate outputs); the MVP sort is a stable O(n²) insertion permutation.

*Divergence:* SQL `ORDER BY`, restricted to a SINGLE sort key (RESTRICTION; no `ORDER BY a, b` — compose a tuple key or reorder).

*Evidence:* `stdlib/mem/wql/grammars/wql.peg` (`order_clause`, `order_body`); `stdlib/mem/wql/ir.logos` (`RSort`); emission `stdlib/mem/wql/rexpr_walk.logos` (`peel_sort`, `sort_perm_frag`)

### `deem.select.limit` — `limit N | param`

`limit <N|p>` truncates to the first N rows: N is either an INTEGER literal or a bare IDENT naming a scalar param from the deem parameter list; lowers to an `RLimit` ABOVE the projection.

*Divergence:* SQL `LIMIT` (no `OFFSET`, RESTRICTION); the param form is the prepared-statement bind.

*Evidence:* `stdlib/mem/wql/grammars/wql.peg` (`KW_LIMIT? IDENT? INTEGER?` in `simple_query`/`join_query`/`aggr_query`); `stdlib/mem/wql/ir.logos` (`RLimit`); emission `stdlib/mem/wql/rexpr_walk.logos` (`peel_limit`, `limit_expr`, `check_limit_param`)

### `deem.select.result-ty` — `: RTy` result-type annotation

A trailing `: <ResultTy>` names the result element type explicitly (a type-name IDENT); presence is tracked by `has_result_ty`.

*Divergence:* EXTENSION — an explicit static result-type ascription, no SQL analogue.

*Evidence:* `stdlib/mem/wql/grammars/wql.peg` (`result_clause`)

## Projections

### `deem.project.scalar` — scalar projection

`select <expr>` where `<expr>` is a scalar EL expression produces a `Vec<T>` whose element type `T` is the projected expression's EL type; `select first` gives `Option<T>`.

*Divergence:* SQL single-column projection.

*Evidence:* `stdlib/mem/wql/rexpr_walk.logos` (`emit_simple`); `stdlib/mem/wql/codegen.logos` (`infer_ty`, `infer_emit_ty`)

### `deem.project.tuple` — tuple projection `(a,b,…)`

`select (a, b, …)` (≥2 components, EL `STuple`) produces a native `Vec<(T1,T2,…)>`; under `select first` it is `Option<(T1,T2,…)>`; tuples are legal ONLY in a `select` position (rejected in where/on/group-by/having/order-by/aggregate-arg/find).

*Divergence:* EXTENSION — multi-column projection is a first-class Logos tuple (matches Rust iterator `.map(|r| (a,b))`), unlike SQL's flat column list.

*Evidence:* `stdlib/mem/wql/grammars/el.peg` (`primary` STuple alternative, `tuple_body`); `stdlib/mem/wql/ir.logos` (`STuple`); type emission `stdlib/mem/wql/codegen.logos` (`push_tuple_ty`); non-select rejection `stdlib/mem/wql/codegen.logos` (`reject_tuple`)

### `deem.project.find-borrow` — `find` returns `Option<&Row>`

`find P` projects nothing — it returns a zero-copy borrow `Option<&Ty>` of the first matching row (early-exit), distinct from `select first` which returns a projected value BY VALUE.

*Divergence:* EXTENSION — see `deem.query.find`.

*Evidence:* `stdlib/mem/wql/rexpr_walk.logos` (`emit_find`)

## Joins

### `deem.join.step` — one join step `[anti] join src var on P`

A join step introduces a new source `src` bound to `var` with a required `on` predicate `P` (classic form); `anti` makes it an anti-join; steps chain N-way and each `on`/`where` may reference every var bound so far plus scalar params.

*Divergence:* SQL `[NOT EXISTS] JOIN … ON`, restricted to the equi/theta forms below (no outer joins).

*Evidence:* `stdlib/mem/wql/grammars/wql.peg` (`join_step`, `join_steps`); `stdlib/mem/wql/ir.logos` (`RJoin`, `RAnti`)

### `deem.join.cascade-hash` — join-strategy cascade by key-type capability

The `on` predicate is split into an equi-key term (`<bound-side> == <new-side>`) plus a residual, and the strategy is chosen from the equi-key type's capability: `Hash+Eq` → HASH join, else `Ord` → TREE join, else `PartialEq` → nested-LOOP join, else a compile error; f64 keys land in the LOOP tier (no hash/tree).

*Divergence:* EXTENSION over SQL (which leaves strategy to a cost planner) — Deem picks the strategy statically from the key TYPE's trait capability, the "strong-typing-as-selector" principle; f64's lack of Hash/Ord is a documented RESTRICTION forcing the loop tier.

*Evidence:* `stdlib/mem/wql/join_sel.logos` (`join_key_caps`, `join_key_caps_named`, `key_caps_of`; `decide_join_step`, tier selection `step_cascade`); equi/residual split `stdlib/mem/wql/join_sel.logos` (`step_terms`, `step_equi_key`), `stdlib/mem/wql/optimize.logos` (`split_and_terms`, `refs_mask`)

### `deem.join.equi-residual-split` — equi-key vs residual predicate split

The conjunctive `on` predicate is decomposed into AND-terms; the first usable `<bound> == <new>` cross-var equality becomes the join KEY (driving the hash/tree probe), and the remaining terms form a residual filter applied after the probe.

*Divergence:* standard relational equi-join / theta-join separation; the split logic is one analysis (`join_sel`) shared by the static planner and emitter; the dynamic interpreter that also used it was deleted at P5.

*Evidence:* `stdlib/mem/wql/join_sel.logos` (`step_terms`, `step_equi_key`, `equi_term_sides`), `stdlib/mem/wql/rexpr_walk.logos` (`emit_step_texts`); shared analysis `stdlib/mem/wql/optimize.logos` (`split_and_terms`/`name_refs`/`refs_mask`)

### `deem.join.anti` — anti-join emission

`anti join src var on P` keeps a bound row iff NO `src` row satisfies `P`; emission has three tiers — nested-loop full-predicate scan, hash-set containment (no residual), and hash/tree bucket-scan (absent-or-all-fail with residual) — each guarding the outer body with `if (!matched) { … }`.

*Divergence:* SQL `WHERE NOT EXISTS` / anti-semi-join; the tiering mirrors the inner-join cascade.

*Evidence:* `stdlib/mem/wql/rexpr_walk.logos` (`build_phase_frag`, `step_wrap`)

## Edge traversal (graph steps)

### `deem.edge.traversal` — `[anti] join base.field[.field] var [on P]`

A traversal step ranges a new `var` over a COLLECTION FIELD PATH of an already-bound row var (`base.field…`); `on` is OPTIONAL (containment IS the join, `P` is a residual filter); it lowers to `REdge`, not `RJoin`.

*Divergence:* EXTENSION — the graph "edge follow" step (ADR 0012 graph data model: `SField` ⊂ `REdge` ⊂ `RFix` is the same edge primitive at three iteration depths); no SQL analogue (closest is `UNNEST`/lateral join).

*Evidence:* `stdlib/mem/wql/grammars/wql.peg` (`join_step`, `path_segs`, `path_seg`, `on_clause`); `stdlib/mem/wql/ir.logos` (`REdge`); emission `stdlib/mem/wql/rexpr_walk.logos` (`collect_chain`, `analyze_chain`, `step_wrap`)

### `deem.edge.always-nested-loop` — traversal is always nested-loop

An `REdge` source depends on outer row vars (no build-once index exists), so traversal ALWAYS emits a nested loop and the join-strategy cascade bypasses `REdge` steps.

*Divergence:* no analogue; an execution-strategy consequence of the correlated source.

*Evidence:* `stdlib/mem/wql/ir.logos` (`REdge`); `stdlib/mem/wql/join_sel.logos` (`traversal_step_sel`); `stdlib/mem/wql/rexpr_walk.logos` (`analyze_chain`)

### `deem.edge.anti-traversal` — anti-traversal

`anti join base.field var [on P]` keeps the bound row iff NO element satisfies `P` (or, with no `on`, iff the collection is empty).

*Divergence:* EXTENSION — anti-semantics over a correlated collection.

*Evidence:* `stdlib/mem/wql/ir.logos` (`REdge`, field `is_anti`); grammar `stdlib/mem/wql/grammars/wql.peg` (`join_step`)

### `deem.edge.path-classification` — traversal form ordered before classic

The traversal step alt is ordered FIRST and demands ≥1 `.field` segment after the head IDENT, so the classic `join src var on …` form (no dot) can never shadow it under PEG ordered choice.

*Divergence:* no analogue; a grammar disambiguation.

*Evidence:* `stdlib/mem/wql/grammars/wql.peg` (`join_step`, `path_segs`)

## Aggregates

### `deem.agg.builtins` — count/sum/min/max/avg

The five builtin aggregates are `count` (nullary), `sum`, `min`, `max`, `avg` (each unary over an EL argument); `is_builtin_agg`/`agg_takes_arg` classify names; unknown aggregate names are diagnosed.

*Divergence:* the SQL aggregate set minus statistical extras; `count(*)` is spelled `count()`.

*Evidence:* `stdlib/mem/wql/el.logos` (`is_builtin_agg`, `agg_takes_arg`); emission ids `AGG_COUNT..AGG_AVG` `stdlib/mem/wql/rexpr_walk.logos` (`AGG_COUNT`, `AGG_SUM`, `AGG_MIN`, `AGG_MAX`, `AGG_AVG`, `agg_of_name`); unknown-name diagnostic `stdlib/mem/wql/rexpr_walk.logos` (`resolve_aggs`)

### `deem.agg.result-ty-table` — the generic aggregate result-type rule table

One shared `agg_result_ty(fn, arg_ty) -> ty` table maps `count:()→INT`, `sum/min/max:T→T` (numeric T), `avg:T→Quot(T)` where `Quot(INT)=Quot(FLT)=FLT` (the exact mean is f64 division); an out-of-domain argument (non-numeric to sum/min/max/avg) or unknown name returns -1 (diagnosed).

*Divergence:* EXTENSION — a single typed rule table, consulted by the static emitter (`compute_agg_reprs`; the dynamic interpreter that also consulted it was deleted at P5), unlike SQL's per-function return-type rules; `avg` always widens to f64 even over integers.

*Evidence:* `stdlib/mem/wql/el.logos` (`agg_result_ty`, `el_quot_ty`); float-repr emission `stdlib/mem/wql/rexpr_walk.logos` (`compute_agg_reprs`, `stamp_agg_out_types`); ADR 0012-queue2 §6

### `deem.agg.avg-float` — avg accumulates and divides as f64

`avg` casts an integer argument to f64, accumulates a f64 sum, and divides by the count as f64, so its result is always FLT regardless of argument type.

*Divergence:* differs from SQL engines where `AVG` of an integer column may stay integer or decimal; Deem fixes `avg → f64`.

*Evidence:* `stdlib/mem/wql/rexpr_walk.logos` (`agg_fold_frag`, `group_binds_frag`); `stdlib/mem/wql/el.logos` (`el_quot_ty`)

## Graph sources and the edge vocabulary

### `deem.graph.vocabulary` — the eight-column edge relation

Every graph-shaped source materializes as ONE relation `edge(parent: i64, key: str, idx: i64, child: i64, kind: str, tag: i64, vi: i64, vs: str)`: container nodes carry structure (ids = handles/addresses; `key` = field/map key, `idx` = array position, −1 otherwise), leaves carry the value in the TYPED payload columns `vi`/`vs` with `kind` as the discriminator. Payload columns are TOTAL — canonical fillers `0`/`""`, never Null (rel rows are set-deduplicated; two-valued Eq only). `bool` rides `vi` as 0/1; `f64` rides `vi` as its IEEE-754 BITS (`kind == "f64"`; bit identity is the honest Eq for floats — NaN payloads and ±0.0 stay distinct; recover via `f64_from_bits`). ONE vocabulary across all producers and binding times: the Writ walker, the native derive, and the runtime tree scan.

A NON-CONTAINER node's `child` id is not its value word (equal ints share a Pod word; equal strings may intern) but the synthetic FNV-1a of two 64-bit words — the parent's word and the edge's ordinal within that parent — folded from offset basis `14695981039346656037` with prime `1099511628211`. The virtual root edge is a row of this same relation, so it takes the same rule at its own coordinates (parent word `0`, ordinal `0`): a null/scalar/string root has `child == FNV(0, 0) == 590684067820433389` at EVERY binding time. This is a derived value, not a per-engine choice. *Known gap, recorded not closed, WITH A REPRO:* being folded from the reserved parent `0`, that id is the one node id in the vocabulary that is document-INDEPENDENT, so two non-container-rooted documents joined on `child` match spuriously. REACHABLE SURFACE, measured: a USER-WRITTEN cross-source join on `child` only. `**` and the gpath steps join on `child` at four sites in the lowering, and all four are SINGLE-SOURCE (`__reach_<src>` is built from one `src`; a gpath step carries the same `src_name` on both sides), so no collision arises there; within one document none arises either, every non-root leaf id being salted by a real parent handle. The two obvious repairs were priced and NEITHER holds as stated — salting the root id with a document handle is not symmetrically implementable (the dynamic side had no document behind a Pod root: `bind_source_tree` stored the value word itself — that bind is now removed, so this half of the pricing is historical), and scoping root ids per document is enforceable only at the direct site (a rel column launders `child` into an untainted `i64`, and the static and dynamic checkers would each need their own copy of the rule). The id rule is therefore an OPEN RULING, not a pending repair.

The VIRTUAL ROOT EDGE's coordinates (`parent == 0`, `key == ""`, `idx == -1`, ordinal `0`) are ONE named constant set — `WG_ROOT_PARENT`/`WG_ROOT_KEY`/`WG_ROOT_IDX`/`WG_ROOT_ORD` in `logos.std.wql.writ_graph` — consumed by all three producers (the static Writ walker, the runtime tree scan, and the `#[derive_graph_source]` materializer, which emits them into the user's module through the `use` its quote already carries) and by the ONE reader, the graph-path anchor in the lowering. `0` is available as "no parent node" because no real node id is `0`: a Writ container id is a live handle and a derived struct id is its address.

*Evidence:* `stdlib/mem/wql/writ_graph.logos` (wg_emit, `WG_ROOT_*`), `stdlib/mem/deem/graphsrc.logos` (the runtime tree scan: `ts_scan`/`ts_walk`, entry points `dyn_graph_edges`/`dyn_graph_edge_rows`; the edge-rows scan `es_scan` died with `stdlib/mem/deem/exec.logos`), `tests/logos/pass/wql_native_graph_e2e.logos` (f64 bits, executed), `tests/logos/pass/wql_graph_null_root_row.logos` (root id, both engines, hand-derived), `tests/logos/pass/derive_graph_source_root_row.logos` (the derive's root row, all eight columns + the three producers and the reader on one coordinate), `tests/logos/pass/wql_graph_root_id_cross_document.logos` (the collision, as a tripwire — it asserts the DEFECT and goes red when the ruling lands)
<!-- spec-gone: stdlib/mem/deem/exec.logos — deleted at P5: the dynamic EXECUTOR (rt_cmp, exec_root, RelCtx, OutTab) -->

### `deem.graph.writ-param` — `g: &Writ` is a graph source

A deem param typed `&Writ` registers the edge relation under the param's own name; the document is scanned edge-per-row (expansion-once: DAG/cycle-safe), with a VIRTUAL ROOT EDGE (`parent == 0`) making the root queryable. No materialized copy of the document exists — the document IS the fact base.

The root edge is UNCONDITIONAL: EVERY document is a row of the relation, including a null-rooted (empty) one, whose whole relation is that single row (`kind == "null"`, `tag == 0`, canonical payload fillers `0`/`""`). The relation's CARDINALITY is therefore binding-time independent — the static walker and the runtime tree scan return the same count for the same document. An empty document does not have an empty graph; it has a one-row graph.

*Evidence:* `stdlib/mem/wql/writ_graph.logos`, `tests/logos/pass/wql_writ_graph_e2e.logos`, `tests/logos/pass/wql_graph_null_root_row.logos` (null root, both engines, all eight columns)

### `deem.graph.path-sugar` — `from g .key [*] * {kind} ** v` graph paths

`from <graph> <step>* <binder>` navigates: `.key` (map/field move), `[*]` (array elements, `idx >= 0`), `*` (any child), `{kind}` (a FILTER on the current node's kind, not a move), `**` (descendant-or-self). Steps desugar to a classic join chain over the edge relation in ONE shared plan→plan pass used by BOTH binding times; `**` lowers to an INJECTED ordinary Datalog relation `__reach_<src>` (self-pairs + transitive step; deduped by name per program), so reachability runs on the existing rel machinery — no second engine.

*Evidence:* `stdlib/mem/wql/lower.logos` (gp_desugar/gp_reach_rel), `tests/logos/pass/wql_gpath_e2e.logos`

### `deem.graph.native-derive` — `#[derive_graph_source]` for native objects

Native Logos objects are deliberately UNTAGGED (types are known statically or via dyn Trait/TypeId; the tag system is a Writ-style special case), so their traversal is GENERATED at compile time by reflection: per annotated struct the derive emits a walker + a materializer `__gs_edges_<T>` + `impl GraphSource for T` — the same vocabulary (node id = address, `tag = 0`, Vec fields as a container node with `idx`-ed elements). Field classes v1: i64/bool/str/f64, `Vec<i64|str|Struct>`, nested annotated structs; dyn-Trait fields (vtable + TypeId) are the named v2.

*Evidence:* `stdlib/mem/compiler/metaprog/derive_graph_source.logos`, `tests/logos/pass/wql_native_graph_e2e.logos`

## Source traits

### `deem.source.trait` — `trait { rel … }` declares a source vocabulary

A trait may declare `rel` members (`rel edge(parent: i64, …);` — a column type must implement `Hash`, checked by sema once every impl is collected; a column typed by a trait type parameter is checked at the impl); an impl binds each rel to a MATERIALIZER (`rel edge = writ_graph_edges;`, `fn(&T) -> Vec<RowTuple>`). A deem param typed by an implementing type carries the trait's relations: a single-rel vocabulary is addressable as the param itself (`from g …`), a multi-rel one is param-prefixed (`from e_trace t …`). The walker is source-type-blind — which params carry relations, their columns, and the materializer all arrive as compiler-computed data (the natspec).

The mechanism is OPEN: any user type may implement a source trait. Since P5 the only source declared in the stdlib is `Writ` (`impl GraphSource for Writ`, `rel edge = writ_graph_edges;`) — the `IncrRec`/`EngineState` instance was withdrawn with the interpreter, see `deem.source.engine-state` below.

*Evidence:* `src/compiler/sema_collect.cpp` (`SemaChecker::check_rel_column_types`), `src/compiler/sema_impl.hpp` (`rel_col_type_hashable`), `tests/logos/fail/deem_rel_col_hashable_fail.logos`, `tests/logos/fail/wql_source_trait_f64_col_fail.logos`; `stdlib/mem/wql/writ_graph.logos` (the `Writ` instance); `tests/logos/pass/wql_source_trait_e2e.logos` exercises the OPEN mechanism with user types (`MyGraph`, `Timetable`) and never names `Writ`; the built-in instance is exercised by `tests/logos/pass/wql_writ_graph_e2e.logos` and `tests/logos/pass/wql_gpath_e2e.logos`.

### `deem.source.engine-state` — WITHDRAWN at P5

**This rule is withdrawn. It is kept as a record of what was removed, not as a description of the language.**

It read: a deem param typed `&IncrRec` carries the `EngineState` vocabulary (`impl EngineState for IncrRec`): four relations `<p>_trace(epoch, kind, step, delta, total, ns)` · `<p>_epochs(…)` · `<p>_tail(…)` · `<p>_controls(…)` — sensor facts about the COMPLETED past (the I1 contract). This was the self-applicability seam (ADR 0015/0016 case S): the engine is a source like any other, and its honesty oracles (Σδ consistency, the raise/converge Encounter pair) were expressed in Deem itself.

P5 deleted the Deem interpreter, and `IncrRec` with it. The SEAM survives — `deem.source.trait` is generic and source-type-blind — but the language has no engine to point it at, so nothing implements `EngineState` and the capability is gone rather than merely untested. It is census §6 row L9, and the C++ fallback in `SemaChecker::seed_builtin_source_impls` that would have silently re-admitted the vocabulary was deleted in the same commit; `logos_00_census_pin` FACT 6(i) reds if any `CUT-SYMBOL` returns as a definition under `stdlib/` or as a string literal under `src/`.

⚠ This entry stood in the spec describing a live rule for the length of the deletion round, with `*Evidence:*` naming two files that no longer exist (and one path that had been wrong — `stdlib/mem/deem/` for a file that lived in `stdlib/mem/deem/`). **No fact of any gate reads `docs/spec/`**, which is why prose in the spec outlived prose in the census.

## Mappings (consumption; the item is specced in items.md)

### `deem.mapping.fusion` — `deem q(w: M)` splices the mapping's rules

A deem param TYPED by a mapping name fuses that mapping's rules into the program: the canonical rel list is prepended, parsed as ONE program, the param's type rewrites to the mapping's source type in the emitted signature, and the mapping's own source param is renamed to the consumer's inside just the spliced rels. Fusion, not materialization: one RelDeps/SCC, one fixpoint; recursion and `**` work across the seam; consumer rels may build on spliced ones. Item order and module boundaries do not matter (pre-scan registry; archives carry consumed mappings as `MAPPING_DEF_DONE` with identity intact; visibility = the fn three tiers).

*Evidence:* `tests/logos/pass/wql_mapping_consume_e2e.logos`, `tests/logos/pass/wql_mapping_cross_module_e2e.logos`

### `deem.mapping.generic` — `mapping M<S: Bound>(g: &S)` instantiated by fusion

A generic mapping is a PURE rule module (no standalone fns; bodies validated at first consumption). `deem q(w: M<T>)` checks the bound per-trait (every rel of the bound bound in T's impls) and substitutes `&S → &T` — the one place S appears. One rule module serves every implementing source type.

*Evidence:* `tests/logos/pass/wql_mapping_generic_e2e.logos`, `tests/logos/fail/wql_mapping_generic_unbound_fail.logos`

### `deem.mapping.scalars` — scalar params bind by name identity

A mapping's scalar params (`floor: i64`) bind at the consumption site by NAME IDENTITY: the consumer declares a param with the same name and type; the spliced rules resolve the scalar as written — no rename, no binding syntax. Missing scalar = named error.

*Evidence:* `tests/logos/pass/wql_mapping_scalar_e2e.logos`

### `deem.mapping.runtime-artifacts` — none

A mapping emits no items. Its rules reach a consumer only by static fusion: the compiler's `mappings_` pre-scan records each mapping's canonical rule text and splices it into the consuming `deem`'s program. The former runtime artifacts `<M>__rules() -> str` / `<M>__src() -> str` were read only by the dynamic `Query::compile_with_mapping`, which P5 deleted with the interpreter; they were removed in #353 (2026-10-03).

*Evidence:* `stdlib/mem/wql/mapping_item.logos` (the handler emits nothing), `tests/logos/pass/wql_mapping_rules_escape_e2e.logos` (rule text with `"`, `\` and newlines survives the splice)

## rel blocks and Datalog

### `deem.datalog.rel-block` — `rel NAME(cols){ bodies }`

A `rel` block declares a named derived relation with SET semantics: `cols` are declared `name: ty` columns, `bodies` are `;`-terminated query producers whose UNION (deduped structurally on insert) is the relation; each body is restricted to from/join/where/select.

*Divergence:* Datalog rules (multiple bodies = a disjunction of rules with the same head); the set/union semantics are the Datalog default.

*Evidence:* `stdlib/mem/wql/grammars/wql.peg` (`program`, `rel_list`, `rel_block`, `rel_cols`, `rel_bodies`); validation `stdlib/mem/wql/plan_walker.logos` (`walk_program_params`, `check_rel_body`, `rel_body_mods_ok`)

### `deem.datalog.fact` — a FROM-less `select` is one row

`select S [: RTy]` with no `from` is a FACT: exactly one row, whatever the sources hold. It is legal as a rel body (an inline table, or a seed such as `select start;` for a scalar parameter) and as the entry query. The handler rewrites it to `from __unit __u select S`, where `__unit(u: i64)` is a native source of one row (`logos.std.wql.unit::wql_unit_rows`), registered only when a program has a fact.

*Divergence:* SQL's FROM-less `SELECT`; Soufflé writes the same thing as a fact clause `r(1, 2).`

*Evidence:* `stdlib/mem/wql/grammars/wql.peg` (`simple_query`'s third alternative); `stdlib/mem/wql/lower.logos` (`desugar_program_facts`); `tests/logos/pass/wql_rel_fact_e2e.logos`

### `deem.datalog.demand` — a rel read with bound columns is evaluated on demand

The program is rewritten by the magic-sets transformation (Soufflé's MST). A clause — the entry query, or a body of a rel — is read left to right in a SIPS order: next, the positive atom with the most bound columns (ties in the written order). A column of an atom is bound when a conjunct of the clause (its WHERE, or the ON of a positive step) equates it to a literal, a scalar parameter, a column of an atom already visited, or a bound column of the clause's head; a head column binds a body column only when its select item is that plain column. Every user-rel atom `S` with bound columns γ reads an ADORNED COPY `S__b<γ>` instead of `S`, and contributes one magic rule to `__m_S__b<γ>` (one column per bound column): the atoms visited before it, the clause's conjuncts over them, and — in a rel body — the head's own magic rel; in the entry, a seed with no atom to its left is a fact. Each body of an adorned copy is the original body, its atoms adorned the same way, joined with its magic rel on the bound columns. A reader that binds no column of `S` reads `S` itself, so a rel read both bound and free is evaluated once whole and once on demand; an original that no reader reaches any more is dead (`deem.datalog.live`). The answer is unchanged; the rows derived are those reachable from the demand.

Only a RECURSIVE rel is adorned: a non-recursive rel's body is evaluated whole before its magic join could filter it, so a copy saves no evaluation and only duplicates code (`nonrec`; measured on Canon, adorning every bound rel doubled the generated code and took a compile from 8.6 s to 69 s). A rel that some evaluated clause reads whole (the entry, a rel read whole, or an adorned body, binding none of its columns) is evaluated whole anyway and is not adorned either (`shared`; decided over the transformation's own graph, narrowed to a fixpoint). A rel is also read whole when a body aggregates or finds (`neg_agg`), or evaluates checked arithmetic (`fallible`: restricting rows could turn an `Err` into an `Ok`); an anti join's atom is always read whole, and a magic rule never contains one (that only widens the demand). When the entry's WHERE / ON is fallible nothing is rewritten. A traversal step (`join n.kids k`) is visited right after the atom it hangs off — its rows are a function of that row — and a magic rule that reaches past it copies it. `[plan] demand -> demand-driven | fully materialized` names each decision.

A demand-driven rel's SCC reads its magic rel, so the internal DRed helpers of that SCC are not emitted; no public surface depends on them for these shapes (measured over every corpus program the rewrite touches). `LOGOS_DEEM_NO_DEMAND` (at compile time, any value) turns the rewrite off.

*Divergence:* Soufflé applies MST on request (`--magic-transform`); here it applies whenever the conditions hold, the plan trace records the decision, and an environment switch turns it off.

### `deem.datalog.live` — a rel no reader reaches is not evaluated

After the demand rewrite, a rel is LIVE when the entry reads it or a live rel's body does — positively, under `anti join`, or in an aggregate. A rel that is not live is not evaluated by the generated fn; its bodies are still checked, and its helper is still emitted (an error in it is still an error). A rel with a body that evaluates checked arithmetic, a comprehension or an aggregate is evaluated even when no reader reaches it: the naive program would return its `Err`. `[plan] live rels -> not evaluated` names each rel left out, and its plan records the absence ground `dead: no reader reaches the entry`. `LOGOS_DEEM_NO_PRUNE` (at compile time) evaluates every rel.

*Divergence:* Soufflé's RemoveRedundantRelations deletes such a relation; here it is kept, checked and not run, so a dead rel's error is not hidden.

*Evidence:* `stdlib/mem/wql/lower.logos` (`magic_program`); `stdlib/mem/wql/why.logos` (`MS_*`); `tests/logos/pass/wql_rel_demand_e2e.logos`

### `deem.datalog.rel-columns` — rel-block columns are i64/str/bool, at most 12

A column of a `rel` block (in a deem body, or spliced from a mapping) must be `i64`/`str`/`bool` — rels are sets deduped by structural equality, so a column type needs a reflexive, injective identity and must be the type the EL computes in: `el_set_col_admit` admits exactly those three, `f64`/`f32` get their own named diagnostic (Eq loss is the reason), and a non-canonical integer such as `u64` is refused naming the remedy. (A source-trait `rel` member is checked by sema against `Hash` instead — `deem.source.trait`.) A rel holds at most 12 columns: its row is a tuple, and a tuple implements `Hash` and `Clone` up to 12 elements, as in Rust; a 13th column is a named error at the declaration. Every other list of a program — rels, bodies, join steps, aggregates, tuple items, call arguments, path segments — holds any number of items.

*Divergence:* RESTRICTION — narrower than SQL/Datalog value domains; f64 is excluded because set membership needs Eq.

*Evidence:* `stdlib/mem/wql/plan_walker.logos` (`walk_program_params` — the 12-column check, `rel_col_ty_ok`); `stdlib/mem/wql/el.logos` (`el_set_col_admit`, `el_set_col_why`); grammar `stdlib/mem/wql/grammars/wql.peg` (`rel_col`, `rel_cols`); `tests/logos/fail/wql_rel_cols13_fail.logos`, `tests/logos/fail/wql_rel_float_col_fail.logos`, `tests/logos/fail/wql_rel_col_wide_int_fail.logos`

### `deem.datalog.rel-body-gates` — rel body modifier gates

A rel body is from/join/where/select ONLY; aggregate and `find` bodies are named errors, and `first`/`distinct`/`order by`/`limit`/`: RTy` are rejected in a body (they are entry-query concerns; distinct is implicit under set semantics); the select width must equal the declared column count.

*Divergence:* RESTRICTION — rel bodies are pure relation producers (Datalog rule bodies), not full queries.

*Evidence:* `stdlib/mem/wql/plan_walker.logos` (`check_rel_body`, `rel_body_mods_ok`); select width `stdlib/mem/wql/rexpr_walk.logos` (`rel_select_ok`)

### `deem.datalog.rel-scan` — the entry query scans rels like sources

The entry query (and other rel bodies) may scan a rel by name exactly like a slice source; the walker rewrites the source name to the emitted rel slice and records an explicit dependency edge (`RelDeps`); self/forward/mutual references are legal (the registry is completed before any body resolves).

*Divergence:* Datalog rule bodies referencing other (or the same) relations.

*Evidence:* `stdlib/mem/wql/plan_walker.logos` (`walk_program_params`, `resolve_source`) (two-pass registration then body resolution); `RelDeps` `stdlib/mem/wql/params.logos` (`RelDeps`)

### `deem.datalog.rel-borrow-gate` — rels cannot be borrowed out

`find` over a rel and a whole-rel-row `select` are compile errors — rels are fn-locals, so a borrow of their rows cannot leave the generated fn.

*Divergence:* EXTENSION — a Logos ownership constraint (borrows may not escape the query fn), no SQL/Datalog analogue.

*Evidence:* `stdlib/mem/wql/plan_walker.logos` (`sel_whole_row_ok`; the `RQuery::Find` arms of `walk_program_params` and `check_rel_body`)

### `deem.datalog.rel-tuple-binding` — rel row vars bind positional tuple columns

A rel-sourced row var binds to a native TUPLE row, so a field step `s.a` emits the POSITIONAL access `s.<idx>` (or `(*s)` for a 1-column scalar rel); the (var,col)→index/type binding is stamped by `stamp_rel_source` and consulted by codegen before the flat name dictionary.

*Divergence:* no analogue; an emission detail of set-typed tuple rows.

*Evidence:* `stdlib/mem/wql/rexpr_walk.logos` (`stamp_rel_source`, `stamp_rel_columns`); `ElTypes` rel-binding table `stdlib/mem/wql/el.logos` (`rel_bind_named`, `rel_col_of`)

### `deem.datalog.scc-condensation` — SCC condensation of the rel dependency graph

The rel dependency graph is condensed into strongly-connected components with a dependencies-first topological order; a singleton SCC without a self-edge materializes one-shot (a helper fn), a recursive SCC (self-edge or multi-rel cycle) becomes one shared semi-naïve fixpoint fn.

*Divergence:* the standard Datalog stratification/SCC evaluation strategy.

*Evidence:* `stdlib/mem/wql/params.logos` (`compute_rel_scc` — Warshall closure + component id + Kahn topo, `rec[c]` = size>1 or self-loop; `RelScc`); `stdlib/mem/wql/plan_walker.logos` (`walk_program_params`, `emit_prelude_oneshot`, `emit_prelude_scc`)

### `deem.datalog.semi-naive` — semi-naïve fixpoint over an SCC

A recursive SCC evaluates by semi-naïve iteration: per member a total set, a next-delta, and a shadow set (total ∪ next-delta); seed bodies (no in-SCC source) run once, then each round promotes delta→total, exits when all deltas empty, and re-runs the recursive bodies against the delta region; mutual recursion (multi-member SCC) is supported.

*Divergence:* textbook Datalog semi-naïve evaluation (delta relations); the delta variant is a loop variable, not IR rewriting.

*Evidence:* `stdlib/mem/wql/rexpr_walk.logos` (`emit_rel_fns`, `emit_scc_fn`); ADR 0012-queue2 §7

### `deem.datalog.termination` — no iteration cap (generative recursion may diverge)

Termination is the standard Datalog contract: recursion over a finite universe reaches a least fixpoint, but a recursive head that MINTS new values (e.g. `select (p.a + 1, …)`) can diverge — this is deliberately NOT capped (a silent cap would change semantics).

*Divergence:* matches Datalog's non-generative termination guarantee; generative recursion is the user's responsibility.

*Evidence:* `stdlib/mem/wql/plan_walker.logos` (module header, TERMINATION); `stdlib/mem/wql/wql.logos` (module header); the fixpoint's only exit `stdlib/mem/wql/rexpr_walk.logos` (`emit_scc_fn`)

### `deem.datalog.stratified-negation` — stratified negation/aggregation

An `anti join R` or an aggregate body reading `R` where `R` is in the SAME SCC as the body's head rel is non-stratifiable — a named compile error listing the cycle members; negation/aggregation against an EARLIER (fully materialized) stratum is fine.

*Divergence:* standard Datalog stratified negation (a cycle through negation or aggregation is rejected).

*Evidence:* `stdlib/mem/wql/plan_walker.logos` (`check_stratified`); negated/aggregated sub-lists `stdlib/mem/wql/params.logos` (`RelDeps` `afrom`/`ato`, `gfrom`/`gto`, `add_anti`, `add_agg`)

## UDF / UDA

### `deem.udf.reflection` — user functions reflected from the trigger module

The deem/trama handlers reflect every top-level `fn` of the trigger module into the UDF registry (name, return EL-lattice tag via `el_ret_class`, declared return type name, arity); codegen resolves a call name against the builtin registry first, then the UDF table (builtins shadow a same-named UDF); the registry has no capacity.

*Divergence:* EXTENSION over CEL/SQL — UDFs are ordinary module-local Logos functions, resolved by reflection, not a separate registration API (static surface).

*Evidence:* `stdlib/mem/wql/el.logos` (`ElTypes` UDF section, `udf_add`/`udf_find`); reflection `stdlib/mem/wql/reflect.logos` (`stamp_udfs_from_module`), `fn_param_count`/`fn_ret_type_name` (arity + return-type reflection)

### `deem.udf.call-check` — arity and return-type checking

`check_calls` validates each call: unknown function (not builtin, not UDF) errors, arity mismatch errors, and an out-of-lattice UDF return type (`el_ret_class` = -1, e.g. a struct/reference/unit) errors; narrower int returns get an `as i64` widening cast at the emit site (u64/u128/i128 beyond i64 range truncate — documented MVP).

*Divergence:* EXTENSION — static UDF type-checking against the EL lattice, the agentic selector (P3).

*Evidence:* `stdlib/mem/wql/codegen.logos` (`check_calls`); `el_ret_class` `stdlib/mem/wql/el.logos` (`el_ret_class`, `el_class_lookup`)

### `deem.uda.triple` — user aggregates are init/step/fin triples

A user-defined aggregate is an init/step/fin triple whose finalizer return classifies the aggregate output column type (reflected UDA return class), enriching the builtin count/sum/min/max/avg set.

*Divergence:* EXTENSION — the classic init/step/final UDA protocol; the finalizer return type drives the projected column type.

*Evidence:* `stdlib/mem/wql/rexpr_walk.logos` (`compute_agg_col_tys`, UDA reflected R class `resolve_aggs`); ADR 0012-queue2 §6

## Optimizer

### `deem.opt.const-fold` — scalar constant folding

`simplify_sexpr` folds constant SBin/SUn/SCond: integer arithmetic (+ - * / %, division/modulo by zero left unfolded) and comparisons, float arithmetic (+ - * /, `%` NOT folded, non-finite results unfolded) and comparisons, boolean == != and && || (with short-circuit on a single const operand), and algebraic identities (`x+0`,`0+x`,`x-0`,`x*1`,`1*x`→x; `x*0`,`0*x`→0); a const-bool ternary collapses to the taken (itself-simplified) branch.

*Divergence:* standard constant folding; run by the static emitter and, over template expressions, by `Tpl::compile` (the query interpreter that also ran it was deleted at P5).

*Evidence:* `stdlib/mem/wql/optimize.logos` (`fold_arith_ii`, `fold_cmp_ii`, `fold_cmp_bb`, `fold_arith_ff`, `fold_cmp_ff`, `simplify_bin`, `simplify_un`, `simplify_sexpr`)

### `deem.opt.where-fold` — `where true`/`where false` folds

A const-true filter predicate drops the filter entirely; a const-false predicate marks the plan empty (yields no rows, emitted as an empty `Vec` with no scan loop).

*Divergence:* relational simplification with no direct SQL analogue at the language level (an optimizer guarantee).

*Evidence:* `stdlib/mem/wql/optimize.logos` (`simplify_rbody` `RExpr::Filter` arm, `empty_proj`)

### `deem.opt.identity-projection` — identity-projection accessor collapse

An unfiltered identity projection over a bare scan (the select is just the loop var or a base-less field ref matching it) is marked `identity` so the emitter returns the source slice `&[Row]` directly, skipping the copy loop.

*Divergence:* EXTENSION — a zero-copy borrow optimization for `from s v select v`, no SQL analogue.

*Evidence:* `stdlib/mem/wql/optimize.logos` (`is_identity_sel`, `input_scan_var`, `simplify_proj_root`)

### `deem.opt.limit-fold` — limit-0 / limit-over-empty fold to empty

`limit 0` (literal) marks the plan empty; a limit over an already-empty sub-plan stays empty.

*Divergence:* optimizer guarantee.

*Evidence:* `stdlib/mem/wql/optimize.logos` (`simplify_rexpr_ref` `RExpr::Limit` arm)

### `deem.opt.sort-const-drop` — order-by over a constant key dropped

Sorting by a key that const-folds to a literal orders nothing (every row compares equal, the pass is stable) → the sort is dropped.

*Divergence:* optimizer guarantee.

*Evidence:* `stdlib/mem/wql/optimize.logos` (`simplify_rbody` `RExpr::Sort` arm)

### `deem.opt.proj-collapse` — nested projection and distinct-over-empty collapse

`RProj(RProj(x))` collapses to a single projection (inner input simplified, outer selection kept); `RDistinct`/`RLimit` over an empty sub-plan stay empty; the empty result has a canonical `RSimplified{empty}` form the emitter renders as an empty `Vec`.

*Divergence:* standard relational peephole simplification.

*Evidence:* `stdlib/mem/wql/optimize.logos` (`simplify_rexpr_ref` `RExpr::Proj`/`RExpr::Distinct`/`RExpr::Limit` arms, `RSimplified`, `empty_proj`, `peel_to_proj`)

### `deem.opt.shared-tiers` — the optimizer is shared by the static emitter and the template engine

`simplify_sexpr`/`simplify_rexpr_ref` are pure IR→IR functions. The STATIC emitter runs both (and `trama_render` runs `simplify_sexpr` for static templates); the runtime TEMPLATE engine re-runs `simplify_sexpr` over every embedded expression at `Tpl::compile` (`simplify_all`). The join-step analysis (equi/residual split, `join_sel`) has static consumers only. The DYNAMIC query interpreter, which re-ran the relational half at query-compile time, was deleted at P5.

*Divergence:* EXTENSION — one optimizer, two consumers (the schemas-as-IR payoff).

*Evidence:* `stdlib/mem/wql/optimize.logos` (`simplify_sexpr`, `simplify_rexpr_ref`, `split_and_terms`, `refs_mask`); `stdlib/mem/wql/join_sel.logos` (`step_terms`, `step_equi_key`); runtime consumer `stdlib/mem/deem/tpl.logos` (`Tpl::compile`, `simplify_all`); ADR 0012-queue2 §1

## Static vs dynamic surfaces

Since P5 (`e1dd0ac5e`, "DELETE THE DEEM INTERPRETER") the static item is the ONLY query surface. What survives of the runtime side is the TEMPLATE engine (`Tpl`, specced in `docs/spec/trama.md` as `trama.dynamic.*`) and the binding types it reads — `SchemaCatalog`, `QEnv`, `RtVal`, `QError` in package `logos.mem.deem` (`stdlib/mem/deem/deem.logos`, `stdlib/mem/deem/tpl.logos`). The `deem.exec.*` ids below that described the runtime query API are kept as permanent addresses and marked withdrawn; the rest are restated against the code that carries them today.

### `deem.exec.static` — the static `deem` item (metacall → native, compile diagnostics)

The static surface parses, type-checks, optimizes and lowers at COMPILE time via metacall, emitting native Logos code linked into the program; all errors are compile DIAGNOSTICS. It is the only query surface: no runtime-string query exists in the language (`deem.exec.dynamic-api`).

*Divergence:* the compile-time-checked prepared-statement model (sqlx-style); the strong typing is the agentic selector at build time (P3).

*Evidence:* `src/compiler/sema_expr.cpp` (`SemaChecker::lower_deem_def`); `stdlib/mem/wql/wql.logos` (`deem`, the `#[token_macro]` handler); ADR 0012 "Static-first sequencing"

### `deem.exec.dynamic-api` — WITHDRAWN at P5

Withdrawn with the interpreter. `Query::compile(text,&cat)?.run(&env)?` — query TEXT parsed, checked, optimized and tree-walked at run time, errors as values, results as `QRows` — was deleted at P5 (census §6 L10); no `Query` or `QRows` exists in the tree. Runtime TEXT is still accepted for templates only (`Tpl::compile`, `trama.dynamic.compile-render`).
<!-- spec-gone: stdlib/mem/deem/query.logos — deleted at P5: Query / QRows, the runtime query-compilation entry point -->

### `deem.exec.reuse` — WITHDRAWN at P5

Withdrawn with the interpreter. It stated that the dynamic query surface re-hosted the parsers, optimizer, plan lowering and semantics verbatim; that surface is gone. The narrower fact that survives — the template engine runs the shared scalar optimizer over its embedded expressions — is stated in `deem.opt.shared-tiers`.

### `deem.exec.catalog` — `schema_catalog!` and `SchemaCatalog`

`resource cat = schema_catalog!{ S1, S2, … };` is a queue-1 metacall macro that reflects the named ADR-0011 `schema` decls out of the trigger module and emits a fn returning a `SchemaCatalog` view over a STATIC Writ blob in .rodata (schema code → {field → (key code, EL type, edge target)}); the template engine's checker and evaluator resolve `e.field` against this catalog.

*Divergence:* EXTENSION — queue-1 serving the runtime over the designated `annotation → metaprog hook → rodata Writ blob → runtime view` channel; no global registry, no link-time magic.

*Evidence:* macro `stdlib/mem/wql/catalog_macro.logos` (`schema_catalog`, `emit_schema_entry`); runtime view `stdlib/mem/deem/deem.logos` (`SchemaCatalog`, `SchemaCatalog::from_static`/`merge_static`, probes `schema_code`/`field_key`/`field_ty`); readers `stdlib/mem/deem/tpl.logos` (`check_root`, `field_read`); ADR 0012-queue2 §5

### `deem.exec.env` — the runtime env: bindings and the UDF registry

`Tpl::render(&env)` takes a `QEnv` binding names to schema'd Writ objects (`bind_node`), Writ arrays of schema'd rows (`bind_source`), and scalars (`bind_i64`/`bind_f64`/`bind_bool`/`bind_str`); at most 24 bindings, a bind past capacity is silently ignored, a rebind overwrites. `register_fn(name, f, args, ret)` registers a UDF of type `fn(&[RtVal]) -> RtVal` with a declared signature (EL type names, at most 4 args) and returns `bool` — `false` on a bad type name, too many args or a full registry, never a silent no-op; calls resolve builtin-first, then the registry (the static surface's precedence). `register_agg` (init/step/fin) registers under the same contract, but since P5 nothing reads the UDA registry: templates have no aggregates, and the query tier that did was deleted.

*Divergence:* EXTENSION — the runtime binding/registry surface (`QEnv`), analogous to a prepared-statement parameter set plus a UDF registry.

*Evidence:* `stdlib/mem/deem/deem.logos` (`QEnv`, `QEnv::bind_node`/`bind_source`/`bind_i64`/…, `register_fn`, `register_agg`, `QENV_CAP`, `QENV_FN_ARGS`); readers `stdlib/mem/deem/tpl.logos` (`env_val`, `check_root`, the `SExpr::Call` arm of `eval_sexpr`); `tests/logos/pass/query_reg_errors_e2e.logos` (the `bool` contract); ADR 0012-queue2 §6

### `deem.exec.bind-kinds` — source binding kinds

Two SOURCE bindings remain. `bind_source` (a Writ array of schema'd rows, `QB_SRC`) is read by the template engine. `bind_edge_rows` (PRE-MATERIALIZED rows in the eight-column edge vocabulary, `QB_EDGE`) is still exported and still writes its kind code, but nothing reads `QB_EDGE`: its reader was the executor's scan, deleted at P5, so the binding is accepted and inert. `bind_source_erased` / `bind_node_erased` / `bind_source_tree` were removed (census §5 C3, ABI 0.38.0 → 0.39.0). The dynamic graph walk survives without a binding: `dyn_graph_edges` / `dyn_graph_edge_rows` take a raw `WAny` root (`deem.graph.vocabulary`).

*Evidence:* `stdlib/mem/deem/deem.logos` (`QB_SRC`, `QB_EDGE`, `QEnv::bind_source`, `QEnv::bind_edge_rows`); `stdlib/mem/deem/tpl.logos` (`env_val`, `check_root`, `src_elem_ty` — no `QB_EDGE` arm); `stdlib/mem/deem/graphsrc.logos` (`dyn_graph_edges`, `dyn_graph_edge_rows`)
<!-- spec-gone: stdlib/mem/deem/check.logos — deleted at P5: the dynamic query CHECKER; its template half had already been ported to stdlib/mem/deem/tpl.logos -->

### `deem.exec.incremental` — WITHDRAWN at P5

Withdrawn with the interpreter. `Query::incremental` — DBSP maintenance of query results under ±-weighted fact deltas over a `FactStore` (ADR 0013), with recursion (`IncrRec`, DRed) and the engine's own history as a source — was deleted at P5 together with `FactStore`, `IncrJoin`, `IncrRec` and `FactHistory` (census §6 L1–L6, L10). Nothing in the tree maintains a deem result incrementally; ADR 0013 stands as a design. Every `query_incr_*` fixture this rule cited died with it.
<!-- spec-gone: stdlib/mem/deem/incr.logos — deleted at P5: the DBSP incremental engine (IncrJoin, FactStore, AggState) -->
<!-- spec-gone: stdlib/mem/deem/incr_rec.logos — deleted at P5: the recursive incremental engine (IncrRec, dred) -->

### `deem.exec.rtval` — the RtVal runtime scalar

The runtime scalar is `RtVal { I(i64) | F(f64) | B(bool) | S(str) | Node(WAny) | Null | Error }`: the EL lattice maps INT/FLT/BOOL/STR onto it, `Node` carries object/row handles, `Null` is a miss (an unset `WAny`-typed field or a null edge read at run time; it propagates CEL-style through EL operators, `deem.exec.lenient-null`), `Error` is a math error (overflow, division or remainder by zero) that aborts the render as a `QError`. It is the value type of the template evaluator and of the UDF registry signature. Equality is `rt_eq` over `rt_kind` codes. `QRows` and the query-side ordering/hashing (`rt_cmp`, `rt_key_hash`) were deleted at P5 with the executor.

*Divergence:* EXTENSION — the dynamic value model; tag dispatch on `RtVal` is strong-typing-as-selector, runtime edition.

*Evidence:* `stdlib/mem/deem/deem.logos` (`RtVal`, `rt_kind`, `rt_i`/`rt_f`/`rt_b`/`rt_s`, `rt_eq`, `wany_to_rt`); `stdlib/mem/deem/tpl.logos` (`eval_sexpr`, `eval_sx` — the Error → `QError` boundary); ADR 0012-queue2 §2

### `deem.exec.qerror` — errors are QError values

`Tpl::compile` / `Tpl::render` failures are `QError` VALUES carrying a positioned message (not compiler diagnostics), returned via `Result` so a running program (typically a model-driven loop) consumes the message as a feedback signal.

*Divergence:* EXTENSION — errors-as-values, the dynamic dual of the static surface's compile diagnostics.

*Evidence:* `stdlib/mem/deem/deem.logos` (`QError`, `QError::message`, `qerr`/`qfail`); `stdlib/mem/deem/tpl.logos` (`Tpl::compile`, `Tpl::render`, `chk_err`); ADR 0012-queue2 §3

### `deem.exec.strict` — strict-on-schema typing at render

Every template root name must resolve to a `QEnv` binding and every `e.field` to a catalog entry, exactly as the static queue resolves against the module AST; an unknown name, unknown field or type mismatch is a `QError`. The full strict check needs the env, so it runs at the top of every `Tpl::render`; `Tpl::compile` runs only the env-independent checks.

*Divergence:* mirrors the static surface's strict schema typing (D4 strict-on-schema).

*Evidence:* `stdlib/mem/deem/tpl.logos` (`Tpl::render` → `check_stmts` with `strict = true`, `check_root`, `check_expr`); catalog probes `stdlib/mem/deem/deem.logos` (`schema_code`/`field_key`/`field_ty`); ADR 0012-queue2 §4

### `deem.exec.lenient-null` — WITHDRAWN at P5

Withdrawn with the interpreter (census §5 C3). Erased bindings — `bind_source_erased` / `bind_node_erased`, which typed a binding `dyn` and gave its rows CEL-style `Null` propagation — were removed, and no query surface has a `dyn` column: a `deem` item's rel columns are stamped concretely (`SemaChecker::native_source_spec`) and must implement `Hash`, and an item whose source parameter carries an erased Writ slot (`&WAny`, `&[WAny]`) is REFUSED, naming the ground (`SemaChecker::enrich_deem_params`, `names_erased_writ_slot_` in `src/compiler/sema_expr.cpp`; doors `tests/logos/fail/deem_erased_source_fail.logos`, `tests/logos/fail/deem_erased_node_fail.logos`). The one lenient route left is in the template engine: a `WAny`-typed field on a strict schema (`FK_ANY`, checked `CT_DYN`) reads as `RtVal::Null` when absent and `Null` propagates through EL operators there (`stdlib/mem/deem/tpl.logos`: `field_read`, `eval_sexpr`); that is a template rule, `trama.dynamic.*`.

### `deem.exec.lenient-bool-one` — a bool is worth ONE wherever a lenient value is read as a number

A runtime `bool` reached through a lenient path is worth `1`/`0` in EVERY numeric spelling: `rt_i` reads it as `1i64`/`0i64`, `rt_f` as `1.0f64`/`0.0f64`, and the two therefore agree. Since P5 the only lenient path is a `WAny` (`FK_ANY`) field read by the template engine: `check_expr` waives the numeric-operand refusal for `ct_is_open(t)` (`CT_UNKNOWN`/`CT_DYN`), so a `WAny` cell holding a bool is `CT_DYN` at check time and `RtVal::B` at run time and reaches both arms — `{{ e.meta + 1.0 }}` over `meta = true` is `2.0`. It is one rule with one value, not a per-site convention. (The `QRows::get_f64` accessor it also governed was deleted at P5.)

**This rule does NOT reach an aggregate accumulator.** No lenient value can be an aggregate argument: templates have no aggregates, and the query tier that rejected `dyn` aggregate args at check time was deleted at P5 (its pin `query_dyn_bool_arith_pinned` died with it, census row 14). Separately, `sum`/`min`/`max` admit no bool in the static tier: `agg_result_ty` (`el.logos`) returns the argument type only for `EL_TY_INT`/`EL_TY_FLT` and `-1` otherwise. What DOES admit a bool is `avg` over a column the schema TYPES as bool — a STRICT-tier rule, `el_quot_ty`'s bool exemption, pinned by `tests/logos/pass/wql_agg_avg_bool_value_rule.logos`. Two rules, two tiers.

*Divergence:* EXTENSION — CEL has no bool→number coercion at all; the lenient path admits it and fixes the value at one, matching the ruling that `avg` admits `bool` with `true` worth `1.0`. The STRICT tier is unaffected: there a bool operand is typed and refused where the EL's rule table refuses it.

*Evidence:* `stdlib/mem/deem/deem.logos` (`rt_i` / `rt_f` `B` arms — `rt_i`'s dates to c00b2888 2026-07-02, `rt_f`'s to ce973c17; `rt_kind`, `ct_is_open`); `stdlib/mem/deem/tpl.logos` (`check_expr`, `field_read`); `stdlib/mem/wql/el.logos` (`agg_result_ty`, `el_quot_ty`); `tests/logos/pass/wql_domain_bool_one_tpl.logos` drives BOTH `B` arms through `Tpl::render` over a `WAny` (FK_ANY) column — an int literal on the other side takes `rt_i` and a float literal takes `rt_f`, because `rt_kind(RtVal::B)` is 3 and not 2 — and pins the WAIVER as its ground: the identical `+ 1` over a statically-typed `bool` column is refused with `'+' needs numeric operands`, at RENDER, not at `Tpl::compile` (which checks with an empty env, where an unbound root is `CT_UNKNOWN` and `ct_is_open` admits it)

### `deem.exec.dyn-cascade` — WITHDRAWN at P5

Withdrawn with the interpreter. It stated that the dynamic join cascade was decided at `Query::compile` from checked key types; no runtime join exists any more. The cascade rule itself holds for the static item and is stated in `deem.join.cascade-hash`.

## Expression Language (EL)

<a id="expression-language-el"></a>
EL is the CEL-class scalar expression sublanguage embedded by every Deem clause body (where/select/on/group-key/aggregate-arg/having/order/find) AND by Trama (`{{ … }}` / `{% if/for/set … %}`); its only coupling to Trama is the `expr: WRef<SExpr>` edge. `docs/spec/trama.md` links these `el.*` rule ids; the shared-sublanguage anchor is this section, `docs/spec/deem.md#expression-language-el` (this file — it was `wql.md` before the rename). EL is a strict PROFILE of the one IR (P1: subsets are profiles, not forks); its grammar is `stdlib/mem/wql/grammars/el.peg`, its shared types (operator ids + value-type lattice) `stdlib/mem/wql/el.logos`, its IR `stdlib/mem/wql/ir.logos`.

### `el.grammar.precedence` — the CEL precedence chain

EL parses a fixed CEL-precedence chain: `ternary → || → && → ==/!= → <=/>=/</> → +/- → */ /%  → unary !/- → postfix .field → primary`; binary levels are left-associative (fold-mode over the running LHS).

*Divergence:* the CEL operator precedence and associativity exactly (`?:` lowest, postfix field access highest).

*Evidence:* `stdlib/mem/wql/grammars/el.peg` (`expr`, `ternary`, `or`, `and`, `equality`, `compare`, `add`, `mul`, `cast`, `unary`, `postfix`, `primary`)

### `el.op.ternary` — conditional `c ? t : e`

`c ? t : e` builds an `SCond` (the CEL conditional); it emits as a Logos `if` expression `(if (c) { t } else { e })`; a const-bool condition const-folds to the taken branch; its inferred type is the then-branch type (both arms are expected to agree).

*Divergence:* CEL conditional `?:`; the branches must be type-compatible (strict, no CEL dynamic-widening).

*Evidence:* `stdlib/mem/wql/grammars/el.peg` (`ternary`); emission `stdlib/mem/wql/codegen.logos` (`emit_sexpr_as`, `check_cond_branches`); fold `stdlib/mem/wql/optimize.logos` (`simplify_sexpr`)

### `el.op.logical` — `||` and `&&`

`||`→`SBin(OP_OR=1)`, `&&`→`SBin(OP_AND=2)`, both boolean-typed, emitted as Logos `||`/`&&`; they short-circuit-fold when one operand is a constant.

*Divergence:* CEL logical or/and.

*Evidence:* `stdlib/mem/wql/grammars/el.peg` (`or`, `and`); ids `stdlib/mem/wql/el.logos` (`OP_OR`, `OP_AND`); emission `stdlib/mem/wql/codegen.logos` (`emit_binop`)

### `el.op.equality` — `==` and `!=`

`==`→`SBin(OP_EQ=3)`, `!=`→`SBin(OP_NE=4)`, boolean-typed; integer/float/bool equality const-folds.

*Divergence:* CEL equality; f64 equality is permitted in EL expressions generally (but see `el.restrict.f64-key` for keyed positions).

*Evidence:* `stdlib/mem/wql/grammars/el.peg` (`equality`); ids `stdlib/mem/wql/el.logos` (`OP_EQ`, `OP_NE`); emission `stdlib/mem/wql/codegen.logos` (`emit_binop`)

### `el.op.compare` — `< <= > >=`

`<`→5, `<=`→6, `>`→7, `>=`→8 (`SBin`), boolean-typed; integer and float comparisons const-fold.

*Divergence:* CEL relational comparisons.

*Evidence:* `stdlib/mem/wql/grammars/el.peg` (`compare`); ids `stdlib/mem/wql/el.logos` (`OP_LT`, `OP_LE`, `OP_GT`, `OP_GE`); emission `stdlib/mem/wql/codegen.logos` (`emit_binop`)

### `el.op.arith` — `+ - * / %`

`+`→9, `-`→10, `*`→11, `/`→12, `%`→13 (`SBin`); numeric-typed with the INT→FLT promotion rule (`el.type.int-float-promote`); integer arithmetic folds (÷/% by zero left unfolded), float arithmetic folds (`%` NOT folded).

*Divergence:* CEL arithmetic; `%` is integer/float modulo (float `%` is a valid operator but does not const-fold).

*Evidence:* `stdlib/mem/wql/grammars/el.peg` (`add`, `mul`); ids `stdlib/mem/wql/el.logos` (`OP_ADD`, `OP_SUB`, `OP_MUL`, `OP_DIV`, `OP_MOD`); emission `stdlib/mem/wql/codegen.logos` (`emit_sexpr_as`, `el_int_op_fn`, `emit_binop`); fold `stdlib/mem/wql/optimize.logos` (`simplify_bin`, `fold_arith_ii`, `fold_arith_ff`)

### `el.op.unary` — `!` and unary `-`

`!x`→`SUn(OP_NOT=1)` (boolean), `-x`→`SUn(OP_NEG=2)` (preserves the operand's numeric type); `!boollit`/`-intlit`/`-floatlit` const-fold.

*Divergence:* CEL logical-not and numeric negation.

*Evidence:* `stdlib/mem/wql/grammars/el.peg` (`unary`); ids `stdlib/mem/wql/el.logos` (`OP_NOT`, `OP_NEG`); emission `stdlib/mem/wql/codegen.logos` (`emit_sexpr_as`); fold `stdlib/mem/wql/optimize.logos` (`simplify_un`)

### `el.op.field` — postfix `.field` access

`base.field` (postfix, left-nested) builds an `SField` chain carrying each field NAME as a `str` (self-describing IR, Option B); a bare IDENT is a base-less `SField` (field-root), rebound to `SVar` at the metacall when it names a comprehension loop variable; a rel-bound var's field emits positional tuple access (`deem.datalog.rel-tuple-binding`).

*Divergence:* CEL field selection; EXPLICITLY no implicit projection (P2 rejects JMESPath-style implicit map projection) and no safe-navigation `?.` (D4 strict — optionality only via `Option`-typed fields).

*Evidence:* `stdlib/mem/wql/grammars/el.peg` (`postfix`, `primary`); `SField` `stdlib/mem/wql/ir.logos` (`SField`); emission `stdlib/mem/wql/codegen.logos` (`emit_sexpr_as`, `root_ident_name`)

### `el.primary.literals` — int / float / bool / string literals

Primary literals are integer (`SLit` int, token→i64 decode), float (`FLOAT = [0-9]+\.[0-9]+`, token→f64 decode, the FLT family), `true`/`false` (bool), and double-quoted string (interned `WString` ref); FLOAT is ordered before INTEGER so `2.5` never lexes as `2` + junk.

*Divergence:* CEL literals; the numeric split (int vs float by a literal `.`) is Rust/Logos-conformant.

*Evidence:* `stdlib/mem/wql/grammars/el.peg` (`FLOAT`, `INTEGER`, `STRING`, `TRUE`, `FALSE`, `primary`); `SLit` `stdlib/mem/wql/ir.logos` (`SLit`); literal-type inference `stdlib/mem/wql/codegen.logos` (`infer_ty_name`)

### `el.primary.param` — bound parameter `$name`

`$name` builds an `SParam` (a bound prepared-statement argument by NAME) in the EL grammar; on Deem SURFACE the `$` sigil is RETIRED — scalar params are referenced bare — but the `SParam`/`$` production remains in EL. The interpreter's prepared-argument path it was kept for was deleted at P5; `SParam` is still read by the static emitter (emitted as the bare name) and by the template engine (`tpl.logos`: `eval_root`, `check_root`).

*Divergence:* CEL has no `$` param; this is a Deem/EL prepared-argument extension, retired on the deem surface (`deem.surface.scalar-param`).

*Evidence:* `stdlib/mem/wql/grammars/el.peg` (`primary`, `DOLLAR`); `SParam` `stdlib/mem/wql/ir.logos` (`SParam`); readers `stdlib/mem/wql/codegen.logos` (`emit_sexpr_as`), `stdlib/mem/deem/tpl.logos` (`eval_sexpr`, `check_expr` `SExpr::Param` arms)

### `el.primary.call` — function/filter call

`ident(args)` builds an `SCall` carrying the call NAME + a materialized `SExprArr` argument list (any number of arguments, one list field); the name resolves against the builtin registry first, then the reflected UDF table.

*Divergence:* CEL function/method calls; D6 canon is Logos-style calls (`upper(x)` / `x.upper()`), the jinja pipe `|` is Trama-only sugar.

*Evidence:* `stdlib/mem/wql/grammars/el.peg` (`primary`, `arglist`); `SCall`/`SExprArr` `stdlib/mem/wql/ir.logos` (`SCall`, `SExprArr`); emission `stdlib/mem/wql/codegen.logos` (`emit_call`, `emit_udf_call`)

### `el.primary.paren-tuple` — grouping vs tuple `(a,b,…)`

`(a, b, …)` with ≥1 top-level comma builds an `STuple` over an `SExprArr` (≥2 components); `(a)` (no comma) is plain grouping and passes the value through; the tuple alt is tried before grouping.

*Divergence:* EXTENSION — CEL has no tuple; the tuple projection is a Logos tuple (see `deem.project.tuple`), legal only in a `select` position.

*Evidence:* `stdlib/mem/wql/grammars/el.peg` (`primary`, `tuple_body`); `STuple` `stdlib/mem/wql/ir.logos` (`STuple`)

### `el.comprehension` — `[expr for v in src if guard]`

`[head for v in src if guard?]` builds an `SComp{plan, head, var}`: the source ident builds an `RScan`, an optional `if guard` folds it into an `RFilter` (the plan is assembled by the grammar); it emits as a block yielding `Vec<HeadTy>` (a while-loop pushing `head` under the optional guard); `v` references parse as `SField`/`SCall` by name and rebind to `SVar` at the metacall.

*Divergence:* EXTENSION — Logos/Python comprehension syntax over CEL semantics (ADR 0012: "comprehension = the Datalog bridge", one comprehension = one rule) rather than CEL's `e.map(x,f)` macros.

*Evidence:* `stdlib/mem/wql/grammars/el.peg` (`comprehension`, `comp_plan`, `comp_source`); `SComp` `stdlib/mem/wql/ir.logos` (`SComp`); emission `stdlib/mem/wql/codegen.logos` (`emit_comp`)

### `el.builtins` — len / upper / lower / contains / starts_with

The builtin functions are `len(x)`→INT (`(x).len()`), `upper(x)`/`lower(x)`→STR (owned `String` via `wql_upper`/`wql_lower`, ASCII byte-wise case folding), `contains(a,b)`→BOOL (`str_contains`), `starts_with(a,b)`→BOOL (`str_starts_with`); arities and return types are the registry (`builtin_arity`/`builtin_ret_ty`).

*Divergence:* a small CEL-canon + common-Trama-filter subset; string builtins are byte-oriented ASCII (MVP), not Unicode-aware.

*Evidence:* `stdlib/mem/wql/el.logos` (registry `builtin_of_name`/`builtin_arity`/`builtin_ret_ty` + `wql_upper`/`wql_lower`); emission `stdlib/mem/wql/codegen.logos` (`emit_call`)

### `el.type.lattice` — the EL_TY value-type lattice {INT,BOOL,STR,FLT}

Static codegen carries a coarse 4-valued type tag — `EL_TY_INT`(0)/`EL_TY_STR`(1)/`EL_TY_BOOL`(2)/`EL_TY_FLT`(3) — to route `push_str` vs `push_i64` vs the f64 format path and to type the row/projection element; a Logos type NAME maps via `el_ty_of_name` (`str`/`String`→STR, `bool`→BOOL, `f64`/`f32`→FLT, else INT — the integer family renders identically), the default being INT.

*Divergence:* a coarsening of the CEL type system to the four scalar families Deem emits; the whole integer family collapses to INT.

*Evidence:* `stdlib/mem/wql/el.logos` (`EL_TY_INT`, `EL_TY_STR`, `EL_TY_BOOL`, `EL_TY_FLT`, `el_ty_of_name`); inference `stdlib/mem/wql/codegen.logos` (`infer_ty`, `infer_ty_name`)

### `el.type.int-float-promote` — INT→FLT promotion with explicit cast

In binary arithmetic where one operand is FLT and the other INT, the result type is FLT and the INT operand is wrapped in an explicit `((expr) as f64)` cast in the emitted source (Logos is Rust-like — no implicit int→float); narrower int UDF returns similarly get an explicit widening cast.

*Divergence:* EXTENSION over CEL's implicit numeric coercion — Deem emits the cast explicitly to satisfy Logos's Rust-style no-implicit-coercion rule.

*Evidence:* `stdlib/mem/wql/codegen.logos` (`infer_ty_name`, `float_node_ty`, `emit_sexpr_as`); `stdlib/mem/wql/el.logos` (`EL_TY_FLT`)

### `el.type.string-concat` — `+` on strings is concatenation

`a + b` where either operand is STR infers STR (concatenation); in a render context it flattens into successive `push_str` calls (no intermediate `String` temporary).

*Divergence:* EXTENSION — CEL supports string `+`; Deem emits it as Logos string concatenation / push-flattening.

*Evidence:* `stdlib/mem/wql/codegen.logos` (`infer_ty_name`, `emit_push_str`)

### `el.type.returns-string` — owned String vs str-view

A call returning an owned `String` (the `upper`/`lower` builtins, or a UDF whose declared return is `String`) is tracked by `returns_string`; in a render/borrow context its result is `.as_str()`-borrowed, and a tuple column of such a call is typed `String` (not `str`).

*Divergence:* no analogue; a Logos ownership/borrow emission detail.

*Evidence:* `stdlib/mem/wql/codegen.logos` (`returns_string`, `emit_push_str_one`, `push_tuple_ty`)

### `el.emit.chunk` — self-contained emission chunk

EL/Deem codegen emits into a chunk that is a SEPARATE AST doc carrying its own `use` list (string/vec/option/hashmap/set/btree), since a chunk does not inherit the trigger module's imports.

*Divergence:* no analogue; a metacall codegen detail.

*Evidence:* the `use` run at the head of each emitting quote —
`stdlib/mem/wql/rexpr_walk.logos` (`emit_fn_quote_blob`, wql!) and
`stdlib/mem/wql/trama_render.logos` (the render-fn quote, trama!). The list was a
shared text prologue (`codegen.logos::begin_chunk`) until ADR 0024 S5 made both
items quotes; a quote states its imports as imports, so there is no prologue fn.

### `el.restrict.f64-key` — f64 is not a hash/set key

f64 lacks Hash+Eq, so it cannot be a rel column, a `group by`/join hash key, or feed set-deduplication — such positions either take the LOOP join tier (an equi-join key, `deem.join.cascade-hash`) or are a named compile error (a rel column, a `group by` key); f64 is fine as a scalar in arithmetic/projection/order-by.

*Divergence:* RESTRICTION — narrower than CEL/SQL where floats may appear anywhere; Deem excludes f64 from keyed/set positions because equality/hashing is unsound.

*Evidence:* `stdlib/mem/wql/plan_walker.logos` (`rel_col_ty_ok`); `stdlib/mem/wql/el.logos` (`el_set_col_admit` → `EL_COL_NO_EQ`); `stdlib/mem/wql/rexpr_walk.logos` (`emit_aggregate`, the `group by` f64 refusal); `stdlib/mem/wql/join_sel.logos` (`step_cascade`); `tests/logos/fail/wql_rel_float_col_fail.logos`, `tests/logos/fail/wql_group_f64_key_fail.logos`; ADR 0012-queue2 §4a (join keys / rel columns)

### `el.restrict.column-decl` — what may be a COLUMN is decided at the source's declaration

A field of a struct/schema BOUND AT A SOURCE is admitted as a column only on positive evidence read off its declared type NODE (`reflect::column_decl_admit`): an EL-lattice scalar; a traversal edge (`[T;N]`/`&[T]`/`[T]`/`Vec<T>`); a struct/schema this module declares (a nested-path base); a `type` alias this module declares (incl. `type El<T> = T`, which stays unresolved by design); a FIELDLESS enum; or any plain type name this module does not declare (the reflection view is module-local, so an imported name is admitted on ignorance, not on evidence). REFUSED with a named front-end error: a TUPLE type; a generic instantiation whose head is none of the above (`Option<T>`); a locally-declared enum with a payload-carrying variant.

*Ground:* `el_ty_of_name` launders every out-of-lattice type NAME to `EL_TY_INT` and the EL's key/projection vectors are `Vec<i64>`, so a laundered column is READ AS ITS FIRST EIGHT BYTES. For a tagged box those bytes are the DISCRIMINANT — measured, `group by` over an `Option<i64>` column returned one group per VARIANT (2 where 3 was right) with rc 0 and no diagnostic, and `select distinct` returned raw stack addresses; for a tuple they are a POINTER, reaching `arith.cmpi` on an `!llvm.ptr`. The name lookup cannot make this distinction (it is handed a name and nothing else, and its leniency is load-bearing for nested structs and generic aliases); the declaration site has the type node.

*Divergence:* RESTRICTION — narrower than SQL, which has no analogue of the eight-byte reading. The refusal is about the SOURCE'S DECLARATION, not about a clause: a query that binds such a struct and never mentions the column is refused too.

*Evidence:* `stdlib/mem/wql/reflect.logos` (`column_decl_admit`, `column_decl_why`); `tests/logos/fail/wql_column_decl_{option,data_enum,tuple}_col_fail.logos`; `tests/logos/fail/wql_domain_layer_mlir_tuple_col_fail.logos`; admitted side `tests/logos/pass/wql_column_decl_fieldless_enum_e2e.logos`

### `el.restrict.strict-optionality` — no `has()` / no `?.`

EL has no CEL `has()` macro and no safe-navigation `?.` — under the static/strict surface everything is mandatory by schema and optionality is expressed only via `Option`-typed schema fields (D4 strict); the explicitly-erased dynamic bindings that gave a query lenient `null` were withdrawn at P5 (`deem.exec.lenient-null`); lenient `null` survives only in the runtime template engine, over a `WAny`-typed schema field.

⚠ **The `Option`-typed schema field half of that sentence is a D4 RULING WITH NO WORKING ARTIFACT BEHIND IT, and is now REFUSED rather than silently wrong.** Measured: an `Option<i64>` column compiled, ran, and answered ONE GROUP PER VARIANT under `group by` while an `i64` control column in the same program answered correctly — see `el.restrict.column-decl`. Making an `Option` column WORK (read as its payload, with a stated null ordering/grouping rule) is an open capability decision, not a defect fix; until it is taken, optionality on a strict source is expressed by a sentinel scalar column or by splitting the payload out.

*Divergence:* RESTRICTION vs CEL (which has `has()` and dynamic missing-key `null`); Deem/EL makes the strict case total; the opt-in lenient query mode went with the interpreter at P5.

*Evidence:* ADR 0012 D4 (§"Resolved open decisions"); ADR 0012-queue2 §4/§4a
