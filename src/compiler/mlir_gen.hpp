// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos
//
// MLIRGen — lower Logos Hermes AST to MLIR (func + arith + scf dialects).

#pragma once

#include <logos/hermes/document.hpp>

#include <mlir/IR/MLIRContext.h>
#include <mlir/IR/OwningOpRef.h>
#include <mlir/IR/BuiltinOps.h>

namespace logos::compiler {

// Convert a Logos Hermes AST (module node) to an MLIR module.
// Returns nullptr on error (diagnostics printed to stderr).
mlir::OwningOpRef<mlir::ModuleOp> mlir_gen(mlir::MLIRContext& ctx,
                                            hermes::HermesCtrView ast) noexcept;

} // namespace logos::compiler
