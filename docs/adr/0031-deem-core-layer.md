# ADR 0031 — The Deem core layer: one normalised clause form between the surface and the plan

Status: PROPOSED (draft for PAIR review, 2026-10-06). Parent: [0024-deem-typed-plan-ir.md](0024-deem-typed-plan-ir.md).
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

| Row | Content | Acceptance |
|---|---|---|
| R0 | Core types + `core_verify` + dump; `lower_program` for all four shapes and rel bodies; nothing consumes it yet | every corpus query lowers and verifies (gate over the pass corpus) |
| R1 | `dl_export` reads Core | byte-identical Datalog on the oracle corpus, then delete the surface-reading exporter |
| R2 | scope / wardedness / stamping checks on Core | same diagnostics corpus-wide; the rel-aggregate check gap (§1) closes by construction |
| R3 | magic sets, liveness, fact desugar, fusion as Core → Core | `wql_rel_sips*`, demand fixtures, census pins hold |
| R4 | typed columns + expression types on Core (ADR 0024 S2 remainder, S3) | `u64` / user-type rel columns admitted by trait; Deem-side diagnostics at positions |
| R5 | DPlan + one-shot translator; planners on DPlan | plan traces and census pins hold; join order now chosen for rel bodies and aggregates |
| R6 | emitters from DPlan; delete per-shape emitters, `RExpr`, `JChain`, `JCh` | full L0 + the oracle; `rexpr_walk` shrinks by its per-shape share |
| R7 | semi-naive, DRed, incremental as translators | incremental fixtures + the diff harness |
| R8 | layer lint on; surface types confined to parse + lower | the lint, with a planted-violation canary |

## 5. Relation to other ADRs

- ADR 0024: S0 (positions) feeds Core spans; S2/S3 land ON Core (R4) instead of on the surface; S4's "plan as data" becomes DPlan; S5 ("emitters read the IR") is R6.
- ADR 0025: batch/cursor plane and materialisation nodes become DPlan operations; scan emission collapses to one site.
- ADR 0016: fusion becomes a Core operation (R3).
- ADR 0030: same lesson in the host compiler; Deem's Core is its HIR.

## 6. Open questions (PAIR)

1. Core as Logos types (recommended, exhaustiveness by the compiler) vs a Writ schema like the surface.
2. Entry = ordinary rel + envelope (recommended) vs an entry-specific node.
3. Order against the typed-domain line: R4 after R0–R3 (recommended: typing lands once, on Core) vs typing the surface first.
4. Incremental (R7) — inside this arc, or after R6 as its own ADR (it is 28.6 % of `rexpr_walk`).
5. Feature freeze on the surface during R0–R6: new constructs land only as lowerings into Core (recommended).
