# ADR 0031 — The Deem core layer: one normalised clause form between the surface and the plan

Status: ACCEPTED 2026-10-06 (Victor: «Отлично. Заканчивай текущую работу и берись за ADR 0031. Потом — уже всё остальное будем делать с учётом ADR 0031»; §6 taken as recommended). Parent: [0024-deem-typed-plan-ir.md](0024-deem-typed-plan-ir.md).
Reference architecture: Soufflé 2.5 (`~/sandbox/souffle`, = `/usr/bin/souffle`).

## 0. Mandate

Victor, 2026-10-06: the main compiler had no HIR, so surface-syntax variety reached L-IR, mono and codegen, and bugs appeared on *combinations* of syntax; ADR 0030 is a month of repair for it. Deem must not repeat that. Soufflé was given as the oracle partly so its **architecture** could be read, not only its answers.

## 1. Measured state (2026-10-06, branch `deem`)

Census of `stdlib/mem/wql` (43.6 kLoC) — notes in the session scratchpad `arch/deem_census.md`.

- **No normalised core.** The surface IR (`RQuery`: `Simple` / `Join` / `Aggr` / `Find`, from `wql.peg`) is what fusion rename, gpath desugar, the Soufflé exporter, magic sets, liveness, fact desugar, both planners and the rel-body emitters consume. `RExpr` exists only for the ENTRY query, after planning, and its emitter recovers the four shapes from it.
- **110 match arms on the surface shape** in 35 functions (`lower` 18, `plan_walker` 9, `dl_export` 6, `rexpr_walk` 3); the grammar spells the 7-clause tail 6 times; the three shape schemas repeat 16 fields and name the base source two ways.
- **One clause, eight views:** `RQuery`, `RExpr`, `JChain` (3 constructors), `JCh`, `MtAtom`+conjuncts, `DxClause`, `NameScope`, `WardProg`; 8 functions each enumerate a clause's sources.
- **Duplicated by shape:** ≥ 9 row-loop emission sites; 3 aggregate folds (batch 873 lines, incremental 2 808 in one fn, recursive-aggregate); entry vs rel-body stamping drifted (the rel aggregate arm skips 4 checks the join arm runs); join reordering runs only for the entry join (`decide_join_orders` has one caller), never for aggregates over joins or rel bodies.
- **≈ 77 % of `rexpr_walk` (13.4 kLoC) is per-shape emission.**
- **Combination bugs on this branch**, 9 of 13 impossible with one core form: aggregate × join (overflow check in one copy only, c9e3bb8ff), row var × aggregate × order, `group by` str key in both emitters (93db5572b), pushdown skipped for join/aggr (c76442b3a), anti/later-step var scope (77877e235, 64a59d720), demand by shape (d0977da27), traversal × demand (f0c1c2990), incremental × rel source answering wrong `Ok` (bce805218), aggregate × join plan trace (114d64475).
- **A core already exists implicitly:** `dl_export` maps every shape except `find` to positive atoms, negated atoms, constraints and a head (each aggregate a rule over an intermediate relation). What stays outside Datalog is an output envelope only the entry has: first/find/order/limit/distinct/result type.

## 2. What Soufflé does (source-cited in scratchpad `arch/souffle.md`)

1. Purely syntactic sugar dies in the PARSER (DNF bodies, multi-head split).
2. One AST pass per removed variety, composed in one place (`MainDriver.cpp`); after every pass `exitIfErrors`.
3. Before lowering the AST is in a canonical core: one head; body = `Atom` / `Negation(Atom)` / binary constraint; no functors inside atoms; every variable grounded; aggregate bodies exactly one atom (ResolveAliases ×4, NormaliseGenerators, MaterializeAggregationQueries, AddNullaries, RemoveBooleanConstraints, FoldAnonymousRecords).
4. Checks (types, groundedness, stratification) run on near-source AST, BEFORE passes invent synthetic relations.
5. Lowering (`ast2ram`, 2.6 kLoC semi-naive) emits ONE verbose form — nested Scan / Filter / Aggregate / Insert; all optimisation (index selection, filter hoisting, if-conversion) is RAM → RAM.
6. Evaluation strategy is a translator FAMILY (`TranslationStrategy`: semi-naive, provenance), not flags.
7. RAM (~60 node kinds) has no AST dependency; interpreter and synthesiser include no AST header.
8. Every stage is dumpable and every pass switchable.

