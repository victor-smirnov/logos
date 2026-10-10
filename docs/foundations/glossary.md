---
title: Glossary
verified-at: 6fa0d2c92
sources: [docs/foundations/overview.md, docs/adr/0032-logos-core-composition.md]
---

# Glossary

Canonical meaning of project terms. One line each; the article that explains the term in depth is linked where one exists. Terms are grouped by layer; within a group, alphabetical.

## Compiler

| Term | Meaning |
|---|---|
| AST | Parser output; Writ `TinyObjectMap` nodes with generated codes and keys, not C++ structs. |
| antiquote | A hole in a quote filled with a value at metaprogram run time: `#ident`, type antiquote, pack antiquote, `#(…)sep*` repeat group. |
| BIR | MIR-like linearisation of L-IR; the source of borrow-check facts ([borrow_bir.inc](../../src/compiler/borrow_bir.inc)). |
| DefId | Stable identity of a definition: FNV-1a hash of (namespace, package, name) ([def_table.hpp](../../include/logos/compiler/def_table.hpp)). |
| factory demand | Record made by mono when it instantiates `CtrClass<CFG>`; the driver drains it by running the container factory. |
| fixpoint (driver) | The compile loop: stages re-run until no `Pending` can be met; the remaining ones are reported as errors. [overview §3.2](overview.md#32-the-compiler-a-fixpoint-around-a-pipeline). |
| HIR | Writ→Writ desugaring layer between AST and sema ([hir_lower.hpp](../../src/compiler/hir_lower.hpp), ADR 0030). |
| L-IR | First fully typed IR; produced by sema, rewritten by mono; C++ handles over a Writ mirror ([lir.hpp](../../include/logos/compiler/lir.hpp)). |
| metacall | Call executed at compile time: the compiler JIT-compiles a thunk, runs it, splices its result. Replaces `const fn`. |
| metaprog handler | `#[metaprog_handler]` fn run by the dispatch loop on target items (ADR 0002). |
| mono | Monomorphization: worklist cloning of generic templates per type-argument set. |
| obligation | A `Self: Trait` question; answered by `obl::select` ([obligation.hpp](../../src/compiler/obligation.hpp)). |
| Pending | An unmet demand recorded instead of a failure (`kind`, `what`, `who`, `where`); met later in the fixpoint or reported. |
| quote | Logos code as a value (`quote_item!`, quote expression, quote type); the only sanctioned way to emit code. |
| `.writ0` | Archive member holding a module's binary AST and L-IR. |
| unit graph | Partition of a program into Source, Generated and Common units with order edges ([unit_graph.hpp](../../include/logos/compiler/unit_graph.hpp)); emitted, not yet consumed. |

## Deem, Writ, Trama, Hest

| Term | Meaning |
|---|---|
| batch-cursor plane | Capability traits a Deem source implements (`BatchStream`, `OrderedBy`, `RandomAccess`, …) ([stream.logos](../../stdlib/lang/stream/stream.logos), ADR 0025). |
| Core (Deem) | Typed Datalog layer between surface and DPlan: `CProgram`, `CRule`, `CAtom`, … ([core.logos](../../stdlib/mem/wql/core.logos)). |
| `deem` item | A prepared query; compiles to a native fn. |
| DPlan | Deem's plan IR: scans, nests, SCCs, incremental nodes ([dplan.logos](../../stdlib/mem/wql/dplan.logos)). |
| DRed | Delete-and-rederive: retraction strategy for recursive incremental views. |
| EL | CEL-class expression sublanguage of Deem clauses and Trama; its type lattice is slated for retirement (ADR 0032 D1). |
| ground | The recorded reason for a plan decision; a decision without a ground is a defect. |
| Hest | Dataflow layer: operator graphs between synchrony radii (ADR 0034, PROPOSED). |
| mapping | Item declaring relations over a Writ graph; consumed only through a `deem`. |
| `rel` | A named relation inside a `deem` or `mapping`, defined by rules. |
| SCC | Strongly connected component of recursive rules; evaluated as one fixpoint plan. |
| schema | Typed view over a Writ map with numeric field keys 0..51. |
| synchrony radius | Region where control flow is valid (shared clock, coherent memory); dataflow crosses between radii. |
| Trama | Jinja2-derived template language; static form `trama!` lowers to a render fn. |
| `WAny` | 8-byte niche-packed Writ value word: `Ref` or `Pod`. |
| Writ | Self-describing, relocatable data format and arena substrate (formerly Hermes). |
| `WritStatic` | Capture-free Writ literal stored as a rodata blob; also the config format of container families. |

## Memoria and Canon

| Term | Meaning |
|---|---|
| Canon | Deem program judging container capabilities from declaration facts; never emits ([canon.logos](../../stdlib/lcm/canon/canon.logos)). |
| container | Metaclass item: `kind`, `entry`, `measure`, `stream`; declares a data structure without naming storage. |
| CoW | Copy-on-write of B+tree nodes under a snapshot; nodes carry intrusive refcounts. |
| `CtrClass<CFG>` | The generic a container declaration aliases to; `CFG` is its config document. |
| family | All code generated for one container config; identified by the config's content hash. |
| kind | The semantic class of a container: `vector`, `ordered_map`, `multimap`, `node`, `branch`. |
| measure | Monoid annotation maintained up the tree; find, rank, select, seek are descents by predicates over measures. |
| snapshot | A consistent version holding a directory `ctr_id → (root, level)`; spans many containers, commits atomically. |
| SSRLE | Run-length symbol-sequence codec; bit-exact with C++ Memoria. |
| store | Owner of snapshots and nodes; hand-written (memory store today), container-agnostic. |

## Runtime and toolchain

| Term | Meaning |
|---|---|
| conuco | In-tree incubator outside the published ABI ([conuco/](../../conuco/)). |
| fiber | Stackful cooperative thread written in Logos; every fn may suspend. |
| lforge | Package and build tool written in Logos; manifests `lforge.writ`. |
| slot | ABI and parallel-install boundary `X.Y[-preview]`. |
| tier | Stdlib layer and capability bound: lang (static memory), mem (+heap), lcm (+virtualised IO, threads, fibers), std (+OS). |

## Process

| Term | Meaning |
|---|---|
| divergence | A registered difference from Rust ([DIVERGENCES.md](../DIVERGENCES.md)): §A blessed, §B catch-up. |
| gate | A test that pins a property of the tree or the compiler (not a program's output); registered in `lt_registry.py`. |
| L0 / L1 / L2 | `lt` levels per task: own tests, their groups, everything else. |
| ledger | Counted list under `tests/logos/`, generated from GitHub issues; a gate holds its `# TOTAL`. |
| lint | A gate over source or docs text, usually with a planted canary or selftest. |
| oracle | An independent judge of correct output: rustc, Soufflé, C++ Memoria. |
| series | A branch of same-kind tasks; sampled runs per task, the whole suite at close. |
| soundness queue | Programs the compiler handles wrongly today ([tests/soundness/open/](../../tests/soundness/open/)); gated in both directions. |
