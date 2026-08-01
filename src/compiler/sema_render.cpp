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

#include <logos/writ/compat.hpp>
#include <logos/writ/compat.hpp>
#include <logos/writ/compat.hpp>

#include <algorithm>
#include <cstdio>
#include <format>
#include <functional>
#include <string>
#include <vector>

namespace logos::compiler {

namespace la = ast;
using writ::TinyMapView;
using writ::AnyVal;

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
    if (dump_syntactic_types_) return render_type_src_syntactic_(node);
    auto t = resolve_type(node);
    if (!t) return "_";
    // SOURCE form, not the name form. What this returns is spliced into a block
    // that is RE-PARSED (that is how `println!` carries its argument types), so
    // a generic enum has to come back with its arguments. See `type_str`.
    return type_str(t, /*source_form=*/true);
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
        // VALUE is the lexer's STRING token captured WITH surrounding
        // quotes — already a valid Logos string literal. Emit as-is.
        // (escape_str_lit would double-quote if applied here.)
        auto raw = str_of(node.get(la::VALUE.code));
        if (raw.size() >= 2 && raw.front() == '"' && raw.back() == '"')
            return std::string(raw);
        return escape_str_lit(raw);
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
        // CALLEE for plain `name(args)` form; NAME after antiquot
        // substitution wrote NAME_VAR(idx) → NAME(string). Antiquot alts
        // carry ARGS as an EXACT call_arg_list wrapper (`{ITEMS:[...]}`)
        // since the grammar unification — the tom_form probe below unwraps
        // it; no payload skip.
        std::string callee_str(str_of(node.get(la::CALLEE.code)));
        if (callee_str.empty()) callee_str = std::string(str_of(node.get(la::NAME.code)));
        // T2-28 `pkg.path::fn(args)`: the PACKAGE qualifier is NOT in CALLEE — it
        // is RECEIVER (first segment) + QUAL_PARTS (the rest). Rendering only
        // CALLEE dropped it, and the whole point of the form is to name ONE of
        // several same-named free fns across packages, so the dropped text does
        // not mean the same thing. Under `-g` that is not cosmetic: the dump is
        // REPARSED and REPLACES the in-memory synth doc, so the emitted call
        // silently re-resolves by bare name — and a same-named fn in the
        // chunk's own package wins over the qualified target.
        // Asked of extract_pkg_qualifier, the function SEMA reads the qualifier
        // with, so render and resolve cannot drift.
        std::string s;
        std::string qual = extract_pkg_qualifier(node);
        if (!qual.empty()) { s += qual; s += "::"; }
        s += callee_str;
        s += "(";
        if (node.has_key(la::ARGS)) {
            auto args_av = node.get(la::ARGS.code);
            if (args_av.is_pointer()) {
                auto args_tom = map_of(args_av);
                bool tom_form = !args_tom.is_null() && args_tom.has_key(la::ITEMS);
                auto items = tom_form
                    ? arr_of(args_tom.get(la::ITEMS.code))
                    : arr_of(args_av);
                for (uint64_t i = 0; i < items.size(); ++i) {
                    if (i > 0) s += ", ";
                    s += render_expr_src(map_of(items.get(i)));
                }
            }
        }
        s += ")";
        return s;
    }

    case la::GENERIC_CALL: {
        // Same qualifier slots as CALL above — `pkg.path::fn::<T>(args)` is the
        // turbofished alternative of the SAME grammar production pair, so the
        // hole was the same one and the fix has to land on both or the next
        // emitter to write a turbofished qualified call rediscovers it.
        std::string s;
        std::string qual = extract_pkg_qualifier(node);
        if (!qual.empty()) { s += qual; s += "::"; }
        s += std::string(str_of(node.get(la::CALLEE.code)));
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
        // Grammar: `Buffer::<u64>::new()` parses to RECEIVER=Buffer,
        // TYPE_PARAMS=<u64>, NAME=new, ARGS=…  — turbofish BEFORE the
        // method name.
        std::string s(str_of(node.get(la::RECEIVER.code)));
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
        s += "::";
        s += std::string(str_of(node.get(la::NAME.code)));
        s += "(";
        if (node.has_key(la::ARGS)) {
            auto args_av = node.get(la::ARGS.code);
            if (args_av.is_pointer()) {
                auto args_tom = map_of(args_av);
                bool tom_form = !args_tom.is_null() && args_tom.has_key(la::ITEMS);
                auto items = tom_form
                    ? arr_of(args_tom.get(la::ITEMS.code))
                    : arr_of(args_av);
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
        std::string s = render_expr_src(map_of(node.get(la::RECEIVER.code)));
        s += ".";
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
            // ARGS is flat ObjectArray (from `$...`) for the bare form,
            // or TOM-with-ITEMS for the turbofish form via call_arg_list.
            auto args_av = node.get(la::ARGS.code);
            if (args_av.is_pointer()) {
                auto args_tom = map_of(args_av);
                bool tom_form = !args_tom.is_null() && args_tom.has_key(la::ITEMS);
                auto items = tom_form
                    ? arr_of(args_tom.get(la::ITEMS.code))
                    : arr_of(args_av);
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
        // Receiver — no parens for clean output (precedence is fine for
        // `.field` chains). FIELD for plain form; NAME after antiquot
        // substitution rewrote NAME_VAR(idx) → NAME(string).
        std::string s = render_expr_src(map_of(node.get(la::RECEIVER.code)));
        s += ".";
        if (node.has_key(la::FIELD))
            s += std::string(str_of(node.get(la::FIELD.code)));
        else if (node.has_key(la::NAME))
            s += std::string(str_of(node.get(la::NAME.code)));
        else if (node.has_key(la::NAME_VAR))
            s += "<antiquot>";
        return s;
    }

    case la::TUPLE_INDEX: {
        std::string s = "(";
        s += render_expr_src(map_of(node.get(la::RECEIVER.code)));
        s += ").";
        s += std::string(str_of(node.get(la::FIELD.code)));
        return s;
    }

    case la::RANGE_EXPR: {
        // `lo..hi` / `lo..=hi`, EITHER BOUND OPTIONAL — `a[..]`, `a[2..]`,
        // `a[..n]`. Missing here, the default arm rendered
        // `/* render_expr: unsupported AST code 112 */` INTO the synthesised
        // block, and `format!("{}", (&a[..]).len())` then failed with
        // "format!: internal — synthesised block failed to parse" — a render
        // gap reported as an internal error at the call site rather than as the
        // missing case it is. Found by the enumerator's new `layout` family,
        // whose every program reads its row count from `(&a[..]).len()`.
        std::string s;
        if (node.has_key(la::LHS))
            s += render_expr_src(map_of(node.get(la::LHS.code)));
        bool incl = false;
        if (node.has_key(la::INCLUSIVE)) {
            AnyVal av = node.get(la::INCLUSIVE.code);
            if (!av.is_null() && av.is_value()) incl = av.as_value<uint8_t>() != 0;
        }
        s += incl ? "..=" : "..";
        if (node.has_key(la::RHS))
            s += render_expr_src(map_of(node.get(la::RHS.code)));
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

    case la::ADDR_OF_MUT: {
        return "&mut " + render_expr_src(map_of(node.get(la::VALUE.code)));
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

    // ── Enum-variant literal / associated-const path ─────────────────────
    // `Option::None`, `Color::Red`, `u64::MAX`, `Option::None::<i64>`,
    // `pkg.path.Enum::Variant`, plus the payload forms `E::V(a, b)`,
    // `E::V::<T>(a)` and the struct shape `E::V { x: e }`.
    //
    // ⚠ AN UNRENDERED ENUM_LIT IS NOT A COSMETIC DUMP DEFECT. Under `-g` the
    // `--gen-dir` dump is REPARSED and the reparse REPLACES the synth doc, so
    // the rendered text is the code that gets compiled. A missing case
    // degrades to a `/* … */` comment, and a comment inside an argument list
    // is a WELL-FORMED parse of the wrong arity — `Result::Ok(Option::None)`
    // comes back as `Result::Ok()`. The round-trip's shape gate is a
    // top-level item census, which cannot see an arity change inside a body,
    // so the only symptom is a sema error against generated source. Every
    // quote-routed emitter returning a unit variant hits this.
    case la::ENUM_LIT:
    case la::ENUM_LIT_DATA: {
        std::string s = node.has_key(la::NAME)
            ? std::string(str_of(node.get(la::NAME.code)))
            : std::string("<antiquot>");
        // `pkg.path.Type::member`: NAME is the FIRST package segment and
        // QUAL_PARTS the rest — the last segment is the type. Rendering the
        // whole path keeps the dump resolvable the way lower_enum_lit reads it.
        if (node.has_key(la::QUAL_PARTS)) {
            auto parts = arr_of(node.get(la::QUAL_PARTS.code));
            for (uint64_t i = 0; i < parts.size(); ++i) {
                s += ".";
                s += std::string(str_of(map_of(parts.get(i)).get(la::NAME.code)));
            }
        }
        // FIELD is the member. Guarded rather than assumed, as PAT_VARIANT_DATA
        // guards it: an antiquot-substituted node need not carry the slot.
        if (node.has_key(la::FIELD)) {
            s += "::";
            s += std::string(str_of(node.get(la::FIELD.code)));
        }
        // ⚠ The turbofish follows the MEMBER here (`Option::None::<i64>`),
        // whereas STATIC_CALL puts it before (`Buffer::<u64>::new()`). Same
        // slot, different position — the grammar alternatives differ.
        if (node.has_key(la::TYPE_PARAMS)) {
            auto tplist = map_of(node.get(la::TYPE_PARAMS.code));
            if (!tplist.is_null() && tplist.has_key(la::ITEMS)) {
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
        if (c == la::ENUM_LIT) return s;   // unit variant / assoc const: no payload
        bool struct_shape =
            node.has_key(la::variant::IS_STRUCT_SHAPE) &&
            node.get(la::variant::IS_STRUCT_SHAPE.code).as_value<int32_t>() != 0;
        if (struct_shape) {
            // `E::V { x: e, y }` — ITEMS are FIELD_INIT / FIELD_SHORTHAND,
            // the same shapes STRUCT_LIT carries.
            s += " { ";
            bool first_f = true;
            if (node.has_key(la::ITEMS)) {
                auto items = arr_of(node.get(la::ITEMS.code));
                for (uint64_t i = 0; i < items.size(); ++i) {
                    if (!first_f) s += ", ";
                    first_f = false;
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
            s += " }";
            return s;
        }
        s += "(";
        if (node.has_key(la::ARGS)) {
            // Dual shape, as on STATIC_CALL/METHOD_CALL: the turbofish form
            // routes through `enum_lit_args` and arrives as a TOM-with-ITEMS,
            // the bare form as a flat ObjectArray from `$...`.
            auto args_av = node.get(la::ARGS.code);
            if (args_av.is_pointer()) {
                auto args_tom = map_of(args_av);
                bool tom_form = !args_tom.is_null() && args_tom.has_key(la::ITEMS);
                auto items = tom_form
                    ? arr_of(args_tom.get(la::ITEMS.code))
                    : arr_of(args_av);
                for (uint64_t i = 0; i < items.size(); ++i) {
                    if (i) s += ", ";
                    s += render_expr_src(map_of(items.get(i)));
                }
            }
        }
        s += ")";
        return s;
    }

    // `e?` — the postfix try operator. VALUE is the operand (grammar's `$0`
    // in the postfix chain). Parenthesised for the same reason BINOP/UNARY
    // children are: cheap, and the reparse cannot mis-bind it.
    //
    // ⚠ Same failure mode as the enum-literal case above, one step worse: a
    // dropped `?` leaves a bare `;` where a statement was, or a comment where
    // a call argument was, and BOTH reparse. Every Deem emitter that binds a
    // mapping rel writes `let r = f(x)?;`.
    case la::TRY_EXPR: {
        return "(" + render_expr_src(map_of(node.get(la::VALUE.code))) + ")?";
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
                // `$...` collector junk (the scrutinee alias) sits in ITEMS
                // alongside the arms — only MATCH_ARM nodes are arms.
                if (code_of(arm) != la::MATCH_ARM) continue;
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

    case la::LIT_WSTATIC: {
        // WritStatic literal at type-arg position — VALUE wraps the nested
        // writ_lit AST (grammar slot 212). Unwrap and render via the writ
        // cases below (`@{...}` outer form). Needed by the ADR 0021 factory
        // drain: the driver hands the CFG document to the metaclass factory
        // as source text keyed by the doc's content hash.
        if (!node.has_key(la::VALUE))
            return "/* render_expr: LIT_WSTATIC without VALUE */";
        return render_expr_src(map_of(node.get(la::VALUE.code)));
    }

    // ── Writ literal expressions ──
    // Outer-position: prefix `@` (`@{...}`, `@[...]`, `@4`, `@"x"`,
    // `@true`, `@null`). Inner positions (WRIT_MAP entry VALUEs,
    // WRIT_ARRAY items) call render_writ_val_inner_ which omits
    // the `@` for scalars (grammar rejects `@4` at writ_val position;
    // map/array can go either way, we omit for cleanliness).
    case la::WRIT_NULL: return "@null";
    case la::WRIT_BOOL: {
        AnyVal v = node.get(la::VALUE.code);
        bool b = !v.is_null() && v.is_value() && v.as_value<uint8_t>() != 0;
        return b ? "@true" : "@false";
    }
    case la::WRIT_INT:
        return "@" + std::string(str_of(node.get(la::VALUE.code)));
    case la::WRIT_NEG_INT:
        return "@-" + std::string(str_of(node.get(la::VALUE.code)));
    case la::WRIT_FLOAT:
        return "@" + std::string(str_of(node.get(la::VALUE.code)));
    case la::WRIT_STR: {
        auto raw = str_of(node.get(la::VALUE.code));
        return "@" + std::string(raw);
    }
    case la::WRIT_MAP: {
        std::string s = "@{";
        if (node.has_key(la::ITEMS)) {
            auto items = arr_of(node.get(la::ITEMS.code));
            for (uint64_t i = 0; i < items.size(); ++i) {
                if (i) s += ", ";
                auto entry = map_of(items.get(i));
                if (code_of(entry) == la::WRIT_ENTRY) {
                    auto key = str_of(entry.get(la::KEY.code));
                    s += std::string(key);
                    s += ": ";
                    if (entry.has_key(la::VALUE))
                        s += render_writ_val_inner_(map_of(entry.get(la::VALUE.code)));
                }
            }
        }
        s += "}";
        return s;
    }
    case la::WRIT_ARRAY: {
        std::string s = "@[";
        if (node.has_key(la::ITEMS)) {
            auto items = arr_of(node.get(la::ITEMS.code));
            for (uint64_t i = 0; i < items.size(); ++i) {
                if (i) s += ", ";
                s += render_writ_val_inner_(map_of(items.get(i)));
            }
        }
        s += "]";
        return s;
    }
    case la::WRIT_TYPE_LIT: {
        // `<type:T>` — a Logos type embedded in a Writ literal. When T is a
        // type-param BOUND in the current scope (a generic const's value doc
        // being re-resolved per instantiation, ADR 0021 Phase 4a), render the
        // SUBSTITUTED type: the captured wstatic_sources text must match the
        // hash walk, which hashes `type_str(resolve_type(T))` — a raw `K`
        // under a concrete hash would hand the factory an unresolvable slot.
        if (node.has_key(la::TYPE)) {
            auto tnode = map_of(node.get(la::TYPE.code));
            // ADR 0021 C1: an unsized/VLE value column canonicalizes to the
            // string tag ("str") — the same representation the concrete-decl
            // doc carries — so identity holds across arrival paths.
            std::string tag = cfg_str_tag_(resolve_type(tnode));
            if (!tag.empty()) return "\"" + tag + "\"";
            std::string sub;
            if (tnode.has_key(la::NAME) && !tnode.has_key(la::ITEMS)) {
                auto tn = std::string(str_of(tnode.get(la::NAME.code)));
                auto pit = current_type_params_.find(tn);
                if (pit != current_type_params_.end() && pit->second &&
                    TypeRef(pit->second).kind() != LogosType::Kind::TypeVar &&
                    TypeRef(pit->second).kind() != LogosType::Kind::ConstVar)
                    sub = type_str(pit->second);
            }
            return "<type:" + (sub.empty() ? render_type_src(tnode) : sub) + ">";
        }
        return "<type:>";
    }
    case la::CFG_SLOT_TYPE: {
        // `<type:CFG.SLOT>` — extract slot of WritStatic-typed const-generic.
        std::string s = "<type:";
        if (node.has_key(la::NAME)) s += std::string(str_of(node.get(la::NAME.code)));
        if (node.has_key(la::ITEMS)) {
            auto items = arr_of(node.get(la::ITEMS.code));
            for (uint64_t i = 0; i < items.size(); ++i) {
                s += ".";
                auto step = map_of(items.get(i));
                if (step.has_key(la::NAME)) s += std::string(str_of(step.get(la::NAME.code)));
            }
        }
        s += ">";
        return s;
    }
    case la::WRIT_CAP_IDENT: {
        return "$" + std::string(str_of(node.get(la::NAME.code)));
    }
    case la::WRIT_CAP_EXPR: {
        return "${" + render_expr_src(map_of(node.get(la::VALUE.code))) + "}";
    }
    case la::UNSAFE_BLOCK: {
        // `unsafe { ... }` may sit at expr position (e.g. RHS of `let x = unsafe { ... }`).
        // Delegate to the stmt-side renderer — it already handles BODY/ITEMS shapes.
        return render_stmt_src(node);
    }
    case la::BLOCK: {
        // A bare block at EXPRESSION position (block-as-expr — spliced
        // statement-fold bodies land here). Same delegation as above.
        return render_stmt_src(node);
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
        std::string s = node.has_key(la::NAME)
            ? std::string(str_of(node.get(la::NAME.code)))
            : std::string{};
        // FIELD only present for fully-qualified `Enum::Variant(args)`
        // form; the tuple-struct / bare-variant shape `Variant(args)`
        // has only NAME and ARGS (B-ts-01 grammar alt). Guard the
        // null-key read so render doesn't dereference a missing slot.
        if (node.has_key(la::FIELD)) {
            s += "::";
            s += std::string(str_of(node.get(la::FIELD.code)));
        }
        s += "(";
        // ARGS is wrapped: pat_variant_args grammar produces
        // `{ ITEMS: [args...] }` (PEG-side $... bucketing). Unwrap
        // one level before iterating.
        if (node.has_key(la::ARGS)) {
            auto args_wrap = map_of(node.get(la::ARGS.code));
            if (!args_wrap.is_null() && args_wrap.has_key(la::ITEMS)) {
                auto items = arr_of(args_wrap.get(la::ITEMS.code));
                for (uint64_t i = 0; i < items.size(); ++i) {
                    if (i) s += ", ";
                    s += render_pat_src(map_of(items.get(i)));
                }
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

    case la::MATCH: {
        // match-as-statement: same node shape as the expression form.
        return render_expr_src(node);
    }

    case la::PLACE_ASSIGN: {
        // G163-2 general place write: `a[i][j] = v;` / `(*p).0 = v;` …
        std::string s = render_expr_src(map_of(node.get(la::RECEIVER.code)));
        s += " = ";
        s += render_expr_src(map_of(node.get(la::VALUE.code)));
        s += ";";
        return s;
    }

    case la::LET: {
        std::string s = "let ";
        if (node.has_key(la::IS_MUT)) {
            AnyVal m = node.get(la::IS_MUT.code);
            if (!m.is_null() && m.is_value() && m.as_value<uint8_t>() != 0) s += "mut ";
        }
        if (node.has_key(la::NAME)) s += std::string(str_of(node.get(la::NAME.code)));
        else if (node.has_key(la::NAME_VAR)) s += "<antiquot>";
        if (node.has_key(la::TYPE)) {
            s += ": ";
            s += render_type_src(map_of(node.get(la::TYPE.code)));
        }
        // `let mut x: T;` — the declare-without-initializer form. Printing
        // ` = ` with nothing after it would render as source that does not
        // parse, and this dump is the oracle for what the emitters produced.
        if (node.has_key(la::VALUE)) {
            s += " = ";
            s += render_expr_src(map_of(node.get(la::VALUE.code)));
        }
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
        auto v = map_of(node.get(la::VALUE.code));
        std::string s = render_expr_src(v);
        // ⚠ A BLOCK-SHAPED expression statement takes NO trailing semicolon.
        // Unlike Rust, this grammar has no `block_expr ';'` statement form:
        // `{ … };`, `if c { … };`, `match x { … };`, `unsafe { … };` and the
        // loop forms are all syntax errors, so an unconditional `;` renders a
        // dump that does not reparse. Reached whenever an emitter puts a bare
        // scope at statement position (the Canon factory does, one per column).
        if (!v.is_null()) {
            switch (code_of(v)) {
            case la::BLOCK: case la::UNSAFE_BLOCK: case la::IF: case la::MATCH:
            case la::WHILE: case la::LOOP: case la::LABELED_LOOP:
            case la::FOR: case la::FOR_EACH:
                return s;
            default: break;
            }
        }
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

    case la::FIELD_WRITE: {
        // RECEIVER (str ident), FIELD (str ident), VALUE (expr).
        std::string s(str_of(node.get(la::RECEIVER.code)));
        s += ".";
        s += std::string(str_of(node.get(la::FIELD.code)));
        s += " = ";
        s += render_expr_src(map_of(node.get(la::VALUE.code)));
        s += ";";
        return s;
    }

    case la::CHAIN_FIELD_WRITE: {
        // RECEIVER (str ident), PATH (TOM with ITEMS = str array), VALUE (expr).
        std::string s(str_of(node.get(la::RECEIVER.code)));
        if (node.has_key(la::PATH)) {
            auto path_node = map_of(node.get(la::PATH.code));
            if (path_node.has_key(la::ITEMS)) {
                auto items = arr_of(path_node.get(la::ITEMS.code));
                for (uint64_t i = 0; i < items.size(); ++i) {
                    s += ".";
                    s += std::string(str_of(items.get(i)));
                }
            }
        }
        s += " = ";
        s += render_expr_src(map_of(node.get(la::VALUE.code)));
        s += ";";
        return s;
    }

    case la::IF: {
        // IF used at stmt position (no enclosing EXPR_STMT). Delegate to
        // expr-render which already knows the full shape; trailing `;`
        // is optional after a brace-bounded expression.
        return render_expr_src(node);
    }

    case la::UNSAFE_BLOCK: {
        // `unsafe { stmts; }` — a stmt that wraps a block with the
        // unsafe modifier. Inner BLOCK lives in BODY/ITEMS; some
        // shapes use BODY directly. Use BODY if present, fall back to
        // treating self as a BLOCK (ITEMS at top level).
        std::string s = "unsafe ";
        if (node.has_key(la::BODY)) {
            auto inner = map_of(node.get(la::BODY.code));
            if (code_of(inner) == la::BLOCK) s += render_block_src(inner);
            else                              s += render_stmt_src(inner);
        } else if (node.has_key(la::ITEMS)) {
            // Direct items on the unsafe node — treat as a block body.
            s += "{ ";
            auto items = arr_of(node.get(la::ITEMS.code));
            for (uint64_t i = 0; i < items.size(); ++i) {
                if (i) s += " ";
                s += render_stmt_src(map_of(items.get(i)));
            }
            s += " }";
        } else {
            s += "{}";
        }
        return s;
    }

    case la::LET_ELSE: {
        // `let PAT = expr else { … };` — PAT / VALUE / BODY (the diverging
        // else-block). Absent here, a --gen-dir dump printed the whole
        // statement as `/* unsupported AST code 141 */`, which reads as
        // "the emitter dropped it" rather than "the renderer cannot say it".
        std::string s = "let ";
        s += render_pat_src(map_of(node.get(la::PAT.code)));
        s += " = ";
        s += render_expr_src(map_of(node.get(la::VALUE.code)));
        s += " else ";
        auto els = map_of(node.get(la::BODY.code));
        if (code_of(els) == la::BLOCK) s += render_block_src(els);
        else                           s += render_stmt_src(els);
        s += ";";
        return s;
    }

    case la::BLOCK_STMT: {
        // bare scoping block `{ stmts… }` at statement position (BODY = block).
        if (node.has_key(la::BODY)) {
            auto inner = map_of(node.get(la::BODY.code));
            if (code_of(inner) == la::BLOCK) return render_block_src(inner);
            return render_stmt_src(inner);
        }
        return "{}";
    }

    default:
        return std::format("/* render_stmt: unsupported AST code {} */;", c);
    }
}

std::string SemaChecker::render_block_src(TinyMapView node) {
    if (node.is_null() || code_of(node) != la::BLOCK) return "{}";
    std::vector<std::string> stmts;
    if (node.has_key(la::ITEMS)) {
        auto items = arr_of(node.get(la::ITEMS.code));
        for (uint64_t i = 0; i < items.size(); ++i) {
            auto st = map_of(items.get(i));
            if (st.is_null()) continue;
            stmts.push_back(render_stmt_src(st));
        }
    }
    if (stmts.empty()) return "{}";
    // Every non-empty body renders one statement per line, indented — no
    // inline `{ return x; }` one-liners (user style rule: structure on its
    // own lines; dumps read like hand-written code). Nested blocks compose
    // recursively via the per-line prefixing below.
    std::string s = "{\n";
    for (auto& st : stmts) {
        size_t start = 0;
        for (;;) {
            size_t nl = st.find('\n', start);
            size_t end = (nl == std::string::npos) ? st.size() : nl;
            s += "    ";
            s.append(st, start, end - start);
            s += "\n";
            if (nl == std::string::npos) break;
            start = nl + 1;
        }
    }
    s += "}";
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
            case '\0': s += "\\0";  break;
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
    if (node.is_null()) return s;
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

std::string SemaChecker::render_type_arg_src_(TinyMapView node) {
    // One element of a `<…>` list at USE position (grammar `type_or_lt_arg`).
    // Most elements are ordinary types, but the rule also admits lifetimes,
    // const-generic ARGUMENTS and quote-only pack/repeat forms — none of which
    // the type renderer knows. Handle those structurally; everything else is a
    // real type and goes to the type renderer.
    if (node.is_null()) return "_";
    int32_t c = code_of(node);
    auto name_of = [&]() -> std::string {
        return node.has_key(la::NAME) ? std::string(str_of(node.get(la::NAME.code)))
                                      : std::string{};
    };
    switch (c) {
    case la::LIFETIME_PARAM:  return name_of().empty() ? "'_" : name_of();
    case la::PACK_EXPAND:     return name_of() + "...";
    case la::ANTIQUOT_TYPE:   return "$" + name_of();
    case la::ANTIQUOT_PACK:   return "$" + name_of() + "...";
    case la::LIT_INT:         return render_expr_src(node);
    case la::PAREN_EXPR:
        // Const-arg block `{ N + 1 }` — the braces are load-bearing (they
        // disambiguate the `>` close-delimiter), so keep them.
        return "{" + render_expr_src(map_of(node.get(la::VALUE.code))) + "}";
    case la::REPEAT_GROUP: {
        // quote-only `#(T),*` splice cursor. OP: 0 = no separator, 1 = comma.
        std::string inner = render_type_arg_src_(map_of(node.get(la::VALUE.code)));
        bool comma = false;
        if (node.has_key(la::OP)) {
            AnyVal op = node.get(la::OP.code);
            comma = !op.is_null() && op.is_value() && op.as_value<int32_t>() == 1;
        }
        return "#(" + inner + ")" + (comma ? ",*" : "*");
    }
    case la::TRAIT_BOUND:     return render_trait_bound_src_(node);
    default:                  return render_type_src(node);
    }
}

std::string SemaChecker::render_type_param_src_(TinyMapView node) {
    // TYPE_PARAM:  NAME, optional ITEMS (bounds), optional IS_VARIADIC,
    //              optional NAME_VAR (antiquot in a quote).
    // CONST_PARAM: NAME, TYPE, optional IS_VARIADIC. Render as
    //              `const NAME: TYPE` so dump output round-trips back
    //              into valid Logos source.
    std::string s;
    if (node.is_null()) return s;
    // The `<…>` after an item name is a DECLARATION list on struct/enum/fn/
    // trait but an ARGUMENT list on an impl header — the grammar files
    // `type_arg_list` into the very same TYPE_PARAMS slot. Only the parameter
    // node codes are parameters; anything else is a type at USE position, and
    // reading its fields as a parameter's is silent corruption: a DYN_TYPE's
    // NAME alone turned `impl Fam<dyn Tag>` into `impl Fam<Tag>`, and a
    // GENERIC_INST's args came out as bounds (`Wrap<u64>` → `Wrap: u64`).
    if (code_of(node) != la::TYPE_PARAM && code_of(node) != la::CONST_PARAM)
        return render_type_arg_src_(node);
    if (code_of(node) == la::CONST_PARAM) {
        s += "const ";
        if (node.has_key(la::NAME)) s += std::string(str_of(node.get(la::NAME.code)));
        else if (node.has_key(la::NAME_VAR)) s += "<antiquot>";
        if (flag_set(node, la::IS_VARIADIC)) s += "...";
        if (node.has_key(la::TYPE)) {
            s += ": ";
            s += render_type_src(map_of(node.get(la::TYPE.code)));
        }
        return s;
    }
    if (node.has_key(la::NAME)) s += std::string(str_of(node.get(la::NAME.code)));
    else if (node.has_key(la::NAME_VAR)) s += "<antiquot>";
    // G158-6 where-predicate subject: `where &T: Trait` / `where T::A: Trait`
    // is a TYPE_PARAM whose subject sits in TYPE, not NAME.
    else if (node.has_key(la::TYPE)) s += render_type_src(map_of(node.get(la::TYPE.code)));
    if (flag_set(node, la::IS_VARIADIC)) s += "...";
    if (node.has_key(la::ITEMS)) {
        auto items = arr_of(node.get(la::ITEMS.code));
        if (items.size() > 0) {
            s += ": ";
            for (uint64_t i = 0; i < items.size(); ++i) {
                if (i) s += " + ";
                auto b = map_of(items.get(i));
                // Bounds are TRAIT_BOUND nodes — not types; routing them
                // through render_type_src would resolve_type-error on the
                // node code (first hit: `container Vector<T: Copy>`).
                s += (!b.is_null() && code_of(b) == la::TRAIT_BOUND)
                         ? render_trait_bound_src_(b)
                         : render_type_src(b);
            }
        }
    }
    return s;
}

std::string SemaChecker::render_trait_bound_src_(TinyMapView node) {
    std::string s;
    if (node.is_null()) return s;
    // HRTB binders (`for<'a> Fn(&'a T)`) are not reconstructed here —
    // no reconstruction consumer admits HRTB bounds yet.
    if (flag_set(node, la::RELAXED)) s += "?";
    if (node.has_key(la::NAME)) s += std::string(str_of(node.get(la::NAME.code)));
    if (node.has_key(la::TYPE_PARAMS)) {
        auto tl = map_of(node.get(la::TYPE_PARAMS.code));
        if (!tl.is_null() && tl.has_key(la::ITEMS)) {
            auto args = arr_of(tl.get(la::ITEMS.code));
            s += "<";
            for (uint64_t i = 0; i < args.size(); ++i) {
                if (i) s += ", ";
                auto a = map_of(args.get(i));
                if (!a.is_null() && code_of(a) == la::ASSOC_EQ_BIND) {
                    if (a.has_key(la::NAME))
                        s += std::string(str_of(a.get(la::NAME.code)));
                    s += " = ";
                    if (a.has_key(la::TYPE))
                        s += render_type_arg_src_(map_of(a.get(la::TYPE.code)));
                } else {
                    s += render_type_arg_src_(a);
                }
            }
            s += ">";
        }
    }
    if (node.has_key(la::PARAMS)) {
        // Fn-family parenthesized form: `Fn(T1, …) -> R`.
        auto pl = map_of(node.get(la::PARAMS.code));
        s += "(";
        if (!pl.is_null() && pl.has_key(la::ITEMS)) {
            auto args = arr_of(pl.get(la::ITEMS.code));
            for (uint64_t i = 0; i < args.size(); ++i) {
                if (i) s += ", ";
                s += render_type_src_syntactic_(map_of(args.get(i)));
            }
        }
        s += ")";
    } else if (node.has_key(la::RET_TYPE)) {
        s += "()";
    }
    if (node.has_key(la::RET_TYPE)) {
        s += " -> ";
        s += render_type_src_syntactic_(map_of(node.get(la::RET_TYPE.code)));
    }
    return s;
}

std::string SemaChecker::render_type_param_list_(TinyMapView node) {
    if (node.is_null()) return {};
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
    if (node.is_null()) return s;
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
    if (node.is_null()) return "()";
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
    if (node.is_null()) return s;
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
    if (node.is_null()) return {};
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

std::string SemaChecker::render_vis_prefix_(TinyMapView node) {
    if (!flag_set(node, la::IS_PUB)) return {};
    std::string scope;
    if (node.has_key(la::VIS)) {
        auto v = map_of(node.get(la::VIS.code));
        if (!v.is_null() && v.has_key(la::NAME))
            scope = std::string(str_of(v.get(la::NAME.code)));
    }
    return scope.empty() ? std::string("pub ") : "pub(" + scope + ") ";
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
        std::string s = render_vis_prefix_(node);
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
            // VALUE may be an expr or a WritStatic literal. Use expr renderer;
            // unsupported shapes (LIT_WSTATIC) are tagged as such.
            s += render_expr_src(map_of(node.get(la::VALUE.code)));
        }
        s += ";";
        return s;
    }

    case la::TYPE_ALIAS: {
        std::string s = render_vis_prefix_(node);
        s += "type ";
        s += std::string(str_of(node.get(la::NAME.code)));
        if (node.has_key(la::TYPE_PARAMS))
            s += render_type_param_list_(map_of(node.get(la::TYPE_PARAMS.code)));
        s += " = ";
        s += render_type_src(map_of(node.get(la::TYPE.code)));
        s += ";";
        return s;
    }

    case la::ASSOC_TYPE_IMPL: {
        // An impl-level associated-type binding `type Name [<params>] = T;`
        // (grammar slot 120). Like TYPE_ALIAS but never carries a visibility
        // prefix. Needed to render an impl body emitted through quote_item!
        // (the ADR 0021 CtrFamily impl's `type Handle = Hs<hex>;`).
        std::string s = "type ";
        s += std::string(str_of(node.get(la::NAME.code)));
        if (node.has_key(la::TYPE_PARAMS))
            s += render_type_param_list_(map_of(node.get(la::TYPE_PARAMS.code)));
        s += " = ";
        s += render_type_src(map_of(node.get(la::TYPE.code)));
        s += ";";
        return s;
    }

    case la::ENUM: {
        std::string s = render_vis_prefix_(node);
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
        std::string s = render_vis_prefix_(node);
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
        // Inherent methods stored alongside (ITEMS) — render INLINE in the
        // struct body (the `struct Foo { fields, fn … }` form the parser
        // accepts). Keeping them inside preserves the item shape across a
        // render→reparse round-trip (--gen-dir swaps the synth doc for the
        // reparse; a split-off `impl` would change the top-level census).
        if (node.has_key(la::ITEMS)) {
            auto items = arr_of(node.get(la::ITEMS.code));
            for (uint64_t i = 0; i < items.size(); ++i) {
                auto child = map_of(items.get(i));
                int32_t cc = code_of(child);
                if (cc < 0) continue;   // `$...` junk
                auto sub = render_item_src(child);
                if (sub.empty()) continue;
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
                auto child = map_of(items.get(i));
                int32_t cc = code_of(child);
                // PEG `$...` collects every matched non-keyword node into
                // the action — for IMPL_BLOCK that means the trait-target
                // simple_type ends up here too, and on generic impls a
                // CODE-less wrapper sometimes slips in too. Skip pure
                // type-position nodes and uncoded slots so the dump shows
                // only real impl members.
                if (cc < 0
                    || cc == la::TYPE_REF.code || cc == la::GENERIC_INST.code
                    || cc == la::PTR_TYPE.code || cc == la::REF_TYPE.code
                    || cc == la::MUT_REF_TYPE.code || cc == la::ARR_TYPE.code
                    || cc == la::SLICE_TYPE.code || cc == la::UNSIZED_SLICE_TYPE.code
                    || cc == la::TUPLE_TYPE.code
                    || cc == la::IMPL_TYPE.code || cc == la::DYN_TYPE.code
                    || cc == la::FN_PTR_TYPE.code
                    || cc == la::TRAIT_BOUND.code || cc == la::TYPE_PARAM.code) continue;
                auto sub = render_item_src(child);
                if (sub.empty()) continue;
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
    case la::STATIC_FN:
    case la::ABSTRACT_FN: {
        std::string s = render_vis_prefix_(node);
        if (c == la::STATIC_FN.code)       s += "static ";
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
        // #[name] / #[name=val] / #[name(args)]. Render with args — needed
        // for round-trip pre-expansion (debt #22 alt B): an annotation
        // dropped from the rendered output would re-fire on the next
        // compile, but we want one-shot expansion.
        std::string s = "#[";
        if (node.has_key(la::NAME)) s += std::string(str_of(node.get(la::NAME.code)));
        if (node.has_key(la::ARGS)) {
            // ARGS is a TOM with ITEMS = [ANNOT_KV / ANNOT_POS, ...].
            auto args_node = map_of(node.get(la::ARGS.code));
            if (args_node.has_key(la::ITEMS)) {
                auto items = arr_of(args_node.get(la::ITEMS.code));
                if (items.size() > 0) {
                    s += "(";
                    auto render_lit = [&](TinyMapView lit) -> std::string {
                        // ANNOT_ARR has its own ITEMS; everything else is
                        // a regular literal expression node.
                        if (code_of(lit) == la::ANNOT_ARR) {
                            std::string a = "[";
                            if (lit.has_key(la::ITEMS)) {
                                auto inner = arr_of(lit.get(la::ITEMS.code));
                                for (uint64_t j = 0; j < inner.size(); ++j) {
                                    if (j) a += ", ";
                                    a += render_expr_src(map_of(inner.get(j)));
                                }
                            }
                            a += "]";
                            return a;
                        }
                        return render_expr_src(lit);
                    };
                    for (uint64_t i = 0; i < items.size(); ++i) {
                        if (i) s += ", ";
                        auto entry = map_of(items.get(i));
                        int32_t ec = code_of(entry);
                        if (ec == la::ANNOT_KV) {
                            if (entry.has_key(la::NAME))
                                s += std::string(str_of(entry.get(la::NAME.code)));
                            s += " = ";
                            if (entry.has_key(la::VALUE))
                                s += render_lit(map_of(entry.get(la::VALUE.code)));
                        } else if (ec == la::ANNOT_POS) {
                            if (entry.has_key(la::VALUE))
                                s += render_lit(map_of(entry.get(la::VALUE.code)));
                        } else {
                            // Bare-IDENT legacy form: just NAME, no CODE.
                            if (entry.has_key(la::NAME))
                                s += std::string(str_of(entry.get(la::NAME.code)));
                        }
                    }
                    s += ")";
                }
            }
        } else if (node.has_key(la::VALUE)) {
            // `#[name = literal]` form.
            s += " = ";
            s += render_expr_src(map_of(node.get(la::VALUE.code)));
        }
        s += "]";
        return s;
    }

    case la::DOC_LINE_LIT: {
        // `/// text` line inside a quote body — VALUE is the raw token text.
        return std::string(str_of(node.get(la::VALUE.code)));
    }

    case la::METACALL_ITEM:
    case la::METACALL_ITEM_DONE: {
        // Item-position metacall: don't recurse into the call expr — it has
        // already been consumed by the metaprog driver. Surface the trace.
        return c == la::METACALL_ITEM.code
            ? std::string("metacall <pending>;")
            : std::string("/* metacall consumed */");
    }

    case la::STATIC_DEF: {
        std::string s = render_vis_prefix_(node);
        s += "static ";
        if (flag_set(node, la::IS_MUT)) s += "mut ";
        s += std::string(str_of(node.get(la::NAME.code)));
        if (node.has_key(la::TYPE)) {
            s += ": ";
            s += render_type_src(map_of(node.get(la::TYPE.code)));
        }
        if (node.has_key(la::VALUE)) {
            s += " = ";
            s += render_expr_src(map_of(node.get(la::VALUE.code)));
        }
        s += ";";
        return s;
    }

    case la::TRAIT_DEF: {
        std::string s = render_vis_prefix_(node);
        if (flag_set(node, la::IS_AUTO))   s += "auto ";
        if (flag_set(node, la::IS_UNSAFE)) s += "unsafe ";
        s += "trait ";
        if (node.has_key(la::NAME)) s += std::string(str_of(node.get(la::NAME.code)));
        else if (node.has_key(la::NAME_VAR)) s += "<antiquot>";
        if (node.has_key(la::TYPE_PARAMS))
            s += render_type_param_list_(map_of(node.get(la::TYPE_PARAMS.code)));
        if (node.has_key(la::SUPERS)) {
            auto sup = map_of(node.get(la::SUPERS.code));
            if (!sup.is_null() && sup.has_key(la::ITEMS)) {
                auto bs = arr_of(sup.get(la::ITEMS.code));
                std::string joined;
                for (uint64_t i = 0; i < bs.size(); ++i) {
                    auto b = map_of(bs.get(i));
                    if (code_of(b) != la::TRAIT_BOUND.code) continue;
                    if (!joined.empty()) joined += " + ";
                    joined += std::string(str_of(b.get(la::NAME.code)));
                }
                if (!joined.empty()) { s += ": "; s += joined; }
            }
        }
        s += " {\n";
        if (node.has_key(la::ITEMS)) {
            auto items = arr_of(node.get(la::ITEMS.code));
            for (uint64_t i = 0; i < items.size(); ++i) {
                auto child = map_of(items.get(i));
                int32_t cc = code_of(child);
                // `$...` collector junk (pub_vis / type-param / supers
                // aliases) is CODE-less or type-positional — skip it, keep
                // real members (methods, rels, assoc items).
                if (cc < 0 || cc == la::TRAIT_BOUND.code
                    || cc == la::TYPE_PARAM.code) continue;
                auto sub = render_item_src(child);
                if (sub.empty()) continue;
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

    case la::REL_SIG:
    case la::REL_DEF: {
        // `rel name(col: ty, …);` — a trait-position rel signature (REL_SIG,
        // no body) or a mapping rel (REL_DEF, RAW_TEXT body verbatim).
        std::string s;
        if (flag_set(node, la::IS_PUB)) s += "pub ";
        s += "rel ";
        s += std::string(str_of(node.get(la::NAME.code)));
        s += render_param_list_(map_of(node.get(la::PARAMS.code)));
        if (node.has_key(la::RAW_TEXT)) {
            s += " {";
            s += std::string(str_of(node.get(la::RAW_TEXT.code)));
            s += "}";
        } else {
            s += ";";
        }
        return s;
    }

    case la::REL_OP: {
        // `op rel.col cmp = fn [exact];` (impl member, ADR 0024 S6). The render
        // must REPARSE to this node — a display channel that drops a member is
        // how a rendered source silently stops being the source.
        std::string s = "op ";
        s += std::string(str_of(node.get(la::TYPE_NAME.code)));
        s += ".";
        s += std::string(str_of(node.get(la::FIELD.code)));
        s += " ";
        s += std::string(str_of(node.get(la::OP.code)));
        s += " = ";
        s += std::string(str_of(node.get(la::NAME.code)));
        if (node.has_key(la::RET_TYPE)) {
            std::string flag(str_of(node.get(la::RET_TYPE.code)));
            if (!flag.empty()) { s += "  "; s += flag; }
        }
        s += ";";
        return s;
    }

    case la::REL_BIND: {
        // `<lead> name = fn;` (impl member) — `rel <r> = <materializer>;`
        // (ADR 0016 §6) or `size <r> = <reporter>;` (ADR 0024 S4).
        //
        // ⚠ The LEAD IS DATA and this used to print the constant "rel ". Under
        // `-g` the dump is REPARSED and replaces the synth doc, so a rendered
        // `size` member came back as a second rel binding for the same rel —
        // the fourth time in this arc that the reading instrument was blind
        // exactly where emitters write.
        std::string lead(str_of(node.get(la::REL_KW.code)));
        if (lead.empty()) lead = "rel";
        std::string s = lead + " ";
        s += std::string(str_of(node.get(la::NAME.code)));
        s += " = ";
        s += std::string(str_of(node.get(la::VALUE.code)));
        s += ";";
        return s;
    }

    case la::CONTAINER_DEF:
    case la::CONTAINER_DEF_DONE: {
        // `pub? container C<T…> for Backing { … }` — multi-line, one clause
        // per line, stream blocks nested + indented (the canonical container
        // decl style; the reparse of this render must reproduce the node).
        std::string s = render_vis_prefix_(node);
        s += "container ";
        s += std::string(str_of(node.get(la::NAME.code)));
        if (node.has_key(la::TYPE_PARAMS))
            s += render_type_param_list_(map_of(node.get(la::TYPE_PARAMS.code)));
        s += " for ";
        s += render_type_src_syntactic_(map_of(node.get(la::TYPE.code)));
        s += " {\n";
        if (node.has_key(la::FIELDS)) {
            auto carr = arr_of(node.get(la::FIELDS.code));
            for (uint64_t i = 0; i < carr.size(); ++i) {
                auto cl = map_of(carr.get(i));
                if (cl.is_null() || code_of(cl) != la::CONTAINER_CLAUSE)
                    continue;
                s += render_container_clause_src_(cl, 1);
            }
        }
        s += "}";
        return s;
    }

    default:
        return std::format("/* render_item: unsupported AST code {} */", c);
    }
}

// One container clause line (BTFL 8b): dispatched on the node's structural
// keys — FIELDS = a `stream N { … }` block (nested clauses, depth+1), ITEMS =
// an `ops { … }` block (one line), PARAMS/no-NAME = `entry { col: ty, … }`
// (one line), NAME [+VALUE] = a word clause (`kind k;` / `measure m;` /
// `measure m(col);`).
std::string SemaChecker::render_container_clause_src_(TinyMapView cl,
                                                      int depth) {
    std::string ind(static_cast<size_t>(depth) * 4, ' ');
    std::string lead(str_of(cl.get(la::REL_KW.code)));
    std::string s = ind + lead;
    if (cl.has_key(la::FIELDS)) {
        // stream block
        s += " ";
        s += std::string(str_of(cl.get(la::NAME.code)));
        s += " {\n";
        auto sarr = arr_of(cl.get(la::FIELDS.code));
        for (uint64_t i = 0; i < sarr.size(); ++i) {
            auto sub = map_of(sarr.get(i));
            if (sub.is_null() || code_of(sub) != la::CONTAINER_CLAUSE)
                continue;
            s += render_container_clause_src_(sub, depth + 1);
        }
        s += ind;
        s += "}\n";
        return s;
    }
    if (cl.has_key(la::ITEMS)) {
        // ops block — `ops { seek(max(key)); insert; … }` on one line
        s += " {";
        auto oarr = arr_of(cl.get(la::ITEMS.code));
        for (uint64_t i = 0; i < oarr.size(); ++i) {
            auto op = map_of(oarr.get(i));
            if (op.is_null() || code_of(op) != la::CONTAINER_OP) continue;
            s += " ";
            s += std::string(str_of(op.get(la::NAME.code)));
            if (op.has_key(la::VALUE)) {
                s += "(";
                s += std::string(str_of(op.get(la::VALUE.code)));
                if (op.has_key(la::OP_ARG)) {
                    s += "(";
                    s += std::string(str_of(op.get(la::OP_ARG.code)));
                    s += ")";
                }
                s += ")";
            }
            s += ";";
        }
        s += " }\n";
        return s;
    }
    if (!cl.has_key(la::NAME)) {
        // entry clause — `entry { col: ty, … }` on one line
        s += " {";
        if (cl.has_key(la::PARAMS)) {
            auto pl = map_of(cl.get(la::PARAMS.code));
            if (!pl.is_null() && pl.has_key(la::ITEMS)) {
                auto parr = arr_of(pl.get(la::ITEMS.code));
                bool first = true;
                for (uint64_t i = 0; i < parr.size(); ++i) {
                    auto p = map_of(parr.get(i));
                    if (p.is_null() || code_of(p) != la::PARAM) continue;
                    s += first ? " " : ", ";
                    first = false;
                    s += render_param_src_(p);
                }
            }
        }
        s += " }\n";
        return s;
    }
    // word / measure clause
    s += " ";
    s += std::string(str_of(cl.get(la::NAME.code)));
    if (cl.has_key(la::VALUE)) {
        s += "(";
        s += std::string(str_of(cl.get(la::VALUE.code)));
        s += ")";
    }
    s += ";\n";
    return s;
}

std::string SemaChecker::render_writ_val_inner_(TinyMapView node) {
    if (node.is_null()) return "null";
    int32_t c = code_of(node);
    switch (c) {
    case la::WRIT_NULL:  return "null";
    case la::WRIT_BOOL: {
        AnyVal v = node.get(la::VALUE.code);
        bool b = !v.is_null() && v.is_value() && v.as_value<uint8_t>() != 0;
        return b ? "true" : "false";
    }
    case la::WRIT_INT:
        return std::string(str_of(node.get(la::VALUE.code)));
    case la::WRIT_NEG_INT:
        return "-" + std::string(str_of(node.get(la::VALUE.code)));
    case la::WRIT_FLOAT:
        return std::string(str_of(node.get(la::VALUE.code)));
    case la::WRIT_STR: {
        // VALUE is the STRING token captured WITH quotes — emit as-is.
        return std::string(str_of(node.get(la::VALUE.code)));
    }
    case la::WRIT_MAP: {
        std::string s = "{";
        if (node.has_key(la::ITEMS)) {
            auto items = arr_of(node.get(la::ITEMS.code));
            for (uint64_t i = 0; i < items.size(); ++i) {
                if (i) s += ", ";
                auto entry = map_of(items.get(i));
                if (code_of(entry) == la::WRIT_ENTRY) {
                    s += std::string(str_of(entry.get(la::KEY.code)));
                    s += ": ";
                    if (entry.has_key(la::VALUE))
                        s += render_writ_val_inner_(map_of(entry.get(la::VALUE.code)));
                }
            }
        }
        s += "}";
        return s;
    }
    case la::WRIT_ARRAY: {
        std::string s = "[";
        if (node.has_key(la::ITEMS)) {
            auto items = arr_of(node.get(la::ITEMS.code));
            for (uint64_t i = 0; i < items.size(); ++i) {
                if (i) s += ", ";
                s += render_writ_val_inner_(map_of(items.get(i)));
            }
        }
        s += "]";
        return s;
    }
    case la::WRIT_TYPE_LIT: {
        if (node.has_key(la::TYPE)) {
            auto tnode = map_of(node.get(la::TYPE.code));
            TypeRef vt = resolve_type(tnode);
            // ADR 0021 C1: an unsized/VLE value column canonicalizes to the
            // string tag ("str"), byte-identical to the concrete-decl doc's
            // WRIT_STR — so the captured CFG source (wstatic_sources → factory
            // + CtrFamily impl) is one representation regardless of arrival
            // path. Sized types keep the `<type:T>` form.
            std::string tag = cfg_str_tag_(vt);
            if (!tag.empty()) return "\"" + tag + "\"";
            return "<type:" + (vt ? type_str(vt) : render_type_src(tnode)) + ">";
        }
        return "<type:>";
    }
    case la::CFG_SLOT_TYPE: {
        std::string s = "<type:";
        if (node.has_key(la::NAME)) s += std::string(str_of(node.get(la::NAME.code)));
        if (node.has_key(la::ITEMS)) {
            auto items = arr_of(node.get(la::ITEMS.code));
            for (uint64_t i = 0; i < items.size(); ++i) {
                s += ".";
                auto step = map_of(items.get(i));
                if (step.has_key(la::NAME)) s += std::string(str_of(step.get(la::NAME.code)));
            }
        }
        s += ">";
        return s;
    }
    default:
        // Fall back to outer render for other shapes (unlikely to be
        // hit in well-formed @-literals, but keep diagnostic clear).
        return render_expr_src(node);
    }
}

std::string SemaChecker::render_module_src(TinyMapView node) {
    // MODULE: NAME, PATH_PARTS, USES, ITEMS.
    if (node.is_null() || code_of(node) != la::MODULE) return "";
    std::string pkg_name = std::string(str_of(node.get(la::NAME.code)))
                         + render_path_parts_(node);
    std::string s = "package " + pkg_name + ";\n\n";
    // Imports: the USES array PLUS any USE nodes aliased into ITEMS by the
    // `$...` collector. For metaprog synth docs the ITEMS-resident copies are
    // the SUBSTITUTED ones (quote-carried `use #pkg;` gets its placeholder
    // index through the ITEMS walk), so both sources must be considered.
    // Hoisted, text-deduped; empty (unresolved/dropped) and self-package
    // imports are skipped — a rendered module must reparse cleanly.
    {
        std::set<std::string> seen;
        std::string uses_out;
        auto add_use = [&](TinyMapView u) {
            if (u.is_null() || code_of(u) != la::USE.code) return;
            std::string one = render_item_src(u);
            if (one == "use ;" || one == "pub use ;") return;      // unresolved
            if (one == "use " + pkg_name + ";") return;            // self-import
            if (!seen.insert(one).second) return;
            uses_out += one;
            uses_out += "\n";
        };
        if (node.has_key(la::USES)) {
            auto uses = arr_of(node.get(la::USES.code));
            for (uint64_t i = 0; i < uses.size(); ++i)
                add_use(map_of(uses.get(i)));
        }
        if (node.has_key(la::ITEMS)) {
            auto items = arr_of(node.get(la::ITEMS.code));
            for (uint64_t i = 0; i < items.size(); ++i) {
                auto av = items.get(i);
                if (av.is_null()) continue;
                auto item = map_of(av);
                if (code_of(item) == la::USE.code) add_use(item);
            }
        }
        if (!uses_out.empty()) { s += uses_out; s += "\n"; }
    }
    if (node.has_key(la::ITEMS)) {
        auto items = arr_of(node.get(la::ITEMS.code));
        for (uint64_t i = 0; i < items.size(); ++i) {
            auto av = items.get(i);
            if (av.is_null()) continue;  // empty slots from PEG flat-array capture
            auto item = map_of(av);
            // USE nodes were hoisted into the imports block above.
            int32_t ic = code_of(item);
            if (ic == la::USE.code || ic < 0) continue;
            s += render_item_src(item);
            s += "\n\n";
        }
    }
    return s;
}

std::string SemaChecker::render_type_src_syntactic_(TinyMapView node) {
    // Pure AST walk — for the --dump-metaprog driver where the type pool
    // is empty (resolve_type would return null for any user struct/alias).
    // Covers the common shapes; unsupported nodes degrade to a `<ty:CODE>`
    // marker so the dump still reads visually as Logos.
    if (node.is_null()) return "_";
    int32_t c = code_of(node);

    auto recur = [&](TinyMapView n) { return render_type_src_syntactic_(n); };

    switch (c) {
    case la::TYPE_REF: {
        if (node.has_key(la::NAME)) return std::string(str_of(node.get(la::NAME.code)));
        if (node.has_key(la::NAME_VAR)) return "<antiquot>";
        return "_";
    }
    case la::GENERIC_INST: {
        std::string s;
        if (node.has_key(la::NAME)) s = std::string(str_of(node.get(la::NAME.code)));
        else if (node.has_key(la::NAME_VAR)) s = "<antiquot>";
        s += "<";
        if (node.has_key(la::ITEMS)) {
            auto items = arr_of(node.get(la::ITEMS.code));
            for (uint64_t i = 0; i < items.size(); ++i) {
                if (i) s += ", ";
                s += recur(map_of(items.get(i)));
            }
        }
        s += ">";
        return s;
    }
    case la::PTR_TYPE: {
        // Grammar: POINTEE (sub-type), MUTPTR (bool, not IS_MUT), NAME (the
        // contextual `zoned` modifier — F3).
        bool is_mut = flag_set(node, la::MUTPTR);
        bool zoned  = node.has_key(la::NAME) &&
                      std::string(str_of(node.get(la::NAME.code))) == "zoned";
        TinyMapView pointee;
        if (node.has_key(la::POINTEE)) pointee = map_of(node.get(la::POINTEE.code));
        else if (node.has_key(la::TYPE)) pointee = map_of(node.get(la::TYPE.code));  // legacy
        std::string pre = zoned ? (is_mut ? "*zoned mut " : "*zoned ")
                                : (is_mut ? "*mut " : "*const ");
        return pre + recur(pointee);
    }
    case la::REF_TYPE: {
        auto inner = node.has_key(la::POINTEE) ? map_of(node.get(la::POINTEE.code))
                                                : map_of(node.get(la::TYPE.code));
        return "&" + recur(inner);
    }
    case la::MUT_REF_TYPE: {
        auto inner = node.has_key(la::POINTEE) ? map_of(node.get(la::POINTEE.code))
                                                : map_of(node.get(la::TYPE.code));
        return "&mut " + recur(inner);
    }
    case la::ARR_TYPE: {
        std::string s = "[";
        s += recur(map_of(node.get(la::TYPE.code)));
        // The length is ONE node now (ARR_LEN), whichever position it sits in.
        if (node.has_key(la::SIZE)) {
            auto ln = map_of(node.get(la::SIZE.code));
            s += "; ";
            if (ln.has_key(la::OP) && ln.has_key(la::NAME)) {
                s += std::string(str_of(ln.get(la::OP.code)));
                s += "...(";
                s += std::string(str_of(ln.get(la::NAME.code)));
                s += ")";
            } else if (ln.has_key(la::BODY)) {
                s += "metacall { ... }";
            } else if (ln.has_key(la::SIZE)) {
                s += std::string(str_of(ln.get(la::SIZE.code)));
            } else if (ln.has_key(la::NAME)) {
                s += std::string(str_of(ln.get(la::NAME.code)));
            }
        }
        s += "]";
        return s;
    }
    case la::SLICE_TYPE:
        return "&[" + recur(map_of(node.get(la::TYPE.code))) + "]";
    case la::UNSIZED_SLICE_TYPE:
        return "[" + recur(map_of(node.get(la::TYPE.code))) + "]";
    case la::TUPLE_TYPE: {
        std::string s = "(";
        if (node.has_key(la::ITEMS)) {
            auto items = arr_of(node.get(la::ITEMS.code));
            uint64_t n = items.size();
            for (uint64_t i = 0; i < n; ++i) {
                if (i) s += ", ";
                s += recur(map_of(items.get(i)));
            }
            if (n == 1) s += ",";
        }
        s += ")";
        return s;
    }
    case la::IMPL_TYPE: {
        std::string s = "impl ";
        if (node.has_key(la::NAME)) s += std::string(str_of(node.get(la::NAME.code)));
        return s;
    }
    case la::DYN_TYPE: {
        // The grammar folds `dyn Trait…` (bare, e.g. inside `*mut dyn …`) and
        // the reference forms `&dyn` / `&mut dyn` / `&'a dyn` into ONE node.
        // IS_REF marks every AMP-led alt, so the `&` survives the render; an
        // outer PTR_TYPE (bare form) supplies its own `*mut`.
        //
        // ITEMS is a MIXED array: type-args AND the trailing `+ Bound` nodes
        // (AUTO_TRAIT_BOUND / AUTO_LIFE_BOUND) land in the same slot, exactly
        // as resolve_type sees them — so bucket by CODE here too rather than
        // spraying everything inside `<…>`. Fn-family shapes carry their
        // arg-types in PARAMS and the result in RET_TYPE.
        std::string s;
        if (flag_set(node, la::IS_REF)) {
            s += "&";
            if (node.has_key(la::LIFETIME))
                s += std::string(str_of(node.get(la::LIFETIME.code))) + " ";
            if (flag_set(node, la::IS_MUT)) s += "mut ";
        }
        s += "dyn ";
        if (node.has_key(la::HRTB_BINDERS)) {
            auto hb = map_of(node.get(la::HRTB_BINDERS.code));
            if (!hb.is_null() && hb.has_key(la::ITEMS)) {
                auto lts = arr_of(hb.get(la::ITEMS.code));
                if (lts.size() > 0) {
                    s += "for<";
                    for (uint64_t i = 0; i < lts.size(); ++i) {
                        if (i) s += ", ";
                        auto lt = map_of(lts.get(i));
                        if (!lt.is_null() && lt.has_key(la::NAME))
                            s += std::string(str_of(lt.get(la::NAME.code)));
                    }
                    s += "> ";
                }
            }
        }
        if (node.has_key(la::NAME)) s += std::string(str_of(node.get(la::NAME.code)));
        // On the Fn-family alts `$...` re-collects the arg-type list (a
        // CODE-less TOM) and the `-> R` node into ITEMS as well; PARAMS and
        // RET_TYPE are the authoritative copies, so take the args from there
        // and let ITEMS contribute bounds only. Same bucketing resolve_type
        // does — it skips the Fn-family through a name check.
        bool fn_form = node.has_key(la::PARAMS) || node.has_key(la::RET_TYPE);
        std::string bounds;
        if (node.has_key(la::ITEMS)) {
            auto items = arr_of(node.get(la::ITEMS.code));
            std::string args;
            for (uint64_t i = 0; i < items.size(); ++i) {
                auto it = map_of(items.get(i));
                int32_t ic = it.is_null() ? -1 : code_of(it);
                if (ic < 0) continue;   // HRTB binder / arg-list TOM — no CODE
                if (ic == la::AUTO_TRAIT_BOUND.code || ic == la::AUTO_LIFE_BOUND.code) {
                    bounds += " + ";
                    if (it.has_key(la::NAME))
                        bounds += std::string(str_of(it.get(la::NAME.code)));
                    continue;
                }
                if (fn_form) continue;
                if (!args.empty()) args += ", ";
                args += render_type_arg_src_(it);
            }
            if (!args.empty()) s += "<" + args + ">";
        }
        if (node.has_key(la::PARAMS)) {
            // Fn-family: `dyn Fn(T1, …)`. An empty PARAMS still needs `()`.
            auto pl = map_of(node.get(la::PARAMS.code));
            s += "(";
            if (!pl.is_null() && pl.has_key(la::ITEMS)) {
                auto args = arr_of(pl.get(la::ITEMS.code));
                for (uint64_t i = 0; i < args.size(); ++i) {
                    if (i) s += ", ";
                    s += recur(map_of(args.get(i)));
                }
            }
            s += ")";
        } else if (node.has_key(la::RET_TYPE)) {
            s += "()";   // `dyn Fn() -> R` — zero-arg alt stores no PARAMS
        }
        if (node.has_key(la::RET_TYPE)) {
            s += " -> ";
            s += recur(map_of(node.get(la::RET_TYPE.code)));
        }
        return s + bounds;
    }
    case la::FN_PTR_TYPE: {
        std::string s = "fn(";
        if (node.has_key(la::PARAMS)) {
            auto pm = map_of(node.get(la::PARAMS.code));
            if (pm.has_key(la::ITEMS)) {
                auto items = arr_of(pm.get(la::ITEMS.code));
                for (uint64_t i = 0; i < items.size(); ++i) {
                    if (i) s += ", ";
                    s += recur(map_of(items.get(i)));
                }
            }
        }
        s += ")";
        if (node.has_key(la::RET_TYPE)) {
            s += " -> ";
            s += recur(map_of(node.get(la::RET_TYPE.code)));
        }
        return s;
    }
    case la::TRAIT_BOUND: {
        // `Trait` or `Trait<T1, T2>` — appears in type-param bounds.
        std::string s;
        if (node.has_key(la::NAME)) s += std::string(str_of(node.get(la::NAME.code)));
        if (node.has_key(la::TYPE_PARAMS)) {
            auto tp = map_of(node.get(la::TYPE_PARAMS.code));
            if (tp.has_key(la::ITEMS)) {
                auto items = arr_of(tp.get(la::ITEMS.code));
                if (items.size() > 0) {
                    s += "<";
                    for (uint64_t i = 0; i < items.size(); ++i) {
                        if (i) s += ", ";
                        s += recur(map_of(items.get(i)));
                    }
                    s += ">";
                }
            }
        }
        return s;
    }
    case la::LIT_WSTATIC: {
        // A `@{...}` WritStatic literal at type-arg position (`Foo<@{...}>`,
        // grammar slot 212 via type_or_lt_arg's writ_lit alt). VALUE wraps the
        // nested writ_lit AST. Reuse the expression renderer's writ cases —
        // they emit the outer `@{...}` form and resolve `<type:T>` slots — so a
        // quote_item! carrying `impl CtrFamily<S> for CtrClass<@{...}>` round-
        // trips through the --gen-dir fidelity gate instead of degrading to the
        // `<ty:212>` marker. (ADR 0021 container factory: the CtrFamily impl.)
        if (!node.has_key(la::VALUE))
            return "/* render_type: LIT_WSTATIC without VALUE */";
        return render_expr_src(map_of(node.get(la::VALUE.code)));
    }
    default:
        return std::format("<ty:{}>", c);
    }
}

// Free entry point for `--dump-metaprog`: renders an entire Writ
// document as Logos source through Stage 2 of the AST pretty-printer.
// Builds a throwaway SemaChecker, points its holder_ at the foreign
// doc, flips dump_syntactic_types_ so type-position rendering doesn't
// touch the (empty) type pool, and dispatches by the root node's
// CODE — MODULE renders the whole package; anything else (a single
// item from a quote_item! splice) falls through to render_item_src.
std::string render_module_source_for_dump(writ::MemHolder* holder,
                                          writ::arena_offset_t root_offset) {
    if (!holder || root_offset == writ::NULL_OFFSET) return {};
    SemaChecker checker;
    checker.set_holder_for_render(holder);
    checker.set_render_syntactic(true);
    TinyMapView root_view{root_offset, holder};
    auto code_av = root_view.get(la::CODE.code);
    int32_t c = (!code_av.is_null() && code_av.is_value())
                ? code_av.as_value<int32_t>() : -1;
    if (c == la::MODULE.code) return checker.render_module_src(root_view);
    return checker.render_item_src(root_view);
}

std::vector<std::string> collect_fn_names_for_dump(writ::MemHolder* holder,
                                                   writ::arena_offset_t root_offset) {
    std::vector<std::string> out;
    if (!holder || root_offset == writ::NULL_OFFSET) return out;
    auto* base = holder->base();

    auto map_at = [&](writ::arena_offset_t off) {
        return writ::TinyMapView(off, holder);
    };
    auto str_at = [&](writ::AnyVal av) -> std::string {
        if (av.is_null()) return {};
        // String values point at writ::ArenaString (length-prefixed bytes).
        // Reuse the same StringView wrapper sema uses.
        auto sv = writ::StringView(av, holder).view();
        return std::string(sv);
    };
    auto arr_at = [&](writ::AnyVal av) {
        return writ::ArrayView(av, holder);
    };

    auto get_fn_or_impl_items = [&](writ::TinyMapView tom, std::string prefix) {
        auto code_av = tom.get(la::CODE.code);
        int32_t c = (!code_av.is_null() && code_av.is_value())
                    ? code_av.as_value<int32_t>() : -1;
        if (c == la::FN.code || c == la::EXTERN_FN.code || c == la::STATIC_FN.code) {
            auto name = str_at(tom.get(la::NAME.code));
            if (name.empty()) return;
            // Emit BOTH the bare method name AND the impl receiver type
            // (when present) as separate index entries. Mono mangles
            // generic types as `Type$G1$Arg__method`, so the joined
            // `Type__method` substring fails to match — listing them
            // separately keeps grep robust across mono variants.
            out.push_back(name);
            if (!prefix.empty()) out.push_back(prefix);
        }
    };

    auto root_tom = map_at(root_offset);
    auto root_code = root_tom.get(la::CODE.code);
    int32_t rc = (!root_code.is_null() && root_code.is_value())
                 ? root_code.as_value<int32_t>() : -1;

    std::function<void(writ::TinyMapView, std::string)> walk_items =
        [&](writ::TinyMapView tom, std::string prefix) {
        auto items_av = tom.get(la::ITEMS.code);
        if (items_av.is_null()) return;
        auto items = arr_at(items_av);
        for (uint64_t i = 0; i < items.size(); ++i) {
            auto child_av = items.get(i);
            if (child_av.is_null()) continue;
            auto child = map_at(child_av.to_offset(base));
            auto code_av = child.get(la::CODE.code);
            int32_t cc = (!code_av.is_null() && code_av.is_value())
                         ? code_av.as_value<int32_t>() : -1;
            if (cc == la::IMPL_BLOCK.code) {
                // Best-effort impl receiver name: from TYPE child if it's
                // a TYPE_REF / GENERIC_INST with NAME. Otherwise use the
                // trait name (NAME field on impl_block).
                std::string recv;
                auto type_av = child.get(la::TYPE.code);
                if (!type_av.is_null()) {
                    auto ty_tom = map_at(type_av.to_offset(base));
                    auto nm_av = ty_tom.get(la::NAME.code);
                    if (!nm_av.is_null()) recv = str_at(nm_av);
                }
                if (recv.empty()) recv = str_at(child.get(la::NAME.code));
                walk_items(child, recv);
            } else {
                get_fn_or_impl_items(child, prefix);
            }
        }
    };

    if (rc == la::MODULE.code) {
        walk_items(root_tom, std::string{});
    } else {
        get_fn_or_impl_items(root_tom, std::string{});
    }
    // Stable order, dedup.
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

} // namespace logos::compiler
