# ADR 0012 — Writ Query Language (WQL): unified query language over Writ

**Status:** DRAFT / SKELETON (2026-06-30) — captured from design discussion; to be
designed in detail. NOTHING here is final; sections marked **[OPEN]** are the live forks.
**Date:** 2026-06-30
**Related:** [ADR 0011 — Writ schemas](0011-writ-schemas.md) (the typing substrate this
builds on); the **Datalog** roadmap item; the planned **Logos VM**.

---

## Context

Writ data (map-like `TinyObjectMap` / `WMap<Wu6,WAny>`, int- and string-keyed maps,
arrays, datatypes, typed values) needs a query/expression facility. The Memoria/C++
predecessor provided two hand-written things over Hermes:

- **HermesPath** — a fork of `jmespath.cpp` (JMESPath) retargeted from JSON to Hermes
  `Object`. A *query DSL* (path navigation, projections, filters, functions). See
  `memoria-core/core/.../hermes/path/`.
- **Template** — a jinja2-like text templating engine (`{{ }}`, `{% if %}`, `{% for in %}`,
  `{% set %}`) whose embedded expressions are HermesPath. See
  `memoria-core/core/.../hermes/template.hpp` + `hermes_template_*`.

**Decision drivers (from discussion 2026-06-30):**

1. **Template is kept as-is, renamed `Trama`** — its jinja semantics are good; the port is a
   near-term task. Its AST (`TplASTCodes`: FOR/IF/ELSE/SET/VAR) is independent of the expression
   language — the only coupling is the `EXPRESSION` attribute + the renderer's `evaluateExpr`.
   Clean seam. **Name `Trama`** (Spanish/LatAm) carries the weaving metaphor: *trama* = the
   **weft** threads (cf. *urdimbre* = warp), also "plot" of a story/telenovela, also slang
   *tramar* "to cook something up". Implementation vocabulary follows: `urdimbre` (warp = the
   fixed template text/structure), `trama` (weft = the threaded data), output = the woven text.
   Joins the Writ/Hest archaic-noun family with a LatAm voice.
2. **HermesPath is rejected** — not because it is broken, but because (a) it is not
   *Logos-native*, (b) it is not expressive enough, and above all (c) hand-writing it over
   Hermes's typing was the painful part. We want one Logos-native facility.
3. **One language, many profiles.** Goal: a SINGLE query language over Writ at **Datalog-class
   expressiveness**, with a restricted **expression profile** (CEL-class) as a strict subset,
   and possibly other profiles. Subsets are profiles over **one typed IR**, NOT separate
   implementations. (This is what reconciles "one language" with "a small subset for templates".)
4. **Models are the primary authors.** The code that physically writes WQL will mostly be
   models; humans mostly read/scoff. This *inverts* normal DSL priorities — see Principles.
5. **Interpret AND JIT.** The internal architecture must support both fast interpretation
   (one-shot queries, runtime templates) and JIT compilation (hot queries). The standard
   **Logos VM** (to be developed separately) is the shared execution substrate.
6. **Builds on Writ schemas (ADR 0011).** The typing substrate — typed views over TOM — now
   exists. WQL is schema-aware: static where a schema is known, dynamic over erased `WAny`.

---

## Principles (load-bearing, derived from the drivers)

### P1 — Subsets are profiles over one IR, not forks

The expression profile (CEL-class) is a syntactic + semantic *restriction* of the full
language: same IR, fewer productions. CEL ⊂ Datalog as a **profile**, not a separate codebase.
A template author sees only the expression profile; an analytical query uses the full profile;
both lower to the same IR and run on the same VM.

### P2 — Optimize for machine authorship + static checking, not human terseness

Because models are the authors:

- Favor **regularity, explicitness, strong typing, great diagnostics, composability**.
  Verbosity is cheap; magic/implicitness is harmful (it makes model errors *silent*).
- The language may be **large** as long as it is **regular** — models handle large regular
  surfaces well and stumble on irregular small ones.
- This is the opposite of JMESPath's implicit-projection cleverness. Explicitly **reject
  implicit projection**.

### P3 — Static schema-typing is the agentic selector

Per [[insight_agentic_tail_sampling]]: model errors are a low-mass tail, cured by an
*external, mass-independent selector* (type checker / oracle). **Schema-aware static typing of
queries IS that selector** — it catches the model's improbable error before execution. So
"models write it" does not conflict with "strongly typed"; it *requires* it.

