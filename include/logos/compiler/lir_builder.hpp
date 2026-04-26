// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos
//
// LirBuilder — single construction surface for L-IR nodes.
//
// Design: ADR 0005. Stage 3f writes the existing variant types
// (lir::ELitInt, lir::EBinOp, ...) and returns owned LExprPtr/LStmtPtr.
// Stage 3g switches the implementation to write directly into a Hermes
// zone; only this file changes, sema callers stay put.
//
// The builder grows lazily — methods are added when sema sites migrate.
// No method exists ahead of its first caller.

#pragma once

#include <logos/compiler/lir.hpp>

#include <cstdint>
#include <string>
#include <utility>

namespace logos::compiler {

class LirBuilder {
public:
    explicit LirBuilder(lir::LProgram& prog) noexcept : prog_(prog) {}

    // ── Expression leaves ────────────────────────────────────────────────

    lir::LExprPtr lit_int (int64_t v, TypeRef ty);
    lir::LExprPtr lit_bool(bool v,    TypeRef ty);
    lir::LExprPtr var_ref (std::string name, TypeRef ty);

    // ── Composite expressions ────────────────────────────────────────────

    lir::LExprPtr bin_op(std::string op,
                         lir::LExprPtr lhs,
                         lir::LExprPtr rhs,
                         TypeRef ty);

    // … grows as sema sites migrate; do not pre-populate.

private:
    lir::LProgram& prog_;  // reserved for Stage 3g (Hermes zone access)
};

} // namespace logos::compiler
