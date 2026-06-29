
#pragma once
#include "module_manifest.hpp"
#include <string>
#include <vector>

namespace logos::compiler {

namespace lir { struct LProgram; }

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
    //   <output_path>.writ0 — binary AST containing only this file.
    // No `ar` step is run. The orchestrator (lforge) parallelises N such
    // invocations and merges their outputs into the final library archive.
    std::string only_file;

    // External archives whose .writ0 modules should be loaded so `use
    // <pkg>;` statements in this module can resolve symbols from other
    // already-built lforge projects (B3 transitive deps).
    std::vector<std::string> extra_lib_files;

    // LLVM optimization level (-O0..-O3) for the emitted package object(s).
    // 0 = skip the opt pipeline (fast build, unoptimized code). Threaded down
    // to the shared lowering tail so `logosc -O2 --emit-module` actually
    // optimizes package/library code (without this the package path is always
    // unoptimized regardless of -O, while the single-file `-c` path optimizes).
    int opt_level = 0;

    // Trap on integer +/-/* overflow (default) vs wrapping (`-C overflow-checks=off`).
    // Threaded to the lowering tail so package/stdlib arithmetic honors the policy.
    bool overflow_checks = true;
};

// Build a binary module (.a archive) from a module manifest.
// Returns true on success.
bool emit_module(const ModuleManifest& manifest,
                 const std::string& output_path,
                 const EmitModuleOptions& opts);

} // namespace logos::compiler
