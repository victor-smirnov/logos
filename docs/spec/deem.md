# Deem

> Scope: Deem — Logos's native query facility over Writ data. Two surfaces share one Writ-schema IR (SExpr scalar tier + RExpr relational/graph tier): the STATIC `deem` LANGUAGE ITEM (`pub? deem q(params) { query }` — metacall → native fn, sqlx-style prepared statement; the historical `deem!` macro is RETIRED, its spelling errors with the replacement written out) and the DYNAMIC `Query::compile(text,&cat)?.run(&env)?` runtime API (package `logos.std.deem`). This spec is ALSO the canonical home of EL (rule domain `el.*`), the CEL-class expression sublanguage embedded by both Deem clauses and Trama (`docs/spec/trama.md` links these `el.*` ids). Deem ships/versions with the language but is a metaprogramming/stdlib surface, so it has its own spec. Source layers: `stdlib/std/wql/grammars/{wql,el}.peg` (PEG surfaces, schema-emission mode), `stdlib/std/wql/*.logos` (engine — ABI-excluded internals), `stdlib/std/deem/deem.logos` (the ABI-stable dynamic API), ADR 0012 (`docs/adr/0012-writ-query-language.md`) + ADR 0012-queue2 (`docs/adr/0012-queue2-interpreter.md`). Each rule's `id` is its permanent linkable address; the domain is `deem` for the query surface and `el` for the shared expression language.

## Surfaces and execution model

### `deem.surface.static-item` — `pub? deem q(params) { query }` language item

The static surface is the `deem` ITEM (grammar/Sema-owned head; contextual lead ident — see `item.deem.*` in `items.md`): the COMPILER parses the body with the C++ parser generated from the same `wql.peg` the runtime parser comes from and hands the stdlib handler a zero-copy pointer into its arena; the handler walks the plan and emits `fn <name>(<params>) -> <Ret>` — a prepared statement compiled to native code. Item visibility is real (`pub`/none); the historical `resource <name> = deem!(…){…};` macro spelling is RETIRED and errors with the item replacement written out (name and params substituted).

*Divergence:* the compile-time-typed-query model is `sqlx::query!` for Writ; unlike SQL there is no runtime query planner in this surface.

*Evidence:* `src/compiler/sema_expr.cpp` (lower_deem_def), `stdlib/std/wql/wql.logos`, `tests/logos/pass/wql_deem_item_e2e.logos`, `tests/logos/fail/wql_deem_macro_retired_fail.logos`

### `deem.surface.params` — parenthesized parameter list

The parens carry a genuine Logos fn parameter list, re-emitted VERBATIM into the generated signature (param order preserved) and thus type-checked by the compiler; the handler also parses it locally to bind sources and type scalars — there is no `$` sigil and no `with` clause (both retired).

*Divergence:* EXTENSION over SQL/LINQ — query inputs are ordinary strongly-typed Logos function parameters, not bind markers.

*Evidence:* `stdlib/std/wql/wql.logos#L15-L32`; parser `stdlib/std/wql/params.logos` (`parse_macro_params`, `MacroParams`)

### `deem.surface.source-param` — slice params are sources

A slice param (`emps: &[Emp]`) is a query SOURCE named by a `from`/`join` clause by its param name; the row type is the slice ELEMENT type, its fields reflected automatically; a source ident matching no slice param is a compile error.

*Divergence:* EXTENSION — sources are typed Rust-style slices, giving static row-field resolution (P3 schema-typing-as-selector).

*Evidence:* `stdlib/std/wql/wql.logos#L24-L30`; reflection `stdlib/std/wql/reflect.logos`

### `deem.surface.scalar-param` — scalar params referenced bare in EL

A scalar param (`i64`/`f64`/`str`/`bool`) is referenced BARE (no sigil) inside EL clause bodies and by `limit`; its EL value-type is seeded from the declared param type (`el_ty_of_name`).

*Divergence:* differs from CEL/SQL bind variables — a scalar param is a plain in-scope name, not a `$`-prefixed or `?` positional bind.

*Evidence:* `stdlib/std/wql/wql.logos#L28-L30`; `stdlib/std/wql/el.logos#L136-L143` (`el_ty_of_name`)

### `deem.surface.pipeline` — clause pipeline order

Evaluation order is `from → [join…] → where → group/aggregate → having → order → project(select) → distinct → limit`; `having` exists only on the aggregate shape.

