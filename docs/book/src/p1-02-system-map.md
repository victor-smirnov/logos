---
title: System map
verified-at: a4f617303
sources: [docs/adr/0032-logos-core-composition.md, docs/adr/0030-hir-core-layer.md, docs/adr/0031-deem-core-layer.md, docs/adr/0028-datalog-engine-borrowck-traits.md, docs/adr/0018-memoria-port.md, docs/adr/0020-memoria-container-interface-plane.md, docs/adr/0021-metaclasses-mono-seam.md, docs/adr/0034-hest-core-graph-layer.md, docs/spec/deem.md, docs/spec/writ.md, docs/spec/modules.md]
---

# System map

The map of the whole system: layers, the compiler, the sublanguages, the storage framework, the runtime, the toolchain, the verification process. Each part gets enough to orient and a pointer to where the detail lives. Supersedes [docs/internals/architecture.md](../../internals/architecture.md), which predates HIR, L-IR handles, BIR, the Datalog borrow checker and the fixpoint driver.

## 1. Problem and constraints

Logos is a systems language with a Rust-shaped core and built-in data, query and persistence layers. Target author: a model. Humans read intent (declarations, plans, justifications, specifications) and review through models; manual verification is reserved for critical code ([ADR 0032](../../adr/0032-logos-core-composition.md) Context). Consequences:

- Declarations a human would find ceremonial are cheap. Verification density is a productivity multiplier, not a DX cost.
- Rust is the reference semantics for the core. Every difference from Rust is registered in [DIVERGENCES.md](../../DIVERGENCES.md): §A blessed (replaced, deliberate model, addition), §B catch-up TODO. "Defer" is not a classification.
- rustc is the oracle for a disputed core program; C++ Memoria is the oracle for container semantics; Soufflé is the oracle for Datalog evaluation.
- Pre-release (version 0.63.0-preview): compatibility is broken freely; the ABI slot is `X.Y[-preview]`, a minor bump is an ABI break.

Implementation constraints:

- The compiler `logosc` is C++23, built with clang 20 and `-fno-rtti`, against LLVM/MLIR. The generated parser does not build with GCC ([AGENTS.md](../../../AGENTS.md)).
- `logosc` emits one object file. Linking is done by the caller (`cc`, or [lforge](../../internals/lforge.md)).
- The stdlib is written in Logos; a small runtime in C and assembly sits under it.

## 2. Model: the layers

The composition agreed on 2026-10-10 ([ADR 0032](../../adr/0032-logos-core-composition.md)). "Logos Core" = Rust-core + Deem + Writ + Hest; Memoria and Canon are clients of Core, not part of it.

| Level | Component | Role | State |
|---|---|---|---|
| what | Deem | relational specification: rules, queries, invariants, incremental views | landed as an item; incremental tier shape-specific |
| how | DPlan | Deem's plan IR: access paths, join order, materialisation, Δ-translation, each decision with its ground | landed ([ADR 0031](../../adr/0031-deem-core-layer.md)) |
| representations | Rust-core | data structures declaring which relational ops they execute at what cost and exactness; sequential kernels as operators | landed (the compiler) |
| between radii | Hest | dataflow graph whose vertices are Rust-core fns or Deem circuits; control flow inside a synchrony radius, dataflow between radii | PROPOSED, no code |
| at rest and on the wire | Writ | self-describing, relocatable values, documents, IR carriers | landed |
| periphery | Memoria, Canon | Memoria: persistent containers and stores with CoW snapshots; Canon: container factory and its facts | landed (wave 0) |

Hardware-coherent multithreading stays in Rust-core. Without cache coherence, programs move to the distributed, Datalog-friendly model: by CALM, monotone strata need no coordination.

Direction decisions of ADR 0032 (all PROPOSED, rows C0-C8 not started):

- D1. One type system: Logos types. The EL lattice is retired; a Writ schema is a Logos type with a self-describing layout; `RtVal` survives only as the dynamic boundary (FFI, config, wire).
- D2. One IR ladder per layer, typed in Logos. Rust-core: AST → HIR → L-IR → BIR → MLIR. Deem: surface → Core → DPlan. Hest: a graph IR shared with DPlan. Writ carries IR across process and persistence boundaries only.
- D3. Seams pass typed values; no text protocols. Emission goes through quotes with antiquotes.
- D4. A data structure's relational interface is stated once, in its source declaration; Canon's facts derive from it.
- D5. `deem` usable inside fn bodies.
- D6. Compiler facts (call graph, effects, capabilities, borrow facts, types, impls) exported as Deem relations (`--emit-facts`).
- D7. One persistent stack: `logos.mem.bt`. The parent snapshot is the z⁻¹ of incremental Deem.
- D8. Tiers become effect bounds checked in sema.
- D9. Canon and Memoria add no language rules; a rule they need becomes a general Core mechanism.

