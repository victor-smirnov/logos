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

`mapping` is an item at the `schema` level — **LANDED 2026-07-09** (grammar-level; supersedes the `mapping!` macro, `mapping` is now a keyword):

```
pub mapping Services(g: &Writ) {
    pub rel engine(owner: i64, name: str) { from g ** .engine e select (e.parent, e.vs); }
    rel big_port(p: i64) { from port r where r.p >= 8443 select r.p; }   // non-pub
}
…  Services::engine(&doc)?   // ordinary static-call resolution; pub enforced
```

Architecture of the lift (the front/back cut): **C++ owns text, names and signatures; the Logos stdlib owns Datalog semantics and emission.**

- Grammar: `MAPPING_DEF`/`REL_DEF` items (logos.peg + ast.hpp mirror); `rel` is a CONTEXTUAL keyword (validated in sema — a global keyword would clash with the `rel` identifier throughout the WQL stdlib itself); rel bodies stay token-level (`RAW_GROUP_BRACE`) — they are rule text for the deem pipeline, not Logos expressions.
- Sema (`lower_mapping_def`): validates the item (param forms, contextual `rel`, duplicate rels, typed columns restricted to i64/str/bool = the joinable/Eq set), reconstructs CANONICAL `(name, params, body)` text from the CHECKED nodes (syntactic type render), and routes it through the shared token-macro item seam (`emit_token_macro_item_site` — the factored tail of `lower_fn_macro_call_item`) to the `__mapping_item` handler (`logos.std.wql.mapping_item`, requires that `use`).
- Handler: same pipeline as deem! (parse_program → gpath desugar → walk_program_params), per rel one generated fn mangled `<M>__<rel>` — `M::rel(args)` resolves through the ordinary `Type::method` static-call path (mangled-name concatenation + flat fn registry) with ZERO new resolution code; per-rel `pub` maps to generated-fn visibility (a `-` fn-name marker consumed at the emit sites), enforced by the ordinary cross-package fn check.
- No marker TYPE is minted yet (nothing needs the nominal type until mappings become values/generic — the functor slice).

Static consumption `deem!(g: M)` (M2b-2) and runtime `env.bind_mapping("g", M, root)` stand as designed; note the registry problem M2b-2 was deferred on is now DISSOLVED — rel signatures are Sema-owned, so consumption needs only a collect-phase mapping table + deem!-param enrichment, no macro-to-macro registry.

## 4. Slices

