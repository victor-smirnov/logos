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

## Testing

The test driver is [tests/logos/run_test.sh](../../tests/logos/run_test.sh). Tests come in two families:

- `tests/logos/pass/<name>.logos` + `<name>.expected` — must compile, run, and produce matching stdout.
- `tests/logos/fail/<name>.logos` + `<name>.expected` — must fail compilation with the expected diagnostic shape.

Hermes, HRPC, and reactor each have an `exerciser_*.cpp` family of in-tree C++ programs that exercise the underlying runtime independently of the compiler.

## Planned Restructuring

Two restructurings are in scope, in different time horizons.

**Rust frontend + C++ codegen split (medium term).** Splits the compiler into a Rust frontend and a C++ codegen backend, with Hermes as the shared IR via a zone pointer. Not yet started; intentionally no AST-level FFI between the halves.

**Service-oriented compiler (longer term).** Decomposes the compiler into a set of cooperating services that communicate via Hermes documents. Services may run in-process or as separate processes; the compiler proper becomes an orchestrator over them, embedded in `lforge`'s wider data-platform orchestration. Individual passes (borrow checker, sema queries, monomorphization, …) can be lifted into services in any order. This architecture is what lets user metaprograms compose with built-in passes on equal footing — they speak the same Hermes IR. See [Metaprogramming → Modular, Service-Oriented Compiler](metaprog.md#modular-service-oriented-compiler).