The measured state behind these decisions (four overlapping type systems, text seams, one fact stated twice, two persistent stacks) is in ADR 0032 "Measured state". Section 6 below lists what is true today.

## 3. Mechanism

### 3.1 Source tree

| Path | Content | Size (2026-10-10) |
|---|---|---|
| [src/compiler/](../../../src/compiler/) | `logosc` | ≈159 kLoC C++ (excluding `probes/`) |
| [include/logos/compiler/](../../../include/logos/compiler/) | compiler headers: L-IR, sema, DefId table, unit graph | ≈10.5 kLoC |
| [src/writ/](../../../src/writ/), [src/jit/](../../../src/jit/), [src/hrpc/](../../../src/hrpc/), [src/reactor/](../../../src/reactor/) | C++ libraries: Writ arenas and codecs, ORC JIT, RPC, reactor | ≈6.4 kLoC |
| [stdlib/](../../../stdlib/) | Logos stdlib in four tiers `lang`, `mem`, `lcm`, `std`; C/asm runtime in `rt` | ≈121 kLoC Logos + ≈1.6 kLoC C/asm |
| [conuco/](../../../conuco/) | in-tree incubator outside the published ABI (`memoria`, `csd`) | |
| [tools/](../../../tools/) | parser generators (`peg_gen_cpp`, `peg_gen_logos`), `lforge`, `dlog` audit queries, `hrpc_gen`, `docgen` | |
| [tests/](../../../tests/) | corpus, imported rustc tests, spec tests, soundness queue, oracles | ≈27 k files |
| [scripts/](../../../scripts/) | `lt`, ledger sync, ABI checks, shadow-run tools | |
| [abi/](../../../abi/) | `logos.abi`, the generated ABI surface; its git history is the ABI changelog | |

### 3.2 The compiler: a fixpoint around a pipeline

The stages are the classic ones. The driver is not: it runs nested loops until no stage can make progress, then reports what was never produced. Entry: `main` in [main.cpp](../../../src/compiler/main.cpp).

```
parse modules (PEG) ──► AST (Writ arena)
        │
        ▼
 ┌─ sema + lowering ──► L-IR ◄──────────────────────────────┐
 │    (HIR desugar on the way in; unmet demand → Pending)    │
 │                                                           │
 │  metaprog dispatch      ≤16 rounds  handlers, hooks       │
 │  deem plan loop         ≤8 rounds   __deem_bind           │ re-sema
 │  metacall loop          ≤16 rounds  JIT thunks, splice ───┘
 │
 ├─ reflection emit
 ├─ borrow check (generic templates)
 ├─ mono (worklist) ──► factory demands ──► container factory drain ≤3 rounds
 ├─ Pending report: every demand never met is an error
 ├─ borrow check (concrete fns)
 └─ MLIR (func/arith/scf/cf/llvm) ──► LLVM ──► object file
```

Parser. A PEG grammar, [logos.peg](../../../tools/peg_gen_cpp/grammars/logos.peg) (≈4.1 k lines), is compiled by `peg_gen_cpp` into `LogosParser` (≈75 k generated lines). No separate lexer; token rules are in the grammar. Deem and EL bodies have their own grammars under [stdlib/mem/wql/grammars/](../../../stdlib/mem/wql/grammars/).

AST. Not C++ structs: nodes are Writ `TinyObjectMap` entries with node codes and field keys generated into `ast.hpp` (keys 0..51). Every node carries `SRC_SPAN` (offset<<23 | length); HIR-synthesised nodes carry `ORIGIN`; path nodes carry their resolution in `RES`.

HIR. A persistent Writ→Writ rewrite, [`hir::Lowering`](../../../src/compiler/hir_lower.hpp), that desugars surface forms before sema reads them: if-let, let-chains, while-let, while → loop, field shorthand, destructuring assignment, format/print macros, `matches!`, `dbg!`, labelled blocks, label and exit resolution. Untouched subtrees are shared. A surface form reaching sema is an internal error. [ADR 0030](../../adr/0030-hir-core-layer.md): H0, S0-S8, L0, R0, Q1 landed; S9b, S10 open; the ADR's header still says DRAFT.