*Divergence:* matches SQL logical clause ordering (WHERE before GROUP BY before HAVING before ORDER BY before the projection's DISTINCT/LIMIT).

*Evidence:* `stdlib/std/wql/grammars/wql.peg#L41-L50`; lowering `stdlib/std/wql/lower.logos#L107-L124` (RQSimple pipeline where→order→project→distinct→limit)

### `deem.surface.program-envelope` — rel blocks + one entry query

The macro body is an `RQProgram` envelope: zero or more `rel NAME(cols){ bodies }` blocks followed by exactly one entry query; a rel-less body still parses as a program (the `rels` edge is NULL).

*Divergence:* the rel/entry split mirrors Datalog's rules + goal; a bare entry query is the degenerate zero-rule program.

*Evidence:* `stdlib/std/wql/grammars/wql.peg#L96-L100,L234-L237`; `stdlib/std/wql/wql.logos#L91-L95`

## Query shapes

### `deem.query.from` — `from src var`

Every query opens with `from <src> <var>`: `src` names a source (slice param or rel), `var` binds the row loop variable used by all downstream EL clause bodies.

*Divergence:* the range-variable binding is Datalog/comprehension style (`from src var`) rather than SQL's post-hoc `FROM t` with column-scoped names.

*Evidence:* `stdlib/std/wql/grammars/wql.peg#L282-L293`

### `deem.query.simple` — RQSimple scan/filter/project

`from src v [where P] select [first] [distinct] S [order by O [desc]] [limit N|p] [: RTy]` — a single-source scan with optional filter, projection, and select-tail modifiers.

*Divergence:* the SQL `SELECT … FROM … WHERE …` single-table query, with the clause keywords reordered to source-first.

*Evidence:* `stdlib/std/wql/grammars/wql.peg#L288-L300`

### `deem.query.join` — RQJoin N-way join chain

`from a x ([anti] join b y on ON)+ [where P] select …` — one or more join steps chained after the source; each step's `on` predicate may reference every var bound so far plus scalar params; the chain lowers left-deep.

*Divergence:* SQL `INNER JOIN … ON` / `WHERE NOT EXISTS` (anti), generalized to an N-way left-deep chain; `LEFT/RIGHT/FULL OUTER` joins are NOT provided (RESTRICTION).

*Evidence:* `stdlib/std/wql/grammars/wql.peg#L313-L334`; lowering to left-deep `RJoin`/`RAnti`/`REdge` `stdlib/std/wql/lower.logos#L134-L198` (`fold_join_steps` + `lower_join`)

### `deem.query.aggregate` — RQAggr group-by + aggregate

`from a x ([anti] join b y on ON)* [where P] group by K aggregate name=fn(arg?),… [having H] select …` — join steps are OPTIONAL here (aggregate over the joined or single-source stream); `having` is a predicate over the group key + aggregate output names.

*Divergence:* SQL `GROUP BY … HAVING …`, restricted to a SINGLE group key expression `K` (RESTRICTION; no multi-column `GROUP BY a,b` — use a tuple key expression).

*Evidence:* `stdlib/std/wql/grammars/wql.peg#L336-L368`; lowering `stdlib/std/wql/lower.logos#L205-L251` (`lower_aggr`: where→RAggr→having-as-RFilter→order→project→distinct→limit)

### `deem.query.find` — RQFind single-row borrow

`from src var find P` — REPLACES where+select: the generated fn returns `Option<&Ty>`, a borrow of the FIRST row matching `P` (early-exit scan), `None` when none match; no other clause may follow.

*Divergence:* EXTENSION — like Rust `Iterator::find` returning a borrow, not a SQL construct; `P` may reference scalar params bare.

*Evidence:* `stdlib/std/wql/grammars/wql.peg#L302-L311`; lowering `stdlib/std/wql/lower.logos#L258-L262` (`lower_find`: RProj(identity) over RFilter over RScan); emission `stdlib/std/wql/rexpr_walk.logos#L916-L975`

### `deem.query.shape-dispatch` — ordered-choice shape selection

The four shapes are distinguished by PEG ordered choice — join, then aggregate, then find, then the simple fallback — each re-parsing the shared `from src var` prefix (packrat-memoized), disambiguated by the structural keyword after the source.

*Divergence:* no analogue; a grammar/parsing detail.

*Evidence:* `stdlib/std/wql/grammars/wql.peg#L276-L279`

## Clauses and modifiers

### `deem.clause.where` — `where P`

`where <P>` filters the row stream to rows for which the EL predicate `P` (a `bool`) holds; lowers to an `RFilter` (σ).

*Divergence:* SQL/LINQ `WHERE` / `.filter(…)`.

*Evidence:* `stdlib/std/wql/grammars/wql.peg#L409-L410`; `RFilter` `stdlib/std/wql/ir.logos#L252`

### `deem.clause.group-by` — `group by K`

`group by <K>` partitions rows by the EL key expression `K`; groups feed the `aggregate` specs; lowers to an `RAggr` (γ) carrying one key + the aggregate-spec array.

*Divergence:* SQL `GROUP BY`, single-key only (see `deem.query.aggregate`).

*Evidence:* `stdlib/std/wql/grammars/wql.peg#L344,L352`; `RAggr` `stdlib/std/wql/ir.logos#L256`

### `deem.clause.aggregate` — `aggregate name=fn(arg?),…`

`aggregate <name>=<fn>(<arg>?),…` binds each aggregate output to a column `name` computed by `fn` over the group; `count()` is the sole nullary form (matched first in ordered choice), the arg-bearing form `fn(e)` carries an EL argument.

*Divergence:* SQL aggregate list with explicit output aliasing (`name=fn(arg)` vs `fn(arg) AS name`).

*Evidence:* `stdlib/std/wql/grammars/wql.peg#L435-L443`; `RQAgg`/`RAgg` `stdlib/std/wql/grammars/wql.peg#L165`, `stdlib/std/wql/ir.logos#L175-L180`

### `deem.clause.having` — `having H`

`having <H>` filters GROUPS by an EL predicate over the group key + aggregate output names; it has no IR node of its own — it lowers to an `RFilter` over the `RAggr` output (one filter mechanism).

*Divergence:* SQL `HAVING`; exists only on the aggregate shape.

*Evidence:* `stdlib/std/wql/grammars/wql.peg#L412-L414`; lowering note `stdlib/std/wql/ir.logos#L258-L262`

### `deem.clause.select` — `select S`

`select <S>` projects each surviving row to the EL expression `S`; lowers to an `RProj` (π); the projected value's EL type determines the row element type of the result `Vec`.

*Divergence:* SQL `SELECT`; a single projection expression (scalar or tuple), NOT a comma-separated column list — multiple columns are a `select (a,b,…)` tuple (see `deem.project.tuple`).

*Evidence:* `stdlib/std/wql/grammars/wql.peg#L428-L429`; `RProj` `stdlib/std/wql/ir.logos#L253`; emission `stdlib/std/wql/rexpr_walk.logos#L688-L908`

### `deem.select.distinct` — `select distinct S`

`select distinct <S>` dedups the PROJECTED values; lowers to an `RDistinct` (δ) ABOVE the projection; the MVP dedup is a linear `__out` scan on native `==` (O(n²)).

*Divergence:* SQL `SELECT DISTINCT`.

*Evidence:* `stdlib/std/wql/grammars/wql.peg#L122`; `RDistinct` `stdlib/std/wql/ir.logos#L265`; dedup `stdlib/std/wql/rexpr_walk.logos#L325-L347`

### `deem.select.first` — `select first S`

`select first <S>` makes the query SINGLE-ROW: the fn returns `Option<ElemTy>` — the first projected value in scan order (or in `order by` order when present) — emitted with early-return on the first match (no `Vec`); `first` excludes `distinct` and `limit`.

*Divergence:* EXTENSION — like `SELECT … LIMIT 1` returning an `Option` rather than a one-row set.

*Evidence:* `stdlib/std/wql/grammars/wql.peg#L46-L50`; emission `stdlib/std/wql/rexpr_walk.logos#L64-L76,L879-L891`; exclusion diagnostics `stdlib/std/wql/plan_walker.logos#L88-L89,L97-L98`

### `deem.select.order-by` — `order by O [desc]`

`order by <O> [desc]` sorts by ONE key expression `O` (ascending default, `desc` for descending); lowers to an `RSort` (τ) that sits UNDER the projection (its key ranges over the input rows / the group key + aggregate outputs); the MVP sort is a stable O(n²) insertion permutation.

*Divergence:* SQL `ORDER BY`, restricted to a SINGLE sort key (RESTRICTION; no `ORDER BY a, b` — compose a tuple key or reorder).

*Evidence:* `stdlib/std/wql/grammars/wql.peg#L416-L419`; `RSort` `stdlib/std/wql/ir.logos#L263`; emission `stdlib/std/wql/rexpr_walk.logos#L292-L303,L349-L406`

### `deem.select.limit` — `limit N | param`

`limit <N|p>` truncates to the first N rows: N is either an INTEGER literal or a bare IDENT naming a scalar param from the deem parameter list; lowers to an `RLimit` ABOVE the projection.

*Divergence:* SQL `LIMIT` (no `OFFSET`, RESTRICTION); the param form is the prepared-statement bind.

*Evidence:* `stdlib/std/wql/grammars/wql.peg#L44-L45,L286`; `RLimit` `stdlib/std/wql/ir.logos#L264`; emission `stdlib/std/wql/rexpr_walk.logos#L267-L320`

### `deem.select.result-ty` — `: RTy` result-type annotation

A trailing `: <ResultTy>` names the result element type explicitly (a type-name IDENT); presence is tracked by `has_result_ty`.

*Divergence:* EXTENSION — an explicit static result-type ascription, no SQL analogue.

*Evidence:* `stdlib/std/wql/grammars/wql.peg#L421-L422`

## Projections

### `deem.project.scalar` — scalar projection

`select <expr>` where `<expr>` is a scalar EL expression produces a `Vec<T>` whose element type `T` is the projected expression's EL type; `select first` gives `Option<T>`.

*Divergence:* SQL single-column projection.

*Evidence:* `stdlib/std/wql/rexpr_walk.logos#L723-L729`; `infer_ty` `stdlib/std/wql/codegen.logos#L78-L142`

### `deem.project.tuple` — tuple projection `(a,b,…)`

`select (a, b, …)` (≥2 components, EL `STuple`) produces a native `Vec<(T1,T2,…)>`; under `select first` it is `Option<(T1,T2,…)>`; tuples are legal ONLY in a `select` position (rejected in where/on/group-by/having/order-by/aggregate-arg/find).

*Divergence:* EXTENSION — multi-column projection is a first-class Logos tuple (matches Rust iterator `.map(|r| (a,b))`), unlike SQL's flat column list.

*Evidence:* `stdlib/std/wql/grammars/el.peg#L259-L263`; `STuple` `stdlib/std/wql/ir.logos#L141`; type emission `push_tuple_ty` `stdlib/std/wql/codegen.logos#L188-L206`; non-select rejection `reject_tuple` `stdlib/std/wql/codegen.logos#L165-L182`

### `deem.project.find-borrow` — `find` returns `Option<&Row>`

`find P` projects nothing — it returns a zero-copy borrow `Option<&Ty>` of the first matching row (early-exit), distinct from `select first` which returns a projected value BY VALUE.

*Divergence:* EXTENSION — see `deem.query.find`.

*Evidence:* `stdlib/std/wql/rexpr_walk.logos#L916-L975`

## Joins

### `deem.join.step` — one join step `[anti] join src var on P`

A join step introduces a new source `src` bound to `var` with a required `on` predicate `P` (classic form); `anti` makes it an anti-join; steps chain N-way and each `on`/`where` may reference every var bound so far plus scalar params.

*Divergence:* SQL `[NOT EXISTS] JOIN … ON`, restricted to the equi/theta forms below (no outer joins).

*Evidence:* `stdlib/std/wql/grammars/wql.peg#L385-L395`; `RJoin`/`RAnti` `stdlib/std/wql/ir.logos#L254-L255`

### `deem.join.cascade-hash` — join-strategy cascade by key-type capability

The `on` predicate is split into an equi-key term (`<bound-side> == <new-side>`) plus a residual, and the strategy is chosen from the equi-key type's capability: `Hash+Eq` → HASH join, else `Ord` → TREE join, else `PartialEq` → nested-LOOP join, else a compile error; f64 keys land in the LOOP tier (no hash/tree).

*Divergence:* EXTENSION over SQL (which leaves strategy to a cost planner) — Deem picks the strategy statically from the key TYPE's trait capability, the "strong-typing-as-selector" principle; f64's lack of Hash/Ord is a documented RESTRICTION forcing the loop tier.

*Evidence:* `stdlib/std/wql/rexpr_walk.logos#L1035-L1046` (`join_key_caps`), `L1174-L1250` (`analyze_step`, tier selection `L1240-L1247`); equi/residual split (shared static+dynamic) `stdlib/std/wql/optimize.logos#L637-L725`

### `deem.join.equi-residual-split` — equi-key vs residual predicate split

The conjunctive `on` predicate is decomposed into AND-terms; the first usable `<bound> == <new>` cross-var equality becomes the join KEY (driving the hash/tree probe), and the remaining terms form a residual filter applied after the probe.

*Divergence:* standard relational equi-join / theta-join separation; the split logic is shared verbatim by the static emitter and the dynamic interpreter.

*Evidence:* `stdlib/std/wql/rexpr_walk.logos#L1170-L1239`; shared analysis `stdlib/std/wql/optimize.logos#L637-L725` (`split_and_terms`/`name_refs`/`refs_mask`)

### `deem.join.anti` — anti-join emission

`anti join src var on P` keeps a bound row iff NO `src` row satisfies `P`; emission has three tiers — nested-loop full-predicate scan, hash-set containment (no residual), and hash/tree bucket-scan (absent-or-all-fail with residual) — each guarding the outer body with `if (!matched) { … }`.

*Divergence:* SQL `WHERE NOT EXISTS` / anti-semi-join; the tiering mirrors the inner-join cascade.

*Evidence:* `stdlib/std/wql/rexpr_walk.logos#L1124-L1135,L1483-L1720`

## Edge traversal (graph steps)

### `deem.edge.traversal` — `[anti] join base.field[.field] var [on P]`

A traversal step ranges a new `var` over a COLLECTION FIELD PATH of an already-bound row var (`base.field…`); `on` is OPTIONAL (containment IS the join, `P` is a residual filter); it lowers to `REdge`, not `RJoin`.

*Divergence:* EXTENSION — the graph "edge follow" step (ADR 0012 graph data model: `SField` ⊂ `REdge` ⊂ `RFix` is the same edge primitive at three iteration depths); no SQL analogue (closest is `UNNEST`/lateral join).

*Evidence:* `stdlib/std/wql/grammars/wql.peg#L370-L406`; `REdge` `stdlib/std/wql/ir.logos#L239-L251`; emission `stdlib/std/wql/rexpr_walk.logos#L1137-L1162,L1252-L1287`

### `deem.edge.always-nested-loop` — traversal is always nested-loop

An `REdge` source depends on outer row vars (no build-once index exists), so traversal ALWAYS emits a nested loop and the join-strategy cascade bypasses `REdge` steps.

*Divergence:* no analogue; an execution-strategy consequence of the correlated source.

*Evidence:* `stdlib/std/wql/ir.logos#L249-L250`; `stdlib/std/wql/rexpr_walk.logos#L1252-L1287`

### `deem.edge.anti-traversal` — anti-traversal

`anti join base.field var [on P]` keeps the bound row iff NO element satisfies `P` (or, with no `on`, iff the collection is empty).

*Divergence:* EXTENSION — anti-semantics over a correlated collection.

*Evidence:* `stdlib/std/wql/ir.logos#L246-L248`; grammar `stdlib/std/wql/grammars/wql.peg#L377-L391`

### `deem.edge.path-classification` — traversal form ordered before classic

The traversal step alt is ordered FIRST and demands ≥1 `.field` segment after the head IDENT, so the classic `join src var on …` form (no dot) can never shadow it under PEG ordered choice.

*Divergence:* no analogue; a grammar disambiguation.

*Evidence:* `stdlib/std/wql/grammars/wql.peg#L370-L402`

## Aggregates

### `deem.agg.builtins` — count/sum/min/max/avg

The five builtin aggregates are `count` (nullary), `sum`, `min`, `max`, `avg` (each unary over an EL argument); `is_builtin_agg`/`agg_takes_arg` classify names; unknown aggregate names are diagnosed.

*Divergence:* the SQL aggregate set minus statistical extras; `count(*)` is spelled `count()`.

*Evidence:* `stdlib/std/wql/el.logos#L188-L196`; emission ids `AGG_COUNT..AGG_AVG` `stdlib/std/wql/rexpr_walk.logos#L49-L53`

### `deem.agg.result-ty-table` — the generic aggregate result-type rule table

One shared `agg_result_ty(fn, arg_ty) -> ty` table maps `count:()→INT`, `sum/min/max:T→T` (numeric T), `avg:T→Quot(T)` where `Quot(INT)=Quot(FLT)=FLT` (the exact mean is f64 division); an out-of-domain argument (non-numeric to sum/min/max/avg) or unknown name returns -1 (diagnosed).

*Divergence:* EXTENSION — a single typed rule table shared by BOTH backend tiers (static emitter migrating to consult it; dynamic interpreter consults it now), unlike SQL's per-function return-type rules; `avg` always widens to f64 even over integers.

*Evidence:* `stdlib/std/wql/el.logos#L167-L209` (`agg_result_ty`, `el_quot_ty`); float-repr emission `stdlib/std/wql/rexpr_walk.logos#L2515-L2547,L2740-L2751`; ADR 0012-queue2 §6

### `deem.agg.avg-float` — avg accumulates and divides as f64

`avg` casts an integer argument to f64, accumulates a f64 sum, and divides by the count as f64, so its result is always FLT regardless of argument type.

*Divergence:* differs from SQL engines where `AVG` of an integer column may stay integer or decimal; Deem fixes `avg → f64`.

*Evidence:* `stdlib/std/wql/rexpr_walk.logos#L2349-L2353,L2398-L2403`; `stdlib/std/wql/el.logos#L182-L185`

## Graph sources and the edge vocabulary

### `deem.graph.vocabulary` — the eight-column edge relation

Every graph-shaped source materializes as ONE relation `edge(parent: i64, key: str, idx: i64, child: i64, kind: str, tag: i64, vi: i64, vs: str)`: container nodes carry structure (ids = handles/addresses; `key` = field/map key, `idx` = array position, −1 otherwise), leaves carry the value in the TYPED payload columns `vi`/`vs` with `kind` as the discriminator. Payload columns are TOTAL — canonical fillers `0`/`""`, never Null (rel rows are set-deduplicated; two-valued Eq only). `bool` rides `vi` as 0/1; `f64` rides `vi` as its IEEE-754 BITS (`kind == "f64"`; bit identity is the honest Eq for floats — NaN payloads and ±0.0 stay distinct; recover via `f64_from_bits`). ONE vocabulary across all producers and binding times: the Writ walker, the native derive, and the runtime tree scan.

*Evidence:* `stdlib/std/wql/writ_graph.logos` (wg_emit), `stdlib/std/deem/exec.logos` (ts_scan/ts_walk/es_scan), `tests/logos/pass/wql_native_graph_e2e.logos` (f64 bits, executed)

### `deem.graph.writ-param` — `g: &Writ` is a graph source

A deem param typed `&Writ` registers the edge relation under the param's own name; the document is scanned edge-per-row (expansion-once: DAG/cycle-safe), with a VIRTUAL ROOT EDGE (`parent == 0`) making the root queryable. No materialized copy of the document exists — the document IS the fact base.

*Evidence:* `stdlib/std/wql/writ_graph.logos`, `tests/logos/pass/wql_writ_graph_e2e.logos`

### `deem.graph.path-sugar` — `from g .key [*] * {kind} ** v` graph paths

`from <graph> <step>* <binder>` navigates: `.key` (map/field move), `[*]` (array elements, `idx >= 0`), `*` (any child), `{kind}` (a FILTER on the current node's kind, not a move), `**` (descendant-or-self). Steps desugar to a classic join chain over the edge relation in ONE shared plan→plan pass used by BOTH binding times; `**` lowers to an INJECTED ordinary Datalog relation `__reach_<src>` (self-pairs + transitive step; deduped by name per program), so reachability runs on the existing rel machinery — no second engine.

*Evidence:* `stdlib/std/wql/lower.logos` (gp_desugar/gp_reach_rel), `tests/logos/pass/wql_gpath_e2e.logos`

### `deem.graph.native-derive` — `#[derive_graph_source]` for native objects

Native Logos objects are deliberately UNTAGGED (types are known statically or via dyn Trait/TypeId; the tag system is a Writ-style special case), so their traversal is GENERATED at compile time by reflection: per annotated struct the derive emits a walker + a materializer `__gs_edges_<T>` + `impl GraphSource for T` — the same vocabulary (node id = address, `tag = 0`, Vec fields as a container node with `idx`-ed elements). Field classes v1: i64/bool/str/f64, `Vec<i64|str|Struct>`, nested annotated structs; dyn-Trait fields (vtable + TypeId) are the named v2.

*Evidence:* `stdlib/std/compiler/metaprog/derive_graph_source.logos`, `tests/logos/pass/wql_native_graph_e2e.logos`

## Source traits

### `deem.source.trait` — `trait { rel … }` declares a source vocabulary

A trait may declare `rel` members (`rel edge(parent: i64, …);` — columns i64/str/bool, the Hash+Eq rule); an impl binds each rel to a MATERIALIZER (`rel edge = writ_graph_edges;`, `fn(&T) -> Vec<RowTuple>`). A deem param typed by an implementing type carries the trait's relations: a single-rel vocabulary is addressable as the param itself (`from g …`), a multi-rel one is param-prefixed (`from e_trace t …`). The walker is source-type-blind — which params carry relations, their columns, and the materializer all arrive as compiler-computed data (the natspec), and the built-in Writ/IncrRec sources are ordinary stdlib impl declarations riding the same mechanism.

*Evidence:* `stdlib/std/wql/writ_graph.logos` (GraphSource), `stdlib/std/deem/mapping_state.logos` (EngineState), `tests/logos/pass/wql_source_trait_e2e.logos`

### `deem.source.engine-state` — `e: &IncrRec` exposes the reasoner's own past

A deem param typed `&IncrRec` carries the `EngineState` vocabulary (`impl EngineState for IncrRec`, stdlib): four relations `<p>_trace(epoch, kind, step, delta, total, ns)` · `<p>_epochs(epoch, ins, del, rounds, ns)` · `<p>_tail(epoch, converged, pending, bound, cutr)` · `<p>_controls(epoch, kind, val)` — sensor facts about the COMPLETED past (the I1 contract), materialized by `logos.std.deem` state materializers. This is the self-applicability seam (ADR 0015/0016 case S): the engine is a source like any other, and its honesty oracles (Σδ consistency, the raise/converge Encounter pair) are expressed in Deem itself.

*Evidence:* `stdlib/std/deem/mapping_state.logos` (EngineState + materializers), `tests/logos/pass/wql_engine_source_e2e.logos`

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

### `deem.mapping.runtime-artifacts` — `<M>__rules()` / `<M>__src()` and `compile_with_mapping`

Mappings are STATIC-ONLY items; the dynamic side only CONSUMES them. Each mapping emits two artifacts — `<M>__rules() -> str` (canonical rel-list text) and `<M>__src() -> str` (its source-param name) — and `Query::compile_with_mapping(text, &cat, bind_as, rules, src)` fuses them into a dynamically-compiled query with the same parse/graft/rename machinery; the source binds via `bind_source_tree(bind_as, root)`.

*Evidence:* `stdlib/std/deem/query.logos` (compile_with_mapping), `tests/logos/pass/query_mapping_runtime_e2e.logos` (parity with the static twin)

## rel blocks and Datalog

### `deem.datalog.rel-block` — `rel NAME(cols){ bodies }`

A `rel` block declares a named derived relation with SET semantics: `cols` are declared `name: ty` columns, `bodies` are `;`-terminated query producers whose UNION (deduped structurally on insert) is the relation; each body is restricted to from/join/where/select.

*Divergence:* Datalog rules (multiple bodies = a disjunction of rules with the same head); the set/union semantics are the Datalog default.

*Evidence:* `stdlib/std/wql/grammars/wql.peg#L239-L267`; validation `stdlib/std/wql/plan_walker.logos#L11-L51`

### `deem.datalog.rel-columns` — rel columns are i64/str/bool (Hash+Eq)

Rel columns must be `i64`/`str`/`bool` — rels are sets deduped by structural equality, so columns need Hash+Eq; `f64`/`f32` get their own named diagnostic (Eq loss is the reason).

*Divergence:* RESTRICTION — narrower than SQL/Datalog value domains; f64 is excluded because set membership needs Eq.

*Evidence:* `stdlib/std/wql/plan_walker.logos#L721-L741`; grammar note `stdlib/std/wql/grammars/wql.peg#L253-L255`

### `deem.datalog.rel-body-gates` — rel body modifier gates

A rel body is from/join/where/select ONLY; aggregate and `find` bodies are named errors, and `first`/`distinct`/`order by`/`limit`/`: RTy` are rejected in a body (they are entry-query concerns; distinct is implicit under set semantics); the select width must equal the declared column count.

*Divergence:* RESTRICTION — rel bodies are pure relation producers (Datalog rule bodies), not full queries.

*Evidence:* `stdlib/std/wql/plan_walker.logos#L184,L234-L275,L636-L721`

### `deem.datalog.rel-scan` — the entry query scans rels like sources

The entry query (and other rel bodies) may scan a rel by name exactly like a slice source; the walker rewrites the source name to the emitted rel slice and records an explicit dependency edge (`RelDeps`); self/forward/mutual references are legal (the registry is completed before any body resolves).

*Divergence:* Datalog rule bodies referencing other (or the same) relations.

*Evidence:* `stdlib/std/wql/plan_walker.logos#L23-L27,L764-L866` (two-pass registration then body resolution); `RelDeps` `stdlib/std/wql/params.logos#L186-L244`

### `deem.datalog.rel-borrow-gate` — rels cannot be borrowed out

`find` over a rel and a whole-rel-row `select` are compile errors — rels are fn-locals, so a borrow of their rows cannot leave the generated fn.

*Divergence:* EXTENSION — a Logos ownership constraint (borrows may not escape the query fn), no SQL/Datalog analogue.

*Evidence:* `stdlib/std/wql/plan_walker.logos#L45-L47,L597-L616,L964-L968`

### `deem.datalog.rel-tuple-binding` — rel row vars bind positional tuple columns

A rel-sourced row var binds to a native TUPLE row, so a field step `s.a` emits the POSITIONAL access `s.<idx>` (or `(*s)` for a 1-column scalar rel); the (var,col)→index/type binding is stamped by `stamp_rel_source` and consulted by codegen before the flat name dictionary.

*Divergence:* no analogue; an emission detail of set-typed tuple rows.

*Evidence:* `stdlib/std/wql/rexpr_walk.logos#L116-L140`; `ElTypes` rel-binding table `stdlib/std/wql/el.logos#L337-L381`

### `deem.datalog.scc-condensation` — SCC condensation of the rel dependency graph

The rel dependency graph is condensed into strongly-connected components with a dependencies-first topological order; a singleton SCC without a self-edge materializes one-shot (a helper fn), a recursive SCC (self-edge or multi-rel cycle) becomes one shared semi-naïve fixpoint fn.

*Divergence:* the standard Datalog stratification/SCC evaluation strategy.

*Evidence:* `stdlib/std/wql/params.logos#L287-L373` (`compute_rel_scc` — Warshall closure + component id + Kahn topo, `rec[c]` = size>1 or self-loop); `stdlib/std/wql/plan_walker.logos#L28-L32`

### `deem.datalog.semi-naive` — semi-naïve fixpoint over an SCC

A recursive SCC evaluates by semi-naïve iteration: per member a total set, a next-delta, and a shadow set (total ∪ next-delta); seed bodies (no in-SCC source) run once, then each round promotes delta→total, exits when all deltas empty, and re-runs the recursive bodies against the delta region; mutual recursion (multi-member SCC) is supported.

*Divergence:* textbook Datalog semi-naïve evaluation (delta relations); the delta variant is a loop variable, not IR rewriting.

*Evidence:* `stdlib/std/wql/rexpr_walk.logos#L3229-L3258,L3778-L4006` (`emit_scc_fn`); ADR 0012-queue2 §7

### `deem.datalog.termination` — no iteration cap (generative recursion may diverge)

Termination is the standard Datalog contract: recursion over a finite universe reaches a least fixpoint, but a recursive head that MINTS new values (e.g. `select (p.a + 1, …)`) can diverge — this is deliberately NOT capped (a silent cap would change semantics).

*Divergence:* matches Datalog's non-generative termination guarantee; generative recursion is the user's responsibility.

*Evidence:* `stdlib/std/wql/plan_walker.logos#L37-L44`; `stdlib/std/wql/wql.logos#L40-L45`

### `deem.datalog.stratified-negation` — stratified negation/aggregation

An `anti join R` or an aggregate body reading `R` where `R` is in the SAME SCC as the body's head rel is non-stratifiable — a named compile error listing the cycle members; negation/aggregation against an EARLIER (fully materialized) stratum is fine.

*Divergence:* standard Datalog stratified negation (a cycle through negation or aggregation is rejected).

*Evidence:* `stdlib/std/wql/plan_walker.logos#L33-L36,L144-L175` (`check_stratified`); negated/aggregated sub-lists `stdlib/std/wql/params.logos#L221-L237`

## UDF / UDA

### `deem.udf.reflection` — user functions reflected from the trigger module

The deem/trama handlers reflect every top-level `fn` of the trigger module into the UDF registry (name, return EL-lattice tag via `el_ret_class`, declared return type name, arity); codegen resolves a call name against the builtin registry first, then the UDF table (builtins shadow a same-named UDF); capacity is 32 top-level fns.

*Divergence:* EXTENSION over CEL/SQL — UDFs are ordinary module-local Logos functions, resolved by reflection, not a separate registration API (static surface).

*Evidence:* `stdlib/std/wql/el.logos#L211-L335` (`ElTypes` UDF section, `udf_add`/`udf_find`); reflection `stdlib/std/wql/reflect.logos#L273-L291` (`stamp_udfs_from_module`), `L249-L265` (arity + return-type reflection)

### `deem.udf.call-check` — arity and return-type checking

`check_calls` validates each call: unknown function (not builtin, not UDF) errors, arity mismatch errors, and an out-of-lattice UDF return type (`el_ret_class` = -1, e.g. a struct/reference/unit) errors; narrower int returns get an `as i64` widening cast at the emit site (u64/u128/i128 beyond i64 range truncate — documented MVP).

*Divergence:* EXTENSION — static UDF type-checking against the EL lattice, the agentic selector (P3).

*Evidence:* `stdlib/std/wql/codegen.logos#L504-L585` (`check_calls`); `el_ret_class` `stdlib/std/wql/el.logos#L155-L165`

### `deem.uda.triple` — user aggregates are init/step/fin triples

A user-defined aggregate is an init/step/fin triple whose finalizer return classifies the aggregate output column type (reflected UDA return class), enriching the builtin count/sum/min/max/avg set.

*Divergence:* EXTENSION — the classic init/step/final UDA protocol; the finalizer return type drives the projected column type.

*Evidence:* `stdlib/std/wql/rexpr_walk.logos#L2740-L2751` (`compute_agg_col_tys`, UDA reflected R class); ADR 0012-queue2 §6

## Optimizer

### `deem.opt.const-fold` — scalar constant folding

`simplify_sexpr` folds constant SBin/SUn/SCond: integer arithmetic (+ - * / %, division/modulo by zero left unfolded) and comparisons, float arithmetic (+ - * /, `%` NOT folded, non-finite results unfolded) and comparisons, boolean == != and && || (with short-circuit on a single const operand), and algebraic identities (`x+0`,`0+x`,`x-0`,`x*1`,`1*x`→x; `x*0`,`0*x`→0); a const-bool ternary collapses to the taken (itself-simplified) branch.

*Divergence:* standard constant folding; shared by both backend tiers (queue-2 runs it at query-compile time).

*Evidence:* `stdlib/std/wql/optimize.logos#L113-L170,L183-L271,L277-L366`

### `deem.opt.where-fold` — `where true`/`where false` folds

A const-true filter predicate drops the filter entirely; a const-false predicate marks the plan empty (yields no rows, emitted as an empty `Vec` with no scan loop).

*Divergence:* relational simplification with no direct SQL analogue at the language level (an optimizer guarantee).

*Evidence:* `stdlib/std/wql/optimize.logos#L509-L528`

### `deem.opt.identity-projection` — identity-projection accessor collapse

An unfiltered identity projection over a bare scan (the select is just the loop var or a base-less field ref matching it) is marked `identity` so the emitter returns the source slice `&[Row]` directly, skipping the copy loop.

*Divergence:* EXTENSION — a zero-copy borrow optimization for `from s v select v`, no SQL analogue.

*Evidence:* `stdlib/std/wql/optimize.logos#L420-L434,L618-L627`

### `deem.opt.limit-fold` — limit-0 / limit-over-empty fold to empty

`limit 0` (literal) marks the plan empty; a limit over an already-empty sub-plan stays empty.

*Divergence:* optimizer guarantee.

*Evidence:* `stdlib/std/wql/optimize.logos#L569-L578`

### `deem.opt.sort-const-drop` — order-by over a constant key dropped

Sorting by a key that const-folds to a literal orders nothing (every row compares equal, the pass is stable) → the sort is dropped.

*Divergence:* optimizer guarantee.

*Evidence:* `stdlib/std/wql/optimize.logos#L530-L545`

### `deem.opt.proj-collapse` — nested projection and distinct-over-empty collapse

`RProj(RProj(x))` collapses to a single projection (inner input simplified, outer selection kept); `RDistinct`/`RLimit` over an empty sub-plan stay empty; the empty result has a canonical `RSimplified{empty}` form the emitter renders as an empty `Vec`.

*Divergence:* standard relational peephole simplification.

*Evidence:* `stdlib/std/wql/optimize.logos#L406-L410,L491-L496,L583-L607,L632-L634`

### `deem.opt.shared-tiers` — the optimizer is shared by both backends

`simplify_sexpr`/`simplify_rexpr_ref` are pure IR→IR functions run by the STATIC emitter and re-run by the DYNAMIC interpreter at query-compile time; the join-step analysis (equi/residual split) is likewise shared, differing only in the type source (`ElTypes` vs the runtime checker) and the sink (source text vs eval).

*Divergence:* EXTENSION — one optimizer, two consumers (the schemas-as-IR payoff).

*Evidence:* `stdlib/std/wql/optimize.logos#L1,L637-L643`; ADR 0012-queue2 §1

## Static vs dynamic surfaces

### `deem.exec.static` — the static `deem` item (metacall → native, compile diagnostics)

The static surface parses, type-checks, optimizes and lowers at COMPILE time via metacall, emitting native Logos code linked into the program; all errors are compile DIAGNOSTICS; there are no runtime-string queries in this surface (queue 1).

*Divergence:* the compile-time-checked prepared-statement model (sqlx-style); the strong typing is the agentic selector at build time (P3).

*Evidence:* `stdlib/std/wql/wql.logos#L74-L97`; ADR 0012 "Static-first sequencing"

### `deem.exec.dynamic-api` — `Query::compile`/`run` (runtime, errors as values)

Query TEXT arriving at RUNTIME is parsed, type-checked, optimized and executed by a tree-walk over the SAME Writ-schema IR via `Query::compile(text,&cat)? .run(&env)?`; errors are VALUES (`Result` + positioned message), the compile-once/run-many contract holds, and `run` is re-entrant over different envs.

*Divergence:* EXTENSION — the runtime interpreter (queue 2); errors are the model's feedback signal, not compiler diagnostics.

*Evidence:* `stdlib/std/deem/deem.logos#L3838-L3850` (`Query::compile` — parse→typecheck→rel-register/validate/SCC/stratify→lower→simplify), `L4158-L4258` (`Query::run` — strict check→cascade→rel materialize→tree-walk→QRows); ADR 0012-queue2 §3

### `deem.exec.reuse` — parsers/optimizer/lowering reused verbatim

The dynamic surface reuses the peg-generated parsers, the IR optimizer, the plan-lowering, and the semantics (join cascade, semi-naïve, stratification, aggregate rules) verbatim — the same algorithms re-hosted from emitters to an evaluator (the payoff of schemas-as-IR).

*Divergence:* no analogue; an architecture consequence.

*Evidence:* ADR 0012-queue2 §1; `stdlib/std/deem/deem.logos#L1928-L1937` ("REUSED" design note — `parse_program`, `lower_rquery_to_rexpr`, `simplify_rexpr_ref`, `compute_rel_scc` all reused verbatim)

### `deem.exec.catalog` — `schema_catalog!` and `SchemaCatalog`

`resource cat = schema_catalog!{ S1, S2, … };` is a queue-1 metacall macro that reflects the named ADR-0011 `schema` decls out of the trigger module and emits a fn returning a `SchemaCatalog` view over a STATIC Writ blob in .rodata (schema code → {field → (key code, EL type, edge target)}); the dynamic checker resolves `e.field` against this catalog.

*Divergence:* EXTENSION — queue-1 serving queue-2 over the designated `annotation → metaprog hook → rodata Writ blob → runtime view` channel; no global registry, no link-time magic.

*Evidence:* macro `stdlib/std/wql/catalog_macro.logos#L1-L30,L247-L277`; runtime view `stdlib/std/deem/deem.logos#L163-L177` (`SchemaCatalog`), `L315-L366` (`from_static`/`merge_static` — two-pass rodata index), `L370-L405` (probes); ADR 0012-queue2 §5

### `deem.exec.env` — the runtime env: sources, params, UDF/UDA registry

`run` takes an `env` binding source names → Writ array handles, scalar params, and registered UDFs/UDAs; `register_fn(name, ptr)` uses an `RtVal`-based signature `fn(&[RtVal]) -> RtVal`, a UDA is an init/step/fin triple; `register_fn`/`register_agg` return `bool` (false = bad type name / capacity, no silent no-op); names resolve builtin-table-first then registry (same precedence as the static surface).

*Divergence:* EXTENSION — the runtime binding/registry surface (`QEnv`), analogous to a prepared-statement parameter set plus a UDF registry; `register_fn` caps at 4 args and takes a typed signature `(args: &[str], ret: str)`.

*Evidence:* `stdlib/std/deem/deem.logos#L457-L499` (`QEnv`), `L522-L564` (`bind_node`/`bind_source`/`bind_i64`/…), `L600-L626` (`register_fn` → bool), `L642-L660` (`register_agg` → bool, init/step/fin); ADR 0012-queue2 §6

### `deem.exec.bind-kinds` — the four source binding kinds

`bind_source` (a Writ array of schema'd rows) · `bind_source_erased` (lenient rows, CEL Null semantics) · `bind_source_tree` (a Writ VALUE scanned virtually, one row per edge, the graph vocabulary) · `bind_edge_rows` (PRE-MATERIALIZED rows in the same edge vocabulary — the runtime twin of a `#[derive_graph_source]` materializer). Tree and edge sources type identically (`vi: i64`, `vs: str`, total) and are REJECTED by the incremental path with a named error (no delta capture — materialize facts via FactStore).

*Evidence:* `stdlib/std/deem/deem.logos` (QB_* + binders), `stdlib/std/deem/check.logos`, `tests/logos/pass/wql_native_graph_e2e.logos` (runtime twin)

### `deem.exec.incremental` — the DBSP incremental path (ADR 0013)

`Query::incremental` maintains results under fact deltas (±-weighted Z-set batches): full relational algebra, recursion, and aggregation with change capture and provenance, oracle-gated against from-scratch recomputation. Facts live in a `FactStore` (the delta boundary: `insert`/`retract` events); virtual sources — tree scans and pre-materialized edge rows — are REJECTED with a named error (re-scan semantics have no delta capture; materialize facts to cross). The engine's own execution history is queryable back through `deem.source.engine-state`.

*Evidence:* `stdlib/std/deem/incr.logos`, `stdlib/std/deem/incr_rec.logos`, `tests/logos/pass/query_incr_*.logos`, ADR 0013

### `deem.exec.rtval` — RtVal runtime scalar and QRows

The runtime scalar is `RtVal { I(i64) | F(f64) | B(bool) | S(str) | Node(WAny) | Null }` (the EL lattice maps INT/FLT/BOOL/STR onto it, `Node` carries row/object handles, `Null` exists only in lenient mode); results are `QRows` with typed getters, `is_null(r,c)`, and a per-column type report (`"dyn"` for lenient columns).

*Divergence:* EXTENSION — the dynamic value model; the runtime cascade is tag dispatch on `RtVal` (strong-typing-as-selector, runtime edition); `rt_eq`/`rt_cmp`/`rt_key_hash` (FNV-1a over tag+payload, hashable tier I/S/B only) implement equality/ordering/hashing.

*Evidence:* `stdlib/std/deem/deem.logos#L728-L786` (`RtVal` enum + accessors), `L801-L810` (`rt_eq`), `L2932-L2946` (`rt_cmp`), `L2954-L2968` (`rt_key_hash`); `QRows` result/typed getters; ADR 0012-queue2 §2

### `deem.exec.qerror` — errors are QError values

Compile/run failures are `QError` VALUES carrying a positioned message (not compiler diagnostics), returned via `Result` so a running program (typically a model-driven loop) consumes the message as a feedback signal.

*Divergence:* EXTENSION — errors-as-values, the dynamic dual of the static surface's compile diagnostics.

*Evidence:* `stdlib/std/deem/deem.logos#L92-L120` (`QError` struct + `message()` + `qerr`/`qfail` builders); ADR 0012-queue2 §3

### `deem.exec.strict` — strict-on-schema typing (dynamic default)

By default every dynamic source is declared with a schema code and `e.field` resolves against the catalog exactly as the static queue resolves against the module AST; unknown field/fn/type mismatch is a `Query::compile` error.

*Divergence:* mirrors the static surface's strict schema typing (D4 strict-on-schema).

*Evidence:* ADR 0012-queue2 §4; `stdlib/std/deem/deem.logos#L4158-L4196` (strict type-check phase in `run`), catalog probes `L370-L405` (`schema_code`/`field_key`/`field_ty`)

### `deem.exec.lenient-null` — lenient/erased sources with CEL Null semantics

`env.bind_source_erased(name, arr)` / `bind_node_erased(name, node)` type a binding `dyn` (runtime-typed); field access on an erased value yields `RtVal::Null` when missing and `Null` propagates CEL-style — `Null` is falsy for `&&`/`||`/`!`, `Null==Null`→true / `Null==x`→false, any `Null` operand makes an ordering comparison false and arithmetic `Null`, a `Null` ternary condition takes the else branch, builtins on a non-string (incl. `Null`) arg → `Null`, a `Null` `where`/`{% if %}` predicate drops the row / skips the branch, non-array lenient values iterate as empty, `Null` render is the empty string, `order by` sorts `Null` keys as 0, `group by` groups `Null` keys together; a `dyn` side never qualifies as a hash key (such joins take the LOOP tier), aggregate args and rel columns of `dyn` type are REJECTED at check time.

*Divergence:* EXTENSION over the strict surface — CEL/JMESPath-style lenient `null` propagation, restricted to explicitly-erased bindings (D4 "lenient → queue-2"); a `WAny`-typed field on a strict schema also resolves leniently.

*Evidence:* ADR 0012-queue2 §4/§4a (the Null propagation table); `stdlib/std/deem/deem.logos#L572-L590` (`bind_node_erased`/`bind_source_erased`), `L1666-L1671` (comparison/equality Null rules), `L1690-L1725` (arithmetic/negation → Null), `L1738-L1763` (builtins on non-string → Null), erased field read `L1589-L1600`

### `deem.exec.dyn-cascade` — per-run join cascade from checked types

The dynamic join cascade is decided at `Query::compile` from the checked key types (hash for I/S/B, loop tier for F), not per-row — the SAME cascade rules as the static surface, re-hosted to the interpreter.

*Divergence:* matches `deem.join.cascade-hash`, evaluated at query-compile time over runtime-checked types.

*Evidence:* ADR 0012-queue2 §7; `stdlib/std/deem/deem.logos#L2431-L2494` (`analyze_join_step` — hash tier for I/S/B `L2465-L2466`, loop tier for F / `CT_DYN` `L2470-L2489`), hash build/probe `L3278-L3329`, loop tier `L3332-L3354`

## Expression Language (EL)

<a id="expression-language-el"></a>
EL is the CEL-class scalar expression sublanguage embedded by every Deem clause body (where/select/on/group-key/aggregate-arg/having/order/find) AND by Trama (`{{ … }}` / `{% if/for/set … %}`); its only coupling to Trama is the `expr: WRef<SExpr>` edge. `docs/spec/trama.md` links these `el.*` rule ids; the shared-sublanguage anchor is this section, `docs/spec/wql.md#expression-language-el`. EL is a strict PROFILE of the one IR (P1: subsets are profiles, not forks); its grammar is `stdlib/std/wql/grammars/el.peg`, its shared types (operator ids + value-type lattice) `stdlib/std/wql/el.logos`, its IR `stdlib/std/wql/ir.logos`.

### `el.grammar.precedence` — the CEL precedence chain

EL parses a fixed CEL-precedence chain: `ternary → || → && → ==/!= → <=/>=/</> → +/- → */ /%  → unary !/- → postfix .field → primary`; binary levels are left-associative (fold-mode over the running LHS).

*Divergence:* the CEL operator precedence and associativity exactly (`?:` lowest, postfix field access highest).

*Evidence:* `stdlib/std/wql/grammars/el.peg#L15-L28,L176-L228`

### `el.op.ternary` — conditional `c ? t : e`

`c ? t : e` builds an `SCond` (the CEL conditional); it emits as a Logos `if` expression `(if (c) { t } else { e })`; a const-bool condition const-folds to the taken branch; its inferred type is the then-branch type (both arms are expected to agree).

*Divergence:* CEL conditional `?:`; the branches must be type-compatible (strict, no CEL dynamic-widening).

*Evidence:* `stdlib/std/wql/grammars/el.peg#L180-L183`; emission `stdlib/std/wql/codegen.logos#L330-L338`; fold `stdlib/std/wql/optimize.logos#L299-L306`

### `el.op.logical` — `||` and `&&`

`||`→`SBin(OP_OR=1)`, `&&`→`SBin(OP_AND=2)`, both boolean-typed, emitted as Logos `||`/`&&`; they short-circuit-fold when one operand is a constant.

*Divergence:* CEL logical or/and.

*Evidence:* `stdlib/std/wql/grammars/el.peg#L185-L189`; ids `stdlib/std/wql/el.logos#L22-L23`; emission `stdlib/std/wql/codegen.logos#L726-L728`

### `el.op.equality` — `==` and `!=`

`==`→`SBin(OP_EQ=3)`, `!=`→`SBin(OP_NE=4)`, boolean-typed; integer/float/bool equality const-folds.

*Divergence:* CEL equality; f64 equality is permitted in EL expressions generally (but see `el.restrict.f64-key` for keyed positions).

*Evidence:* `stdlib/std/wql/grammars/el.peg#L191-L195`; ids `stdlib/std/wql/el.logos#L24-L25`; emission `stdlib/std/wql/codegen.logos#L729-L730`

### `el.op.compare` — `< <= > >=`

`<`→5, `<=`→6, `>`→7, `>=`→8 (`SBin`), boolean-typed; integer and float comparisons const-fold.

*Divergence:* CEL relational comparisons.

*Evidence:* `stdlib/std/wql/grammars/el.peg#L197-L204`; ids `stdlib/std/wql/el.logos#L26-L29`; emission `stdlib/std/wql/codegen.logos#L731-L734`

### `el.op.arith` — `+ - * / %`

`+`→9, `-`→10, `*`→11, `/`→12, `%`→13 (`SBin`); numeric-typed with the INT→FLT promotion rule (`el.type.int-float-promote`); integer arithmetic folds (÷/% by zero left unfolded), float arithmetic folds (`%` NOT folded).

*Divergence:* CEL arithmetic; `%` is integer/float modulo (float `%` is a valid operator but does not const-fold).

*Evidence:* `stdlib/std/wql/grammars/el.peg#L206-L217`; ids `stdlib/std/wql/el.logos#L30-L34`; emission `stdlib/std/wql/codegen.logos#L735-L739`; fold `stdlib/std/wql/optimize.logos#L113-L170`

### `el.op.unary` — `!` and unary `-`

`!x`→`SUn(OP_NOT=1)` (boolean), `-x`→`SUn(OP_NEG=2)` (preserves the operand's numeric type); `!boollit`/`-intlit`/`-floatlit` const-fold.

*Divergence:* CEL logical-not and numeric negation.

*Evidence:* `stdlib/std/wql/grammars/el.peg#L219-L222`; ids `stdlib/std/wql/el.logos#L36-L37`; emission `stdlib/std/wql/codegen.logos#L321-L327`

### `el.op.field` — postfix `.field` access

`base.field` (postfix, left-nested) builds an `SField` chain carrying each field NAME as a `str` (self-describing IR, Option B); a bare IDENT is a base-less `SField` (field-root), rebound to `SVar` at the metacall when it names a comprehension loop variable; a rel-bound var's field emits positional tuple access (`deem.datalog.rel-tuple-binding`).

*Divergence:* CEL field selection; EXPLICITLY no implicit projection (P2 rejects JMESPath-style implicit map projection) and no safe-navigation `?.` (D4 strict — optionality only via `Option`-typed fields).

*Evidence:* `stdlib/std/wql/grammars/el.peg#L224-L228,L246-L250`; `SField` `stdlib/std/wql/ir.logos#L135`; emission `stdlib/std/wql/codegen.logos#L264-L297`

### `el.primary.literals` — int / float / bool / string literals

Primary literals are integer (`SLit` int, token→i64 decode), float (`FLOAT = [0-9]+\.[0-9]+`, token→f64 decode, the FLT family), `true`/`false` (bool), and double-quoted string (interned `WString` ref); FLOAT is ordered before INTEGER so `2.5` never lexes as `2` + junk.

*Divergence:* CEL literals; the numeric split (int vs float by a literal `.`) is Rust/Logos-conformant.

*Evidence:* `stdlib/std/wql/grammars/el.peg#L163-L168,L241-L245`; `SLit` `stdlib/std/wql/ir.logos#L132`; literal-type inference `stdlib/std/wql/codegen.logos#L80-L85`

### `el.primary.param` — bound parameter `$name`

`$name` builds an `SParam` (a bound prepared-statement argument by NAME) in the EL grammar; on Deem SURFACE the `$` sigil is RETIRED — scalar params are referenced bare — but the `SParam`/`$` production remains in EL for the interpreter's prepared-argument path.

*Divergence:* CEL has no `$` param; this is a Deem/EL prepared-argument extension, retired on the deem surface (`deem.surface.scalar-param`).

*Evidence:* `stdlib/std/wql/grammars/el.peg#L246`; `SParam` `stdlib/std/wql/ir.logos#L133`

### `el.primary.call` — function/filter call

`ident(args)` builds an `SCall` carrying the call NAME + a materialized `SExprArr` argument list (up to 8 args, fan-out slots a0..a7); the name resolves against the builtin registry first, then the reflected UDF table.

*Divergence:* CEL function/method calls; D6 canon is Logos-style calls (`upper(x)` / `x.upper()`), the jinja pipe `|` is Trama-only sugar.

*Evidence:* `stdlib/std/wql/grammars/el.peg#L247,L252-L257`; `SCall`/`SExprArr` `stdlib/std/wql/ir.logos#L138,L58-L120`; emission `stdlib/std/wql/codegen.logos#L329,L365-L400`

### `el.primary.paren-tuple` — grouping vs tuple `(a,b,…)`

`(a, b, …)` with ≥1 top-level comma builds an `STuple` over an `SExprArr` (≥2 components); `(a)` (no comma) is plain grouping and passes the value through; the tuple alt is tried before grouping.

*Divergence:* EXTENSION — CEL has no tuple; the tuple projection is a Logos tuple (see `deem.project.tuple`), legal only in a `select` position.

*Evidence:* `stdlib/std/wql/grammars/el.peg#L238-L239,L259-L263`; `STuple` `stdlib/std/wql/ir.logos#L141`

### `el.comprehension` — `[expr for v in src if guard]`

`[head for v in src if guard?]` builds an `SComp{plan, head, var}`: the source ident builds an `RScan`, an optional `if guard` folds it into an `RFilter` (the plan is assembled by the grammar); it emits as a block yielding `Vec<HeadTy>` (a while-loop pushing `head` under the optional guard); `v` references parse as `SField`/`SCall` by name and rebind to `SVar` at the metacall.

*Divergence:* EXTENSION — Logos/Python comprehension syntax over CEL semantics (ADR 0012: "comprehension = the Datalog bridge", one comprehension = one rule) rather than CEL's `e.map(x,f)` macros.

*Evidence:* `stdlib/std/wql/grammars/el.peg#L265-L283`; `SComp` `stdlib/std/wql/ir.logos#L140`; emission `stdlib/std/wql/codegen.logos#L633-L688`

### `el.builtins` — len / upper / lower / contains / starts_with

The builtin functions are `len(x)`→INT (`(x).len()`), `upper(x)`/`lower(x)`→STR (owned `String` via `wql_upper`/`wql_lower`, ASCII byte-wise case folding), `contains(a,b)`→BOOL (`str_contains`), `starts_with(a,b)`→BOOL (`str_starts_with`); arities and return types are the registry (`builtin_arity`/`builtin_ret_ty`).

*Divergence:* a small CEL-canon + common-Trama-filter subset; string builtins are byte-oriented ASCII (MVP), not Unicode-aware.

*Evidence:* `stdlib/std/wql/el.logos#L39-L108` (registry + `wql_upper`/`wql_lower`); emission `stdlib/std/wql/codegen.logos#L365-L400`

### `el.type.lattice` — the EL_TY value-type lattice {INT,BOOL,STR,FLT}

Static codegen carries a coarse 4-valued type tag — `EL_TY_INT`(0)/`EL_TY_STR`(1)/`EL_TY_BOOL`(2)/`EL_TY_FLT`(3) — to route `push_str` vs `push_i64` vs the f64 format path and to type the row/projection element; a Logos type NAME maps via `el_ty_of_name` (`str`/`String`→STR, `bool`→BOOL, `f64`/`f32`→FLT, else INT — the integer family renders identically), the default being INT.

*Divergence:* a coarsening of the CEL type system to the four scalar families Deem emits; the whole integer family collapses to INT.

*Evidence:* `stdlib/std/wql/el.logos#L119-L143`; inference `stdlib/std/wql/codegen.logos#L78-L142`

### `el.type.int-float-promote` — INT→FLT promotion with explicit cast

In binary arithmetic where one operand is FLT and the other INT, the result type is FLT and the INT operand is wrapped in an explicit `((expr) as f64)` cast in the emitted source (Logos is Rust-like — no implicit int→float); narrower int UDF returns similarly get an explicit widening cast.

*Divergence:* EXTENSION over CEL's implicit numeric coercion — Deem emits the cast explicitly to satisfy Logos's Rust-style no-implicit-coercion rule.

*Evidence:* `stdlib/std/wql/codegen.logos#L103-L116,L303-L319`; `stdlib/std/wql/el.logos#L124-L130`

### `el.type.string-concat` — `+` on strings is concatenation

`a + b` where either operand is STR infers STR (concatenation); in a render context it flattens into successive `push_str` calls (no intermediate `String` temporary).

*Divergence:* EXTENSION — CEL supports string `+`; Deem emits it as Logos string concatenation / push-flattening.

*Evidence:* `stdlib/std/wql/codegen.logos#L111-L112,L775-L788`

### `el.type.returns-string` — owned String vs str-view

A call returning an owned `String` (the `upper`/`lower` builtins, or a UDF whose declared return is `String`) is tracked by `returns_string`; in a render/borrow context its result is `.as_str()`-borrowed, and a tuple column of such a call is typed `String` (not `str`).

*Divergence:* no analogue; a Logos ownership/borrow emission detail.

*Evidence:* `stdlib/std/wql/codegen.logos#L813-L822,L188-L206,L798-L801`

### `el.emit.chunk` — self-contained emission chunk

EL/Deem codegen emits into a chunk that is a SEPARATE AST doc carrying its own `use` list (string/vec/option/hashmap/set/btree), since a chunk does not inherit the trigger module's imports.

*Divergence:* no analogue; a metacall codegen detail.

*Evidence:* `stdlib/std/wql/codegen.logos#L42-L50` (`begin_chunk`)

### `el.restrict.f64-key` — f64 is not a hash/set key

f64 lacks Hash+Eq, so it cannot be a rel column, a `group by`/join hash key, or feed set-deduplication — such positions either take the LOOP join tier (dynamic) or are a named compile error (rel columns); f64 is fine as a scalar in arithmetic/projection/order-by.

*Divergence:* RESTRICTION — narrower than CEL/SQL where floats may appear anywhere; Deem excludes f64 from keyed/set positions because equality/hashing is unsound.

*Evidence:* `stdlib/std/wql/plan_walker.logos#L721-L741`; `stdlib/std/wql/el.logos#L182-L185`; ADR 0012-queue2 §4a (join keys / rel columns)

### `el.restrict.strict-optionality` — no `has()` / no `?.`

EL has no CEL `has()` macro and no safe-navigation `?.` — under the static/strict surface everything is mandatory by schema and optionality is expressed only via `Option`-typed schema fields (D4 strict); lenient `null` exists only for explicitly-erased dynamic bindings (`deem.exec.lenient-null`).

*Divergence:* RESTRICTION vs CEL (which has `has()` and dynamic missing-key `null`); Deem/EL makes the strict case total and pushes leniency into an opt-in dynamic mode.

*Evidence:* ADR 0012 D4 (§"Resolved open decisions"); ADR 0012-queue2 §4/§4a
