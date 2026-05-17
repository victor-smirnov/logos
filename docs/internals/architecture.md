# Compiler Architecture

The Logos compiler is `logosc`, a single executable built from [src/compiler/](../../src/compiler/). It is a conventional pipeline: source → AST → typed AST → MLIR → LLVM IR → object → linked native binary.

## Pipeline

```
.logos source
    │
    │  PEG parser (generated from tools/peg_gen)
    ▼
AST
    │
    │  sema_collect / sema_decl / sema_expr / sema_stmt / sema_auto_trait
    ▼
typed AST
    │
    │  borrow_check
    ▼
borrow-checked typed AST
    │
    │  mono (mono_scan / mono_subst / mono_clone / mono_impl)
    ▼
monomorphized program
    │
    │  mlir_gen (mlir_gen_fn / mlir_gen_expr / mlir_gen_stmt / mlir_gen_types / mlir_gen_dyn)
    ▼
MLIR module
    │
    │  emit_module → LLVM lowering pipeline
    ▼
LLVM IR
    │
    │  LLVM optimization + codegen
    ▼
object file
    │
    │  link with stdlib/rt + Hermes runtime
    ▼
native binary
```

## Component Map

| Stage | Files |
|-------|-------|
| Parser | Generated from `tools/peg_gen`, hosted in `src/compiler/` |
| Semantic analysis | [sema.cpp](../../src/compiler/sema.cpp), [sema_decl.cpp](../../src/compiler/sema_decl.cpp), [sema_expr.cpp](../../src/compiler/sema_expr.cpp), [sema_stmt.cpp](../../src/compiler/sema_stmt.cpp), [sema_collect.cpp](../../src/compiler/sema_collect.cpp), [sema_auto_trait.cpp](../../src/compiler/sema_auto_trait.cpp) |
| Borrow checking | [borrow_check.cpp](../../src/compiler/borrow_check.cpp) |
| Monomorphization | [mono.cpp](../../src/compiler/mono.cpp), [mono_scan.cpp](../../src/compiler/mono_scan.cpp), [mono_subst.cpp](../../src/compiler/mono_subst.cpp), [mono_clone.cpp](../../src/compiler/mono_clone.cpp) |
| MLIR generation | [mlir_gen.cpp](../../src/compiler/mlir_gen.cpp) and the per-shape `mlir_gen_*.cpp` files |
| Module loading | [module_loader.cpp](../../src/compiler/module_loader.cpp), [module_manifest.cpp](../../src/compiler/module_manifest.cpp) |
| Reflection emission | [reflection_emit.cpp](../../src/compiler/reflection_emit.cpp) |
| Driver | [main.cpp](../../src/compiler/main.cpp) |

The compiler is currently written in C++23 and built with `-fno-rtti`. It depends on LLVM/MLIR development packages.

## Semantic Analysis

Sema runs in passes that mostly correspond to the file split:

1. **Collect** — index all top-level declarations across the loaded modules so name resolution is order-independent.
2. **Decl** — type-check declarations: structs, enums, function signatures, trait declarations, trait impls.
3. **Expr / Stmt** — type-check function bodies. Expression typing produces a fully annotated tree.
4. **Auto trait** — derive marker traits (planned: `Send`/`Sync`-style) from field types.

Throughout, sema works in terms of `TypeRef` accessors rather than raw type pointers. The migration to this abstraction is in progress (Phase 2c.4e), with the larger files — `sema_expr`, `sema_stmt`, `mlir_gen_*`, `mono_impl` — still on the to-do list.

## Borrow Checker

The borrow checker is flow-sensitive and runs after sema. It tracks ownership, exclusivity, provenance, and lifetimes, including named lifetimes on function signatures. Its state has gone through four major phases (exclusivity, provenance, named lifetimes, escape analysis) and is exercised by hundreds of tests.

## Monomorphization

Monomorphization walks reachable concrete instantiations of generic functions and types and substitutes type parameters to produce specialized definitions. The output is a flat program with no remaining type parameters. `mono_clone` deep-copies AST nodes during substitution; `mono_scan` discovers new instantiations triggered by previous ones.

## MLIR Generation

`mlir_gen` lowers the monomorphized AST to MLIR using a mix of standard dialects and Logos-specific operations. The split is:

- `mlir_gen_types` — type lowering (primitives, structs, enums, references, fat pointers).
- `mlir_gen_fn` — function/method skeleton.
- `mlir_gen_expr`, `mlir_gen_stmt` — expression/statement lowering.
- `mlir_gen_dyn` — runtime polymorphism (tag dispatch, trait objects).

`emit_module` lowers MLIR to LLVM IR and runs the LLVM pipeline.

## Runtime Linkage

Compiled binaries link against:

- `stdlib/rt` — language runtime: allocator hooks, panic handling, drop glue, fiber scheduling primitives.
- `stdlib/std` — standard library written in Logos itself, compiled and linked alongside user code.
- The Hermes runtime — C++ implementation of zones, codecs, parsers, and the type registry, in [src/hermes/](../../src/hermes/).
- LLVM/MLIR support libraries.

