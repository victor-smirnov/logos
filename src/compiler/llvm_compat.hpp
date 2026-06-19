// src/compiler/llvm_compat.hpp
//
// Thin shims over the handful of LLVM/MLIR C++ APIs that changed signature
// across the LLVM releases Logos supports. Logos builds against the
// distribution's STOCK LLVM/MLIR (Ubuntu 24.04 = series 20) as well as newer
// trees (21). The version split is centralised here, gated on
// LLVM_VERSION_MAJOR, so the rest of the compiler stays version-agnostic.
#pragma once

#include <memory>

#include <llvm/Config/llvm-config.h>
#include <llvm/ADT/APFloat.h>
#include <llvm/IR/Module.h>
#include <llvm/TargetParser/Host.h>
#include <llvm/TargetParser/Triple.h>

#include <mlir/Conversion/SCFToControlFlow/SCFToControlFlow.h>
#include <mlir/Dialect/Arith/IR/Arith.h>
#include <mlir/IR/Builders.h>
#include <mlir/IR/BuiltinAttributes.h>
#include <mlir/IR/BuiltinTypes.h>
#include <mlir/Pass/Pass.h>

namespace logos::compat {

// SCF→ControlFlow lowering pass: `createConvertSCFToCFPass` (≤20) was renamed
// to `createSCFToControlFlowPass` (≥21).
inline std::unique_ptr<mlir::Pass> create_scf_to_cf_pass() {
#if LLVM_VERSION_MAJOR >= 21
    return mlir::createSCFToControlFlowPass();
#else
    return mlir::createConvertSCFToCFPass();
#endif
}

// llvm::Module::setTargetTriple took a StringRef (≤20) and switched to
// llvm::Triple (≥21).
inline void set_default_target_triple(llvm::Module& m) {
    std::string triple = llvm::sys::getDefaultTargetTriple();
#if LLVM_VERSION_MAJOR >= 21
    m.setTargetTriple(llvm::Triple(triple));
#else
    m.setTargetTriple(triple);
#endif
}

// arith::ConstantFloatOp::build swapped its (value, type) parameter order
// between 20 and 21. Route through the version-stable arith::ConstantOp +
// FloatAttr, which is identical in effect and needs no version split.
inline mlir::Value const_float(mlir::OpBuilder& b, mlir::Location loc,
                               mlir::FloatType ty, const llvm::APFloat& v) {
    return b.create<mlir::arith::ConstantOp>(loc, mlir::FloatAttr::get(ty, v));
}

} // namespace logos::compat