**Its measured weakness, which this ADR must not copy:** the core is a convention over the SAME AST type, with no shape check before lowering, and the lowering's visitor has a default case. Disabling `RemoveBooleanConstraints` makes `a(x) :- b(x), false.` answer `{1,2}` with exit 0.

## 3. Decision

Deem gets four layers with three hard boundaries:

```
 surface            core (the Deem HIR)          plan                 Logos
 RQuery (wql.peg) ─▶ Core Program ─(core→core)─▶ DPlan ─(dplan→dplan)─▶ emitted fns
        desugar+lower        checks, magic,       lower by       one emitter per
        (once, early)        liveness, types      STRATEGY       DPlan op kind
```

### 3.1 Core — a DISTINCT type, not a convention

- **Representation: Logos types (enums/structs), not a Writ schema** — so the COMPILER checks exhaustiveness of every match over core nodes (the "no default case" that Soufflé lacks comes for free, and a new node kind reds every consumer at build time). Writ stays the surface carrier only.
- **Shape** (the union of the three shape schemas, which `dl_export` shows is enough):
  - `Program { rels: Vec<Rel>, entry: Entry, params, natives, consts }`
  - `Rel { name, cols: Vec<(name, Type)>, clauses: Vec<Clause> }`
  - `Clause { head: Vec<Expr>, body: Vec<Literal>, group: Option<Group> }`
  - `Literal = Atom { src: SrcRef, var, mode: Pos | Neg } | Traverse { parent_var, field, var, mode } | Cond(Expr) | Bind(var, Expr)`
  - `SrcRef = Rel(id) | Param(id) | Native(id, rel) | Unit`
  - `Group { keys: Vec<Expr>, aggs: Vec<Agg>, having: Option<Expr> }`
  - `Entry { rel: RelId, env: Envelope }`, `Envelope { mode: All | First | Find, distinct, order: Vec<(Expr, dir)>, limit: Option<Expr>, ty }` — the entry IS an ordinary rel plus the output envelope; nothing else is entry-only.
  - every node carries its source span and its provenance (query / mapping M), ADR 0024 S0.
- **Invariants** (stated in one place, enforced by `core_verify`): every variable bound by exactly one literal before use, names renamed apart per clause; atoms read sources only (no expressions inside); one group per clause, aggregates only in `group`; no surface sugar (gpath, FROM-less, facts, fusion renames) survives.
- **`core_verify` runs after lowering and after EVERY core → core pass; a violation is an ICE** (loud, the compiler stops) — never a refusal and never a silent skip.

### 3.2 Desugar + lower: once, early, for entry and rel bodies alike

The existing surface → surface passes (`desugar_program_gpaths`, FROM-less → `__unit`, `desugar_program_facts`) run first, then ONE `lower_program : RQProgram → Core`. Mapping fusion becomes a node operation (splice core rels, rename by id) instead of a text splice plus a string rename spec.

### 3.3 Checks and rewrites read and write Core only

Scope (`NameScope`), wardedness, stratification, the typed-column admission and expression typing (ADR 0024 S2/S3) read Core; magic sets / SIPS, liveness, rel inlining and constant folding are Core → Core and re-verify. `MtAtom`, `DxClause`, `NameScope`'s private walks, `WardProg`'s clause walk and `JCh` disappear into Core.

### 3.4 DPlan — a RAM-like plan IR; strategies translate, passes optimise