Sema. `sema_lower` type-checks and lowers to L-IR in one pass; class `SemaChecker` ([sema_impl.hpp](../../../src/compiler/sema_impl.hpp)), phases Init, Collect, Lower. Bodies are typed in [sema_expr.cpp](../../../src/compiler/sema_expr.cpp) (≈28 kLoC), [sema_stmt.cpp](../../../src/compiler/sema_stmt.cpp), [sema_decl.cpp](../../../src/compiler/sema_decl.cpp). One `TypePool` lives across all sema runs of a compile (`SemaCache`); a `TypeRef` is a handle into a Writ arena.

Identity. [`DefId`](../../../include/logos/compiler/def_table.hpp) is a stable FNV-1a hash of (namespace, package, name), the analogue of rustc's DefPathHash. `DefTable` aborts on collision. Lang items map to DefIds.

Trait selection. [`obl::select`](../../../src/compiler/obligation.hpp) is the one answer to "does `Self: Trait` hold", shared by sema and mono (ADR 0030 S9 C-OBL). Datalog trait resolution (ADR 0028 Slice T) is not landed.

L-IR. The first fully typed IR. C++ node types are handles over a Writ mirror (`LExprPtr = lir_view::ExprRef`); writers in [lir_mirror.cpp](../../../src/compiler/lir_mirror.cpp), builder [`LirBuilder`](../../../include/logos/compiler/lir_builder.hpp), container [`LProgram`](../../../include/logos/compiler/lir.hpp). Producer: sema; mono rewrites it; consumers: borrow checker, reflection, unit graph, MLIR generation, archive serialisation. Because L-IR lives in Writ arenas, a precompiled stdlib archive ships its post-mono L-IR and the consumer references it across arenas instead of re-lowering ([multi-arena-ir.md](../../internals/multi-arena-ir.md)). Arena ceiling: 4 GB (32-bit offsets).

The fixpoint. A stage that needs something not produced yet records a [`Pending`](../../../include/logos/compiler/lir.hpp) entry (`kind`, `what`, `who`, `where`) via `note_pending_` instead of failing: an emitted type, a container family, a Deem plan. A later loop produces it and sema re-runs. Loop bounds: `kMaxMetaprogIters` = 16, `kMaxPlanIters` = 8, `kMaxMetacallIters` = 16, `kMaxFactoryDrainRounds` = 3. When nothing moves, every remaining `Pending` becomes an error naming who needed what. Rule: the producer runs where the demand is. Inside mono the same shape repeats: a worklist, deferred method enqueues, vtable demand. [`UnitGraph`](../../../include/logos/compiler/unit_graph.hpp) partitions the program into Source, Generated and Common units so a metafunction's provider builds before its consumer; it is emitted (`--emit-units`) but nothing consumes it yet.

Metaprogramming. Compile-time computation beyond arithmetic goes through `metacall`; there is no `const fn` and no const-eval (DIVERGENCES §A). Sema records `metacall_sites`; the driver emits a `__metacall_thunk_<id>` per site, JIT-compiles it with ORC ([`Jit`](../../../include/logos/jit/jit.hpp)), runs it, splices the result into the AST by offset, re-runs sema. Code is produced with quotes (`quote_item!`, quote expressions and types) and antiquotes (`#ident`, type and pack antiquotes, `#(…)sep*` repeat groups). Standing rule: quotes cover 100 % of codegen; text emission is a workaround to be retired (ADR 0032 C3 counts ≈672 text-building calls left in the Deem emitter). The JIT'd code calls back into the compiler through `extern "C"` host functions in `main.cpp`: `logos_emit_source`, `logos_quote_expr_subst`, `logos_parse_as`, `logos_metaprog_gensym`, `logos_metaprog_error`. `#[metaprog_handler]` functions ([ADR 0002](../../adr/0002-target-driven-pre-sema-hooks.md)) run in the dispatch loop. Logos-side metaprog libraries: [stdlib/mem/compiler/metaprog/](../../../stdlib/mem/compiler/metaprog/), shipped `ast_only`. `--gen-dir` writes generated sources as `.gen.logos`.

