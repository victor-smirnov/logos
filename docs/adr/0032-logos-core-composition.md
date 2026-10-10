# ADR 0032. Logos Core: Rust-core, Deem, Writ and Hest as one language

Status: PROPOSED for pair review (2026-10-10). Umbrella ADR of the set 0032-0038. Nothing here is implemented. Direction set by Victor, 2026-10-10: «Logos Core = Rust + Deem + Writ + Hest (+ Conan + Memoria, но это уже не такая органическая часть ядра). Основное направление сейчас -- это инкрементность в Deem и полная притирка компонентов Core друг к другу. Нужно будет добавить object capabilities и effects. Подтянуть или сделать выгрузку в z3/ccv5/etc. Сделать интеграцию с Lean.» ("Conan" = Canon, "ccv5" = cvc5.)

## Context

Logos is a language for model authors. Humans read and write code mainly in far OOD; in InD and near OOD review goes through models, and only critical code (payments, security) gets manual verification on top of formal verification. Consequences for the language: declarations that humans would find ceremonial are cheap; verification density is a productivity multiplier, not a DX cost; the human reads intent (declarations, plans, justifications, specifications), not loops.

The layering agreed in the 2026-10-10 discussion:

| Level | Component | Role |
|---|---|---|
| what | Deem | relational specification of the computation: rules, queries, invariants, incremental views |
| how | DPlan (Deem's plan IR) | the schedule: access paths, join order, materialisation, Δ-translation; every decision with its ground |
| representations | Rust-core | data structures that declare which relational operations they execute, at what cost and exactness; sequential kernels (parsers, codecs, numeric) as operators |
| between radii | Hest | dataflow graph whose vertices are Rust-core fns or Deem circuits; CF inside a synchrony radius, DF between radii |
| data at rest and on the wire | Writ | self-describing, relocatable representation of values, documents, IR carriers |
| periphery | Memoria, Canon | Memoria: persistent containers and stores (CoW snapshots); Canon: container factory and its facts |

Hardware-dependent multithreading stays in the Rust-core while hardware relies on cache-coherence algorithms. Where there is no coherence (LCM), programs move to the distributed, Datalog-friendly model (CALM: monotone strata need no coordination).

## Measured state (2026-10-10)

Census of the seams, file:line in the session notes; summary:

- **Four overlapping type systems.** Logos types (authoritative for Deem typing since ADR 0031 R4); the EL lattice (`stdlib/mem/wql/el.logos:394-610`, `EL_TY_*`, still routing emission and `RtVal`, still described in `docs/spec/deem.md:813`); Writ schema field types (`WritField`, `docs/spec/writ.md:117`); `RtVal` (runtime values of the template engine).
- **Two IR media in one Deem pipeline.** Surface `RQuery`, `SExpr`, `RExpr` are Writ schemas (`plan.logos:1-37` "SCHEMA MANDATE"); Core and DPlan are Logos types (`core.logos:5-9`).
- **Text seams.** The natspec string C++ → Logos (`sema_expr.cpp:24735`, parsed by `plan_walker.logos:1463`); the `enrich` mini-format (`wql.logos:180-200`); `__deem_bind` metacall built by string surgery (`src/compiler/main.cpp:812-830`); emission as text spliced into `quote_item!` shells and reparsed (≈672 text-building calls and ≈266 parse calls in `rexpr_walk.logos`).
- **One fact stated twice.** A container's capabilities live in `canon_verdicts` (`canon.logos:12-23`) and again in the emitted `impl OrderedMapSource … op … exact` (`container_item.logos:1741-1757`); `__deem_bind` computes the first and ignores it (`deem_bind.logos:116-139`).
- **Two persistent stacks.** `logos.mem.bt` (`trait Snapshot`, `map.logos:72`; CoW `memstore.logos`) and `logos.lcm.deem.data` (ADR 0017 survivor, used by `data_*` tests and `derive_branch_node` only).
- **Three producers of one Writ-graph edge vocabulary** (`writ_graph.logos:249`, `graphsrc.logos:258,331`, `derive_graph_source.logos`).
- **Deem is an item only.** `deem` binds at item position (`sema_expr.cpp:25944`); no fn-body form; `deem!` retired.
- **Incremental maintenance is shape-specific.** ADR 0013's DBSP engine was implemented in the interpreter and deleted with it (e1dd0ac5e, 2026-08-09); the compiled tier grew a per-shape handle (≈4 700 lines, 24 admission grounds). ADR 0033.
- **Hest has no ADR and no graph construct.** C++ HRPC, Logos fibers / `Chan<T>` / `Latch` / `Reactor` exist; no `Stream`, `Delta` or graph item. ADR 0034.
- **No effects, no capabilities, no solver.** No `may_suspend` (DIVERGENCES A4: every fn implicitly suspendable); the only checked fn property is `unsafe` (local, E0133); tiers lang → mem → lcm → std (`module_manifest.cpp:50-62`) are enforced at link level only and `extern fn` bypasses them; std APIs carry ambient authority; Deem UDFs are not checked for purity; the metacall JIT resolves any process symbol (`jit.cpp:163`); no SMT, no Lean, no contracts anywhere. ADR 0035-0038.
- **Stale status lines.** ADR 0028 ("ACCEPTED as direction"; the Datalog BC is the default since S7), ADR 0030 ("DRAFT… nothing implemented"; S0-S10 and Q1 rows landed), `docs/spec/deem.md` `deem.exec.incremental` ("nothing maintains a deem result incrementally"; the compiled handle exists).

## Decisions

### D1. One type system: Logos types

Every component types its values with Logos types. The EL lattice is retired (the remaining routing of emission and `RtVal` moves to Logos types; `EL_TY_*` and the class table are deleted). A Writ schema is a Logos type with a declared self-describing layout, not a parallel type language; schema field types are Logos types. `RtVal` survives only as the one dynamic boundary type over Writ (FFI, config, wire), per the 2026-08-06 decision against a dynamic Logos.

### D2. One IR ladder per layer, typed in Logos

Rust-core: AST → HIR → L-IR → BIR → MLIR (ADR 0030). Deem: surface → Core → DPlan (ADR 0031). Hest: graph IR, shared with DPlan's operator and statement layer (ADR 0034). Checked IR is Logos types (exhaustive `match`); Writ is the carrier for IR that crosses a process or persistence boundary (L-IR on Writ, cached plans, dumps). The Deem surface tree may stay a Writ schema because the generated parser produces it; nothing after lowering reads it (enforced since ADR 0031 R8).

### D3. Structured seams, no text protocols

Every seam between components passes typed values: natspec, `enrich` and `__deem_bind` become metaprog values (compiler queries returning structs); emission goes through quotes with antiquotes, never text spliced and reparsed (the standing rule "quotes cover 100 % of codegen"). Acceptance per seam: the text parser on the receiving side is deleted.

### D4. A declaration is stated once

A data structure's relational interface (relations, ops with demand pattern, cost, exactness, order, `unique`, `size`) is its source declaration. Canon's facts about a container are derived from that declaration or generate it, never stated beside it. `__deem_bind` plans from the one statement.

### D5. Deem is callable from fn bodies

A `deem` query is usable as an expression inside a fn body (local query over local sources), not only as an item. Deem rules are also usable as specifications (ADR 0037 D5).

### D6. The compiler's facts are Deem relations

Call graph, effects (ADR 0035), capabilities (ADR 0036), borrow facts (ADR 0028), type and impl facts are exported under a stable relational schema (`--emit-facts`, replacing the env-only `LOGOS_DL_DUMP`) and readable as Deem sources. First consumers: model review (global questions as queries), the effect checker, OOD detection (unwitnessed grounds). The ADR 0028 engine computes over them now; Deem replaces it with the logosc rewrite.

### D7. One persistent stack

`logos.mem.bt` (`Snapshot`, `Store`, CoW memstore) is the store layer of Core; `logos.lcm.deem.data` is retired after its consumers move. The parent snapshot is the z⁻¹ of incremental Deem (ADR 0033 D6).

### D8. Tiers become effect bounds

lang ("synthesizable, static memory"), mem (+ malloc), lcm (+ virtualised IO, threads, fibers), std (+ processes, OS IO) are restated as effect bounds (ADR 0035 D5) and checked in sema on every fn of the tier, `extern fn` included. Link order stays as it is.

### D9. Canon and Memoria are clients of Core, not part of it

They use Core's mechanisms (source declarations, Deem rules, Writ, capabilities) and add no language rules. A rule they need goes into Core as a general mechanism.

## The set

| ADR | Subject | Depends on |
|---|---|---|
| 0032 | this: composition, seams, rows C0-C8 | |
| 0033 | incremental Deem: DBSP as a DPlan translator | 0031 |
| 0034 | Hest core: graph layer, `Stream`/`Delta`, IR shared with DPlan | 0033 |
| 0035 | effects: inferred effect sets, named bounds, tiers | 0028 engine, 0030 HIR |
| 0036 | object capabilities: no ambient authority | 0035 |
| 0037 | specifications and SMT offload | 0035, 0028 S4 linearisation |
| 0038 | Lean integration | 0037 |

Priority (Victor): 0033 first, together with the seam rows of this ADR; 0035 → 0036 next; 0037 → 0038 after effects give purity.

## Rows

| Row | Content | Acceptance |
|---|---|---|
| C0 | status hygiene: ADR 0028 and 0030 status lines; `deem.md` `deem.exec.incremental` rewritten to describe the compiled handle until ADR 0033 replaces it | docs match code |
| C1 | EL lattice retired (D1): emission and `RtVal` routing on Logos types; `EL_TY_*`, class table, spec §813 deleted | gen + traces byte-identical; `logos_09_ty_default` stays 0 |
| C2 | structured seams (D3): natspec, `enrich`, `__deem_bind` as metaprog values | each receiving text parser deleted; gen identical |
| C3 | quote-only emission in `rexpr_walk`, `codegen`, `plan_walker` | text-building call census → 0, gate pins it |
| C4 | one declaration (D4): Canon facts from the source declaration | `canon_verdicts` reads the declaration; a planted disagreement is impossible by construction |
| C5 | `deem` in fn bodies (D5) | fixtures: local query over a local `Vec`, a borrowed container, a closure capture |
| C6 | compiler facts (D6): `--emit-facts`, schema doc, a Deem program reading them | the BC facts and the call graph read by a Deem fixture |
| C7 | one persistent stack (D7) | `logos.lcm.deem.data` deleted; `data_*` fixtures ported |
| C8 | one Writ-graph edge producer | two producers deleted; edge vocabulary id-collision ruling closed |

## Open

- O1. D5 surface: an expression form `deem { … }` or calls to a locally declared `deem` item. Recommendation: the expression form lowers to a local item (one mechanism).
- O2. D6 stability: the facts schema is versioned like the ABI or pinned per compiler build. Recommendation: pinned per build until the logosc rewrite; versioned after.
- O3. D7 direction: if `logos.lcm.deem.data` has a property `logos.mem.bt` lacks (branching), it is ported first.
