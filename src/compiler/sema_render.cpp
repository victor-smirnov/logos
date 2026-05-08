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
#include "ctfe.hpp"

#include <logos/hermes/tiny_object_map.hpp>
#include <logos/hermes/object_array.hpp>
#include <logos/hermes/access.hpp>

#include <cstdio>
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
            s += "<antiquot>";
        }
        return s;
    }

    case la::TUPLE_INDEX: {
        std::string s = "(";
        s += render_expr_src(map_of(node.get(la::RECEIVER.code)));
        s += ").";
        s += std::string(str_of(node.get(la::FIELD.code)));
        return s;
    }

    case la::INDEX_READ: {
        std::string s = "(";
        s += render_expr_src(map_of(node.get(la::RECEIVER.code)));
        s += ")[";
        s += render_expr_src(map_of(node.get(la::VALUE.code)));
        s += "]";
        return s;
    }

    case la::DEREF: {
        return "(*" + render_expr_src(map_of(node.get(la::VALUE.code))) + ")";
    }

    case la::TUPLE_LIT: {
        std::string s = "(";
        if (node.has_key(la::ITEMS)) {
            auto items = arr_of(node.get(la::ITEMS.code));
            uint64_t n = items.size();
            for (uint64_t i = 0; i < n; ++i) {
                if (i) s += ", ";
                s += render_expr_src(map_of(items.get(i)));
            }
            if (n == 1) s += ",";
        }
        s += ")";
        return s;
    }

    case la::ARR_LIT: {
        std::string s = "[";
        if (node.has_key(la::ITEMS)) {
            auto items = arr_of(node.get(la::ITEMS.code));
            for (uint64_t i = 0; i < items.size(); ++i) {
                if (i) s += ", ";
                s += render_expr_src(map_of(items.get(i)));
            }
        }
        s += "]";
        return s;
    }

    case la::STRUCT_LIT: {
        std::string s;
        if (node.has_key(la::NAME))
            s += std::string(str_of(node.get(la::NAME.code)));
        else
            s += "<antiquot>";
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
        s += " { ";
        bool first = true;
        if (node.has_key(la::ITEMS)) {
            auto items = arr_of(node.get(la::ITEMS.code));
            for (uint64_t i = 0; i < items.size(); ++i) {
                if (!first) s += ", ";
                first = false;
                auto fi = map_of(items.get(i));
                int32_t fc = code_of(fi);
                if (fc == la::FIELD_INIT) {
                    s += std::string(str_of(fi.get(la::NAME.code)));
                    s += ": ";
                    s += render_expr_src(map_of(fi.get(la::VALUE.code)));
                } else if (fc == la::FIELD_SHORTHAND) {
                    s += std::string(str_of(fi.get(la::NAME.code)));
                } else {
                    s += "/* unknown field shape */";
                }
            }
        }
        if (node.has_key(la::BASE)) {
            if (!first) s += ", ";
            s += "..";
            s += render_expr_src(map_of(node.get(la::BASE.code)));
        }
        s += " }";
        return s;
    }

    case la::IF: {
        std::string s = "if ";
        if (node.has_key(la::COND)) {
            s += render_expr_src(map_of(node.get(la::COND.code)));
        } else if (node.has_key(la::PAT)) {
            s += "let ";
            s += render_pat_src(map_of(node.get(la::PAT.code)));
            s += " = ";
            s += render_expr_src(map_of(node.get(la::VALUE.code)));
        }
        s += " ";
        s += render_block_src(map_of(node.get(la::THEN.code)));
        if (node.has_key(la::ELSE)) {
            auto e = map_of(node.get(la::ELSE.code));
            int32_t ec = code_of(e);
            // Else branch is either another IF (else-if) or a BLOCK.
            if (ec == la::IF) {
                s += " else ";
                s += render_expr_src(e);
            } else {
                s += " else ";
                s += render_block_src(e);
            }
        }
        return s;
    }

    case la::MATCH: {
        std::string s = "match ";
        s += render_expr_src(map_of(node.get(la::VALUE.code)));
        s += " { ";
        if (node.has_key(la::ITEMS)) {
            auto items = arr_of(node.get(la::ITEMS.code));
            for (uint64_t i = 0; i < items.size(); ++i) {
                auto arm = map_of(items.get(i));
                s += render_pat_src(map_of(arm.get(la::LHS.code)));
                if (arm.has_key(la::GUARD)) {
                    s += " if ";
                    s += render_expr_src(map_of(arm.get(la::GUARD.code)));
                }
                s += " => ";
                if (arm.has_key(la::BODY)) {
                    s += render_block_src(map_of(arm.get(la::BODY.code)));
                } else if (arm.has_key(la::EXPR)) {
                    s += render_expr_src(map_of(arm.get(la::EXPR.code)));
                    s += ",";
                }
                s += " ";
            }
        }
        s += "}";
        return s;
    }

    case la::LIT_HSTATIC: {
        // Hermes literal — out of scope for renderer (would need a full
        // hermes-to-source dump). Leave as a sentinel; capture-detector will
        // never reach here for value-position consts because module-level
        // hstatic refs are already inlined elsewhere.
        return "/* render_expr: LIT_HSTATIC unsupported in metacall block */";
    }

    default:
        return std::format("/* render_expr: unsupported AST code {} */", c);
    }
}

