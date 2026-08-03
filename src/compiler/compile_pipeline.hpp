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
    bool debug_info        = false; // -g: emit DWARF (line tables, subprograms,
                                    // locals, types) via MLIR LLVM-dialect DI attrs.
    std::string source_path;        // primary input path → DWARF compile-unit file.
    bool emit_mlir         = false; // print MLIR text to stdout, return 0
    bool emit_llvm         = false; // print LLVM IR to stdout, return 0 (PRE-opt)
    bool emit_llvm_opt     = false; // print LLVM IR to stdout AFTER the opt
                                    // pipeline (honors opt_level), then return 0
    bool overflow_checks   = true;  // trap on int +/-/* overflow; false = wrapping
                                    // (`-C overflow-checks=off`, vectorizable arith)
    std::string target_cpu = "generic"; // backend CPU (`-C target-cpu=`); "native"
                                    // = host CPU (enables AVX/AVX2/AVX-512)
    // When set, the LLVM module is moved into *jit_module_out instead of
    // being emitted as an object file — caller (main.cpp --jit) takes
    // ownership and drives JIT compilation themselves. Bypasses the whole
    // target-machine + object-emission tail.
    std::unique_ptr<llvm::Module>* jit_module_out = nullptr;
    // --dump-metaprog snapshots: when non-empty, write _global_post_mono.mlir
    // and _global_post_mlirgen.ll under this dir.
    std::string dump_metaprog_dir;
    // ── SHARDING ────────────────────────────────────────────────────────────
    // Emit only the function bodies belonging to shard `shard_index` of
    // `shard_count`; every other body becomes a forward declaration, exactly as
    // a dependency-archive symbol already does. shard_index < 0 = emit
    // everything (today's behaviour, and the only behaviour when count == 1).
    //
    // WHY AN ARBITRARY SPLIT IS SOUND: an ordinary call between two units is
    // resolved by the LINKER and imposes no build order. Measured on
    // liblogos-mem.a: 5 785 defined symbols, ALL external, ZERO internal — so
    // no body can become unreachable by landing in a different object.
    // Semantic ownership is needed only where an ORDER constraint exists (a
    // metafunction's provider before its consumer), which is one edge in the
    // whole stdlib. Everything else only needs BALANCE.
    int shard_index = -1;
    int shard_count = 1;
};

// Resolve `-C target-cpu=` to the concrete backend CPU name: "" → "generic",
// "native" → the host CPU (llvm::sys::getHostCPUName). SINGLE SOURCE for both
// the TargetMachine construction and codegen-time feature decisions — the two
// must never diverge (mlir_gen emitting an instruction ISel then rejects).
std::string resolve_target_cpu(const std::string& target_cpu);

// Whether the resolved target CPU enables BMI2 (hardware pdep/pext).
// "generic" (the default x86-64 SSE2 baseline) → false. Feeds mlir_gen's
// pdep_u64/pext_u64 lowering choice: inline llvm.x86.bmi.* vs rt-fallback call.
bool target_cpu_has_bmi2(const std::string& target_cpu);

// The BACKEND's data layout for the resolved target, as an LLVM datalayout
// string. SINGLE SOURCE for "how many bytes does a value occupy": mlir-gen
// attaches it to the MLIR module (dlti.dl_spec + llvm.data_layout) so that
// `mlir::DataLayout` answers with the SAME numbers ISel will use.
//
// Without it MLIR falls back to `getDefaultABIAlignment`, whose integer rule is
// `width < 64 ? PowerOf2Ceil(bytes) : 4` — i64/i128 get ABI ALIGNMENT 4, so
// `{i32, i64}` sized 12 instead of 16 and every value copy of such a struct
// dropped its last 4 bytes. Returns "" if the target cannot be looked up (the
// caller then leaves the module unannotated, as before).
std::string target_data_layout_string(const std::string& target_cpu);

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
