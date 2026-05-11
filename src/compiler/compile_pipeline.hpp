// compile_pipeline — shared lowering tail used by main.cpp + emit_module.cpp.
// Takes a mono'd + borrow-checked LProgram and drives:
//
//   LProgram → mlir_gen → MLIR→LLVM lowering → opt → object file
//
// Pre-pass divergences (reflection_emit, metacall-thunk-drop, per-file
// filter, etc.) stay with the caller; the helper just covers the MLIR/LLVM
// stretch that was duplicated character-for-character between the two
// pipelines.
#pragma once

#include <logos/compiler/lir.hpp>

#include <memory>
#include <string>

namespace llvm { class Module; }

namespace logos::compiler {

struct LowerEmitOpts {
    int  opt_level         = 0;     // 0 = no opt-pipeline, 1..3 → O1/O2/O3
    bool function_sections = true;  // per-fn / per-data sections (--gc-sections)
    bool emit_mlir         = false; // print MLIR text to stdout, return 0
    bool emit_llvm         = false; // print LLVM IR to stdout, return 0
    // When set, the LLVM module is moved into *jit_module_out instead of
    // being emitted as an object file — caller (main.cpp --jit) takes
    // ownership and drives JIT compilation themselves. Bypasses the whole
    // target-machine + object-emission tail.
    std::unique_ptr<llvm::Module>* jit_module_out = nullptr;
    // --dump-metaprog snapshots: when non-empty, write _global_post_mono.mlir
    // and _global_post_mlirgen.ll under this dir.
    std::string dump_metaprog_dir;
};

// Lowers prog → object file at `output_path`. On --jit, returns the JIT'd
// main()'s exit code instead. Returns 0 on success / non-zero on failure.
//
// Caller invariants:
//   - prog has been mono'd + borrow-checked
//   - prog.binary_symbols set as needed (per-file mode, link from archives)
//   - prog.functions cleaned (e.g. metacall thunks removed if undesired)
int lower_and_emit_object(
    lir::LProgram&     prog,
    const std::string& output_path,
    const LowerEmitOpts& opts);

} // namespace logos::compiler
