// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos
//
// CTFE — pure-AST compile-time expression evaluator for metacall args.
//
// Operates over Hermes TinyMapView nodes. No type-pool / sema dependency:
// values carry a LogosType::Kind tag derived from literal suffixes (or
// IntLit/FloatLit when unsuffixed) and from operator type rules.
//
// Supported nodes:
//   LIT_INT, LIT_FLOAT, LIT_BOOL, LIT_STR, PAREN_EXPR, UNARY (- !),
//   BINOP (+ - * / % << >> & | ^ && || == != < <= > >=).
//
// Anything else returns CtfeError; caller decides what to do (typically
// emit a "metacall: argument N is not a compile-time constant" diag).

#pragma once

#include <logos/compiler/ast.hpp>
#include <logos/compiler/sema.hpp>
#include <logos/hermes/tiny_object_map.hpp>
#include <logos/hermes/view.hpp>
#include <logos/hermes/mem_holder.hpp>
#include <logos/core/expected.hpp>

#include <cstdint>
#include <string>

namespace logos::compiler::ctfe {

struct CtfeError {
    std::string msg;
};

struct CtfeValue {
    // Kind reuses LogosType::Kind so caller can compare with primitive types.
    // Only scalar/Bool/IntLit/FloatLit/Slice<u8>(=str) values appear.
    LogosType::Kind kind = LogosType::Kind::Error;

    // Storage. Only the field matching `kind` is meaningful.
    int64_t  i = 0;   // integer-tag kinds (signed view)
    uint64_t u = 0;   // integer-tag kinds (unsigned view; mirrors `i` bits)
    double   f = 0.0; // F32 / F64 / FloatLit
    bool     b = false;
    std::string s;    // Slice<u8> (str literal)
};

// Evaluate one AST expression node. `holder` is the doc that owns `node`.
logos::expected<CtfeValue, CtfeError>
eval_expr(hermes::TinyMapView node, hermes::MemHolder* holder) noexcept;

} // namespace logos::compiler::ctfe