- **Operations:** `Scan(src, landing)`, `Traverse`, `Probe(build side, strategy hash | tree | loop)`, `Filter`, `Aggregate`, `Project/Insert`, `Exists` (anti); **statements:** `Seq`, `Loop`, `Exit`, `Merge`, `Swap`, `Clear` (semi-naive), materialisation nodes (`Drain` / `Sort` / `Arrange`, ADR 0025 S2) and the envelope ops (`Sort`, `Limit`, `Distinct`, `First`).
- **Translators = a strategy family** (Soufflé §2.6): one-shot, semi-naive, DRed, incremental — each Core → DPlan; none is a branch inside an emitter.
- **Planning decisions live on DPlan** (access path / landing, join order, join strategy, materialise-vs-stream), each with its justification (ADR 0024 S4). Join reordering then applies to rel bodies and aggregates over joins, not only to the entry.

### 3.5 Emitters consume DPlan only

One emitter per DPlan kind: one scan (all landings, both layouts), one probe, one aggregate fold, one envelope. `RExpr`, `JChain`, `emit_simple` / `emit_join_chain` / `emit_aggregate` / `emit_find` and the rel-body shape arms are deleted. The Soufflé exporter reads **Core** (it is the Datalog mapping) and so checks the planner + emitter, not the lowering.

### 3.6 Enforcement — the boundaries are gates, not prose

