# ADR 0031 — The Deem core layer: one normalised clause form between the surface and the plan

Status: IMPLEMENTED 2026-10-09 — every row R0–R8 closed (§7). ACCEPTED 2026-10-06 (Victor: «Отлично. Заканчивай текущую работу и берись за ADR 0031. Потом — уже всё остальное будем делать с учётом ADR 0031»; §6 taken as recommended). Parent: [0024-deem-typed-plan-ir.md](0024-deem-typed-plan-ir.md).
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

### 4.2 R3 broken down (2026-10-07)

The pipeline today (`wql.logos`): mapping fusion (renames on the parse tree) → graph-path desugar → **the R0 core** + the R2 checks → the oracle export → demand (magic sets, `lower::magic_program`) → live rels (`live_rel_mask`) → fact desugar → the walker, which registers every rel, records the dependency edges and checks every body **on the parse tree**, and lowers the planned core from it. A rewrite moved onto the core needs a walker that reads the core, because the rewrites ADD rels (magic rels, adorned copies, `__unit`) that the registry must hold.

| Step | What | Acceptance |
|---|---|---|
| R3.0 | the walker on the core: registration (names, columns, natives after), dependency edges, the per-body checks (sources, traversals, scalar-element fields, whole-row selects, bare-ident classification) — the planned core lowered once, before the walker, from the rewritten surface | same diagnostics corpus-wide; gen + traces byte-identical |
| R3.1 | fact desugar Core → Core | SHADOW first: the core rewrite against the lowering of the rewritten surface, compared by `core_text`, ICE on a difference; then the surface pass is deleted |
| R3.2 | live rels Core → Core | same |
| R3.3 | demand (magic sets / SIPS) Core → Core | same; `wql_rel_sips*`, demand fixtures, census pins |
| R3.4 | graph paths lowered straight into the core (no surface desugar) | same |
| R3.5 | mapping fusion on the core | same |

### 4.3 R4 broken down (2026-10-08; Victor: «бери R4 как в Rust»)

What R4.0 left: atoms are typed on Core, expressions are not. The expression checks (`codegen::check_calls` and its family) run inside the emitters, per emitted clause (22 call sites), and report without a position; a column type is admitted by membership in the EL lattice and the select-against-column check compares four-value CLASSES (an unknown name is "INT"). Measured on the R4 start: a `u64` rel column was refused ("the EL computes in another type"), a user type with `Eq + Hash` was refused ("outside the lattice"), and `where r.name == 5` reached the host compiler (`<metaprog-blob-subst>:1: operator '==': type mismatch (&[u8] vs i64)`). Rust is the reference for every rule below.

| Step | What | Acceptance |
|---|---|---|
| R4.1 | a rel column is admitted by `Eq + Hash` (a `HashSet` key), asked of the language; the select component is checked against the column's TYPE (literal inference, the A18 widening, `String` through its view); an unsuffixed literal in a rel row is emitted in its column's type; the lattice predicate `el_set_col_admit` deleted | u64 / u128 / user-type fixtures with values that differ between u64 and its i64 image; refusals name the missing trait |
| R4.2 | the expression checks run once on Core, before planning (`typing::core_typecheck`), and the emitters' call sites are deleted | same diagnostics corpus-wide; gen + traces byte-identical |
| R4.3 | every type diagnostic located at its node (`SExpr.soff` through the query source), never at the host's generated line | fail fixtures carry `file:line:col` |
| R4.4 | operand typing is unification over Logos types (one type per operator, literal inference, `bool` conditions), replacing the class-based agreement test; literals up to `u64::MAX` | `r.name == 5` refused by Deem; every operator × type pair the corpus has |
| R4.5 | every name typed (consts by declaration, imported row types by the compiler, whole-row vars); the emitters read the checker's environment; the `i64` baseline is a traced fallback held at zero by a gate | census of the baseline's hits = 0 (`logos_09_ty_default`) |

### 4.4 R7.4 broken down (2026-10-09)