Borrow checking. Runs twice: over generic templates before mono, over concrete fns after mono. The authoritative checker is the Datalog one ([ADR 0028](../../adr/0028-datalog-engine-borrowck-traits.md) S7, default since `LOGOS_DL_BC` stopped being opt-in):

- Facts come from BIR, a MIR-like linearisation of L-IR, [borrow_bir.inc](../../../src/compiler/borrow_bir.inc) (`bir_check`).
- Rules are Datalog, [polonius.dl](../../../src/compiler/dl/rules/polonius.dl) and [logos_bc.dl](../../../src/compiler/dl/rules/logos_bc.dl), embedded into the binary; engine `logos_dl` in [src/compiler/dl/](../../../src/compiler/dl/).
- Diagnostics use rustc's wording; the corpus is pinned to it.
- A fn BIR cannot lower yet falls back to the old flow-sensitive checker in [borrow_check.cpp](../../../src/compiler/borrow_check.cpp) rather than going unchecked (`dl_bc_live`). In a library build an uncheckable generic template is an internal error.
- `LOGOS_DL_BC=0` restores the old checker (bisect only; it fails ≈800 fixtures by text). `LOGOS_DL_SHADOW=bc` runs both and logs disagreements; tools in [scripts/bc-shadow/](../../../scripts/bc-shadow/).
- Rule from the ADR 0028 arc: BIR facts come from sema via L-IR, never inferred from shapes or names.

Monomorphization. `mono_pass` ([mono.hpp](../../../include/logos/compiler/mono.hpp)) clones generic templates per type-argument set over a worklist; `MonoOpts::entry_points` restricts to roots, `prev_out` extends a previous run. Instantiating a `CtrClass<CFG>` marker records a `FactoryDemand`; the driver drains these by running the container factory as a metacall (section 3.6).

Codegen. [`mlir_gen`](../../../src/compiler/mlir_gen.hpp) emits standard MLIR dialects only (func, arith, scf, cf, llvm, DLTI); there is no custom Logos dialect. [`lower_and_emit_object`](../../../src/compiler/compile_pipeline.hpp) lowers to the LLVM dialect, runs LLVM, writes the object. Options: `-O0..3`, `-C target-cpu=`, `-C overflow-checks=`, `-g` (DWARF), `--emit-mlir`, `--emit-llvm`, sharding. The JIT is used only for metaprogramming; there is no user-facing JIT run.

Modules and archives. `logosc --emit-module <manifest>` builds `libNAME.a` = `NAME.o` + `NAME.writ0` (binary AST and L-IR) + ABI sidecars ([emit_module.cpp](../../../src/compiler/emit_module.cpp)). A manifest may declare `lowering lazy` (ship AST only, the consumer lowers on use). Symbols are mangled by module id; same-named packages from different modules coexist. Normative rules: [docs/spec/modules.md](../../spec/modules.md).

### 3.3 Stdlib tiers

| Tier | Root | Adds | Archive |
|---|---|---|---|
| lang | [stdlib/lang/](../../../stdlib/lang/) | static memory only ("synthesizable") | `liblogos-lang.a` |
| mem | [stdlib/mem/](../../../stdlib/mem/) | heap allocation | `liblogos-mem.a` |
| lcm | [stdlib/lcm/](../../../stdlib/lcm/) | virtualised IO, threads, fibers | `liblogos-lcm.a` |
| std | [stdlib/std/](../../../stdlib/std/) | processes, concrete OS (io_uring, pthread) | `liblogos-std.a` |

Dependency order lang ← mem ← lcm ← std. The manifest parser accepts exactly these four ([module_manifest.cpp](../../../src/compiler/module_manifest.cpp)). Tiers are enforced at link level only and `extern fn` bypasses them; ADR 0035 proposes them as effect bounds. Core and std types were ported from Rust's core/std; provenance in [docs/core-port/](../../core-port/).

### 3.4 Deem

Deem is the relational sublanguage: Datalog with joins, aggregation, recursion, mappings over graphs, compiled to native code. Normative: [docs/spec/deem.md](../../spec/deem.md). Engine code: [stdlib/mem/wql/](../../../stdlib/mem/wql/) (≈45 kLoC; package names `logos.std.wql.*`).

Surface. A `deem` item, `pub? deem q(params) { rel …; entry query }`, is a prepared statement: it compiles to `fn q(params) -> Ret`. Slice parameters are sources, scalar parameters are referenced bare. Query shapes: simple, join (incl. anti-join), group/aggregate with `having`, `find`. A `mapping` item declares relations over a Writ graph and is consumed only through a `deem`. Example ([wql_rel_col_literal_type_e2e.logos](../../../tests/logos/pass/wql_rel_col_literal_type_e2e.logos)):

