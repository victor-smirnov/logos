// Logos project — https://github.com/victor-smirnov/logos
//
// MLIRGen — lower Logos L-IR to MLIR (func + arith + cf + llvm dialects).

#pragma once

#include <logos/compiler/lir.hpp>

#include <mlir/IR/MLIRContext.h>
#include <mlir/IR/OwningOpRef.h>
#include <mlir/IR/BuiltinOps.h>

namespace logos::compiler {

// Convert a fully-typed Logos L-IR program to a single MLIR module.
// Returns nullptr on error (diagnostics printed to stderr).
mlir::OwningOpRef<mlir::ModuleOp> mlir_gen(mlir::MLIRContext& ctx,
                                            const lir::LProgram& prog) noexcept;

} // namespace logos::compiler