| slice | content | gate |
|---|---|---|
| M1 | `mapping` item + case-1/2a lowering (subsume today's APIs; both binding times) — **M1a LANDED** (`deem!(g: &Writ)` = the graph source as a native rel, `acf38a0f`; the `mapping` item proper arrives with M2b) | `wql_writ_graph_e2e` (recursion over the graph param, graph ⋈ slice, two docs, hand-walk oracle) |
| M2 | case-3 path-rule DSL → rel lowering (`.` / `[*]` / `**` / kind filters) — **M2a LANDED** (`d0c82fff` + `**`): path steps as query-surface sugar (`from g .db .pool [*] {kind} ** v`), ONE shared plan→plan desugar for both binding times, anchored at the VIRTUAL ROOT EDGE (parent = 0 — both walkers emit it; root queryable, no root-handle plumbing); `**` = a reach hop through an INJECTED ordinary Datalog rel (`__reach_<src>`, descendant-or-self; both engines evaluate it with existing rel machinery — no new evaluator code); `{kind}` = filter, not movement; named errors for bind-less last step / binder-on-filter / dangling or consecutive `**`. NOTE: the sugar generalized beyond the mapping layer — it works in ANY deem body/entry, which is the language-level directive done right | `wql_gpath_e2e` (static: every step kind, mid-path bindings, gpath in rel bodies, aggregate heads, path+join) + `query_gpath_e2e` (runtime: sugar ≡ hand-written classic AND `**` ≡ hand-written reach rel — differential parity, exactly this row's gate) |
| M2b | mapping packaging — **v1 LANDED** (`mapping!` macro), **v2 LANDED 2026-07-09: the language ITEM** (§3): grammar+Sema own the item and rel signatures, `Services::engine(&doc)` path calls, per-rel `pub`→fn visibility, canonical-text seam to the stdlib pipeline; the `mapping!` macro is REMOVED (`mapping` = keyword). **M2b-2 consumption LANDED 2026-07-09**: `deem!(g: Services)` — a param TYPED by a mapping name splices that mapping's rules into the consumer's program (FUSION, not materialization: one program, shared SCC/fixpoint). Mechanics: a `mappings_` pre-scan registers every mapping's signature + canonical rule text before lowering (item order irrelevant); at the deem! site the mapping's rel list is prepended and parsed as ONE program, the param's type rewrites to the mapping's source type in the emitted fn, and the handler renames the mapping's own source param to the consumer's inside just the spliced rels (`old=new@lo-hi;` beside the IR — enabled by the now-IDEMPOTENT walk_program_params, whose `__rel_<r>_sl` rewrite is accepted as an alias on re-walk). v1 = single-source mappings only (scalar mapping params = named error; binding surface is a later slice); same-module only (cross-module = decl-export slice). Deferred: per-call dep-rel rematerialization (M6 pairs) | `wql_mapping_e2e` (ITEM form: paths+`**`, cross-rel through a NON-pub rel, scalar param, two mappings per module, rel doc-comments); `wql_mapping_consume_e2e` (fusion: rename real, mapping rel ⋈ graph param ⋈ consumer slice, consumer rel over a spliced rel, hand-written parity, consumer declared BEFORE the mapping); fail `wql_mapping_consume_scalar_fail` |
| M3 | per-relation materialization annotation | plan inspection + perf smoke; semantics unchanged |
| M4 | case 2b: static (reflection walk) + the per-tag traverse protocol shared with 2a | graph tests over native object graphs (incl. Rc cycles) |
| M5 | case S: `EngineState` mapping replacing the hand-rolled S1–S3 accessors — **LANDED** (static form): a `deem!` param typed `&IncrRec` registers four native relations — `<p>_trace(epoch,kind,step,delta,total,ns)` / `<p>_epochs` / `<p>_tail(epoch,converged,pending,bound,cutr)` / `<p>_controls(epoch,kind,val)` — materialized by `logos.std.deem` state materializers (`mapping_state.logos`); I1 is now a mapping-class contract (rows = sensor facts about the completed past); the chunk pulls `use logos.std.deem` CONDITIONALLY (unconditional = module cycle, wql must not import deem). The runtime twin (`bind_engine` for the self-applicable loop proper) pairs with ADR 0015 S5 | `wql_engine_source_e2e`: the Encounter rule as a static deem! query over `<p>_tail` (one row at the cut, ZERO after raise+converge — the §6 honesty pair through this path), Σδ consistency AS A DEEM AGGREGATE over `<p>_trace` (the S1 oracle expressed in Deem itself), controls audit, epochs ledger |
| M6 | demand-driven mapping bodies (magic sets on the lowered rules) | oracle: results ≡ full walk; visit counts strictly smaller |

Ordering notes: M1/M2 unblock Gellish; M4 gates on the tag-system user-type feature maturing (the traverse seam is designed, not blocked); M5 pairs naturally with ADR 0015 S5; M6 belongs to the reasoning-in-large opening.

## 5. Deferred / out of scope

- Incremental mappings (mapping + delta events) — arrives with the systemic incrementality step; the FactStore path remains the live-fact route until then.
- Cross-mapping composition (mappings importing mappings' relations) — reasoning-in-large's module system proper.
- Writ-core changes: none (additive throughout; the no-materialization rule stands).

## 6. Sources as relational interfaces (design; user directive 2026-07-09 «не забудь про impl Trait потом»)

**Problem.** Every native source flavor is a hardcoded triple in `register_native_rels` (plan_walker.logos): DETECTION is a string compare on the param's syntactic type (`str_eq(prm.tys[p], "Writ")` / `"IncrRec"`), the rel VOCABULARY (column names/types, row tuple) is inlined per flavor, and the MATERIALIZER is an `nkind` integer switched over five function names in the emitted prelude. Adding a source type (the user's motivating case: a `map` type in Writ, or any user type once the tag system opens) means editing the walker. That is a closed dispatch where the language already owns the open one: traits.

**Decision.** A trait may declare `rel` members; an impl binds each rel to a materializer; a deem!/mapping param typed by an implementing type carries that trait's relations. One declaration surface replaces all three hardcoded heads:

```logos
pub trait GraphSource {
    /// One row per edge of the object graph (the M1a/64dd1286 vocabulary).
    rel edge(parent: i64, key: str, idx: i64, child: i64,
             kind: str, tag: i64, vi: i64, vs: str);
}
impl GraphSource for Writ {
    rel edge = writ_graph_edges;      // fn(&Writ) -> Vec<(i64, str, …)>
}

pub trait EngineState {
    rel trace(epoch: i64, kind: i64, step: i64, delta: i64, total: i64, ns: i64);
    rel epochs(epoch: i64, ins: i64, del: i64, rounds: i64, ns: i64);
    rel tail(epoch: i64, converged: i64, pending: i64, bound: i64, cutr: i64);
    rel controls(epoch: i64, kind: i64, val: i64);
}
impl EngineState for IncrRec {
    rel trace = deem_state_trace;   rel epochs = deem_state_epochs;
    rel tail  = deem_state_tail;    rel controls = deem_state_controls;
}
```

- **Rel naming at the use site**: rel `r` of param `p` is addressable as `p_r`; when the trait declares exactly ONE rel, `p` alone aliases it. This preserves both existing surfaces verbatim: `from g ** .engine e` (GraphSource's single `edge`) and `from e_trace t` (EngineState's four).
- **Impl member contract (v1 = native impls)**: `rel r = f;` where `f: fn(&T) -> Vec<RowTuple>` and RowTuple matches the declared columns positionally. Column types stay i64/str/bool — the same Hash+Eq rule as mapping rel columns (`val_f64` cannot be a rel; the honest boundary stands). RULES-backED impls (`impl GraphSource for MyMap { rel edge { from … } }`) are the M4/2b pairing — they need a primitive access trait beneath them (the per-tag traverse protocol) and arrive with it.
- **Architecture cut — same as fusion (M2b-2)**: Sema owns detection and signatures; the walker consumes a SPEC. Sema keeps a `source_impls_` registry (type → [trait, rels, materializer fn names]), filled by a pre-scan like `mappings_`. At a deem! site, each param whose type has source impls contributes `p:rel=fn(cols…);…` to the `extra` slot beside the rule IR. `register_native_rels` reads the spec instead of comparing type strings; `emit_prelude_oneshot` prints the materializer NAME from the spec — `nkind` and both `str_eq` heads die. `deem!(g: &Writ)` / `deem!(e: &IncrRec)` keep working through blessed built-in impls of these two traits, declared in the stdlib where the materializers live (writ_graph.logos / deem's mapping_state.logos) — the built-ins become the FIRST registrations of the open mechanism, not a parallel path.
- **Generic mappings**: `mapping M<S: GraphSource>(g: &S) { … }` — the bound names the rel vocabulary `g` carries inside the body; Sema checks body sources against it (possible now that rel signatures are Sema-owned); the consumption site (`deem!(w: M)` with `w`'s actual type) instantiates. This is what makes a mapping reusable across Writ and any future graph-shaped type without edits — the user's original ask.

**Slicing**: (T1) grammar+Sema: `rel` members in trait/impl items, signature validation, `source_impls_` registry + pre-scan; (T2) spec through the seam, walker de-hardcoded, stdlib built-in impls, gates = existing wql suites stay green with the switch flipped; (T3) `mapping M<S: Bound>`; (T4, with M4) rules-backed impls. Follows the mapping-item playbook: C++ owns text/names/signatures, Logos stdlib owns Datalog semantics/emission.

**T1+T2 LANDED 2026-07-09.** REL_SIG (262) / REL_BIND (263) parse in trait/impl bodies (contextual `rel`, ordered before the keyword-led members — costless); collect_trait/collect_impl fill `trait_rels_` / `source_impls_`; the natspec (`<regname>=<matfn>[@<module>]:<param>(<col> <ty>,…);`) travels as slot 3 of the unified 5-slot rule-IR ABI `(name, params, extra, natspec, ir)`. The walker is source-type-blind: `register_native_rels` parses the spec (no type-name compares), the prelude prints the spec's materializer, and the chunk imports each distinct materializer module (`native_use_text` — writ_graph and deem imports are now conditional BY CONSTRUCTION, replacing has_engine_param). The Writ/IncrRec built-ins are seeded registrations of the same registry — zero hardcoded source types remain in stdlib/std/wql. Deviation from the sketch above, deliberate: a single-rel vocabulary registers under the param name ONLY (no additional `p_r` alias) — one rule, no double registration against the 8-rel cap. Gates: wql_source_trait_e2e (user type + recursion over it + slice join + multi-rel vocabulary), fail unknown-rel / f64-column; entire wql/deem/trama suite green on the de-hardcoded path.