### P4 — Schema-aware hybrid typing

Static where the navigated shape is a known Writ schema (errors at parse/compile time);
dynamic over erased `WAny` (lenient at runtime). Strictness follows from the type — the same
check-policy principle as ADR 0011 schemas. **[OPEN]** exact null/missing-key semantics
(strict on schema, lenient `null` on erased? optional strict mode?).

---

## Proposed architecture (sketch — to be refined)

```
Surface profiles:   [expr / CEL]    [Datalog-full]    [path-shorthand?]
                          \              |               /
                           ▼            ▼              ▼
Typed IR:            scalar algebra   ⊂   relational / logic algebra
                     (schema-aware; over erased Writ → dynamic)
                            |
             ┌──────────────┼────────────────┐
             ▼              ▼                 ▼
        tree-walk       JIT → Logos VM    lower → Logos LIR
        interpreter     (hot queries)     (query known at Logos compile time)
```

- **Two-tier IR**: a **scalar algebra** (the whole CEL profile + template `{{ }}` lower here)
  nested inside a **relational/logic algebra** (Datalog: scan / join / select / project /
  antijoin / aggregate / **fixpoint** with semi-naïve evaluation).
- **Types** from Writ schemas (ADR 0011); erased data → dynamic.
- **Backends decoupled from the IR**: tree-walk interpreter (one-shot / runtime templates);
  JIT to the Logos VM (hot queries); direct lowering to Logos LIR where the query is known at
  Logos compile time (native, reusing the schema accessors built in ADR 0011).
- **Logos VM** is the shared substrate, developed separately.

---

## Sequencing — directions × queues (REVISED 2026-07-01: static-first)

Two orthogonal axes.

**Directions (workstreams / components):**
1. **IR + EE** (internal engine) — designed FULL and Datalog-ready over a **graph data model**,
   but *implements* only the EL+comprehension subset. Includes the data-source abstraction
   (graph = nodes + ref-edges; not Writ-only) and the lightweight scalar path. Sub-workflow.
2. **Frontends** — Trama (settled, jinja2-derived) + EL (**CEL semantics + Logos comprehension
   syntax**). Full Datalog NOT now; base only. Sub-workflow, parallel with (1).
3. **Integration into language + tooling** — the `resource` concept (`wql!{}` / `trama!{}`
   compile-time blocks via metacall). Most design-intensive; converges (1)+(2).

