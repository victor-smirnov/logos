// Logos project — https://github.com/victor-smirnov/logos
//
// MLIRGen — lower Logos L-IR to MLIR (func + arith + cf + llvm dialects).

#pragma once

#include <logos/compiler/lir.hpp>

#include <mlir/IR/MLIRContext.h>
#include <mlir/IR/OwningOpRef.h>
#include <mlir/IR/BuiltinOps.h>

#include <string_view>

namespace logos::compiler {

// Convert a fully-typed Logos L-IR program to a single MLIR module.
// Returns nullptr on error (diagnostics printed to stderr).
// debug_info: when true, attach DWARF debug-info attributes (DICompileUnit /
// DISubprogram / DILocation / DILocalVariable / DI types) so the emitted object
// carries source-level debug info for gdb/lldb.
// main_source: primary input source path, used as the DWARF compile-unit file
// and as a fallback when a function carries no per-file source_file (-g only).
// target_has_bmi2: the RESOLVED backend target CPU has BMI2 (pdep/pext).
// Computed by the caller via target_cpu_has_bmi2() (compile_pipeline.hpp) from
// the SAME resolved cpu string the TargetMachine is created with, so the
// codegen-time decision (inline llvm.x86.bmi.* vs rt-fallback call) can never
// diverge from ISel-time reality. Default false → rt-fallback call (correct on
// every target; the runtime cpuid-dispatches to hardware pdep/pext anyway).
mlir::OwningOpRef<mlir::ModuleOp> mlir_gen(mlir::MLIRContext& ctx,
                                            const lir::LProgram& prog,
                                            bool debug_info = false,
                                            std::string_view main_source = {},
                                            bool overflow_checks = true,
                                            bool target_has_bmi2 = false,
                                            std::string_view target_cpu = {},
                                            int shard_index = -1,
                                            int shard_count = 1,
                                            bool metaprog_round = false) noexcept;

} // namespace logos::compiler
