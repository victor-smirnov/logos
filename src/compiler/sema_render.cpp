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

} // namespace logos::compiler
