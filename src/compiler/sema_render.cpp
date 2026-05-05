// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos
//
// AST → Logos source pretty-printer.
//
// Renders a sub-AST back to surface Logos text so that
// `metacall (<expr>)` / `metacall { ... }` can synthesise a JIT-thunk
// body containing arbitrary expressions or statements.
//
// Stage 1 (this commit) covers literals, variables, BINOP/UNARY/PAREN/
// CAST, free-/generic-/static-/method-calls, field reads, and the
// stmt subset needed for a metacall block: LET, RETURN, EXPR_STMT,
// TAIL_EXPR, BLOCK. Control-flow / composite literals / patterns
// arrive in Stage 2.
//
// Operator precedence: BINOP/UNARY/CAST children are wrapped in parens
// unconditionally. Cheap and always correct; the parser parses any
// excess parens fine, and the resulting source is human-readable.

#include "sema_impl.hpp"

#include <logos/hermes/tiny_object_map.hpp>
#include <logos/hermes/object_array.hpp>
#include <logos/hermes/access.hpp>

#include <format>
#include <string>

namespace logos::compiler {

namespace la = ast;
using hermes::TinyMapView;
using hermes::AnyVal;

namespace {

std::string escape_str_lit(std::string_view raw) {
    std::string s = "\"";
    for (char c : raw) {
        switch (c) {
        case '\\': s += "\\\\"; break;
        case '"':  s += "\\\""; break;
        case '\n': s += "\\n";  break;
        case '\r': s += "\\r";  break;
        case '\t': s += "\\t";  break;
        case '\0': s += "\\0";  break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\x%02X",
                              static_cast<unsigned>(static_cast<unsigned char>(c)));
                s += buf;
            } else {
                s += c;
            }
            break;
        }
    }
    s += "\"";
    return s;
}

} // namespace

std::string SemaChecker::render_type_src(TinyMapView node) {
    if (node.is_null()) return "_";
    auto t = resolve_type(node);
    if (!t) return "_";
    return type_str(t);
}

std::string SemaChecker::render_expr_src(TinyMapView node) {
    if (node.is_null()) return "()";
    int32_t c = code_of(node);

    switch (c) {

    case la::LIT_INT: {
        std::string s;
        if (node.has_key(la::LO_NEG)) {
            AnyVal neg = node.get(la::LO_NEG.code);
            if (!neg.is_null() && neg.is_value() && neg.as_value<uint8_t>() != 0) s += "-";
        }
        s += std::string(str_of(node.get(la::VALUE.code)));
        return s;
    }
    case la::LIT_BOOL: {
        AnyVal v = node.get(la::VALUE.code);
        bool b = !v.is_null() && v.is_value() && v.as_value<uint8_t>() != 0;
        return b ? "true" : "false";
    }
    case la::LIT_STR: {
        return escape_str_lit(str_of(node.get(la::VALUE.code)));
    }
    case la::LIT_FLOAT: {
        return std::string(str_of(node.get(la::VALUE.code)));
    }
    case la::LIT_CHAR: {
        // VALUE preserves the original `'X'` text including quotes.
        return std::string(str_of(node.get(la::VALUE.code)));
    }
    case la::VAR_REF: {
        return std::string(str_of(node.get(la::NAME.code)));
    }

    case la::PAREN_EXPR: {
        return "(" + render_expr_src(map_of(node.get(la::VALUE.code))) + ")";
    }

    case la::BINOP: {
        std::string op(str_of(node.get(la::OP.code)));
        auto lhs = render_expr_src(map_of(node.get(la::LHS.code)));
        auto rhs = render_expr_src(map_of(node.get(la::RHS.code)));
        return std::format("({} {} {})", lhs, op, rhs);
    }

    case la::UNARY: {
        std::string op(str_of(node.get(la::OP.code)));
        auto v = render_expr_src(map_of(node.get(la::VALUE.code)));
        // `&` and `!` and `-` all attach without space.
        return std::format("({}{})", op, v);
    }

    case la::CAST: {
        auto v  = render_expr_src(map_of(node.get(la::VALUE.code)));
        auto ty = render_type_src(map_of(node.get(la::TYPE.code)));
        return std::format("({} as {})", v, ty);
    }

    case la::CALL: {
        std::string s(str_of(node.get(la::CALLEE.code)));
        s += "(";
        if (node.has_key(la::ARGS)) {
            auto args = arr_of(node.get(la::ARGS.code));
            for (uint64_t i = 0; i < args.size(); ++i) {
                if (i) s += ", ";
                s += render_expr_src(map_of(args.get(i)));
            }
        }
        s += ")";
        return s;
    }

    case la::GENERIC_CALL: {
        std::string s(str_of(node.get(la::CALLEE.code)));
        if (node.has_key(la::TYPE_PARAMS)) {
            auto tplist = map_of(node.get(la::TYPE_PARAMS.code));
            if (tplist.has_key(la::ITEMS)) {
                auto items = arr_of(tplist.get(la::ITEMS.code));
                if (items.size() > 0) {
                    s += "::<";
                    for (uint64_t i = 0; i < items.size(); ++i) {
                        if (i) s += ", ";
                        s += render_type_src(map_of(items.get(i)));
                    }
                    s += ">";
                }
            }
        }
        s += "(";
        if (node.has_key(la::ARGS)) {
            auto am = map_of(node.get(la::ARGS.code));
            if (am.has_key(la::ITEMS)) {
                auto items = arr_of(am.get(la::ITEMS.code));
                for (uint64_t i = 0; i < items.size(); ++i) {
                    if (i) s += ", ";
                    s += render_expr_src(map_of(items.get(i)));
                }
            }
        }
        s += ")";
        return s;
    }

    case la::STATIC_CALL: {
        std::string s(str_of(node.get(la::RECEIVER.code)));
        s += "::";
        s += std::string(str_of(node.get(la::NAME.code)));
        if (node.has_key(la::TYPE_PARAMS)) {
            auto tplist = map_of(node.get(la::TYPE_PARAMS.code));
            if (tplist.has_key(la::ITEMS)) {
                auto items = arr_of(tplist.get(la::ITEMS.code));
                if (items.size() > 0) {
                    s += "::<";
                    for (uint64_t i = 0; i < items.size(); ++i) {
                        if (i) s += ", ";
                        s += render_type_src(map_of(items.get(i)));
                    }
                    s += ">";
                }
            }
        }
        s += "(";
        if (node.has_key(la::ARGS)) {
            auto am = map_of(node.get(la::ARGS.code));
            if (am.has_key(la::ITEMS)) {
                auto items = arr_of(am.get(la::ITEMS.code));
                for (uint64_t i = 0; i < items.size(); ++i) {
                    if (i) s += ", ";
                    s += render_expr_src(map_of(items.get(i)));
                }
            }
        }
        s += ")";
        return s;
    }

    case la::METHOD_CALL: {
        std::string s = "(";
        s += render_expr_src(map_of(node.get(la::RECEIVER.code)));
        s += ").";
        s += std::string(str_of(node.get(la::NAME.code)));
        if (node.has_key(la::TYPE_PARAMS)) {
            auto tplist = map_of(node.get(la::TYPE_PARAMS.code));
            if (tplist.has_key(la::ITEMS)) {
                auto items = arr_of(tplist.get(la::ITEMS.code));
                if (items.size() > 0) {
                    s += "::<";
                    for (uint64_t i = 0; i < items.size(); ++i) {
                        if (i) s += ", ";
                        s += render_type_src(map_of(items.get(i)));
                    }
                    s += ">";
                }
            }
        }
        s += "(";
        if (node.has_key(la::ARGS)) {
            auto am = map_of(node.get(la::ARGS.code));
            if (am.has_key(la::ITEMS)) {
                auto items = arr_of(am.get(la::ITEMS.code));
                for (uint64_t i = 0; i < items.size(); ++i) {
                    if (i) s += ", ";
                    s += render_expr_src(map_of(items.get(i)));
                }
            }
        }
        s += ")";
        return s;
    }

    case la::FIELD_READ: {
        std::string s = "(";
        s += render_expr_src(map_of(node.get(la::RECEIVER.code)));
        s += ").";
        if (node.has_key(la::FIELD)) {
            s += std::string(str_of(node.get(la::FIELD.code)));
        } else if (node.has_key(la::NAME_VAR)) {
            // Rare path used by quote-cursor antiquot; not legal in metacall.
            s += "<antiquot>";
        }
        return s;
    }

    default:
        // Stage 2 will fill in the rest. For now return a placeholder that
        // the parser will choke on if it ever survives — so failures are
        // loud, not silent.
        return std::format("/* render_expr: unsupported AST code {} */", c);
    }
}