std::string SemaChecker::render_pat_src(TinyMapView node) {
    if (node.is_null()) return "_";
    int32_t c = code_of(node);
    switch (c) {
    case la::PAT_WILD: {
        if (node.has_key(la::NAME))
            return std::string(str_of(node.get(la::NAME.code)));
        return "_";
    }
    case la::PAT_UNIT: return "()";
    case la::PAT_INT: {
        std::string s;
        if (node.has_key(la::LO_NEG)) {
            AnyVal n = node.get(la::LO_NEG.code);
            if (!n.is_null() && n.is_value() && n.as_value<uint8_t>() != 0) s += "-";
        }
        s += std::string(str_of(node.get(la::VALUE.code)));
        return s;
    }
    case la::PAT_BOOL: {
        AnyVal v = node.get(la::VALUE.code);
        bool b = !v.is_null() && v.is_value() && v.as_value<uint8_t>() != 0;
        return b ? "true" : "false";
    }
    case la::PAT_VARIANT: {
        std::string s(str_of(node.get(la::NAME.code)));
        s += "::";
        s += std::string(str_of(node.get(la::FIELD.code)));
        return s;
    }
    case la::PAT_VARIANT_DATA: {
        std::string s(str_of(node.get(la::NAME.code)));
        s += "::";
        s += std::string(str_of(node.get(la::FIELD.code)));
        s += "(";
        if (node.has_key(la::ARGS)) {
            auto items = arr_of(node.get(la::ARGS.code));
            for (uint64_t i = 0; i < items.size(); ++i) {
                if (i) s += ", ";
                s += render_pat_src(map_of(items.get(i)));
            }
        }
        s += ")";
        return s;
    }
    case la::PAT_TUPLE: {
        std::string s = "(";
        if (node.has_key(la::ITEMS)) {
            auto items = arr_of(node.get(la::ITEMS.code));
            uint64_t n = items.size();
            for (uint64_t i = 0; i < n; ++i) {
                if (i) s += ", ";
                s += render_pat_src(map_of(items.get(i)));
            }
            if (n == 1) s += ",";
        }
        s += ")";
        return s;
    }
    case la::PAT_OR: {
        std::string s;
        if (node.has_key(la::ITEMS)) {
            auto items = arr_of(node.get(la::ITEMS.code));
            for (uint64_t i = 0; i < items.size(); ++i) {
                if (i) s += " | ";
                s += render_pat_src(map_of(items.get(i)));
            }
        }
        return s;
    }
    case la::PAT_RANGE: {
        std::string s;
        bool lo_neg = false, hi_neg = false;
        if (node.has_key(la::LO_NEG)) {
            AnyVal n = node.get(la::LO_NEG.code);
            lo_neg = !n.is_null() && n.is_value() && n.as_value<uint8_t>() != 0;
        }
        if (node.has_key(la::HI_NEG)) {
            AnyVal n = node.get(la::HI_NEG.code);
            hi_neg = !n.is_null() && n.is_value() && n.as_value<uint8_t>() != 0;
        }
        if (lo_neg) s += "-";
        s += render_pat_src(map_of(node.get(la::LHS.code)));
        s += "..=";
        if (hi_neg) s += "-";
        s += render_pat_src(map_of(node.get(la::RHS.code)));
        return s;
    }
    case la::PAT_REF: {
        std::string s = "&";
        if (node.has_key(la::IS_MUT)) {
            AnyVal m = node.get(la::IS_MUT.code);
            if (!m.is_null() && m.is_value() && m.as_value<uint8_t>() != 0) s += "mut ";
        }
        s += render_pat_src(map_of(node.get(la::VALUE.code)));
        return s;
    }
    case la::PAT_REST: return "..";
    case la::PAT_STRUCT: {
        std::string s(str_of(node.get(la::NAME.code)));
        s += " { ";
        if (node.has_key(la::ITEMS)) {
            auto items = arr_of(node.get(la::ITEMS.code));
            for (uint64_t i = 0; i < items.size(); ++i) {
                if (i) s += ", ";
                auto fi = map_of(items.get(i));
                if (code_of(fi) == la::PAT_FIELD) {
                    s += std::string(str_of(fi.get(la::NAME.code)));
                    if (fi.has_key(la::VALUE)) {
                        s += ": ";
                        s += render_pat_src(map_of(fi.get(la::VALUE.code)));
                    }
                } else if (code_of(fi) == la::PAT_REST) {
                    s += "..";
                }
            }
        }
        s += " }";
        return s;
    }
    default:
        return std::format("/* render_pat: unsupported AST code {} */", c);
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

    case la::ASSIGN: {
        std::string s(str_of(node.get(la::NAME.code)));
        s += " = ";
        s += render_expr_src(map_of(node.get(la::VALUE.code)));
        s += ";";
        return s;
    }

    case la::COMPOUND_ASSIGN: {
        std::string s(str_of(node.get(la::NAME.code)));
        s += " ";
        s += std::string(str_of(node.get(la::OP.code)));
        s += " ";
        s += render_expr_src(map_of(node.get(la::VALUE.code)));
        s += ";";
        return s;
    }

    case la::DEREF_WRITE: {
        std::string s = "*";
        s += std::string(str_of(node.get(la::NAME.code)));
        s += " = ";
        s += render_expr_src(map_of(node.get(la::VALUE.code)));
        s += ";";
        return s;
    }

    case la::DEREF_COMPOUND: {
        std::string s = "*";
        s += std::string(str_of(node.get(la::NAME.code)));
        s += " ";
        s += std::string(str_of(node.get(la::OP.code)));
        s += " ";
        s += render_expr_src(map_of(node.get(la::VALUE.code)));
        s += ";";
        return s;
    }

    case la::WHILE: {
        std::string s = "while ";
        if (node.has_key(la::COND)) {
            s += render_expr_src(map_of(node.get(la::COND.code)));
        } else if (node.has_key(la::PAT)) {
            s += "let ";
            s += render_pat_src(map_of(node.get(la::PAT.code)));
            s += " = ";
            s += render_expr_src(map_of(node.get(la::VALUE.code)));
        }
        s += " ";
        s += render_block_src(map_of(node.get(la::BODY.code)));
        return s;
    }

    case la::LOOP: {
        std::string s = "loop ";
        s += render_block_src(map_of(node.get(la::BODY.code)));
        return s;
    }

    case la::LABELED_LOOP: {
        std::string s = "'";
        s += std::string(str_of(node.get(la::LABEL.code)));
        s += ": ";
        s += render_block_src(map_of(node.get(la::BODY.code)));
        return s;
    }

    case la::FOR: {
        std::string s = "for ";
        s += std::string(str_of(node.get(la::NAME.code)));
        s += " in ";
        s += render_expr_src(map_of(node.get(la::LHS.code)));
        bool incl = false;
        if (node.has_key(la::INCLUSIVE)) {
            AnyVal n = node.get(la::INCLUSIVE.code);
            incl = !n.is_null() && n.is_value() && n.as_value<uint8_t>() != 0;
        }
        s += incl ? "..=" : "..";
        s += render_expr_src(map_of(node.get(la::RHS.code)));
        s += " ";
        s += render_block_src(map_of(node.get(la::BODY.code)));
        return s;
    }

    case la::FOR_EACH: {
        std::string s = "for ";
        s += std::string(str_of(node.get(la::NAME.code)));
        s += " in ";
        s += render_expr_src(map_of(node.get(la::ITER.code)));
        s += " ";
        s += render_block_src(map_of(node.get(la::BODY.code)));
        return s;
    }

    case la::BREAK: {
        std::string s = "break";
        if (node.has_key(la::LABEL)) {
            s += " '";
            s += std::string(str_of(node.get(la::LABEL.code)));
        }
        if (node.has_key(la::VALUE)) {
            s += " ";
            s += render_expr_src(map_of(node.get(la::VALUE.code)));
        }
        s += ";";
        return s;
    }

    case la::CONTINUE: {
        std::string s = "continue";
        if (node.has_key(la::LABEL)) {
            s += " '";
            s += std::string(str_of(node.get(la::LABEL.code)));
        }
        s += ";";
        return s;
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

lir::LExprPtr SemaChecker::make_metacall_placeholder_expr(TypeRef ty) {
    using K = LogosType::Kind;
    auto k = ty.kind();
    if (k == K::Bool) return builder().lit_bool(false, ty);
    if (k == K::F32 || k == K::F64 || k == K::FloatLit)
        return builder().lit_float(0.0, ty);
    if (k == K::Slice && ty.elem() && ty.elem().kind() == K::U8)
        return builder().lit_str("", ty);
    // All integer kinds (and IntLit) accept lit_int.
    return builder().lit_int(0, ty);
}

std::string SemaChecker::render_ctfe_lit(const ctfe::CtfeValue& v) {
    using K = LogosType::Kind;
    if (v.kind == K::Bool) return v.b ? "true" : "false";
    if (v.kind == K::Slice) {
        std::string s = "\"";
        for (char c : v.s) {
            switch (c) {
            case '\\': s += "\\\\"; break;
            case '"':  s += "\\\""; break;
            case '\n': s += "\\n";  break;
            case '\r': s += "\\r";  break;
            case '\t': s += "\\t";  break;
            default:   s += c;      break;
            }
        }
        s += "\"";
        return s;
    }
    if (v.kind == K::F32 || v.kind == K::F64 || v.kind == K::FloatLit) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.17g", v.f);
        std::string s = buf;
        // Need a decimal point so the parser sees LIT_FLOAT, not LIT_INT.
        if (s.find('.') == std::string::npos && s.find('e') == std::string::npos
            && s.find('n') == std::string::npos /* nan */
            && s.find('i') == std::string::npos /* inf */) {
            s += ".0";
        }
        if (v.kind == K::F32)      s += "f32";
        else if (v.kind == K::F64) s += "f64";
        return s;
    }
    // Integer kinds.
    std::string s;
    bool sgn = (v.kind == K::I8  || v.kind == K::I16 || v.kind == K::I24 ||
                v.kind == K::I32 || v.kind == K::I56 || v.kind == K::I64 ||
                v.kind == K::I128 || v.kind == K::IntLit);
    if (sgn) {
        // INT64_MIN dance: -(-INT64_MIN) is UB; emit "(-N)" via two's-complement
        // arithmetic on the magnitude string.
        if (v.i < 0) { s = "(-"; s += std::to_string(-(v.i + 1)); s.back()++; s += ")"; }
        else         s = std::to_string(v.i);
    } else {
        s = std::to_string(v.u);
    }
    switch (v.kind) {
    case K::I8:  s += "i8";  break;
    case K::I16: s += "i16"; break;
    case K::I32: s += "i32"; break;
    case K::I64: s += "i64"; break;
    case K::U8:  s += "u8";  break;
    case K::U16: s += "u16"; break;
    case K::U32: s += "u32"; break;
    case K::U64: s += "u64"; break;
    default: break;  // IntLit / I24 / U24 / I56 / U56 — leave unsuffixed
    }
    return s;
}

// ── Stage 2: item-position rendering ─────────────────────────────────────────
// Renders top-level items (fn, struct, enum, impl, use, type_alias, const_def)
// back to Logos source. Used by `--dump-metaprog` to print metafn-generated
// AST documents in human/agent-readable form. Coverage is best-effort: if a
// node shape isn't handled, we emit a `/* unsupported */` comment rather
// than crashing — the dump is a debugging aid, not a re-compilation contract.

namespace {

bool flag_set(TinyMapView node, ast::Key k) {
    if (!node.has_key(k)) return false;
    auto v = node.get(k.code);
    return !v.is_null() && v.is_value() && v.as_value<uint8_t>() != 0;
}

} // namespace

std::string SemaChecker::render_path_parts_(TinyMapView node) {
    // PATH_PARTS: array of {NAME: ident}, dot-separated tail of a path.
    std::string s;
    if (!node.has_key(la::mod::PATH_PARTS)) return s;
    auto av = node.get(la::mod::PATH_PARTS.code);
    if (av.is_null()) return s;
    auto items = arr_of(av);
    for (uint64_t i = 0; i < items.size(); ++i) {
        auto p = map_of(items.get(i));
        s += ".";
        s += std::string(str_of(p.get(la::NAME.code)));
    }
    return s;
}

std::string SemaChecker::render_type_param_src_(TinyMapView node) {
    // TYPE_PARAM: NAME, optional ITEMS (bounds), optional IS_VARIADIC,
    // optional NAME_VAR (antiquot in a quote).
    std::string s;
    if (node.has_key(la::NAME)) s += std::string(str_of(node.get(la::NAME.code)));
    else if (node.has_key(la::NAME_VAR)) s += "<antiquot>";
    if (flag_set(node, la::IS_VARIADIC)) s += "...";
    if (node.has_key(la::ITEMS)) {
        auto items = arr_of(node.get(la::ITEMS.code));
        if (items.size() > 0) {
            s += ": ";
            for (uint64_t i = 0; i < items.size(); ++i) {
                if (i) s += " + ";
                s += render_type_src(map_of(items.get(i)));
            }
        }
    }
    return s;
}

std::string SemaChecker::render_type_param_list_(TinyMapView node) {
    if (!node.has_key(la::ITEMS)) return {};
    auto items = arr_of(node.get(la::ITEMS.code));
    if (items.size() == 0) return {};
    std::string s = "<";
    for (uint64_t i = 0; i < items.size(); ++i) {
        if (i) s += ", ";
        s += render_type_param_src_(map_of(items.get(i)));
    }
    s += ">";
    return s;
}

std::string SemaChecker::render_param_src_(TinyMapView node) {
    // PARAM: NAME, TYPE, IS_REF, IS_MUT, IS_VARIADIC. Self-receivers come
    // through as PARAM with IS_REF and no TYPE.
    std::string s;
    bool is_ref = flag_set(node, la::IS_REF);
    bool is_mut = flag_set(node, la::IS_MUT);
    bool is_var = flag_set(node, la::IS_VARIADIC);
    std::string name;
    if (node.has_key(la::NAME)) name = std::string(str_of(node.get(la::NAME.code)));
    if (is_ref && !node.has_key(la::TYPE)) {
        s += "&";
        if (is_mut) s += "mut ";
        s += name.empty() ? "self" : name;
        return s;
    }
    if (is_mut && !is_ref) s += "mut ";
    s += name;
    if (node.has_key(la::TYPE)) {
        s += ": ";
        s += render_type_src(map_of(node.get(la::TYPE.code)));
    }
    if (is_var) s += "...";
    return s;
}

std::string SemaChecker::render_param_list_(TinyMapView node) {
    if (!node.has_key(la::ITEMS)) return "()";
    auto items = arr_of(node.get(la::ITEMS.code));
    std::string s = "(";
    for (uint64_t i = 0; i < items.size(); ++i) {
        if (i) s += ", ";
        s += render_param_src_(map_of(items.get(i)));
    }
    s += ")";
    return s;
}

std::string SemaChecker::render_field_def_src_(TinyMapView node) {
    // FIELD_DEF: NAME, TYPE, IS_PUB, IS_VARIADIC.
    std::string s;
    if (flag_set(node, la::IS_PUB)) s += "pub ";
    s += std::string(str_of(node.get(la::NAME.code)));
    s += ": ";
    s += render_type_src(map_of(node.get(la::TYPE.code)));
    if (flag_set(node, la::IS_VARIADIC)) s += "...";
    return s;
}

std::string SemaChecker::render_variant_def_src_(TinyMapView node) {
    // VARIANT_DEF: NAME, optional VALUE (discriminant), optional ITEMS
    // (tuple-style payload types).
    std::string s(str_of(node.get(la::NAME.code)));
    if (node.has_key(la::ITEMS)) {
        auto items = arr_of(node.get(la::ITEMS.code));
        if (items.size() > 0) {
            s += "(";
            for (uint64_t i = 0; i < items.size(); ++i) {
                if (i) s += ", ";
                s += render_type_src(map_of(items.get(i)));
            }
            s += ")";
        }
    }
    if (node.has_key(la::VALUE)) {
        s += " = ";
        if (flag_set(node, la::LO_NEG)) s += "-";
        s += std::string(str_of(node.get(la::VALUE.code)));
    }
    return s;
}

std::string SemaChecker::render_item_src(TinyMapView node) {
    if (node.is_null()) return "";
    int32_t c = code_of(node);

    switch (c) {

    case la::USE: {
        std::string s;
        if (flag_set(node, la::IS_PUB)) s += "pub ";
        s += "use ";
        s += std::string(str_of(node.get(la::NAME.code)));
        s += render_path_parts_(node);
        s += ";";
        return s;
    }

    case la::CONST_DEF: {
        std::string s;
        if (flag_set(node, la::IS_PUB)) s += "pub ";
        s += "const ";
        s += std::string(str_of(node.get(la::NAME.code)));
        if (node.has_key(la::TYPE_PARAMS))
            s += render_type_param_list_(map_of(node.get(la::TYPE_PARAMS.code)));
        if (node.has_key(la::TYPE)) {
            s += ": ";
            s += render_type_src(map_of(node.get(la::TYPE.code)));
        }
        if (node.has_key(la::VALUE)) {
            s += " = ";
            // VALUE may be an expr or a HermesStatic literal. Use expr renderer;
            // unsupported shapes (LIT_HSTATIC) are tagged as such.
            s += render_expr_src(map_of(node.get(la::VALUE.code)));
        }
        s += ";";
        return s;
    }

    case la::TYPE_ALIAS: {
        std::string s;
        if (flag_set(node, la::IS_PUB)) s += "pub ";
        s += "type ";
        s += std::string(str_of(node.get(la::NAME.code)));
        if (node.has_key(la::TYPE_PARAMS))
            s += render_type_param_list_(map_of(node.get(la::TYPE_PARAMS.code)));
        s += " = ";
        s += render_type_src(map_of(node.get(la::TYPE.code)));
        s += ";";
        return s;
    }

    case la::ENUM: {
        std::string s;
        if (flag_set(node, la::IS_PUB)) s += "pub ";
        s += "enum ";
        if (node.has_key(la::NAME)) s += std::string(str_of(node.get(la::NAME.code)));
        else if (node.has_key(la::NAME_VAR)) s += "<antiquot>";
        if (node.has_key(la::TYPE_PARAMS))
            s += render_type_param_list_(map_of(node.get(la::TYPE_PARAMS.code)));
        if (node.has_key(la::TYPE)) {
            s += ": ";
            s += render_type_src(map_of(node.get(la::TYPE.code)));
        }
        s += " {\n";
        if (node.has_key(la::ITEMS)) {
            auto items = arr_of(node.get(la::ITEMS.code));
            for (uint64_t i = 0; i < items.size(); ++i) {
                s += "    ";
                s += render_variant_def_src_(map_of(items.get(i)));
                s += ",\n";
            }
        }
        s += "}";
        return s;
    }

    case la::STRUCT: {
        std::string s;
        if (flag_set(node, la::IS_PUB)) s += "pub ";
        s += "struct ";
        if (node.has_key(la::NAME)) s += std::string(str_of(node.get(la::NAME.code)));
        else if (node.has_key(la::NAME_VAR)) s += "<antiquot>";
        if (node.has_key(la::TYPE_PARAMS))
            s += render_type_param_list_(map_of(node.get(la::TYPE_PARAMS.code)));
        // Field-less / unit struct: STRUCT with TYPE-only (newtype) — rare,
        // surface here as a comment to avoid losing info.
        if (!node.has_key(la::FIELDS)) {
            s += ";";
            return s;
        }
        s += " {\n";
        auto fields = arr_of(node.get(la::FIELDS.code));
        for (uint64_t i = 0; i < fields.size(); ++i) {
            s += "    ";
            s += render_field_def_src_(map_of(fields.get(i)));
            s += ",\n";
        }
        s += "}";
        // Inherent methods stored alongside (ITEMS) — render after the struct
        // body as `impl Name { ... }` for round-trippable output. The parser
        // accepts the legacy `struct Foo { fields, fn ... }` form too, but
        // splitting is clearer.
        if (node.has_key(la::ITEMS)) {
            auto items = arr_of(node.get(la::ITEMS.code));
            if (items.size() > 0) {
                s += "\n\nimpl ";
                if (node.has_key(la::NAME)) s += std::string(str_of(node.get(la::NAME.code)));
                if (node.has_key(la::TYPE_PARAMS))
                    s += render_type_param_list_(map_of(node.get(la::TYPE_PARAMS.code)));
                s += " {\n";
                for (uint64_t i = 0; i < items.size(); ++i) {
                    auto sub = render_item_src(map_of(items.get(i)));
                    // Indent each line.
                    std::string indented;
                    size_t start = 0;
                    while (start < sub.size()) {
                        size_t nl = sub.find('\n', start);
                        if (nl == std::string::npos) {
                            indented += "    ";
                            indented += sub.substr(start);
                            break;
                        }
                        indented += "    ";
                        indented += sub.substr(start, nl - start + 1);
                        start = nl + 1;
                    }
                    s += indented;
                    s += "\n";
                }
                s += "}";
            }
        }
        return s;
    }

    case la::IMPL_BLOCK: {
        std::string s;
        if (flag_set(node, la::IS_UNSAFE)) s += "unsafe ";
        if (flag_set(node, la::IS_NEGATIVE)) s += "/* negative */ ";
        s += "impl";
        if (node.has_key(la::IMPL_TYPE_PARAMS))
            s += render_type_param_list_(map_of(node.get(la::IMPL_TYPE_PARAMS.code)));
        if (node.has_key(la::NAME)) {
            s += " ";
            s += std::string(str_of(node.get(la::NAME.code)));
            if (node.has_key(la::TYPE_PARAMS))
                s += render_type_param_list_(map_of(node.get(la::TYPE_PARAMS.code)));
            s += " for ";
            s += render_type_src(map_of(node.get(la::TYPE.code)));
        } else {
            // Inherent impl: just `impl<TP> Type { ... }`.
            if (node.has_key(la::TYPE_PARAMS))
                s += render_type_param_list_(map_of(node.get(la::TYPE_PARAMS.code)));
            s += " ";
            s += render_type_src(map_of(node.get(la::TYPE.code)));
        }
        s += " {\n";
        if (node.has_key(la::ITEMS)) {
            auto items = arr_of(node.get(la::ITEMS.code));
            for (uint64_t i = 0; i < items.size(); ++i) {
                auto sub = render_item_src(map_of(items.get(i)));
                std::string indented;
                size_t start = 0;
                while (start < sub.size()) {
                    size_t nl = sub.find('\n', start);
                    if (nl == std::string::npos) {
                        indented += "    ";
                        indented += sub.substr(start);
                        break;
                    }
                    indented += "    ";
                    indented += sub.substr(start, nl - start + 1);
                    start = nl + 1;
                }
                s += indented;
                s += "\n";
            }
        }
        s += "}";
        return s;
    }

    case la::FN:
    case la::EXTERN_FN:
    case la::STATIC_FN: {
        std::string s;
        if (flag_set(node, la::IS_PUB))    s += "pub ";
        if (flag_set(node, la::IS_UNSAFE)) s += "unsafe ";
        if (c == la::EXTERN_FN.code)       s += "extern ";
        s += "fn ";
        s += std::string(str_of(node.get(la::NAME.code)));
        if (node.has_key(la::TYPE_PARAMS))
            s += render_type_param_list_(map_of(node.get(la::TYPE_PARAMS.code)));
        s += render_param_list_(map_of(node.get(la::PARAMS.code)));
        if (node.has_key(la::RET_TYPE)) {
            s += " -> ";
            s += render_type_src(map_of(node.get(la::RET_TYPE.code)));
        }
        if (node.has_key(la::BODY)) {
            s += " ";
            s += render_block_src(map_of(node.get(la::BODY.code)));
        } else {
            s += ";";
        }
        return s;
    }

    case la::ANNOTATION: {
        // #[name] / #[name=val] / #[name(args)]. Render approximately —
        // the args may be ANNOT_KV/ANNOT_POS/ANNOT_ARR.
        std::string s = "#[";
        if (node.has_key(la::NAME)) s += std::string(str_of(node.get(la::NAME.code)));
        s += "]";
        return s;
    }

    case la::METACALL_ITEM:
    case la::METACALL_ITEM_DONE: {
        // Item-position metacall: don't recurse into the call expr — it has
        // already been consumed by the metaprog driver. Surface the trace.
        return c == la::METACALL_ITEM.code
            ? std::string("metacall <pending>;")
            : std::string("/* metacall consumed */");
    }

    default:
        return std::format("/* render_item: unsupported AST code {} */", c);
    }
}

std::string SemaChecker::render_module_src(TinyMapView node) {
    // MODULE: NAME, PATH_PARTS, USES, ITEMS.
    if (node.is_null() || code_of(node) != la::MODULE) return "";
    std::string s = "package ";
    s += std::string(str_of(node.get(la::NAME.code)));
    s += render_path_parts_(node);
    s += ";\n\n";
    if (node.has_key(la::USES)) {
        auto uses = arr_of(node.get(la::USES.code));
        for (uint64_t i = 0; i < uses.size(); ++i) {
            s += render_item_src(map_of(uses.get(i)));
            s += "\n";
        }
        if (uses.size() > 0) s += "\n";
    }
    if (node.has_key(la::ITEMS)) {
        auto items = arr_of(node.get(la::ITEMS.code));
        for (uint64_t i = 0; i < items.size(); ++i) {
            s += render_item_src(map_of(items.get(i)));
            s += "\n\n";
        }
    }
    return s;
}

} // namespace logos::compiler