**Queues (execution maturity over the ONE schema'd IR):**
1. **Static / compiled** (FIRST) — `resource` blocks compiled at build time via metacall →
   Logos LIR, statically linked, executed like prepared statements. No runtime-string queries.
   Easier + better in Logos than interp-first was in C++ (we own the compiler + metacall +
   schemas; the EL↔Writ↔runtime "many cases" pain is taken by the Logos frontend).
2. **Interpreter** (dynamic) — tree-walk over the schema'd IR for runtime-loaded templates/queries.
3. **JIT** — IR → Logos VM bytecode for hot queries.

**Work organization:** (a) **seam-spike FIRST** (not parallel) — IR schemas + FE→IR contract +
resource ABI, companion [`0012-impl-seam.md`](0012-impl-seam.md); (b) then **parallel** dir1+dir2;
(c) then **converge** on dir3. Respects roadmap "Datalog later, don't interleave"
([[project_roadmap_2026_05]]).

---

## Open decisions (RESOLUTIONS in "Design session 2026-07-01" below)

- **[OPEN] D1 — separate DSL vs Logos dialect.** Lean: a *separate surface* that shares the
  Logos **type system (schemas) + VM + metacall embedding + "feel"**, but is not literal Logos
  (relational/fixpoint semantics do not fit imperative Logos). "Logos-native" preserved via
  types + integration.
- **[OPEN] D2 — Datalog flavor.** Pure Datalog vs Datalog + functions + aggregations
  (Soufflé / Logica class). Lean: the latter (CEL scalars live inside it anyway).
- **[OPEN] D3 — code start point.** Design the *whole* unified IR (incl. relational) on paper
  (this ADR) first, then code from Phase 0. Lean: yes — paper-design the full IR, code from
  Phase 0.
- **[OPEN] D4 — null / missing-key semantics.** Strict on known schema (compile error), lenient
  `null`/empty on erased, optional strict mode. (See P4.)
- **[OPEN] D5 — collection ops in the *expression* profile.** Lean: start WITHOUT
  expression-level map/filter (no projections!); `{% for %}`/`{% if %}` cover ~90%. Add
  `.filter(|x| …)` later as honest Logos closures if needed.
- **[OPEN] D6 — functions/filters surface.** Logos-style calls (`x.upper()` / `upper(x)`) as the
  canon; jinja pipe `|` as optional sugar (`{{ x.price | round(2) }}`).
- **[OPEN] D7 — schema binding to a template/query.** In-template header vs call-site generic
  (`render::<RootSchema>(data)`) vs inference. Lean: call-site generic from Logos; runtime
  descriptor / dynamic for erased.
- **[OPEN]** naming (WQL is a placeholder), surface grammar, VM ISA, the Logos-LIR lowering path.

---

## Design session 2026-07-01 — decisions (supersede skeleton tentatives)

All three directions + work-org **accepted**. Decisions:

### Static-first sequencing — an inversion, not a compromise
C++ interp-first was *forced* (no host-compiler access → runtime parse→AST→walk; the
EL↔Writ↔runtime "many cases" pain is a symptom). In Logos we own the compiler + metacall +
schemas, so compile-first is the cheapest AND best first step: the integration is generated at
compile time, typed by schemas, once. Compile-time checking = the agentic selector (P3) from day 1.

### Graph data model (first-class)
Writ data is a **graph** from the start: nodes = schema'd objects, **edges = ref fields**
(`WRef<S>` / arrays of refs) — so the graph topology is *already encoded in the schemas*.
Datalog-over-graphs is the canonical fit (reachability = recursion over the edge relation).
**Edge unification:** `SField` (1 scalar step) ⊂ `REdge` (1 step over a row-set) ⊂ `RFix`
(unbounded) — the SAME edge primitive at three iteration depths. Path navigation = a bounded
edge-follow chain; Datalog recursion = fixpoint over edges. Possibly-absent edges are
`Option`-typed → explicit handling (see D4).

### IR = Writ schemas (dogfood ADR 0011)
The IR is declared as `schema` (per node) + `schema enum` (per family), exactly like the C++
HermesPath/Template AST was TinyObjectMap+NamedCode — now first-class. The three queues are
**three consumers of one schema'd IR**: static backend = metacall walks it → LIR; interpreter =
`match` over the schema enums; JIT = IR → VM bytecode. IR serialization is free (it is Writ).

### `resource` / `wql!{}` / `trama!{}` = `sqlx::query!` for Writ
Compile-time blocks the compiler lifts via metacall → typecheck against schema → IR → lower to
LIR → emit (function + static data), statically linked, run as prepared statements. Named
top-level `resource` items (reusable) AND inline forms. Result type computed at compile time from
the schema (statically-typed rows, sqlx-style). **No runtime-string queries in queue 1.**

### EL = CEL semantics + Logos comprehension syntax
CEL types/operators/null-rules/function-macros, but iteration rendered as Logos comprehensions
(not `e.map(x,f)`). **Comprehension = the Datalog bridge** (one comprehension = one rule;
Datalog later adds multiple rules + recursion) → EL→Datalog is syntactically *additive*. MVP EL:
CEL scalars (arith/bool/compare/field-access/calls) + comprehension.

### Resolved open decisions
- **D3 → YES** paper-design the whole IR first (the seam-spike).
- **D4 → STRICT (option 2):** everything mandatory by schema; optionality ONLY via explicit
  `Option`-typed schema fields. No `has()` / `?.` safe-nav. (Lenient-`null` deferred to queue-2
  interpreter, where erased data lives.)
- **D5 → comprehensions ARE in** (revises skeleton's "start without" — they are the Datalog seam).
- **D7 → static case: schema ALWAYS known at compile time** (mandatory; that is what makes it static).
- **D1** → embedded DSL via metacall regardless of surface; still a *separate surface* sharing
  Logos types + VM. **D2** → Datalog + functions + aggregations (full Datalog deferred, base laid).

### Work split
Seam-spike (assistant drafts, user reviews) → parallel dir1 (EE) + dir2 (FE) → converge dir3.
Companion: [`0012-impl-seam.md`](0012-impl-seam.md).

---

## Notes

- The whole thing rests on the ADR 0011 typing-over-TOM foundation, completed 2026-06-30 — that
  is what makes schema-aware queries cheap (reuse field resolution + WAny↔T + the typed views).
- Trama port and the EL profile decouple via the Trama AST `EXPRESSION` seam.