## Multi-arena IR

The compiler's intermediate representation (types + LIR mirror) is stored in **Hermes arenas** — bump-allocated, 32-bit-offset, append-only buffers backed by `hermes::MemHolder`. The multi-arena IR refactor (Phases 0-8, see [multi-arena-ir.md](multi-arena-ir.md)) decouples the IR layout from the consuming code paths so the compiler can:

- **Reference IR across module boundaries** via `hermes::ExternalRef` (7-byte payload: 24-bit arena_id + 32-bit obj_id). The reader-side `lir_view` accessors detect ExternalRef AnyVals and dispatch through `hermes::global_arena_pool()`.
- **Skip re-lowering pre-compiled stdlib** on every compile. `liblstdlib.a` ships a full LIR blob (post-mono Hermes arena bytes) under a `.lhermes` ELF section. The user-side `module_loader` registers the blob's arena with the process-global `ArenaPool`; sema's "skeleton" path (`LOGOS_SEMA_USE_BLOB=1`, on by default) skips body lowering for fns whose template body is resolvable via `EXPORTS`; mono walks the foreign arena via `lir_view` and substitutes into the user's local arena.
- **Ship lazy archives** that contain only parsed AST (`lowering lazy` in `logos.module`). The consumer lowers items locally on use; a post-mono reach analysis prunes lazy fns that no non-lazy caller transitively references.

**Component map**
- [`include/logos/hermes/arena_pool.hpp`](../../include/logos/hermes/arena_pool.hpp) — `ArenaPool` registry mapping `arena_id` → `MemHolder*`.
- [`include/logos/hermes/external_ref.hpp`](../../include/logos/hermes/external_ref.hpp) — `ExternalRef` payload + `resolve_external_ref` dispatch.
- [`include/logos/hermes/lir_arena_root.hpp`](../../include/logos/hermes/lir_arena_root.hpp) — `LirArenaRoot` typed object at `DocumentHeader.root_offset`, holding the per-arena `EXPORTS` map (name → obj_id) used by sema's body-skeleton path.
- [`include/logos/compiler/lir_view.hpp`](../../include/logos/compiler/lir_view.hpp) — view-layer over the IR mirror; `detail::make_sub_ref<T>` + `make_child_typeref` propagate the parent's `arena_id` through every cross-arena walk.
- [`src/compiler/sema.cpp`](../../src/compiler/sema.cpp) `TypePool::alloc` / `TypePool::intern_foreign` — single allocation chokepoint; relocates any foreign child `TypeRef` field into the local pool before computing UIDs.
- [`src/compiler/lir_mirror.cpp`](../../src/compiler/lir_mirror.cpp) `LirMirrorEmitter::type_av` — lazy localization at the IR-write boundary.

**Invariants**: see [project_multi_arena_ir.md](../../../../.claude/projects/-home-victor-devel-logos/memory/project_multi_arena_ir.md) for the full refactor log and [memory/invariants.md](../../../../.claude/projects/-home-victor-devel-logos/memory/invariants.md) §I-20–I-23 for the four invariants that govern cross-arena correctness.

The Hermes arena has a hard 4 GB ceiling (32-bit offsets). Above 3.5 GB used, `TypePool::alloc` emits a warning; above 3.9 GB it aborts with a diagnostic pointing at module-splitting as the workaround. Full rolling multi-arena (where one logical module spans multiple `MemHolder`s with cross-arena refs internally) is the Phase 7 deliverable, deferred until a real workload approaches the ceiling — `LOGOS_ARENA_WARN_MB` / `LOGOS_ARENA_ERR_MB` env vars override the thresholds for stress-testing the safety net itself.

## Testing

The test driver is [tests/logos/run_test.sh](../../tests/logos/run_test.sh). Tests come in two families:

- `tests/logos/pass/<name>.logos` + `<name>.expected` — must compile, run, and produce matching stdout.
- `tests/logos/fail/<name>.logos` + `<name>.expected` — must fail compilation with the expected diagnostic shape.

Hermes, HRPC, and reactor each have an `exerciser_*.cpp` family of in-tree C++ programs that exercise the underlying runtime independently of the compiler.

## Planned Restructuring

Two restructurings are in scope, in different time horizons.

**Rust frontend + C++ codegen split (medium term).** Splits the compiler into a Rust frontend and a C++ codegen backend, with Hermes as the shared IR via a zone pointer. Not yet started; intentionally no AST-level FFI between the halves.

**Service-oriented compiler (longer term).** Decomposes the compiler into a set of cooperating services that communicate via Hermes documents. Services may run in-process or as separate processes; the compiler proper becomes an orchestrator over them, embedded in `lforge`'s wider data-platform orchestration. Individual passes (borrow checker, sema queries, monomorphization, …) can be lifted into services in any order. This architecture is what lets user metaprograms compose with built-in passes on equal footing — they speak the same Hermes IR. See [Metaprogramming → Modular, Service-Oriented Compiler](metaprog.md#modular-service-oriented-compiler).