```
pub deem q(ss: &[S]) {
    rel r(a: i64, b: u128, c: i32, d: u8) { from ss s select (s.id, 0, -1, 255); }
    from r x select (x.a, x.b, x.c, x.d)
}
```

`deem` binds only at item position ([`lower_deem_def`](../../../src/compiler/sema_expr.cpp)); the `deem!` macro is retired; a fn-body form is ADR 0032 D5.

Pipeline ([ADR 0031](../../adr/0031-deem-core-layer.md), IMPLEMENTED 2026-10-09):

1. Sema owns name, params, visibility; it passes the body to the `#[token_macro]` handler `deem` in [wql.logos](../../../stdlib/mem/wql/wql.logos). Container binding goes through the `__deem_bind` metacall ([deem_bind.logos](../../../stdlib/lcm/wql/deem_bind.logos)). Both seams are text today (ADR 0032 C2).
2. Surface `RQuery` (a Writ schema from the generated parser) is lowered to Core: Logos types `CProgram`, `CRule`, `CAtom`, … in [core.logos](../../../stdlib/mem/wql/core.logos), verified by `core_verify`. Nothing below lowering reads surface types; [deem_layer_lint.sh](../../../tests/logos/deem_layer_lint.sh) enforces it.
3. Core → Core rewrites in [crewrite.logos](../../../stdlib/mem/wql/crewrite.logos): fact desugaring, liveness, magic sets, graph-path desugaring, fusion. Typing by the Logos type system ([typecheck.logos](../../../stdlib/mem/wql/typecheck.logos)).
4. Planning to DPlan ([dplan.logos](../../../stdlib/mem/wql/dplan.logos)): scans, nests, runs, SCCs (recursive components), `DIncr` for incremental admission. Join order and selectivity in [join_order.logos](../../../stdlib/mem/wql/join_order.logos), [join_cost.logos](../../../stdlib/mem/wql/join_cost.logos). Recursion: semi-naive SCC drivers; DRed for retraction.
5. Emission of Logos fns from DPlan ([rexpr_walk.logos](../../../stdlib/mem/wql/rexpr_walk.logos), [codegen.logos](../../../stdlib/mem/wql/codegen.logos), [plan_walker.logos](../../../stdlib/mem/wql/plan_walker.logos)).

Sources are traits, not a fixed storage. The batch-cursor plane ([ADR 0025](../../adr/0025-deem-batch-cursor-plane.md)) defines capabilities in [stream.logos](../../../stdlib/lang/stream/stream.logos): `BatchStream`, `Rewind`, `SizedStream`, `OrderedBy`, `Landed`, `Bidirectional`, `RandomAccess`. A Memoria container is a source by implementing them.

Incremental maintenance exists as a compiled per-shape handle (≈4.7 k lines, 24 admission grounds). The ADR 0013 DBSP engine lived in the interpreter and was deleted with it at P5 (`e1dd0ac5e`, 2026-08-09). [ADR 0033](../../adr/0033-incremental-deem-dbsp-translator.md) (PROPOSED) replaces the per-shape handle with a DPlan → DPlan translation (`Delay`, `Integrate`, `Differentiate`, `Distinct^Δ`).

EL is the CEL-class expression language shared by Deem clauses and Trama; its value lattice (`EL_TY_*`) is the type system ADR 0032 D1 retires.

### 3.5 Writ

Writ (formerly Hermes, [ADR 0010](../../adr/0010-rename-hermes-to-writ-hest.md)) is the self-describing data format and arena substrate. Normative: [docs/spec/writ.md](../../spec/writ.md).

- `WAny`: one 8-byte niche-packed word, `Ref(*const u8)` or `Pod(u64)` (i56 payload + 7-bit code) ([anyval.logos](../../../stdlib/lang/writ/anyval.logos)).
- Two forms: at rest with self-relative refs, as values with absolute refs; the compiler owns the bridge.
- `schema S { field: T = N, … }`: a typed view over a `WMap` with keys 0..51; field access desugars through `WritField` ([wmap.logos](../../../stdlib/lang/writ/wmap.logos)).
- Data literals `@{…}`, `@[…]`, `@42`, `@null` with `$`-captures; a capture-free literal becomes a `WritStatic` rodata blob.
- Runtime: Logos side [stdlib/lang/writ/](../../../stdlib/lang/writ/), [stdlib/mem/writ/](../../../stdlib/mem/writ/); C++ side [src/writ/](../../../src/writ/).

