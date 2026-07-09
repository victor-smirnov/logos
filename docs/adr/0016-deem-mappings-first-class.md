# ADR 0016 — Deem mappings as first-class objects

- Status: **Accepted** (design baseline; slices gated individually)
- Date: 2026-07-08
- Builds on: ADR 0012 (runtime query engine), ADR 0013 (incremental engine), ADR 0015 (self-applicable reasoner — the S-case below), the virtual graph sources (commits `da86fddf`, `64dd1286`), the no-gratuitous-materialization rule.
- Companion threads: reasoning-in-large (ADR 0015 §9 — mappings are its first motivating instance), Gellish ontologies (relation-type vocabularies = mapping libraries), the tag-system user-type integration (the traverse seam).

---

## 0. Context

One day of work produced five ways to get data in front of Deem: `bind_source` (schema'd arrays), `bind_source_erased` (lenient rows), `bind_source_tree` (virtual graph edges), `deem!` slice params (static), and a hand-written walk materializing edge rows (the static bridge). These are five instances of ONE thing that currently has no name in the system: the **mapping** — a declarative object taking a *data shape* to a *set of relations*.

The governing rule (feedback, 2026-07-08): the store is the fact base; queries read THROUGH it; materialization is a targeted, per-relation, profile-proven optimization — never the data model.

## 1. Decision

A **mapping** is a first-class object:

```
mapping M over <source shape> {
    <relation>(<typed columns>) from <enumeration rule>;
    ...
}
```

It declares (a) the source shape, (b) the relations it exposes — names + typed columns, exactly what the checker needs for hash-tier joins and rel columns — and (c) the enumeration semantics producing the rows. Three orthogonalities define the design:

1. **Mapping ⊥ binding time.** The same mapping object compiles statically (`deem!` emits inline loops), interprets at runtime (`bind_mapping(env, M, root)`), and — later, systemically — participates in incremental maintenance (mapping + delta events at the fact boundary). Today's per-API split (`bind_source_*` vs `deem!` param kinds) is the un-generalized residue.
2. **Mapping ⊥ engine.** Domain mappings LOWER to Deem rules over generic-structural relations (§3); there is no second engine. A mapping is a named, typed, reusable **rule module** — the first concrete instance of reasoning-in-large's module system.
3. **Mapping ⊥ optimization.** Demand-driven evaluation (magic sets) applies to mapping bodies (don't walk what the query doesn't touch); materializing a specific exposed relation is a per-relation ANNOTATION on the mapping (the CREATE-INDEX posture), not a code change at use sites.

## 2. The case taxonomy

| case | source shape | enumeration | status |
|---|---|---|---|
| **1 — tabular** | array of homogeneous rows | scan; mapping ≈ column typing (degenerate: one rule) | shipped: `bind_source`/`bind_source_erased`/`deem!` slices |
| **2a — structural: Writ graph** | tag-dispatched object graph | the generic EDGE vocabulary: `(parent, key, idx, child, kind, tag, vi, vs)`; edges always emit, containers expand once (DAG/cycle safe); container id = handle, leaf id = synthetic FNV(parent, ordinal) | shipped: `bind_source_tree` (`64dd1286`) |
| **2b — structural: Logos graph** | native language objects (structs, Box/Rc, Vec, references) | same edge vocabulary; key = field name, idx = element position, child id = address (stable while alive; cycles legal via Rc) | open |
| **3 — domain** | any 2x source | user-declared path-pattern rules → domain relations: `port(svc, p) ⇐ svc.ports[*]`, `engine(owner, n) ⇐ **.engine` | open (the mapping DSL) |
| **S — self-applicability** | a Deem engine instance | the engine's own state as EDB: trace, residual descriptor, journal | hand-rolled precursor shipped (ADR 0015 S1–S3 accessors) |

### 2b — the shared traverse seam

Writ had one dispatch point (`WAny::type_code`); native objects have none — but both halves already exist in the language:

- **static**: field enumeration via reflection/metaprog (derive-style generation of the walk per type at compile time);
- **dynamic**: the user-type/tag-system integration — `#[datatype]` objects carry tags; the per-tag **traverse protocol** documented in the graph walker (enumerate `(key, idx, child)`; dispatched through the same table as `#[tag_dispatch]`) serves BOTH 2a and 2b: Writ containers and Logos objects register in one table. One walker, open dispatch — not two walkers.

Identity in 2b: the address, stable under the re-scan contract (source immutable for the duration of a query) — the same contract 2a already carries.

### 3 — domain mappings lower to Deem

A path rule is sugar for a rel over the structural vocabulary:

```
port(svc, p) ⇐ svc.ports[*]
   ≡  from edges e1 join edges e2 on e2.parent == e1.child
      where e1.key == "ports" select (e1.parent, e2.vi)
```

`**` (descendant) lowers to the recursive `dsc` rel. Consequences: the checker/planner/incremental machinery apply unchanged; Gellish relation-type vocabularies become mapping LIBRARIES; demand-driven evaluation is the optimization story for large documents. Case 1 is the degenerate single-scan mapping — the whole taxonomy is one object.

### S — the engine as a source

ADR 0015's S1–S3 built by hand exactly what this case gives for free: `TraceStep`/`TruncatedTail`/journal accessors + per-test materialization (`tail_edb(...)` in the L0/L1 tests) are a hand-rolled `mapping EngineState over deem_engine`. Under the mapping frame:

- **I1 becomes a mapping-class contract** (sensor facts about the completed past, append-only) — a checkable property of the source class, not a code discipline;
- **S5 (the self-ontology) IS this mapping's declaration** — the slice reduces from "write accessors + vocabulary" to "declare the mapping";
- the Nous Observer reads the engine through the SAME first-class object as any data — νόησις νοήσεως with no special cases.

## 3. Language integration

`mapping` becomes an item at the `schema` level (precedent: `schema`, `resource` + `deem!` — item declarations with codegen behind them). Static use: `deem!(g: M)` — the macro inlines the mapping's enumeration (case 1 = today's slice scan; case 2 = the generated walk; case 3 = expanded rules). Runtime use: `env.bind_mapping("g", M, root)` — one binding replaces the `bind_source_*` family (which remains as the low-level API the mapping compiler targets).

