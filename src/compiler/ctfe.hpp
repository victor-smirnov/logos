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
#include <logos/hermes2/compat.hpp>
#include <logos/hermes2/compat.hpp>
#include <logos/hermes2/compat.hpp>
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

// §6.9: path-to-const resolution. When a bare identifier appears in
// expression position inside a `metacall { … }` arg or otherwise
// const-evaluable context, the evaluator looks the name up via this
// callback; the implementor (sema) supplies the AST node for the
// const's RHS, which gets recursively evaluated. Returning nullptr
// (`!`-resolved view) leaves the original "expression is not a
// compile-time constant" diagnostic in place.
struct ConstResolver {
    virtual ~ConstResolver() = default;
    // Look up `name` and return the RHS expression node + the holder
    // that owns it (may differ from the caller's holder for cross-
    // package consts). Returns null view when the name doesn't
    // resolve to a const-evaluable item.
    virtual hermes2::TinyMapView lookup_const(std::string_view name,
                                             hermes2::MemHolder** out_holder) = 0;
};

// Evaluate one AST expression node. `holder` is the doc that owns `node`.
// `resolver` is consulted on VAR_REF nodes (may be null — disables
// path-to-const resolution).
logos::expected<CtfeValue, CtfeError>
eval_expr(hermes2::TinyMapView node, hermes2::MemHolder* holder,
          ConstResolver* resolver = nullptr) noexcept;

} // namespace logos::compiler::ctfe