Writ is also the compiler's memory: the AST, the type pool and L-IR live in Writ arenas, and `.writ0` archive members are Writ documents. The same format is the configuration language of Memoria (`WritStatic` configs) and the manifest language of lforge (`lforge.writ`).

### 3.6 Memoria and Canon

Memoria is a C++ framework for persistent, snapshot-versioned containers (B+trees with CoW, packed node layouts, memory and SWMR stores), being ported into Logos ([ADR 0018](../../adr/0018-memoria-port.md)). Thesis: Logos is Memoria in the form of a programming language. Writ replaces Hermes, metaprogramming replaces the C++ template framework and its `mbt` codegen tool, Deem is new on top. C++ Memoria is a semantics reference and test oracle; byte compatibility is not a goal, except the SSRLE codec, which mirrors the C++ encoder bit-exactly.

Container declaration ([map.logos](../../../conuco/memoria/src/ctr/map.logos)):

```
pub container Map<K, V> {
    kind ordered_map;
    entry { key: K, val: V }
    measure max(key);
}
```

A declaration does not name storage. It lowers ([ADR 0021](../../adr/0021-metaclasses-mono-seam.md)) to a `WritStatic` config document, a transparent alias `type Map<K,V> = CtrClass<CFG>`, declaration-time facts, and a factory that runs when mono demands the family. Identity is the content hash of the config: equal configs are one family. `container` is the only metaclass; there is no user-facing metaclass syntax. Kinds today: `vector`, `ordered_map`, `multimap`, `node`, `branch` ([vocab.logos](../../../stdlib/lcm/canon/vocab.logos)).

Interface model ([ADR 0020](../../adr/0020-memoria-container-interface-plane.md)): three axes, element contract, kind semantics (traits with laws), measures (monoid annotations, finger-tree style). Find, rank, select, range and seek are one operation: descent by a predicate over measures.

Canon ([canon.logos](../../../stdlib/lcm/canon/canon.logos)) is a Deem program over facts mirrored from declarations; it judges what a container can do (`can_scan`, `can_rank`, `can_seek`, column mode, missing measures). Rule: Canon never emits, metaprogramming never decides. The factory that emits is [container_item.logos](../../../stdlib/lcm/canon/container_item.logos). ADR 0032 D4 notes the capability fact is stated twice today (Canon verdicts and the emitted source impl).

Container ⟂ store. Containers are generated against a storage capability; stores are hand-written. [memstore.logos](../../../stdlib/mem/bt/memstore.logos) is the memory store: a `MemorySnapshot` holds a directory `ctr_id → (root, level)`, so one snapshot spans many containers and commits them atomically. Nodes carry intrusive refcounts; `cow_needed` decides copy-on-write. Boundary traits `Store`, `Snapshot`, `Ctr` in [map.logos](../../../stdlib/mem/bt/map.logos). Store and snapshot handles are cross-thread (`Arc`); container handles are thread-local (`Rc`). The profile is data, not a type ([storecfg.logos](../../../stdlib/mem/bt/storecfg.logos)).

Substrate: packed layouts in [stdlib/mem/pkd/](../../../stdlib/mem/pkd/) (packed allocator, `PackedDataTypeBuffer`, SSRLE codec and symbol sequence); B+tree layers in [stdlib/mem/bt/](../../../stdlib/mem/bt/) (single-stream `btss`, multi-stream free-layout `btfl`, node CRUD algebra of [ADR 0023](../../adr/0023-node-stream-crud-algebra.md)).

Not built: the SWMR store, the `ctr_type_hash` registry and `open_dyn`, the queue kind, row-store tables ([ADR 0027](../../adr/0027-row-store-tables.md)). The second persistent stack from [ADR 0017](../../adr/0017-deem-storage-engine.md) (`logos.lcm.deem.data`) was deleted at ADR 0039 M4b; its DView containers live on as a Canon family. [ADR 0039](../../adr/0039-persistent-snapshot-structures.md) (PROPOSED): persistent snapshottable structures as one logical level over many implementations.

### 3.7 Trama