1. **Layer lint:** modules below the core boundary may not name a surface node (`RQuery`, `RQSimple`, `RQJoin`, `RQAggr`, `RQFind`, their field codes); emitters may not name Core nodes. (Soufflé's "no AST include in backends", made a gate.)
2. `core_verify` (and `dplan_verify`) between passes — ICE.
3. Dumps: `LOGOS_DEEM_DUMP=core|dplan` prints each layer (Soufflé `--show`), so a fixture can pin a layer, not only a result.
4. The existing oracle (Soufflé, now on Core) and census gates stay as acceptance.

## 4. Rows (each leaves the tree green; the old path is DELETED in the row that replaces it, after a census of every corpus)

**Order revised 2026-10-06 (Victor: «переставь порядок, R5/R6 вперёд»).** A Core → Core rewrite (magic sets, liveness, typing) has no consumer while the walker and the emitters read the surface: it would have to be raised back into the surface or done twice. So the plan IR and the emitters move onto Core first, and the rewrites follow onto a core that something already reads.

| Row | Content | Acceptance |
|---|---|---|
| R0 | Core types + `core_verify` + dump; `lower_program` for all four shapes and rel bodies; nothing consumes it yet | every corpus query lowers and verifies (gate over the pass corpus) |
| R1 | `dl_export` reads Core | byte-identical Datalog on the oracle corpus, then delete the surface-reading exporter |
| R2 | scope / rule-shape / wardedness checks on Core, run on the program as written (before any rewrite) | same diagnostics corpus-wide; the rel-aggregate check gap (§1) closes by construction |
| R5 | DPlan + one-shot translator; planners on DPlan | plan traces and census pins hold; join order now chosen for rel bodies and aggregates |
| R6 | emitters from DPlan; delete per-shape emitters, `RExpr`, `JChain`, `JCh` | full L0 + the oracle; `rexpr_walk` shrinks by its per-shape share |
| R3 | magic sets, liveness, fact desugar, fusion as Core → Core | `wql_rel_sips*`, demand fixtures, census pins hold |
| R4 | typed columns + expression types on Core (ADR 0024 S2 remainder, S3) | `u64` / user-type rel columns admitted by trait; Deem-side diagnostics at positions |
| R7 | semi-naive, DRed, incremental as translators | incremental fixtures + the diff harness |
| R8 | layer lint on; surface types confined to parse + lower | the lint, with a planted-violation canary |

### 4.1 R5/R6 broken down (from a measured map of the planner and the emitters, 2026-10-06)

Three facts set the start: (1) the R0 core is built before the demand rewrite and the fact desugar, and the walker then renames sources and stamps row types onto the surface — so until R3 the plan reads a SECOND, "planned" core lowered after those rewrites and before the walker, with a resolver binding each atom to a source id and a row type; (2) join ORDER is decided inside `emit_join_chain` for the entry join only — rel bodies and aggregates over joins get no order and no strategy; (3) eight decisions exist only at emission time (join order, NDV facts, the peepholes, group-frame purity — computed twice —, arrange / key-vector / group-frame nodes, incremental / retraction / DRed choices, direct-door eligibility, prepared-plan grounds). DPlan needs, beyond the list in §3.4, a `Choose` node (up to four candidate nests, the discriminant fixed / from the prepared plan / deferred to run, with the cost table) and the statements `Call`, `Guard`, `Latch`/`Compact`, `Txn`.

**R4.0 before R5.2 (measured 2026-10-07).** Join order cannot move into the planner by moving the decision after the walker's stamping: `plan_prove_once` needs the strategies in its FIRST pass, before stamping, and the planner already types from declarations (`plan_stamp_src`: param types, rel columns) while the emitter types from the stamped surface. Two typings of one atom is the defect class this ADR exists to remove; so the declaration-level part of R4 — every atom's row type and columns, computed once on Core — comes first, and R5.2 reads it.

Acceptance instrument for every step: the corpus's `--gen-dir` units and `LOGOS_TRACE_PLAN=facts` traces (`build/tests/logos/facts/*/{gen,plan.err}`), snapshotted before and after and compared per fixture by item (all deems, not only `wql_`/`deem_`: 26 of the 40 direct doors live in `memoria_*`/`container_item_*`).

| Step | What | Acceptance |
|---|---|---|
| R5.0 | DPlan types, `dplan_verify` (ICE), `LOGOS_DEEM_DUMP=dplan`, the planned core + resolver, a translator for the entry's simple scan, in SHADOW mode (ICE when DPlan disagrees with what the emitter reads) | hand-written golden; snapshot unchanged |
| R5.1 | planners write DPlan; the old fields become one-way projections of it; the IR is no longer mutated | gen + traces byte-identical; census pins hold |
| R4.0 | atom typing on Core from declarations (a source's row type, rel columns, scalar params, by-value step types), once, right after `core_build`, read by planner and emitter alike; the emitter's per-shape typing first becomes one function (`wql.typing::type_chain`) | gen + traces byte-identical |
| R5.2 | join order moves from the emitter into the planner (entry) | gen identical; traces equal as a per-fixture multiset |
| R5.3 | join order + strategy for rel bodies and aggregates over joins (a BEHAVIOUR change, own commit) | oracle + L0; pins re-derived by hand |
| R6.1–R6.5 | emitters from DPlan: door + prepared surface, scans + envelope, find, joins (`Probe`/`Arrange`/`Choose`), aggregates | byte snapshot per step; the step's named gates |
| R6.6 | landings and rel / SCC bodies from DPlan (driver skeletons wait for R7) | fixpoint census pins, `incr_*` gates |
| R6.7 | delete `RExpr`, `JChain`, `JCh`, `simplify_rexpr_ref`, `set_occ_src` | census of every corpus |

## 5. Relation to other ADRs

- ADR 0024: S0 (positions) feeds Core spans; S2/S3 land ON Core (R4) instead of on the surface; S4's "plan as data" becomes DPlan; S5 ("emitters read the IR") is R6.
- ADR 0025: batch/cursor plane and materialisation nodes become DPlan operations; scan emission collapses to one site.
- ADR 0016: fusion becomes a Core operation (R3).
- ADR 0030: same lesson in the host compiler; Deem's Core is its HIR.

## 6. Decisions (the draft's open questions, taken as recommended)

1. The core is LOGOS TYPES (enums / structs) — exhaustiveness of every match is checked by the compiler. Scalar expressions stay `SExpr` handles (already one algebra for every shape) until a row needs otherwise.
2. The entry is an ordinary clause plus the output ENVELOPE (`CEnvelope`: mode, distinct, order, limit, result type) — nothing else is entry-only.
3. Typed columns and expression types (ADR 0024 S2 remainder, S3) land ON Core, as R4 — after R0–R3.
4. Incremental (R7) stays in this arc, after R6.
5. Surface feature freeze during R0–R6: a new construct lands only as a lowering into Core.

**R0's verifier is STRUCTURAL.** Lowering runs before the walker, which is what diagnoses a user's scope errors; so in R0 `core_verify` checks only what the lowering itself guarantees (a clause's body opens with a positive atom, literals are well-formed, rel indices resolve, the envelope is sane). The binding invariants (every variable bound before use, renamed apart) join it in R2, when the scope checks move onto Core — a user error must never surface as an ICE.

## 7. Progress

- **R0 — 43c6a88da.** `logos.std.wql.core`; structural `core_verify` (ICE); `LOGOS_DEEM_DUMP=core`; gate `logos_09_core_dump` against a hand-written golden of 8 shapes.
- **R2 — checks on Core.** `logos.std.wql.check`: the scope checks (`core_scope_ok`, moved from the walker's `query_names_ok`, same clause order and texts) and the rule-shape checks (`core_rules_ok`: a rel body's default envelope, a rel aggregate's single aggregate, unique row vars) run on the program as written, before any rewrite. Closed a live gap: an aggregating rel body's `order by` / `limit` / `select first` was silently dropped (fail/wql_rel_aggr_order_fail). Conditions carry their site (`CCond { e, site: Where | On }`). Wardedness and the element-type checks move with typing (R4).
- **R1 — the oracle reads Core.** `dl_export` renders rules from `CRule`s (`dx_rule`, `dx_core_body`, `dx_aggr_rule`, the entry helpers); the surface-reading clause/join/aggregate/entry functions are deleted, and the recursive-aggregate test is `core_rule_reaches`. Acceptance: the exported Datalog and the skip reasons of all 223 oracle fixtures are byte-identical to the pre-R1 export (the one difference is `wql_rel_fact_e2e`, which the R0 verifier had made an ICE before the fix and so had no baseline).
- **R5.0 — c4949fbb1.** `logos.std.wql.dplan`: the entry scan's arm (`DScanArm`, 7 shapes) is a plan value the scan emitter matches on.
- **R5.1 — 2b990ee92.** The plan no longer rewrites the query (`plan_where_retired` instead of `has_where = false` on the parse node); the group-frame class is one decision (`plan_group_pure`), its two derivations deleted.
- **Planned core — 8c39dc741.** `MacroParams.plan_core`: the program as emitted (after every rewrite and the walker's stamping), lowered again, each atom resolved against the registry (`CAtom.reg`, `name`).
- **R6.2 — 30cb0da52.** `Simple` / `find` entries from the plan (`plan_entry_simple` → `DSimple`, `DForm` Empty | Identity | Scan).
- **R6.4 — a4b162195.** Join-chain entries from the plan (`plan_entry_chain`, `jchain_from_core`). A constant-empty join's prepared-plan axis was lost by the first cut; no corpus query exercised it — fixture wql_empty_axis_e2e does.
- **R6.5 — 2282f21fd.** Group entries from the plan (`plan_entry_group`; the `where` under a group is not folded, the `having` is). The relational entry path (`lower_rquery_to_rexpr`, `emit_rexpr`, the relational simplifier) is deleted; its rules live once in `dp_fold_entry`. Found: #737 (`where … select <row var>` is a type error in generated code).
- **R6.6.** Rel bodies from the planned core's rules (`rel_body_frag(ri, b, overrides)`); the semi-naive delta variants are an override vector per chain slot, not a parse node rewritten and restored (`set_occ_src`, deleted, with the two identical surface chain builders). A rel body the rewrites left join-shaped with ZERO steps now takes the single-source fragment — the same loop, another index name — in 4 fixtures (the intended normalisation). Closed: an aggregating rel body's `having` was silently not applied (fail/wql_rel_aggr_having_fail).
- **R4.0 step 1 — a2b7170d7.** The emitter's seven copies of clause typing (schema, rel-source columns, by-value step types, scalar params, comprehension vars, UDFs) are one function, `wql.typing::type_chain`; snapshot byte-identical.