The incremental entry's admission (`incr_eligible`: 24 decline grounds) and its retraction surface (`retract_ok`) are decided INSIDE the group emitter, from values only the emitter computed: the select's element type (`tuple_result` / `comp_head_ty` → `result_ty`, `tres`), the key's emitted type, the aggregates' accumulators and finalized columns. R4 moved the last two onto Core (`typing::type_aggs`, R4.2a); the rest follows, so that "does this query get a handle, and why not" is a plan fact with its ground, not an emission side effect.

| Step | What | Acceptance |
|---|---|---|
| R7.4a | the entry's result type on Core (`typing::entry_result_ty`: scalar, tuple, comprehension, owned `String`, an annotated `result_ty`), read by every entry emitter (from `tuple_result`, `comp_head_ty`) | gen + traces byte-identical |
| R7.4b | `dplan::plan_incremental`: admission + retraction surface decided by the planner from the planned core, the `GroupTy`, the key type and the result type, recorded with its ground (`DIncr { admit, retract, rel_m, why }`), traced once; `emit_incremental` reads it | gen + traces byte-identical; `incr_eligibility_gate`, every `wql_incr_*` fixture |
| R7.4c | the handle's state (accumulators, rel-backed totals) described by the plan, the emitter spelling it | same |

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
- **R4.0 steps 2–4 — b3e1fc1d3, 16eb17acf, 8a5c94e22.** `typing::core_resolve` resolves and types every atom and traversal from the declarations once the registry is complete (shadow-checked against the walker's stamping first: zero disagreements corpus-wide). The planned core IS that core: the planner types the entry with `type_clause` and records each step's strategy on the atom (`RQJoinStep` loses keys 9–12), `plan_prove_once` reads them there, and `core_bind_sources` binds the plan's spellings after the access plan (a streamed native rel is `__rel_<r>`, a plan fact, not a declaration). The walker no longer writes the parse tree; it resolves sources only to diagnose and record dependency edges. The wardedness verdict reads the core (one `ward_rule` per `CRule`). Snapshot byte-identical at every step.
- **R5.2 — cf77eea1b.** The entry's join order is the planner's: `join_order::plan_join_order` decides it from the planned core (`jch_from_core`, `type_clause`, the sort key typed once) and hands the emitter a `JoinPlan`; the emitter's `jch_of` and its second derivation of the key's types are deleted. Gen and traces byte-identical.
- **R5.3a — b2e592fd9.** Every step's strategy is the planner's, once per rule (`plan_decide_rest`); the emitter re-decided each rel body's steps once per semi-naïve VARIANT and now ICEs on an undecided step. The entry's after-traversal steps are decided after its order, so C2 still reads pass 1. Gen identical; 241 duplicate decision lines leave the traces (census `hash join` 866 → 647, derived by hand).
- **R5.3 — ORDER for rel bodies and aggregates: decided NOT to reorder here (2026-10-07).** (1) Rel bodies: a body's step indexes are rebuilt every fixpoint round (the variant fragment builds `__hm` over its sources each time it runs), so moving the delta occurrence first trades a scan of a total for an index build over the same total — O(|T|) per round either way; delta-first pays only when a total's index persists across rounds, which is R7's (semi-naïve drivers as translators). The row moves there. (2) Aggregates over joins: a reorder is not answer-preserving — groups come out in first-seen key order (C1 without a sort), an f64 sum's value depends on the summation order, and a checked integer sum can overflow mid-way in one order and not in another (`Err` vs `Ok`). No reorder is admissible without a licence these three do not have; the derivation is not extended to them. R5 is closed by R5.0–R5.3a.
- **R6.1 — 08c2612bc, 91d4bc611.** The door and prepared emitters already read only what their callers hand them. What still read the parse tree past the walker's checks moved onto the core: the entry dispatch (`walk_query`: shape from the rule, `select first` / `distinct` / `limit` / `result_ty` from the `CEnvelope`), the rel emitters' rule counts, the access decision (`plan_decide_access`: base atom by registry index, the core's `where`) and the read-once proof (`plan_prove_once`). `rexpr_walk` reads no parse type; the planner's pass 1 does not read `src_root`. Byte-identical.
- **R6.7a — 2f6245a8a.** The emitter's `JChain` is the plan's `dplan::DNest`, built from a core clause by `dplan::plan_nest`; every nest emitter consumes it. Byte-identical.
- **R6.7b — 23bf4fa92.** One probe emitter: a nest's steps are analysed into one `NestSteps` record, wrapped by `nest_wrap` and built by `nest_builds`, for all three nest emitters (entry join, aggregate over a join, rel body), each calling them in its former order. Byte-identical.
- **R6.7c — 3fb805854.** The dead relational algebra is deleted: nothing built `RJoin` / `RAnti` / `REdge` / `RAggr` / `RFix` / `RProj` / `RSort` / `RLimit` / `RDistinct` since R6.5; `RExpr` is `Scan | Filter`, a comprehension's source and filter. −283 lines, byte-identical.
- **R6.8 — f71492bc8.** One scan emitter per source shape for every scan position: a declared slice param (`slice_scan`: the R-A wrap + the batch pull, was nine copies), a row-at-a-time iterator (`pull_scan`, was three), a batch producer (`batch_scan_frag`, already one); each site keeps its composition (wrap placement, ordinal, trace ground). The envelope was already one set of emitters (`push_guarded_frag`, `sort_perm_frag`, `limit_expr`, `key_norm_body`). The entry shapes' composition functions remain: they differ by what they compute (a sort collection, a group fold, a rel insert), not by copies of one thing. Byte-identical.
- **R3.0 — e22bc231a.** The walker reads the core: registration, dependency edges and every body check (same diagnostics, same order); it reads the parse tree only for "is there an entry".
- **R3.1 — 16877e3ea.** Facts on the core (`crewrite::core_desugar_facts`); the handler hands the walker the core. `core_same` / `crewrite_shadow` — the field-by-field core comparison every R3 move is shadowed with.
- **R3.2 — aba5f1e67.** Live rels on the core (`core_live_rels`).
- **R3.3 — bea7992a1.** Demand (MST by SIPS) on the core (`core_magic`), two-phase (immutable analysis, one write phase, `core_relink`); the surface MST (~800 lines) deleted. Shadow clean; a control (copies left unrestricted) ICEs on wql_rel_sips_e2e.
- **R3.4 — c9819f966.** Graph paths lowered on the core (`CClause.gpath` carried by the lowering, `core_desugar_gpaths` lowers it, `core_verify` refuses a leftover); the program is lowered ONCE (the R0 core is the planned program). Shadow clean; control (anchor constant changed) ICEs on wql_gpath_e2e. The switch build caught the walker emitting rel fns only when the PARSE TREE had rels.
- **R3.5 — 6eeed36f2.** Mapping fusion on the core (`core_fuse`). **R3 closed**: every rewrite of the program is Core → Core; lower.logos 2042 → 101 lines.
- Each R3 step: shadow over build + L0 first (R3.5 excepted: a single rename, its oracle the 10 mapping fixtures in the snapshot), then the switch; gen + traces byte-identical.
- **R8 — b8c752f73.** `logos_00_deem_layer_lint`: no `RQ*` (surface) type in the code of any Deem module but the schemas, the generated parser, the lowering, the handler and the position resolver; a planted canary proves it fires. Graph paths ride the core as `Vec<CPathStep>`; the walker no longer views the parse tree.
- **R7.1 — 59c33b345.** An SCC's fixpoint is a plan (`dplan::DScc`: per member its rules, the seeds, and one `DRun` per in-SCC occurrence of every recursive rule — the rule and its per-slot delta/total renames); the semi-naïve (`_scc`, `_scc_i`) and DRed (`_od`, `_odp`) drivers spell it. Byte-identical.
- **R7.2 — 684bf27b2.** The index over a source a fixpoint's loop does not change (a parameter, a rel outside the SCC; `DRun.inv`) is built once, before the loop, not every round. Single-source reachability over N edges: 1350 ms → 1 ms at N = 4000, linear to 256k. Answers and traces unchanged; gate `logos_09_scc_index_hoist` (structural) + fixture.
- **R7.3 — ec581d92a.** A fixpoint variant drives from its DELTA (`dplan::delta_first`): the delta atom first, the rest in written order, each `on` re-attached to the last source it reads; admissible only if every positive join step keeps a linking condition (else the written order stays — 3 variants in the corpus). The planner plans the SCCs before emission (`plan_sccs`) and decides the reordered variants' steps with the rule's own decision (`plan_decide_clause`, moved from the walker). `from edges e join reach r …`: 133 ms → 1 ms at 4000 edges, linear. Answers unchanged; census `hash join` decisions +69 (derived per fixture). `LOGOS_DEEM_NO_HOIST` / `LOGOS_DEEM_NO_DELTA_FIRST` switch R7.2 / R7.3 off for measuring.
- **R7.2b — 55c427bbd.** DRed's propagation driver (`_odp`) builds its loop-invariant indexes once, before its loop, like the semi-naive drivers. Answers and traces unchanged (26 DRed units differ by the moved builds only).
- **R4.1.** A rel column is admitted by `Eq + Hash`, asked of the language (`plan_walker::rel_col_ty_ok`); `el_set_col_admit` and its five verdict codes are deleted. The select component is checked against the column's type (`codegen::expr_reaches_ty`: one type, an unsuffixed literal of the column's kind, the A18 widening, `String` through its view), and an unsuffixed literal in a rel row is emitted in its column's type (`rel_row_value`) and must fit it. New: u64 / u128 / user-type / literal-typed columns (values above 2^63 answer as u64); `fail/wql_rel_col_noinj_fail` became `wql_rel_col_u128_e2e`. Every other fixture's gen and trace byte-identical; census pins re-derived from the four new fixtures.
- **R4.2a — 4d20a28ff.** A group's output types (float or not, an integer argument's cast, a user aggregate's resolution, the accumulator, the finalized column, each output name's type) are `typing::type_aggs` over the core's `CAgg`s; the aggregate ids move to `el`.
- **R4.2b.** `logos.std.wql.typecheck::core_typecheck`: every rule's expressions are checked once on the core, after the walker's scope and source checks and before the plan is finished — the call and operator checks, tuple refusals, `Copy` projections, a rel row against its columns (`rel_select_ok`, moved), a group key's identity (`group_key_ok`, moved out of the emitter), every aggregate's argument against its accumulator, `key_norm_ok` (moved). The emitters' 22 check sites are deleted; a rel body was checked once per semi-naive variant, an entry's retired `where` and a constant-empty query's clauses not at all. Same diagnostics corpus-wide, except `fail/wql_rel_col_wide_int_fail`, whose `u64 + i64` argument is now refused first as mixed-sign arithmetic (as in Rust) — its argument was made well-typed so it still pins the value column's order check. Gen + traces byte-identical (but for the chunk's import of the new module).
- **R4.3.** Every type diagnostic is located at its node in the query (`file:line:col`), never at a generated line. The query as a source is plain data (`el::QSrc`, built by `srcloc::query_src`); the type environment carries it (`ElTypes.src`) and the checks report through `codegen::ty_error`. Found and fixed on the way: an EMBEDDED parse (an EL expression inside the query) stamped `soff` relative to its own sub-range, in both generated parsers — so every offset was expression-relative; `peg_gen_cpp` now hands the sub-parser its base (`src_base_`), `peg_gen_logos` a `parse_<rule>_at(src, doc, base)`. Fixture `fail/wql_type_error_located_fail`. Gen + traces byte-identical.
- **R4.4.** Operand typing is one rule for every class, as in Rust: two values of known different classes (`str` vs an integer, a `bool` vs an integer) and a literal of another class (`r.name == 5`, which reached the host compiler) are refused at their place; `str` and `String` are one class. A name the environment never recorded (an imported const, a native source's untyped field) is NOT the dictionary's `i64` baseline but unknown — no claim is made about it (R4.5 types those names). An integer literal above `i64::MAX` is the value of the unsigned 64/128-bit type beside it (a comparison, a rel row): its value is read off its spelling (`SLit.txt`), in the emitter and in the Datalog export — the Soufflé oracle caught the export printing the lexer's poison. Fixtures `wql_wide_lit_e2e`, `fail/wql_str_int_compare_fail`, `fail/wql_wide_lit_signed_fail`.
- **R4.5a.** Measured: the `i64` baseline of `ElTypes::ty_name_of` answered 25 times over the Deem corpus (13 distinct names) — imported struct fields (the handler's view is one module), consts, row variables read whole, and a constant-empty group query's outputs. The checker now types every free name a rule reads (`typing::stamp_free_names`): a row variable read whole is its row type (a whole-row `select` is returned, never copied, so it asks for no `Copy`); a const or static is its DECLARED type (`value_type_name`, which reports `str` lowered as `&[u8]` — mapped back). Fixtures `wql_const_typed_e2e`, `fail/wql_const_type_mismatch_fail`. Left for R4.5b/c: the emitters still type through their own environments (they should read the checker's), and an imported struct's fields need a compiler query (requested: `field_type_name`).
- **R4.5c.** A row type declared in ANOTHER module (`logos.mem.deem::DynEdge`) is typed by asking the compiler for its fields (`struct_field_count` / `struct_field_name` / `field_type_name`, main c905dc453): the handler's module view could not see it and every such field read as the `i64` baseline (13 of the 25 measured hits). And a `deem` over a generated container family is emitted into the package it was DECLARED in (`__deem_bind`'s `decl_pkg`), not the family's `logos.gen` — its caller saw it only through the compiler's empty→all fallback (Q1 4d census). Gen differs only there: `package test;` + `use logos.gen;` in three Memoria fixtures.
- **R4.5b.** One type environment per rule: the checker computes it (`typecheck::rule_type_env`: the clause's sources, the query, every free name, the entry head's comprehension variables) and records it on the walker's context (`MacroParams.plan_tys`, rel `r`'s rule `b` at `plan_ty_off[r] + b`, the entry last); the seven emitters that built their own (`type_chain`) start from a copy (`entry_env` / `rule_env`), the group emitter adding its key and outputs on top. Gen + traces byte-identical.
- **R4.5d — R4 closed.** The last measured hit (a constant-empty GROUP query, whose emitter typed no `key` / aggregate outputs) is typed (`typing::type_group_outputs`); re-measured over every pass AND fail fixture that uses Deem: 0 on well-formed programs. The baseline stays only as a fallback after a user error already reported (a refused tuple column leaves its name untyped — so it is not an ICE, which would mask that diagnostic): under `LOGOS_TRACE_PLAN` it prints `[plan] ty-default <name>`, and gate `logos_09_ty_default` holds the count at 0 over the pass corpus's traces, with a fail fixture as its live canary. `type_chain` / `stamp_rel_source` deleted. Gen + traces byte-identical.
- **R4 summary.** A rel column is any `Eq + Hash` type, typed as declared (R4.1); every expression is type-checked once, on the core, before planning (R4.2), with each diagnostic at its `file:line:col` in the query (R4.3); operands follow one Rust rule across every class, with literals inferred from their context, including above `i64::MAX` (R4.4); every name an expression reads has its own type — consts by declaration, imported structs by the compiler's field queries, whole-row variables by their row type — and the emitters read the checker's environment (R4.5).
- **R7.4a.** The entry's result element type is one function, `typing::entry_result_ty` (scalar, tuple, comprehension `Vec<…>`, owned `String`, an annotation), read by the four entry emitters (scan, constant-empty, join, group), whose own copies — the join and group ones without a comprehension arm — are deleted with `tuple_result` / `comp_head_ty` / `is_comp_root`; the tuple-shape refusals they made are the checker's (`typecheck::tuple_shape_ok`, located). Gen + traces byte-identical.
- **R7.4b.** Whether a group entry gets an incremental handle, whether it retracts, and whether it is DRed-maintained are the PLANNER's (`dplan::plan_incremental` → `DIncr`), decided from the planned core, the clause's `GroupTy`, the key's type and the result type (R4, R7.4a) before the batch form is emitted; the admission's 24 grounds, the retraction surface and the DRed choice trace from there, once. `emit_incremental` reads the `DIncr` it is handed; `incr_eligible` (now `incr_admit`), `retract_ok`, `incr_retract_eligible`, the DRed decision and their helpers moved with it, over `CAgg`. Gen byte-identical, traces equal as a multiset.
- **R7.4c.** The last decision the incremental emitter still made — a retracting single-source handle names a retracted row by the row's own value or by its key footprint — is the plan's (`DIncr.rowid`, traced there); the emitter spells the identity with the same function and ICEs if the two disagree. The rest of `emit_incremental` spells a `DIncr` and a `GroupTy`; it decides nothing. Gen + traces byte-identical.
- **R7 closed.** Each SCC's fixpoint is a plan (R7.1); loop-invariant indexes are built once (R7.2, R7.2b); variants drive from their delta (R7.3); whether a query gets an incremental handle, which one, and how a retracted row is named are planner decisions with their grounds (R7.4).
- **#737 closed.** A row selected WHOLE where the plan did not fold the query to the identity / head-row forms (which return the source's rows by reference) is copied out of the source when its type is `Copy`, cloned when it is `Clone` — as `.copied()` / `.cloned()` would need in Rust — and refused at the select otherwise (`codegen::emit_proj_value`, located; the check sits where the copy is made, because only the plan knows which queries fold). Fixtures `wql_whole_row_select_e2e`, `fail/wql_whole_row_select_fail`.
- **Recursive min/max over any integer or bool.** A recursive aggregate's value column may be any type that is totally ordered and compared in its own type (`Ord`, as `Iterator::min` needs): an integer of any width or `bool` (`typecheck::lattice_value_ok`, moved out of the emitter; it allowed only `i64` — the absorb gate already compared in the column's type). The seed row is emitted in its columns' types (`split_agg_proj`: `(s.id, 0)` against `(i64, u64)`). Fixture `wql_rel_lattice_u64_e2e` (a u64 weight above 2^63; control: an i64 comparison in the gate answers the wrong path); `fail/wql_rel_lattice_str_fail`; `fail/wql_rel_col_wide_int_fail` retired (its shape is admitted). `docs/spec/deem.md` `deem.datalog.rel-columns` rewritten (it still described the deleted lattice predicate).
- **Lattice over `str` and `Ord + Copy` types.** The value column may also be `str` (byte-lexicographic) or a user type that is `Ord` and `Copy` (the absorb gate's `<` is the type's own; each improving value is copied into the best map); a type without `Ord` is refused naming it. Fixtures `wql_rel_lattice_ord_e2e` (a least `str` tag and a greatest derived-`Ord` level reaching each node), `fail/wql_rel_lattice_no_ord_fail`. The spec's lattice RESTRICTION is lifted.
- **Tuple group keys.** `group by (a, b)` groups by every component (a `HashMap<(K1, K2), …>` key): its type is the tuple of the components' (`typing::group_key_ty`, read by the checker, the emitter and the planner), `key` is the tuple (a tuple type prints as itself), and each component must itself be an identity (`group_key_ok`, per component). The incremental handle maintains tuple-keyed groups under insertion and retraction unchanged. Fixtures `wql_group_tuple_key_e2e`, `fail/wql_group_tuple_key_f64_fail`; the spec's single-key RESTRICTION is lifted.
- **Multi-key recursive aggregates.** A recursive `min`/`max` rel's KEY is every column but the last (one column, or a tuple — a `HashMap<(K0, K1), V>` best map) and its VALUE is the last; the absorb push, the seed rows, the state and the materialized total spell the row from the key's components (`agg_key_ty` / `agg_val_ty` / `agg_key_text` / `agg_row_text`); a `group by` key of the wrong arity is refused at the key. Fixtures `wql_rel_lattice_multikey_e2e` (shortest distance per (node, mode); a one-column key would mix the modes and answer differently), `fail/wql_rel_lattice_key_arity_fail`. Every existing lattice fixture's gen byte-identical.