A Jinja2-derived template language ([docs/spec/trama.md](../../spec/trama.md)). Static form `resource NAME = trama!(params){ body }` lowers by metacall to `fn NAME(params) -> String` ([trama_render.logos](../../../stdlib/mem/wql/trama_render.logos)); a dynamic engine compiles template text at run time. Expressions inside `{{ }}` and tags are EL.

### 3.8 Hest

Hest is the dataflow layer: the unit of composition above the fn is the operator graph; control flow is an optimisation valid inside a synchrony radius ([ADR 0034](../../adr/0034-hest-core-graph-layer.md), PROPOSED). The graph is a language construct. Actors are rejected. Proposed: `Stream<T>` by epoch, `Delta<T>` from ADR 0033, a graph IR shared with DPlan, Kahn-network determinism with `Merge` as the only nondeterministic node, checks written as Datalog over graph facts. Exists today, as substrate only: C++ HRPC ([src/hrpc/](../../../src/hrpc/)), fibers, `Chan<T>` and `Latch` ([sync.logos](../../../stdlib/lcm/fiber/sync.logos)), the per-thread reactor ([reactor.logos](../../../stdlib/std/rt/fiber/reactor.logos)).

### 3.9 Runtime and memory

- Native runtime [stdlib/rt/](../../../stdlib/rt/): context switch [fiber_ctx.S](../../../stdlib/rt/fiber_ctx.S) (own archive, kept out of the metaprog JIT because its TLS relocations do not resolve there), atomics, io_uring wrapper, cpuid-dispatched pdep/pext fallback, clocks.
- Fibers: stackful, cooperative, written in Logos ([fiber.logos](../../../stdlib/lcm/fiber/fiber.logos)); no coloured functions, every fn is implicitly suspendable (DIVERGENCES A4). `#[may_suspend]` was never implemented; ADR 0035 proposes a `Suspend` effect.
- std: a Seastar-style per-thread reactor, each worker with a scheduler, an io_uring ring and an SPSC inbox.
- Memory: Rust-style ownership and RAII (drop flags, sema-inserted drops, one malloc/free pair in codegen) plus Writ regions (arena allocation, offset addressing, the whole region freed at once). Audit: [ANALYSIS.md](../../memory-management/ANALYSIS.md). Zoned types (zone as a generic parameter) are designed, not implemented ([zone-as-parameter.md](../../internals/zone-as-parameter.md)).

### 3.10 Toolchain

- lforge ([tools/lforge/](../../../tools/lforge/), written in Logos): builds packages from `lforge.writ` manifests, git dependencies with minimal version selection and a lock file, content-addressed build cache, links with `cc`. No central registry.
- Versioning: the version and ABI slot are set in the root `CMakeLists.txt`; `logosc --emit-abi` regenerates [logos.abi](../../../abi/logos.abi); [abi-check.sh](../../../scripts/abi-check.sh) gates the bump. Archive rule: same major, library minor ≤ compiler minor.

### 3.11 Verification process

- `lt` ([scripts/lt](../../../scripts/lt)) discovers ≈12 k tests from the tree ([lt_discover.py](../../../scripts/lt_discover.py), data in [lt_registry.py](../../../tests/lt_registry.py)); ctest is retired. Levels per task: L0 = the task's tests, L1 = their groups ([groups.rules](../../../tests/groups.rules)), L2 = the rest; disjoint, together the whole suite. Results in `build/testdb.sqlite`. Series: a branch of same-kind tasks, sampled runs per task, whole suite at close.
- Corpus: [tests/logos/pass/](../../../tests/logos/pass/) (≈4.7 k) must compile, run and match `.expected`; [tests/logos/fail/](../../../tests/logos/fail/) (≈2.0 k) must fail with the expected diagnostic. [tests/imported/](../../../tests/imported/) (≈4.6 k) are translated from rustc's suite with provenance. [tests/spec/](../../../tests/spec/) (610) are extracted from the spec.
- Soundness queue: [tests/soundness/open/](../../../tests/soundness/open/) holds programs the compiler gets wrong today; the gate fails in both directions (a row that starts behaving, a program without a row). Open work lives in GitHub issues; the ledgers under `tests/logos/` are generated from them by [issues-to-ledger.py](../../../scripts/issues-to-ledger.py).
- Lints pin claims in docs to code: [spec_path_lint.sh](../../../tests/logos/spec_path_lint.sh) for spec evidence, [probe-log-lint.py](../../../scripts/probe-log-lint.py) for probe sites, [book_lint.py](../../../scripts/book_lint.py) for this book.