## 4. Slices

| slice | content | gate |
|---|---|---|
| M1 | `mapping` item + case-1/2a lowering (subsume today's APIs; both binding times) — **M1a LANDED** (`deem!(g: &Writ)` = the graph source as a native rel, `acf38a0f`; the `mapping` item proper arrives with M2b) | `wql_writ_graph_e2e` (recursion over the graph param, graph ⋈ slice, two docs, hand-walk oracle) |
| M2 | case-3 path-rule DSL → rel lowering (`.` / `[*]` / `**` / kind filters) — **M2a LANDED** (`d0c82fff` + `**`): path steps as query-surface sugar (`from g .db .pool [*] {kind} ** v`), ONE shared plan→plan desugar for both binding times, anchored at the VIRTUAL ROOT EDGE (parent = 0 — both walkers emit it; root queryable, no root-handle plumbing); `**` = a reach hop through an INJECTED ordinary Datalog rel (`__reach_<src>`, descendant-or-self; both engines evaluate it with existing rel machinery — no new evaluator code); `{kind}` = filter, not movement; named errors for bind-less last step / binder-on-filter / dangling or consecutive `**`. NOTE: the sugar generalized beyond the mapping layer — it works in ANY deem body/entry, which is the language-level directive done right | `wql_gpath_e2e` (static: every step kind, mid-path bindings, gpath in rel bodies, aggregate heads, path+join) + `query_gpath_e2e` (runtime: sugar ≡ hand-written classic AND `**` ≡ hand-written reach rel — differential parity, exactly this row's gate) |
| M2b | `mapping!` packaging — **v1 LANDED**: a named rel-set over a source shape, one generated PUBLIC fn per relation (`Services_engine(&doc)`), compiled through the SAME pipeline as deem! (gpath sugar, `**`, recursion, cross-rel refs — mappings are UNRESTRICTED rule modules per the design decision); scalar params ride along. Deferred: `deem!(g: Services)` consumption (needs a macro-to-macro registry — M2b-2), per-call dep-rel rematerialization (M6 pairs) | `wql_mapping_e2e` (paths+`**`, cross-rel, scalar param, two mappings per module) |
| M3 | per-relation materialization annotation | plan inspection + perf smoke; semantics unchanged |
| M4 | case 2b: static (reflection walk) + the per-tag traverse protocol shared with 2a | graph tests over native object graphs (incl. Rc cycles) |
| M5 | case S: `EngineState` mapping replacing the hand-rolled S1–S3 accessors | ADR 0015 tests re-expressed via the mapping |
| M6 | demand-driven mapping bodies (magic sets on the lowered rules) | oracle: results ≡ full walk; visit counts strictly smaller |

Ordering notes: M1/M2 unblock Gellish; M4 gates on the tag-system user-type feature maturing (the traverse seam is designed, not blocked); M5 pairs naturally with ADR 0015 S5; M6 belongs to the reasoning-in-large opening.

## 5. Deferred / out of scope

- Incremental mappings (mapping + delta events) — arrives with the systemic incrementality step; the FactStore path remains the live-fact route until then.
- Cross-mapping composition (mappings importing mappings' relations) — reasoning-in-large's module system proper.
- Writ-core changes: none (additive throughout; the no-materialization rule stands).
