
#pragma once
#include "module_manifest.hpp"
#include <string>
#include <vector>

namespace logos::compiler {

namespace lir { struct LProgram; }

struct EmitModuleOptions {
    // --stats. It used to be accepted and silently ignored on this path (main
    // returns at the --emit-module branch, long before the stats block), so the
    // ONE build that takes minutes had no phase breakdown at all.
    bool stats = false;

    std::vector<std::string> extra_search_paths;  // -I flags
    bool emit_mlir = false;
    bool emit_llvm = false;
    // --emit-docs: in addition to the normal module artifacts, walk the post-sema
    // decl views and write <output_path>.docwr — a Writ-SDN container of
    // documentation facts (items + impl edges) that `lforge doc` loads as a Deem
    // EDB. Sibling of the .abi-layout sidecar; reads the `.doc()` accessors that
    // sema already populates from /// //! /** */ comments (ADR 0014).
    bool emit_docs = false;
    // --emit-units: write <output_path>.units — the UnitGraph as Writ-SDN
    // (nodes, order edges with provenance, SCC ids, levels, the order the
    // driver actually walked, and a census). The graph is BUILT regardless;
    // this only decides whether it is written where a gate can read it, and it
    // changes no compilation decision.
    bool emit_units = false;
    // ⚠ The PATH was missing, so `--emit-units=<path>` was accepted on the
    // command line and then silently ignored on this route: only the bool
    // crossed the boundary and the sidecar always landed at
    // "<output_path>.units". Measured: `--emit-units=.../mem.units.sdn`
    // produced `det.A.a.units`. A flag parsed and dropped is worse than an
    // absent one — it answers "I wrote it where you asked" with silence.
    // Empty + emit_units → the derived "<output_path>.units" default.
    std::string emit_units_path;
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

    // Backend target CPU (`-C target-cpu=`); "native" = host CPU (AVX…).
    std::string target_cpu = "generic";
};

// Build a binary module (.a archive) from a module manifest.
// Returns true on success.
bool emit_module(const ModuleManifest& manifest,
                 const std::string& output_path,
                 const EmitModuleOptions& opts);

} // namespace logos::compiler