## 4. Alternatives and prior art

| Choice | Alternative | Reason |
|---|---|---|
| Rust semantics for the core, rustc as oracle | own core semantics | an independent oracle for every disputed program; divergences are explicit and registered |
| `metacall` + JIT for compile-time code | `const fn` / const-eval | one mechanism (compiled Logos) instead of an interpreter beside the compiler |
| quotes with antiquotes | text emission and reparse | typed seams; text emission is counted and driven to zero |
| fixpoint with `Pending` and a final report | fixed pass order | generated items, container families and plans are demanded by later stages; a fixed order needs either speculation or a phase per feature |
| Datalog (Polonius-style) borrow rules over BIR | hand-written flow-sensitive checker | rules are data, checkable against an oracle; the old checker remains only as fallback |
| standard MLIR dialects | a custom Logos dialect | none built; Logos-specific lowering happens before MLIR |
| Deem compiled to native fns (prepared statements) | a query interpreter | the interpreter was deleted at P5; one execution tier |
| Soufflé as oracle, not as engine | embedding Soufflé | Deem must compile to Logos code over Logos containers; Soufflé checks evaluation results |
| container declaration without storage; content-hashed families | C++ Memoria template framework, api/impl firewall, type erasure | metaprogramming and mono replace typelists and mixins; no compilation firewall needed |
| stackful fibers, no coloured fns | async/await | Rust divergence §A4; suspension becomes an effect (ADR 0035) instead of a colour |
| Hest as a graph construct | actor model | determinism (Kahn networks), checkable graph facts |

## 5. Evidence

- rustc oracle audit, 2026-09-16 ([rustc-fixture-audit-2026-09.md](../../rustc-fixture-audit-2026-09.md)): 786 fixtures judged legal by reading were measured against Rust twins: 752 confirmed, 6 contradicted, 23 divergences, 5 not expressible.
- Deem against Soufflé: 223 oracle fixtures byte-identical at ADR 0031 R1 ([souffle_oracle_gate.sh](../../../tests/logos/souffle_oracle_gate.sh)).
- Container seek parity (a seek through the generated projection vs a direct `find`): 1.32× instructions against a 1.45× ceiling, 2026-10-06 ([ctr_point_parity_gate.sh](../../../tests/logos/ctr_point_parity_gate.sh)).
- Borrow checker switch: the corpus is pinned to the Datalog checker's diagnostics; the old checker agrees on verdicts and fails ≈800 fixtures by wording.

## 6. Status and known drift

What is true at `verified-at` and contradicts another document:

| Document | Says | Code |
|---|---|---|
| [docs/internals/architecture.md](../../internals/architecture.md) | pipeline without HIR/BIR; borrow check before mono; flow-sensitive checker | this article §3.2 |
| [ADR 0028](../../adr/0028-datalog-engine-borrowck-traits.md) status line | "ACCEPTED as direction" | S7 landed: Datalog checker is the default |
| [ADR 0030](../../adr/0030-hir-core-layer.md) header | "nothing implemented" | H0, S0-S8, L0, R0, Q1 landed |
| [deem.md](../../spec/deem.md) `deem.exec.incremental` | withdrawn | a compiled incremental handle exists |
| [modules.md](../../spec/modules.md) `module.manifest.tier-closed-set` | lang, mem, std | four tiers incl. lcm |
| [ctr.logos](../../../conuco/memoria/src/ctr/ctr.logos) comment | "Declaration sugar comes later" | `container` items exist |
| [TRADEMARKS.md](../../../TRADEMARKS.md) | Trama = query engine; `deem!` blocks | Trama is the template language; `deem!` retired |

Open directions, all PROPOSED: ADR 0032 rows C0-C8 (composition), [0033](../../adr/0033-incremental-deem-dbsp-translator.md) (incremental Deem), [0034](../../adr/0034-hest-core-graph-layer.md) (Hest), [0035](../../adr/0035-effects.md) (effects), [0036](../../adr/0036-object-capabilities.md) (capabilities), [0037](../../adr/0037-specifications-and-smt.md) (specifications, SMT), [0038](../../adr/0038-lean-integration.md) (Lean). Priority: 0033 with the seam rows first, then 0035 → 0036, then 0037 → 0038.

Names without code: Nous (ontology layer over Deem and Writ, prose only in ADRs 0016, 0020, 0021).
