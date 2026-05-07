// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov

#pragma once
#include "module_manifest.hpp"
#include <memory>
#include <string>
#include <vector>

namespace llvm {
class Module;
class LLVMContext;
} // namespace llvm

namespace logos::compiler {

namespace lir { struct LProgram; }

// Lower an already-typechecked, monomorphized, borrow-checked LProgram
// to an LLVM module. Returns nullptr on failure (errors printed to stderr).
// Used by both .o emission (emit_module) and in-process JIT (logosc --jit).
std::unique_ptr<llvm::Module>
lower_to_llvm_module(const lir::LProgram& prog, llvm::LLVMContext& llvm_ctx);


struct EmitModuleOptions {
    std::vector<std::string> extra_search_paths;  // -I flags
    bool emit_mlir = false;
    bool emit_llvm = false;
    // Per-file emit mode (B1.7). When non-empty, emit_module produces only
    // the artifacts for THIS source file:
    //   <output_path>.o       — object code with body emission filtered to
    //                           items whose lir::LFunction.source_file
    //                           matches this path; all other fns become
    //                           forward-decls so the linker can resolve
    //                           them later from sibling per-file objects.
    //   <output_path>.hermes0 — binary AST containing only this file.
    // No `ar` step is run. The orchestrator (lforge) parallelises N such
    // invocations and merges their outputs into the final library archive.
    std::string only_file;

    // External archives whose .hermes0 modules should be loaded so `use
    // <pkg>;` statements in this module can resolve symbols from other
    // already-built lforge projects (B3 transitive deps).
    std::vector<std::string> extra_lib_files;
};

// Build a binary module (.a archive) from a module manifest.
// Returns true on success.
bool emit_module(const ModuleManifest& manifest,
                 const std::string& output_path,
                 const EmitModuleOptions& opts);

} // namespace logos::compiler