std::string SemaChecker::render_stmt_src(TinyMapView node) {
    if (node.is_null()) return ";";
    int32_t c = code_of(node);

    switch (c) {

    case la::LET: {
        std::string s = "let ";
        if (node.has_key(la::IS_MUT)) {
            AnyVal m = node.get(la::IS_MUT.code);
            if (!m.is_null() && m.is_value() && m.as_value<uint8_t>() != 0) s += "mut ";
        }
        s += std::string(str_of(node.get(la::NAME.code)));
        if (node.has_key(la::TYPE)) {
            s += ": ";
            s += render_type_src(map_of(node.get(la::TYPE.code)));
        }
        s += " = ";
        s += render_expr_src(map_of(node.get(la::VALUE.code)));
        s += ";";
        return s;
    }

    case la::RETURN: {
        std::string s = "return";
        if (node.has_key(la::VALUE)) {
            auto v = node.get(la::VALUE.code);
            if (!v.is_null()) {
                s += " ";
                s += render_expr_src(map_of(v));
            }
        }
        s += ";";
        return s;
    }

    case la::EXPR_STMT: {
        std::string s = render_expr_src(map_of(node.get(la::VALUE.code)));
        s += ";";
        return s;
    }

    case la::TAIL_EXPR: {
        // No trailing semicolon — caller (block) handles placement.
        return render_expr_src(map_of(node.get(la::VALUE.code)));
    }

    case la::BLOCK: {
        return render_block_src(node);
    }

    default:
        return std::format("/* render_stmt: unsupported AST code {} */;", c);
    }
}

std::string SemaChecker::render_block_src(TinyMapView node) {
    if (node.is_null() || code_of(node) != la::BLOCK) return "{}";
    std::string s = "{ ";
    if (node.has_key(la::ITEMS)) {
        auto items = arr_of(node.get(la::ITEMS.code));
        for (uint64_t i = 0; i < items.size(); ++i) {
            if (i) s += " ";
            s += render_stmt_src(map_of(items.get(i)));
        }
    }
    s += " }";
    return s;
}

} // namespace logos::compiler
