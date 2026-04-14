// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos

#include "codegen.hpp"
#include "grammar_ast.hpp"

#include <algorithm>
#include <charconv>
#include <filesystem>
#include <format>
#include <fstream>
#include <print>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

namespace fs  = std::filesystem;
namespace ast = logos::peg_gen::ast;

using logos::hermes::AnyVal;
using logos::hermes::ArrayView;
using logos::hermes::MapView;
using logos::hermes::TinyMapView;
using logos::hermes::StringView;
using logos::hermes::MemHolder;

namespace logos::peg_gen {

// ═══════════════════════════════════════════════════════════════════════════
// Plain C++ structs — intermediate representation extracted from grammar doc.
// Keeps the codegen itself free of Hermes navigation.
// ═══════════════════════════════════════════════════════════════════════════

struct NameDecl  { std::string name; int32_t code; std::string group; };
struct ImportRef { std::string alias; std::string output; }; // output = base name

struct TokenDecl {
    std::string name;
    int32_t     kind;   // ast::TOKEN_LITERAL / TOKEN_REGEX / TOKEN_SKIP
    std::string pattern;
};

struct PrecLevel {
    int32_t                  assoc;  // ast::ASSOC_LEFT / RIGHT / NONE
    std::vector<std::string> tokens;
};

// Action expression inside => { FIELD: expr }
struct ActionExpr {
    int32_t     kind;        // ast::CAPTURE / ARRAY_CAPTURE / INT_LIT / STR_LIT / BOOL_LIT
    int32_t     index = 0;   // for CAPTURE: $n
    std::string value;       // for STR_LIT / symbolic name
    int32_t     int_val = 0; // for INT_LIT / BOOL_LIT
};

struct ActionField { std::string name; ActionExpr expr; };
struct Action      { std::vector<ActionField> fields; };

struct Item {
    int32_t     kind;           // ast::RULE_REF / TOKEN_REF / LITERAL / OPT / REP / GROUP / ...
    std::string name;           // rule/token name or literal text
    std::string grammar_alias;  // for cross-grammar RULE_REF
    int32_t     min = 1, max = 1; // for REP
    // Children: for GROUP → sub_alts; for OPT/REP/LOOKAHEAD/NEG_AHEAD → sub_items[0]
    std::vector<Item> sub_items;

    struct Alt;
    std::vector<Alt> sub_alts;
};

struct Item::Alt {
    std::vector<Item>         seq;
    std::optional<Action>     action;
};

struct Rule {
    std::string            name;
    std::string            group;   // rule's group (empty = none)
    std::vector<Item::Alt> alts;
};

struct GrammarInfo {
    // %meta
    std::string name, cxx_namespace, output;

    // %import (already resolved — just alias + output base name)
    std::vector<ImportRef> imports;

    // %export
    std::vector<std::string> exports;

    // %fields / %nodes
    std::vector<NameDecl> fields, nodes;

    // %tokens
    std::vector<TokenDecl> tokens;

    // %prec
    std::vector<PrecLevel> prec;

    // %rules
    std::vector<Rule> rules;
};

// ═══════════════════════════════════════════════════════════════════════════
// GrammarReader — navigates HermesCtr grammar document → GrammarInfo
// ═══════════════════════════════════════════════════════════════════════════

static std::string read_str(AnyVal val, MemHolder* h) {
    if (val.is_null() || !val.is_pointer()) return {};
    return std::string(StringView(val.to_offset(), h).view());
}

static int32_t read_int(AnyVal val) {
    if (val.is_null() || !val.is_value()) return 0;
    return val.as_value<int32_t>();
}

static std::string to_pascal(const std::string& snake) {
    std::string result;
    bool cap = true;
    for (char c : snake) {
        if (c == '_') { cap = true; }
        else { result += cap ? char(std::toupper(c)) : c; cap = false; }
    }
    return result;
}

class GrammarReader {
public:
    static GrammarInfo read(const logos::hermes::HermesCtrView& doc,
                            const std::vector<ResolvedModule>&  all_modules) {
        GrammarInfo g;
        MemHolder* h = doc.holder();

        auto root_obj = doc.root_object();
        if (root_obj.is_null()) return g;
        MapView root = root_obj.as_map();

        read_meta(root, h, g);
        read_exports(root, h, g);
        read_name_decls(root, "fields", h, g.fields);
        read_name_decls(root, "nodes",  h, g.nodes);
        read_tokens(root, h, g);
        read_prec(root, h, g);
        read_rules(root, h, g);
        read_imports(root, h, all_modules, g);
        return g;
    }

private:
    static void read_meta(MapView& root, MemHolder* h, GrammarInfo& g) {
        AnyVal meta_val = root.get("meta");
        if (meta_val.is_null()) return;
        TinyMapView meta(meta_val.to_offset(), h);
        g.name          = read_str(meta.get(uint8_t(ast::NAME)),      h);
        g.cxx_namespace = read_str(meta.get(uint8_t(ast::NAMESPACE)), h);
        g.output        = read_str(meta.get(uint8_t(ast::OUTPUT)),    h);
    }

    static void read_exports(MapView& root, MemHolder* h, GrammarInfo& g) {
        AnyVal arr_val = root.get("exports");
        if (arr_val.is_null()) return;
        ArrayView arr(arr_val.to_offset(), h);
        for (uint64_t i = 0; i < arr.size(); ++i) {
            AnyVal elem = arr.get(i);
            if (!elem.is_null()) g.exports.push_back(read_str(elem, h));
        }
    }

    static void read_name_decls(MapView& root, const char* key, MemHolder* h,
                                std::vector<NameDecl>& out) {
        AnyVal arr_val = root.get(key);
        if (arr_val.is_null()) return;
        ArrayView arr(arr_val.to_offset(), h);
        for (uint64_t i = 0; i < arr.size(); ++i) {
            AnyVal elem = arr.get(i);
            if (elem.is_null()) continue;
            TinyMapView node(elem.to_offset(), h);
            int32_t code = read_int(node.get(uint8_t(ast::CODE)));
            if (code == int32_t(ast::GROUP_DECL)) {
                // Group block: recurse into FIELDS, tag each entry with the group name.
                std::string gname = read_str(node.get(uint8_t(ast::NAME)), h);
                AnyVal fields_av = node.get(uint8_t(ast::FIELDS));
                if (fields_av.is_null()) continue;
                ArrayView fields(fields_av.to_offset(), h);
                for (uint64_t j = 0; j < fields.size(); ++j) {
                    AnyVal fe = fields.get(j);
                    if (fe.is_null()) continue;
                    TinyMapView fn(fe.to_offset(), h);
                    out.push_back({
                        read_str(fn.get(uint8_t(ast::NAME)), h),
                        read_int(fn.get(uint8_t(ast::VALUE))),
                        gname
                    });
                }
            } else {
                out.push_back({
                    read_str(node.get(uint8_t(ast::NAME)),  h),
                    read_int(node.get(uint8_t(ast::VALUE))),
                    ""  // global
                });
            }
        }
    }

    static void read_tokens(MapView& root, MemHolder* h, GrammarInfo& g) {
        AnyVal arr_val = root.get("tokens");
        if (arr_val.is_null()) return;
        ArrayView arr(arr_val.to_offset(), h);
        for (uint64_t i = 0; i < arr.size(); ++i) {
            AnyVal elem = arr.get(i);
            if (elem.is_null()) continue;
            TinyMapView node(elem.to_offset(), h);
            g.tokens.push_back({
                read_str(node.get(uint8_t(ast::NAME)),    h),
                read_int(node.get(uint8_t(ast::KIND))),
                read_str(node.get(uint8_t(ast::PATTERN)), h)
            });
        }
    }

    static void read_prec(MapView& root, MemHolder* h, GrammarInfo& g) {
        AnyVal arr_val = root.get("prec");
        if (arr_val.is_null()) return;
        ArrayView arr(arr_val.to_offset(), h);
        for (uint64_t i = 0; i < arr.size(); ++i) {
            AnyVal elem = arr.get(i);
            if (elem.is_null()) continue;
            TinyMapView node(elem.to_offset(), h);
            PrecLevel level;
            level.assoc = read_int(node.get(uint8_t(ast::ASSOC)));
            AnyVal toks_val = node.get(uint8_t(ast::TOKENS));
            if (!toks_val.is_null()) {
                ArrayView toks(toks_val.to_offset(), h);
                for (uint64_t j = 0; j < toks.size(); ++j)
                    level.tokens.push_back(read_str(toks.get(j), h));
            }
            g.prec.push_back(std::move(level));
        }
    }

    static ActionExpr read_action_expr(AnyVal val, MemHolder* h) {
        if (val.is_null() || !val.is_pointer()) return {};
        TinyMapView node(val.to_offset(), h);
        int32_t kind = read_int(node.get(uint8_t(ast::CODE)));
        ActionExpr e;
        e.kind = kind;
        if (kind == int32_t(ast::CAPTURE)) {
            e.index = read_int(node.get(uint8_t(ast::INDEX)));
        } else if (kind == int32_t(ast::INT_LIT) || kind == int32_t(ast::BOOL_LIT)) {
            e.int_val = read_int(node.get(uint8_t(ast::VALUE)));
        } else if (kind == int32_t(ast::STR_LIT)) {
            e.value = read_str(node.get(uint8_t(ast::VALUE)), h);
        }
        return e;
    }

    static std::optional<Action> read_action(AnyVal val, MemHolder* h) {
        if (val.is_null() || !val.is_pointer()) return std::nullopt;
        // Action is an ObjectMap: field_name → action_expr
        MapView action_map(val.to_offset(), h);
        Action a;
        action_map.for_each([&](logos::hermes::ArenaString* key, AnyVal* expr_slot) {
            a.fields.push_back({
                std::string(key->view()),
                read_action_expr(*expr_slot, h)
            });
        });
        return a;
    }

    static Item::Alt read_alt(AnyVal val, MemHolder* h) {
        Item::Alt alt;
        TinyMapView node(val.to_offset(), h);
        AnyVal seq_val    = node.get(uint8_t(ast::SEQ));
        AnyVal action_val = node.get(uint8_t(ast::ACTION));

        if (!seq_val.is_null()) {
            ArrayView seq(seq_val.to_offset(), h);
            for (uint64_t i = 0; i < seq.size(); ++i)
                alt.seq.push_back(read_item(seq.get(i), h));
        }
        alt.action = read_action(action_val, h);
        return alt;
    }

    static Item read_item(AnyVal val, MemHolder* h) {
        Item item;
        if (val.is_null() || !val.is_pointer()) return item;
        TinyMapView node(val.to_offset(), h);
        item.kind          = read_int(node.get(uint8_t(ast::CODE)));
        item.name          = read_str(node.get(uint8_t(ast::NAME)),    h);
        item.grammar_alias = read_str(node.get(uint8_t(ast::GRAMMAR)), h);
        item.min           = read_int(node.get(uint8_t(ast::MIN)));
        item.max           = read_int(node.get(uint8_t(ast::MAX)));

        // Sub-item (OPT, REP, LOOKAHEAD, NEG_AHEAD)
        AnyVal sub_val = node.get(uint8_t(ast::ITEM));
        if (!sub_val.is_null()) item.sub_items.push_back(read_item(sub_val, h));

        // Sub-alts (GROUP)
        AnyVal alts_val = node.get(uint8_t(ast::ALTS));
        if (!alts_val.is_null()) {
            ArrayView alts(alts_val.to_offset(), h);
            for (uint64_t i = 0; i < alts.size(); ++i)
                item.sub_alts.push_back(read_alt(alts.get(i), h));
        }
        return item;
    }

    static void read_rules(MapView& root, MemHolder* h, GrammarInfo& g) {
        AnyVal arr_val = root.get("rules");
        if (arr_val.is_null()) return;
        ArrayView arr(arr_val.to_offset(), h);
        for (uint64_t i = 0; i < arr.size(); ++i) {
            AnyVal elem = arr.get(i);
            if (elem.is_null()) continue;
            TinyMapView node(elem.to_offset(), h);
            Rule rule;
            rule.name = read_str(node.get(uint8_t(ast::NAME)), h);
            rule.group = read_str(node.get(uint8_t(ast::GROUP_NAME)), h);
            AnyVal alts_val = node.get(uint8_t(ast::ALTS));
            if (!alts_val.is_null()) {
                ArrayView alts(alts_val.to_offset(), h);
                for (uint64_t j = 0; j < alts.size(); ++j)
                    rule.alts.push_back(read_alt(alts.get(j), h));
            }
            g.rules.push_back(std::move(rule));
        }
    }

    static void read_imports(MapView& root, MemHolder* h,
                             const std::vector<ResolvedModule>& all_modules,
                             GrammarInfo& g) {
        AnyVal arr_val = root.get("imports");
        if (arr_val.is_null()) return;
        ArrayView arr(arr_val.to_offset(), h);
        for (uint64_t i = 0; i < arr.size(); ++i) {
            AnyVal elem = arr.get(i);
            if (elem.is_null()) continue;
            TinyMapView node(elem.to_offset(), h);
            std::string alias = read_str(node.get(uint8_t(ast::ALIAS)), h);
            std::string path  = read_str(node.get(uint8_t(ast::PATH)),  h);
            // Find the output name from the resolved module list.
            std::string out_name;
            for (const auto& m : all_modules) {
                if (m.alias == alias) {
                    // Read output from that module's meta.
                    if (m.grammar.has_root()) {
                        auto robj = m.grammar.root_object();
                        if (!robj.is_null()) {
                            auto rmap = robj.as_map();
                            AnyVal meta_v = rmap.get("meta");
                            if (!meta_v.is_null()) {
                                TinyMapView meta(meta_v.to_offset(), m.grammar.holder());
                                out_name = read_str(meta.get(uint8_t(ast::OUTPUT)),
                                                    m.grammar.holder());
                            }
                        }
                    }
                    break;
                }
            }
            if (out_name.empty()) out_name = alias;
            g.imports.push_back({alias, out_name});
        }
    }
};

// ═══════════════════════════════════════════════════════════════════════════
// CodeWriter — indented output stream helper
// ═══════════════════════════════════════════════════════════════════════════

class CodeWriter {
public:
    explicit CodeWriter(std::ostream& out) : out_(out) {}

    void line(std::string_view text = {}) {
        if (!text.empty()) {
            for (int i = 0; i < indent_; ++i) out_ << "    ";
        }
        out_ << text << '\n';
    }

    template <typename... Args>
    void fmt(std::format_string<Args...> f, Args&&... args) {
        line(std::format(f, std::forward<Args>(args)...));
    }

    void indent()   { ++indent_; }
    void dedent()   { if (indent_ > 0) --indent_; }

    struct Block {
        CodeWriter& w;
        ~Block() { w.dedent(); }
    };

    Block block(std::string_view open = "{") {
        line(open); indent();
        return Block{*this};
    }

private:
    std::ostream& out_;
    int           indent_ = 0;
};

// ═══════════════════════════════════════════════════════════════════════════
// CodeGen — GrammarInfo → C++ header + source
// ═══════════════════════════════════════════════════════════════════════════

class CodeGen {
public:
    CodeGen(const GrammarInfo& g, const fs::path& out_dir)
        : g_(g), out_dir_(out_dir)
        , parser_class_(to_pascal(g.name) + "Parser")
        , ast_ns_(g.name + "_ast") {}

    void emit_all() {
        emit_header();
        emit_source();
    }

private:
    const GrammarInfo& g_;
    fs::path           out_dir_;
    std::string        parser_class_;
    std::string        ast_ns_;
    int                lc_ = 0;   // label counter — reset per rule, always increasing
    std::string        rcap_var_;       // name of the rule-captures array for $... in current alt
    std::string        cur_rule_group_; // current rule's group tag (empty = none)
    std::string        cur_fold_var_;   // name of the fold accumulator variable (for $0)
    std::string        fold_init_cap_;  // cap name to initialise the next fold REP from

    // Returns a unique label suffix string within the current rule.
    std::string fresh() { return std::to_string(lc_++); }

    // Returns true if any item tree references the GT_TYPE pseudo-token.
    static bool items_use_gt_type(const std::vector<Item>& items) {
        for (const auto& item : items) {
            if (item.kind == int32_t(ast::TOKEN_REF) && item.name == "GT_TYPE")
                return true;
            if (items_use_gt_type(item.sub_items)) return true;
            for (const auto& sa : item.sub_alts)
                if (items_use_gt_type(sa.seq)) return true;
        }
        return false;
    }

    // Returns true if any rule in the grammar references the GT_TYPE pseudo-token.
    static bool grammar_uses_gt_type(const GrammarInfo& g) {
        for (const auto& rule : g.rules)
            for (const auto& alt : rule.alts)
                if (items_use_gt_type(alt.seq)) return true;
        return false;
    }

    static bool action_has_array_capture(const Action& action) {
        for (const auto& f : action.fields)
            if (f.expr.kind == int32_t(ast::ARRAY_CAPTURE)) return true;
        return false;
    }

    static bool action_has_fold_capture(const Action& action) {
        for (const auto& f : action.fields)
            if (f.expr.kind == int32_t(ast::FOLD_CAPTURE)) return true;
        return false;
    }

    // A REP is in fold-mode when its body GROUP has at least one alt with $0 ($FOLD_CAPTURE).
    static bool rep_is_fold(const Item& rep) {
        if (rep.sub_items.empty()) return false;
        const auto& body = rep.sub_items[0];
        if (body.kind == int32_t(ast::GROUP)) {
            for (const auto& sa : body.sub_alts)
                if (sa.action && action_has_fold_capture(*sa.action)) return true;
        }
        return false;
    }

    // ── Header ──────────────────────────────────────────────────────────────

    void emit_header() {
        std::ofstream f(out_dir_ / (g_.output + ".hpp"));
        CodeWriter w(f);

        w.line("// Generated by peg_gen — DO NOT EDIT.");
        w.line("// SPDX-License-Identifier: Apache-2.0");
        w.line();
        w.fmt("#pragma once");
        w.line();
        w.line("#include <string_view>");
        w.line("#include <logos/core/named_code.hpp>");
        w.line("#include <logos/hermes/view.hpp>");
        for (const auto& imp : g_.imports)
            w.fmt("#include \"{}.hpp\"", imp.output);
        w.line();

        // Open namespace.
        w.fmt("namespace {} {{", g_.cxx_namespace);
        w.line();

        // AST constants namespace.
        w.fmt("// ── AST field keys (%fields) and node codes (%nodes) ─────────────────────");
        w.fmt("namespace {} {{", ast_ns_);
        w.indent();
        w.line("using Key  = logos::NamedCode<uint8_t>;");
        w.line("using Code = logos::NamedCode<int32_t>;");
        w.line();

        if (!g_.fields.empty()) {
            w.line("// Field keys (TinyObjectMap slot indices).  Group-scoped keys");
            w.line("// live in nested namespaces; the same slot number may be reused");
            w.line("// across groups because distinct node types never co-exist in one map.");
            // Emit global first.
            for (const auto& f : g_.fields)
                if (f.group.empty())
                    w.fmt("inline constexpr Key  {:20s} {{\"{}\", {}}};", f.name, f.name, f.code);
            // Emit each group.  Collect groups preserving declaration order.
            std::vector<std::string> groups;
            for (const auto& f : g_.fields) {
                if (f.group.empty()) continue;
                if (std::find(groups.begin(), groups.end(), f.group) == groups.end())
                    groups.push_back(f.group);
            }
            for (const auto& g : groups) {
                w.line();
                w.fmt("namespace {} {{", g);
                w.indent();
                for (const auto& f : g_.fields)
                    if (f.group == g)
                        w.fmt("inline constexpr Key  {:20s} {{\"{}\", {}}};", f.name, f.name, f.code);
                w.dedent();
                w.fmt("}} // namespace {}", g);
            }
            w.line();
        }
        if (!g_.nodes.empty()) {
            w.line("// Node type discriminants");
            for (const auto& n : g_.nodes)
                w.fmt("inline constexpr Code {:20s} {{\"{}\", {}}};", n.name, n.name, n.code);
            w.line();
        }
        w.dedent();
        w.fmt("}} // namespace {}", ast_ns_);
        w.line();

        // Token enum (generated from %tokens).
        if (!g_.tokens.empty()) {
            w.line("// ── Token kinds (generated from %tokens) ────────────────────────────────");
            int32_t skip_code = int32_t(ast::TOKEN_SKIP);
            w.fmt("enum class TK_{} : int {{", to_upper(g_.name));
            w.indent();
            w.line("Eof = 0,");
            w.line("Invalid,");
            for (const auto& t : g_.tokens) {
                if (t.kind == skip_code) continue; // skips are not tokens
                w.fmt("{},   // {}", safe_tok_name(t.name), t.pattern);
            }
            w.dedent();
            w.line("};");
            w.line();
        }

        // Parser class.
        w.fmt("// ── Parser ───────────────────────────────────────────────────────────────");
        w.fmt("class {} {{", parser_class_);
        w.line("public:");
        w.indent();

        w.fmt("explicit {}(std::string_view source);", parser_class_);
        w.line();

        if (!g_.exports.empty()) {
            w.line("// Entry points for exported rules.");
            for (const auto& e : g_.exports)
                w.fmt("logos::hermes::HermesCtr parse_{}();", e);
            w.line();
        }

        if (!g_.tokens.empty()) {
            w.line("// Returns true when all input tokens have been consumed.");
            w.line("bool at_eof() { return peek_token().kind == TK::Eof; }");
            w.line("// Line number of the next unconsumed token (1-based).");
            w.line("uint32_t next_line() { return peek_token().line; }");
            w.line("// Text of the next unconsumed token.");
            w.line("std::string_view next_text() { return peek_token().text; }");
            w.line("// Furthest token the parser ever successfully consumed.");
            w.line("// Points to the deepest parse position before backtracking —");
            w.line("// use this for error reporting instead of next_text()/next_line().");
            w.line("uint32_t         furthest_line() const { return furthest_.line ? furthest_.line : 1; }");
            w.line("std::string_view furthest_text() const { return furthest_.text; }");
            w.line();
        }

        w.dedent();
        w.line("private:");
        w.indent();

        // Rule method declarations.
        for (const auto& r : g_.rules)
            w.fmt("logos::hermes::AnyVal rule_{}();", r.name);
        if (!g_.prec.empty()) {
            w.line();
            w.line("// Pratt expression parser (generated from %prec).");
            w.line("logos::hermes::AnyVal pratt_expr(int min_prec = 0);");
            w.line("logos::hermes::AnyVal pratt_atom();");
        }

        w.line();
        w.line("// Lexer state.");
        if (!g_.tokens.empty()) {
            w.fmt("using TK = TK_{};", to_upper(g_.name));
            w.line("struct Token { TK kind; std::string_view text; uint32_t line = 0; };");
            w.line("Token lex_one();");
            w.line("Token next_token();");
            w.line("Token peek_token();");
            w.line("bool  try_token(TK kind);");
            w.line("Token expect_token(TK kind, std::string_view what);");
            if (grammar_uses_gt_type(g_))
                w.line("bool  try_token_gt(); // GT_TYPE pseudo-token: also splits SHR (>>) into two GTs");
        }

        w.line();
        w.line("logos::hermes::HermesCtr doc_;");
        w.line("std::string_view         source_;");
        w.line("size_t                   pos_ = 0;");
        w.line("uint32_t                 line_ = 1;  // current source line (1-based)");
        if (!g_.tokens.empty()) {
            w.line("Token                    la_{};");
            w.line("bool                     have_la_ = false;");
            w.line("Token                    furthest_{}; // furthest successfully consumed token");
        }

        // Sub-parser fields for imported grammars.
        for (const auto& imp : g_.imports)
            w.fmt("{0}::{1} {2}_;",
                  g_.cxx_namespace, // TODO: use imported module's namespace
                  to_pascal(imp.alias) + "Parser",
                  imp.alias);

        w.dedent();
        w.fmt("}}; // class {}", parser_class_);
        w.line();
        w.fmt("}} // namespace {}", g_.cxx_namespace);
        w.line();
    }

    // ── Source ───────────────────────────────────────────────────────────────

    void emit_source() {
        std::ofstream f(out_dir_ / (g_.output + ".cpp"));
        CodeWriter w(f);

        w.line("// Generated by peg_gen — DO NOT EDIT.");
        w.line("// SPDX-License-Identifier: Apache-2.0");
        w.line();
        w.fmt("#include \"{}.hpp\"", g_.output);
        w.line("#include <logos/hermes/access.hpp>");
        w.line("#include <logos/verification/assert.hpp>");
        w.line("#include <charconv>");
        w.line("#include <cctype>");
        w.line();
        w.fmt("namespace {} {{", g_.cxx_namespace);
        w.line();
        w.fmt("using namespace {};", ast_ns_);
        w.line("using logos::hermes::AnyVal;");
        // Bring TK into namespace scope so out-of-class method definitions can reference it.
        if (!g_.tokens.empty())
            w.fmt("using TK = TK_{};", to_upper(g_.name));
        w.line();

        emit_lexer(w);
        emit_public_entries(w);
        emit_rules(w);
        if (!g_.prec.empty()) emit_pratt(w);

        w.fmt("}} // namespace {}", g_.cxx_namespace);
        w.line();
    }

    // ── Lexer ────────────────────────────────────────────────────────────────

    void emit_lexer(CodeWriter& w) {
        if (g_.tokens.empty()) return;

        int32_t lit_code  = int32_t(ast::TOKEN_LITERAL);
        int32_t skip_code = int32_t(ast::TOKEN_SKIP);

        w.line("// ── Lexer ─────────────────────────────────────────────────────────────────");
        w.line();

        // next / peek / try / expect
        w.fmt("{0}::{0}(std::string_view source) : source_(source) {{}}", parser_class_);
        w.line();
        w.fmt("{0}::Token {0}::next_token() {{", parser_class_);
        w.indent();
        w.line("Token t;");
        w.line("if (have_la_) { have_la_ = false; t = la_; } else { t = lex_one(); }");
        w.line("// Track furthest consumed position for error reporting.");
        w.line("if (t.kind != TK::Eof &&");
        w.line("    (t.line > furthest_.line ||");
        w.line("     (t.line == furthest_.line && t.text.data() >= furthest_.text.data())))");
        w.line("    furthest_ = t;");
        w.line("return t;");
        w.dedent();
        w.line("}");
        w.line();

        w.fmt("{0}::Token {0}::peek_token() {{", parser_class_);
        w.indent();
        w.line("if (!have_la_) { la_ = lex_one(); have_la_ = true; }");
        w.line("return la_;");
        w.dedent();
        w.line("}");
        w.line();

        w.fmt("bool {0}::try_token(TK kind) {{", parser_class_);
        w.indent();
        w.line("Token t = peek_token();");
        w.line("if (t.kind == kind) {");
        w.indent();
        w.line("if (t.line > furthest_.line || (t.line == furthest_.line && t.text.data() >= furthest_.text.data()))");
        w.line("    furthest_ = t;");
        w.line("next_token(); return true;");
        w.dedent();
        w.line("}");
        w.line("return false;");
        w.dedent();
        w.line("}");
        w.line();

        // GT_TYPE pseudo-token: accepts TK::GT or splits TK::SHR (>>) into GT + pushed-back GT.
        // Only emitted for grammars that actually use the GT_TYPE pseudo-token.
        if (grammar_uses_gt_type(g_)) {
            w.fmt("bool {0}::try_token_gt() {{", parser_class_);
            w.indent();
            w.line("Token t = peek_token();");
            w.line("if (t.kind == TK::GT) {");
            w.indent();
            w.line("if (t.line > furthest_.line || (t.line == furthest_.line && t.text.data() >= furthest_.text.data()))");
            w.line("    furthest_ = t;");
            w.line("next_token(); return true;");
            w.dedent();
            w.line("}");
            w.line("if (t.kind == TK::SHR) {");
            w.indent();
            w.line("if (t.line > furthest_.line || (t.line == furthest_.line && t.text.data() >= furthest_.text.data()))");
            w.line("    furthest_ = t;");
            w.line("next_token();");
            w.line("la_ = Token{TK::GT, t.text.substr(1, 1), t.line};");
            w.line("have_la_ = true;");
            w.line("return true;");
            w.dedent();
            w.line("}");
            w.line("return false;");
            w.dedent();
            w.line("}");
            w.line();
        }

        w.fmt("{0}::Token {0}::expect_token(TK kind, std::string_view what) {{", parser_class_);
        w.indent();
        w.line("Token t = next_token();");
        w.fmt("LOGOS_ASSERT(t.kind == kind, \"{}-LEX-001\",", to_upper(g_.name));
        w.line("    \"expected {}, got '{}'\", what, t.text);");
        w.line("return t;");
        w.dedent();
        w.line("}");
        w.line();

        // lex_one
        w.fmt("{0}::Token {0}::lex_one() {{", parser_class_);
        w.indent();

        // Emit skip patterns first.
        w.line("// Skip whitespace and comments.");
        w.line("while (pos_ < source_.size()) {");
        w.indent();
        w.line("char c = source_[pos_];");
        // Common whitespace
        bool has_ws_skip = false;
        for (const auto& t : g_.tokens) {
            if (t.kind == skip_code && (t.pattern == R"(/[ \t\n\r]+/)" ||
                t.pattern == "/[ \t\n\r]+/")) {
                has_ws_skip = true; break;
            }
        }
        if (has_ws_skip) {
            w.line(R"(if (c == ' ' || c == '\t' || c == '\n' || c == '\r') { if (c == '\n') ++line_; ++pos_; continue; })");
        }
        // Helper: strip outer /.../ delimiters to get the raw regex content.
        auto regex_inner = [](const std::string& p) -> std::string_view {
            std::string_view sv = p;
            if (sv.size() >= 2 && sv.front() == '/' && sv.back() == '/')
                sv = sv.substr(1, sv.size() - 2);
            return sv;
        };
        // Line comment skip: pattern inner starts with // or \/\/ (escaped-slash pair).
        for (const auto& t : g_.tokens) {
            if (t.kind != skip_code) continue;
            auto inner = regex_inner(t.pattern);
            if (inner.starts_with("//") || inner.starts_with("\\/\\/")) {
                w.line("if (c == '/' && pos_+1 < source_.size() && source_[pos_+1] == '/') {");
                w.line(R"(    while (pos_ < source_.size() && source_[pos_] != '\n') ++pos_;)");
                w.line("    continue; }");
                break;
            }
        }
        // Block comment: pattern inner starts with /* or \/\* (escaped-slash + escaped-star).
        for (const auto& t : g_.tokens) {
            if (t.kind != skip_code) continue;
            auto inner = regex_inner(t.pattern);
            if (inner.starts_with("/*") || inner.starts_with("\\/\\*")) {
                w.line("if (c == '/' && pos_+1 < source_.size() && source_[pos_+1] == '*') {");
                w.line("    pos_ += 2;");
                w.line("    while (pos_+1 < source_.size() &&");
                w.line("           !(source_[pos_] == '*' && source_[pos_+1] == '/')) ++pos_;");
                w.line("    if (pos_+1 < source_.size()) pos_ += 2;");
                w.line("    continue; }");
                break;
            }
        }
        w.line("break;");
        w.dedent();
        w.line("}");
        w.line("if (pos_ >= source_.size()) return {TK::Eof, {}, line_};");
        w.line("size_t   start      = pos_;");
        w.line("uint32_t start_line_ = line_;");
        w.line("char     c           = source_[pos_];");
        w.line("(void)c;");
        w.line();

        // Keyword literals — longest match first, sorted by length desc.
        std::vector<const TokenDecl*> literals;
        for (const auto& t : g_.tokens)
            if (t.kind == lit_code) literals.push_back(&t);
        std::sort(literals.begin(), literals.end(), [](auto* a, auto* b) {
            return unquote(a->pattern).size() > unquote(b->pattern).size();
        });

        if (!literals.empty()) {
            w.line("// Keyword / punctuation literals.");
            for (const auto* t : literals) {
                std::string pat = std::string(unquote(t->pattern));
                if (pat.size() == 1) {
                    w.fmt("if (c == '{}') {{ ++pos_; return {{TK::{}, source_.substr(start, 1), start_line_}}; }}",
                          escape_char(pat[0]), safe_tok_name(t->name));
                } else {
                    // Word-like keywords (all alnum/underscore) need a boundary check:
                    // the character after the match must not be alnum/underscore.
                    bool is_word = std::all_of(pat.begin(), pat.end(),
                        [](char ch) { return std::isalnum(static_cast<unsigned char>(ch)) || ch == '_'; });
                    w.fmt("if (source_.substr(pos_, {0}).size() == {0} &&", pat.size());
                    if (is_word) {
                        w.fmt("    source_.substr(pos_, {}) == \"{}\" &&", pat.size(), pat);
                        w.fmt("    (pos_ + {} >= source_.size() || (!std::isalnum(source_[pos_ + {}]) && source_[pos_ + {}] != '_'))) {{",
                              pat.size(), pat.size(), pat.size());
                    } else {
                        w.fmt("    source_.substr(pos_, {}) == \"{}\") {{", pat.size(), pat);
                    }
                    w.fmt("    pos_ += {}; return {{TK::{}, source_.substr(start, {}), start_line_}}; }}",
                          pat.size(), safe_tok_name(t->name), pat.size());
                }
            }
            w.line();
        }

        // Well-known regex patterns — generated as hand-coded matchers.
        emit_regex_tokens(w);

        w.line("++pos_; // unknown character — surface it to the parser");
        w.line("return {TK::Invalid, source_.substr(start, 1), start_line_};");
        w.dedent();
        w.line("}");
        w.line();
    }

    // Return name of the first FLOAT-like regex token (pattern has '.' and '[0-9]'), or "".
    std::string find_float_token_name() const {
        for (const auto& t : g_.tokens) {
            if (t.kind != int32_t(ast::TOKEN_REGEX)) continue;
            std::string_view pat = std::string_view(t.pattern);
            if (pat.size() >= 2 && pat.front() == '/' && pat.back() == '/')
                pat = pat.substr(1, pat.size() - 2);
            if (pat.find('.') != std::string::npos && pat.find("[0-9]") != std::string::npos)
                return safe_tok_name(t.name);
        }
        return {};
    }

    // Detect regex features for enhanced number lexing.
    static bool pat_has_hex(std::string_view pat)  { return pat.find("a-f") != std::string::npos || pat.find("a-F") != std::string::npos; }
    static bool pat_has_bin(std::string_view pat)  { return pat.find("0[bB]") != std::string::npos || pat.find("0b") != std::string::npos; }
    static bool pat_has_oct(std::string_view pat)  { return pat.find("0[oO]") != std::string::npos || pat.find("0o") != std::string::npos; }
    static bool pat_is_integer_regex(std::string_view pat) {
        const bool has_digits = pat.find("[0-9]+") != std::string::npos ||
                                pat.find("[0-9][0-9_]*") != std::string::npos;
        const bool has_decimal_point = pat.find("\\.") != std::string::npos;
        return has_digits && !has_decimal_point;
    }
    static bool pat_has_int_suffix(std::string_view pat) {
        return pat.find("i8") != std::string::npos    || pat.find("i16") != std::string::npos ||
               pat.find("i32") != std::string::npos   || pat.find("i64") != std::string::npos ||
               pat.find("u8") != std::string::npos    || pat.find("u16") != std::string::npos ||
               pat.find("u32") != std::string::npos   || pat.find("u64") != std::string::npos ||
               pat.find("usize") != std::string::npos || pat.find("isize") != std::string::npos ||
               pat.find("ull") != std::string::npos   || pat.find("_u") != std::string::npos;
    }
    static bool pat_has_float_suffix(std::string_view pat) {
        return pat.find("f32") != std::string::npos || pat.find("f64") != std::string::npos ||
               pat.find("[fd]") != std::string::npos;
    }

    // Emit suffix matching for integer tokens.
    // Supports Rust-style suffixes (i8/i16/i32/i64/u8/u16/u32/u64/usize/isize)
    // plus older C-style suffixes kept for backwards compatibility.
    static void emit_int_suffix_matching(CodeWriter& w) {
        // Helper lambda in generated code: try to match a suffix string.
        w.line("// Integer type suffix (longest match).");
        w.line("auto try_suffix = [&](std::string_view sfx) -> bool {");
        w.line("    if (pos_ + sfx.size() <= source_.size() &&");
        w.line("        source_.substr(pos_, sfx.size()) == sfx) {");
        w.line("        pos_ += sfx.size(); return true;");
        w.line("    }");
        w.line("    return false;");
        w.line("};");
        // Ordered longest-first. The C-style 'u' suffix must be checked last
        // and must NOT be followed by an alnum/underscore (to avoid eating IDENT).
        w.line("if (!try_suffix(\"usize\") && !try_suffix(\"isize\") &&");
        w.line("    !try_suffix(\"_u64\") && !try_suffix(\"_u32\") && !try_suffix(\"_u16\") && !try_suffix(\"_u8\") &&");
        w.line("    !try_suffix(\"_s64\") && !try_suffix(\"_s32\") && !try_suffix(\"_s16\") && !try_suffix(\"_s8\") &&");
        w.line("    !try_suffix(\"i128\") && !try_suffix(\"i64\") && !try_suffix(\"i56\") && !try_suffix(\"i32\") && !try_suffix(\"i16\") && !try_suffix(\"i8\") &&");
        w.line("    !try_suffix(\"u128\") && !try_suffix(\"u64\") && !try_suffix(\"u56\") && !try_suffix(\"u32\") && !try_suffix(\"u16\") && !try_suffix(\"u8\") &&");
        w.line("    !try_suffix(\"ull\") && !try_suffix(\"ul\") && !try_suffix(\"ll\")) {");
        w.line("    // Single-char 'u' — only if not followed by alnum/underscore.");
        w.line("    if (pos_ < source_.size() && source_[pos_] == 'u' &&");
        w.line("        (pos_+1 >= source_.size() || (!std::isalnum(source_[pos_+1]) && source_[pos_+1] != '_')))");
        w.line("        ++pos_;");
        w.line("}");
    }

    void emit_regex_tokens(CodeWriter& w) {
        const std::string float_tok = find_float_token_name();

        for (const auto& t : g_.tokens) {
            if (t.kind != int32_t(ast::TOKEN_REGEX)) continue;
            std::string_view pat = std::string_view(t.pattern);
            // Strip /.../ delimiters.
            if (pat.size() >= 2 && pat.front() == '/' && pat.back() == '/')
                pat = pat.substr(1, pat.size() - 2);

            // IDENT-like: [a-zA-Z_][a-zA-Z0-9_]*
            if (pat == "[a-zA-Z_][a-zA-Z0-9_]*" || pat == "[a-zA-Z_]\\w*") {
                w.fmt("// {} = /{}/", t.name, pat);
                w.fmt("if (std::isalpha(c) || c == '_') {{");
                w.indent();
                w.line("while (pos_ < source_.size() && (std::isalnum(source_[pos_]) || source_[pos_] == '_'))");
                w.line("    ++pos_;");
                w.fmt("return {{TK::{}, source_.substr(start, pos_ - start), start_line_}};", safe_tok_name(t.name));
                w.dedent();
                w.line("}");
            }
            // INTEGER: patterns containing [0-9]+ without '.' are integer tokens.
            // Supports optional base prefixes (0x, 0b, 0o) and type suffixes
            // (_u8..._u64, _s8..._s64, ull, ul, ll, u) when detected in the regex.
            // If a FLOAT token also exists, performs longest-match: after decimal digits,
            // if '.' followed by a digit is found, consumes fractional part and returns FLOAT.
            else if (pat_is_integer_regex(pat)) {
                const bool hex = pat_has_hex(pat);
                const bool bin = pat_has_bin(pat);
                const bool oct = pat_has_oct(pat);
                const bool int_sfx = pat_has_int_suffix(pat);
                // Float suffix is detected from the FLOAT token pattern.
                std::string_view float_pat;
                for (const auto& ft : g_.tokens) {
                    if (ft.kind != int32_t(ast::TOKEN_REGEX)) continue;
                    float_pat = ft.pattern;
                    if (float_pat.size() >= 2 && float_pat.front() == '/' && float_pat.back() == '/')
                        float_pat = float_pat.substr(1, float_pat.size() - 2);
                    if (float_pat.find('.') != std::string::npos && float_pat.find("[0-9]") != std::string::npos)
                        break;
                    float_pat = {};
                }
                const bool flt_sfx = pat_has_float_suffix(float_pat);

                w.fmt("// {} = /{}/", t.name, pat);
                // Entry condition: digit or negative-digit. Dot-digit (.5) is NOT used
                // because '.' is already the DOT token and t.1.0 would otherwise be
                // lexed as FLOAT(".1") instead of DOT + INTEGER("1") + DOT + INTEGER("0").
                if (!float_tok.empty()) {
                    w.fmt("if (std::isdigit(c) || (c == '-' && pos_+1 < source_.size() && std::isdigit(source_[pos_+1]))) {{");
                } else {
                    w.fmt("if (std::isdigit(c) || (c == '-' && pos_+1 < source_.size() && std::isdigit(source_[pos_+1]))) {{");
                }
                w.indent();
                w.line("if (c == '-') ++pos_;");

                if (hex || bin || oct) {
                    // Base prefix detection.
                    w.line("int base = 10;");
                    w.line("if (pos_ < source_.size() && source_[pos_] == '0' && pos_+1 < source_.size()) {");
                    w.indent();
                    w.line("char nx = source_[pos_+1];");
                    if (hex) w.line("if (nx == 'x' || nx == 'X') { base = 16; pos_ += 2; }");
                    if (bin) w.fmt("{}if (nx == 'b' || nx == 'B') {{ base = 2; pos_ += 2; }}", hex ? "else " : "");
                    if (oct) w.fmt("{}if (nx == 'o' || nx == 'O') {{ base = 8; pos_ += 2; }}", (hex || bin) ? "else " : "");
                    w.dedent();
                    w.line("}");
                    // Consume digits per base.
                    w.line("if (base == 16) {");
                    w.line("    while (pos_ < source_.size() && (std::isxdigit(source_[pos_]) || source_[pos_] == '_')) ++pos_;");
                    w.line("} else if (base == 2) {");
                    w.line("    while (pos_ < source_.size() && (source_[pos_] == '0' || source_[pos_] == '1' || source_[pos_] == '_')) ++pos_;");
                    w.line("} else if (base == 8) {");
                    w.line("    while (pos_ < source_.size() && ((source_[pos_] >= '0' && source_[pos_] <= '7') || source_[pos_] == '_')) ++pos_;");
                    w.line("} else {");
                    w.line("    while (pos_ < source_.size() && (std::isdigit(source_[pos_]) || source_[pos_] == '_')) ++pos_;");
                    w.line("}");
                } else {
                    w.line("while (pos_ < source_.size() && (std::isdigit(source_[pos_]) || source_[pos_] == '_')) ++pos_;");
                }

                if (!float_tok.empty()) {
                    // Longest-match: decimal-only → if next is '.' followed by digit, it's a float.
                    // But NOT if the token started right after a '.': that's a tuple index (t.1.0).
                    if (hex || bin || oct) {
                        w.line("if (base == 10 && pos_ < source_.size() && source_[pos_] == '.'");
                    } else {
                        w.line("if (pos_ < source_.size() && source_[pos_] == '.'");
                    }
                    w.line("    && pos_+1 < source_.size() && std::isdigit(source_[pos_+1])");
                    w.line("    && !(start > 0 && source_[start - 1] == '.')) {");
                    w.indent();
                    w.line("++pos_; // consume '.'");
                    w.line("while (pos_ < source_.size() && (std::isdigit(source_[pos_]) || source_[pos_] == '_')) ++pos_;");
                    w.line("if (pos_ < source_.size() && (source_[pos_] == 'e' || source_[pos_] == 'E')) {");
                    w.indent();
                    w.line("++pos_;");
                    w.line("if (pos_ < source_.size() && (source_[pos_] == '+' || source_[pos_] == '-')) ++pos_;");
                    w.line("while (pos_ < source_.size() && (std::isdigit(source_[pos_]) || source_[pos_] == '_')) ++pos_;");
                    w.dedent();
                    w.line("}");
                    if (flt_sfx) {
                        w.line("if (pos_ + 3 <= source_.size() &&");
                        w.line("    (source_.substr(pos_, 3) == \"f32\" || source_.substr(pos_, 3) == \"f64\")) {");
                        w.line("    pos_ += 3;");
                        w.line("} else if (pos_ < source_.size() && (source_[pos_] == 'f' || source_[pos_] == 'd')) {");
                        w.line("    ++pos_;");
                        w.line("}");
                    }
                    w.fmt("return {{TK::{}, source_.substr(start, pos_ - start), start_line_}};", float_tok);
                    w.dedent();
                    w.line("}");
                }

                if (int_sfx) {
                    // Integer type suffixes — try longest match.
                    emit_int_suffix_matching(w);
                }

                w.fmt("return {{TK::{}, source_.substr(start, pos_ - start), start_line_}};", safe_tok_name(t.name));
                w.dedent();
                w.line("}");
            }
            // FLOAT: handled by the INTEGER longest-match handler above — skip.
            else if (pat.find('.') != std::string::npos && pat.find("[0-9]") != std::string::npos) {
                w.fmt("// {} = /{}/ — handled by INTEGER longest-match above", t.name, pat);
            }
            // RAW_STRING: r"...", r#"..."#, r##"..."##, etc.
            // Count '#' after 'r' to determine delimiter depth.
            else if (pat.size() >= 4 && pat[0] == 'r' && pat[1] == '"') {
                w.fmt("// {} = /{}/ (also r#\"...\"#, r##\"...\"##, ...)", t.name, pat);
                w.line(R"(if (c == 'r' && pos_+1 < source_.size() && (source_[pos_+1] == '"' || source_[pos_+1] == '#')) {)");
                w.indent();
                w.line("size_t hashes = 0;");
                w.line("size_t p = pos_ + 1;");
                w.line("while (p < source_.size() && source_[p] == '#') { ++hashes; ++p; }");
                w.line(R"(if (p < source_.size() && source_[p] == '"') {)");
                w.indent();
                w.line("pos_ = p + 1; // skip r###...\"");
                w.line("bool found = false;");
                w.line("while (!found && pos_ < source_.size()) {");
                w.indent();
                w.line(R"(if (source_[pos_] == '"') {)");
                w.line("    size_t h = 0;");
                w.line("    while (h < hashes && pos_+1+h < source_.size() && source_[pos_+1+h] == '#') ++h;");
                w.line("    if (h == hashes) { pos_ += 1 + hashes; found = true; }");
                w.line("    else ++pos_;");
                w.line("} else ++pos_;");
                w.dedent();
                w.line("}");
                w.fmt("return {{TK::{}, source_.substr(start, pos_ - start), start_line_}};", safe_tok_name(t.name));
                w.dedent();
                w.line("}");
                w.dedent();
                w.line("}");
            }
            // STRING: \"([^\"\\\\]|\\\\.)*\"
            else if (pat.find('"') != std::string::npos) {
                w.fmt("// {} = /{}/", t.name, pat);
                w.line(R"(if (c == '"') {)");
                w.indent();
                w.line("++pos_;");
                w.line(R"(while (pos_ < source_.size() && source_[pos_] != '"') {)");
                w.line(R"(    if (source_[pos_] == '\\') ++pos_;)");
                w.line("    ++pos_;");
                w.line("}");
                w.line("if (pos_ < source_.size()) ++pos_;");
                w.fmt("return {{TK::{}, source_.substr(start, pos_ - start), start_line_}};", safe_tok_name(t.name));
                w.dedent();
                w.line("}");
            }
            // LIFETIME-like: starts with apostrophe, then [a-z][a-z0-9_]*
            // Matches Rust-style lifetime annotations: 'a, 'static, 'lifetime_name
            // NOTE: c = source_[pos_] is read WITHOUT incrementing pos_, so pos_
            // still points at the apostrophe.  We must check pos_+1 for the letter
            // after it, then advance pos_ past the apostrophe before the while loop.
            else if (pat.size() >= 2 && pat.front() == '\'' && pat[1] == '[') {
                bool lower_start = pat.find("[a-z") != std::string::npos;
                bool alnum_rest  = pat.find("[a-z0-9_]") != std::string::npos
                                || pat.find("[a-zA-Z0-9_]") != std::string::npos;
                w.fmt("// {} = /{}/", t.name, pat);
                if (lower_start) {
                    w.line("if (c == '\\'' && pos_ + 1 < source_.size() && std::islower(source_[pos_ + 1])) {");
                } else {
                    w.line("if (c == '\\'' && pos_ + 1 < source_.size() && std::isalpha(source_[pos_ + 1])) {");
                }
                w.indent();
                w.line("++pos_;  // advance past apostrophe");
                if (alnum_rest) {
                    w.line("while (pos_ < source_.size() && (std::isalnum(source_[pos_]) || source_[pos_] == '_'))");
                    w.line("    ++pos_;");
                } else {
                    w.line("if (pos_ < source_.size() && std::isalpha(source_[pos_])) ++pos_;");
                }
                w.fmt("return {{TK::{}, source_.substr(start, pos_ - start), start_line_}};", safe_tok_name(t.name));
                w.dedent();
                w.line("}");
            }
            else {
                w.fmt("// TODO: {} = /{}/  — add hand-coded matcher", t.name, pat);
            }
            w.line();
        }
    }

    // ── Public entry points ───────────────────────────────────────────────────

    void emit_public_entries(CodeWriter& w) {
        if (g_.exports.empty()) return;
        w.line("// ── Public entry points ───────────────────────────────────────────────────");
        w.line();
        for (const auto& e : g_.exports) {
            w.fmt("logos::hermes::HermesCtr {}::parse_{}() {{", parser_class_, e);
            w.indent();
            w.line("doc_ = logos::hermes::make_doc(524288).get();");
            w.fmt("AnyVal root = rule_{}();", e);
            w.fmt("LOGOS_ASSERT(!root.is_null(), \"{}-PARSE-001\", \"parse_{}: expected {}\");",
                  to_upper(g_.name), e, e);
            w.line("logos::hermes::HermesCtrAccess::set_root_offset(doc_, root.to_offset());");
            w.line("return std::move(doc_);");
            w.dedent();
            w.line("}");
            w.line();
        }
    }

    // ── Rule implementations ──────────────────────────────────────────────────

    void emit_rules(CodeWriter& w) {
        w.line("// ── Grammar rules ─────────────────────────────────────────────────────────");
        w.line();
        for (const auto& rule : g_.rules) emit_rule(w, rule);
    }

    void emit_rule(CodeWriter& w, const Rule& rule) {
        lc_ = 0;  // reset label counter for this rule
        cur_rule_group_ = rule.group;
        w.fmt("AnyVal {}::rule_{}() {{", parser_class_, rule.name);
        w.indent();
        w.line("size_t saved_pos;");
        w.line("bool   saved_la;");
        w.line("size_t saved_doc_;");
        w.line();

        for (size_t i = 0; i < rule.alts.size(); ++i)
            emit_alt(w, rule, rule.alts[i], i);

        w.line("return AnyVal{}; // no alternative matched");
        w.dedent();
        w.line("}");
        w.line();
    }

    void emit_alt(CodeWriter& w, const Rule& rule, const Item::Alt& alt, size_t idx) {
        // Comment showing the PEG source of this alternative.
        w.fmt("// Alternative {}: {}", idx + 1, peg_text(alt));
        // Outer block: holds saved_tok so it's accessible in the restore code below.
        w.line("{");
        w.indent();
        w.line("saved_pos  = pos_;");
        w.line("saved_la   = have_la_;");
        w.line("saved_doc_ = doc_.arena_checkpoint();");
        w.line("Token    saved_tok_  = la_;");
        w.line("uint32_t saved_line_ = line_;");
        w.line();
        // Inner block: all captures and node pointers are scoped here.
        // The backtrack label below is OUTSIDE this block so gotos don't cross inits.
        w.line("{");
        w.indent();
        // first_line_: line of the first token of this alt's match (for SRC_LINE in AST nodes).
        w.line("[[maybe_unused]] uint32_t first_line_ = peek_token().line;");

        // Backtrack label for this alternative — used as fail_label for all items.
        std::string alt_fail = std::format("bt_{}_{}", rule.name, idx);

        // If the action uses $..., declare a rule-captures collector array.
        // RULE_REF results anywhere in the sequence push to it; TOKEN_REF results don't.
        rcap_var_.clear();
        if (alt.action && action_has_array_capture(*alt.action)) {
            rcap_var_ = "rcap_" + std::to_string(lc_++);
            w.fmt("auto {} = doc_.make_array(4).get();", rcap_var_);
        }

        // Capture slots (one per item in the sequence).
        // We number them from 1 ($1, $2, ...) to match grammar action syntax.
        // All captures have type AnyVal — tokens are interned as arena strings,
        // rule results are stored as arena object offsets.
        std::vector<std::string> captures(alt.seq.size() + 1); // 1-indexed
        for (size_t i = 0; i < alt.seq.size(); ++i) {
            std::string cap = std::format("cap{}", i + 1);
            captures[i + 1] = cap;
            // For fold-mode REP/OPT: provide the preceding item's cap as the fold initialiser.
            if ((alt.seq[i].kind == int32_t(ast::REP) || alt.seq[i].kind == int32_t(ast::OPT))
                && rep_is_fold(alt.seq[i]) && i > 0)
                fold_init_cap_ = captures[i]; // captures[i] = cap of item i-1 (1-indexed)
            emit_item_match(w, alt.seq[i], cap, alt_fail, i);
            fold_init_cap_.clear();
        }

        // Action: build AST node.  rcap_var_ is still set here for ARRAY_CAPTURE use.
        if (alt.action) {
            emit_action(w, *alt.action, captures);
            rcap_var_.clear();
        } else if (alt.seq.size() == 1) {
            rcap_var_.clear();
            // No action + single item → pass through.
            w.fmt("return {};", captures[1]);
        } else {
            rcap_var_.clear();
            // No action + multiple items → return the last capture.
            // This covers fold-chain rules like `atom <- primary_expr postfix*`
            // where the fold REP accumulates into its cap.
            w.fmt("return {};", captures[alt.seq.size()]);
        }

        w.dedent();
        w.line("}"); // end inner block
        // Backtrack label is here, AFTER the inner block — no initialization is crossed.
        w.fmt("[[maybe_unused]] bt_{}_{}:", rule.name, idx);
        w.line("pos_      = saved_pos;");
        w.line("have_la_  = saved_la;");
        w.line("la_       = saved_tok_;");
        w.line("line_     = saved_line_;");
        w.line("doc_.arena_rollback(saved_doc_);");
        w.dedent();
        w.line("}");
        w.line();
    }

    // Emit code to match one item; on failure goto fail_label.
    // item_idx is just used for generating unique sub-label names within this item.
    void emit_item_match(CodeWriter& w, const Item& item, const std::string& cap,
                         const std::string& fail_label, size_t item_idx) {
        switch (item.kind) {

        case int32_t(ast::TOKEN_REF): {
            // GT_TYPE is a pseudo-token: matches '>' but also splits '>>' (SHR)
            // into two '>' tokens to allow nested generics like Foo<Bar<T>>.
            if (item.name == "GT_TYPE") {
                w.fmt("if (!try_token_gt()) goto {};", fail_label);
                w.fmt("[[maybe_unused]] AnyVal {} = AnyVal{{}};", cap);
                break;
            }
            // Match token, intern text as arena string → AnyVal offset.
            w.fmt("if (peek_token().kind != TK::{}) goto {};",
                  safe_tok_name(item.name), fail_label);
            w.fmt("Token tok_{0}_ = next_token();", cap);
            w.fmt("[[maybe_unused]] AnyVal {0} = doc_.make_string(tok_{0}_.text).get().to_anyval();", cap);
            break;
        }

        case int32_t(ast::RULE_REF): {
            std::string call;
            if (item.grammar_alias.empty())
                call = std::format("rule_{}()", item.name);
            else
                call = std::format("{0}_.rule_{1}()", item.grammar_alias, item.name);
            w.fmt("[[maybe_unused]] AnyVal {} = {};", cap, call);
            w.fmt("if ({}.is_null()) goto {};", cap, fail_label);
            // Collect into rule-captures array if $... is used in this alt's action.
            if (!rcap_var_.empty())
                w.fmt("{}.push_back({}).get();", rcap_var_, cap);
            break;
        }

        case int32_t(ast::LITERAL): {
            // Match literal string, intern as arena string → AnyVal offset.
            size_t n = item.name.size();
            w.fmt("if (pos_ + {0} > source_.size() || source_.substr(pos_, {0}) != \"{1}\") goto {2};",
                  n, item.name, fail_label);
            w.fmt("[[maybe_unused]] AnyVal {0} = doc_.make_string(source_.substr(pos_, {1})).get().to_anyval();", cap, n);
            w.fmt("pos_ += {};", n);
            break;
        }

        case int32_t(ast::OPT): {
            // Optional: try sub-item, silently ignore failure. cap = null AnyVal if no match.
            std::string id       = fresh();
            std::string done_lbl = "opt_done_" + id;
            std::string fail_lbl = "opt_fail_" + id;

            // Fold-mode OPT: body GROUP references $0 via FOLD_CAPTURE, so the
            // pre-OPT capture must be threaded through.  Matches rep_is_fold's
            // logic; both quantifiers share the same predicate.
            const bool fold_mode = rep_is_fold(item) && !fold_init_cap_.empty();
            if (fold_mode) {
                std::string fold_acc = "opt_acc_" + id;
                w.fmt("AnyVal {} = {};", fold_acc, fold_init_cap_);
                cur_fold_var_ = fold_acc;
                w.line("{");
                w.indent();
                w.line("size_t opt_pos_ = pos_; bool opt_la_ = have_la_; Token opt_tok_ = la_; uint32_t opt_line_ = line_;");
                w.line("size_t opt_doc_ = doc_.arena_checkpoint();");
                w.line("{");
                w.indent();
                if (!item.sub_items.empty()) {
                    std::string sub_cap = cap + "_s";
                    emit_item_match(w, item.sub_items[0], sub_cap, fail_lbl, 0);
                    w.fmt("{} = {};", fold_acc, sub_cap);
                }
                w.fmt("goto {};", done_lbl);
                w.dedent();
                w.line("}");
                w.fmt("{}: ;", fail_lbl);
                w.line("pos_ = opt_pos_; have_la_ = opt_la_; la_ = opt_tok_; line_ = opt_line_;");
                w.line("doc_.arena_rollback(opt_doc_);");
                w.fmt("{}: ;", done_lbl);
                w.dedent();
                w.line("}");
                w.fmt("[[maybe_unused]] AnyVal {} = {};", cap, fold_acc);
                cur_fold_var_.clear();
                break;
            }

            w.fmt("[[maybe_unused]] AnyVal {} = AnyVal{{}};", cap);
            w.line("{");
            w.indent();
            w.line("size_t opt_pos_ = pos_; bool opt_la_ = have_la_; Token opt_tok_ = la_; uint32_t opt_line_ = line_;");
            w.line("size_t opt_doc_ = doc_.arena_checkpoint();");
            w.line("{"); // inner scope for sub-item
            w.indent();
            if (!item.sub_items.empty()) {
                std::string sub_cap = cap + "_s";
                emit_item_match(w, item.sub_items[0], sub_cap, fail_lbl, 0);
                w.fmt("{} = {};", cap, sub_cap);
            }
            w.fmt("goto {};", done_lbl);
            w.dedent();
            w.line("}");
            w.fmt("{}: ;", fail_lbl);
            w.line("pos_ = opt_pos_; have_la_ = opt_la_; la_ = opt_tok_; line_ = opt_line_;");
            w.line("doc_.arena_rollback(opt_doc_);");
            w.fmt("{}: ;", done_lbl);
            w.dedent();
            w.line("}");
            break;
        }

        case int32_t(ast::REP): {
            std::string id       = fresh();
            std::string fail_lbl = "rep_fail_" + id;

            if (rep_is_fold(item) && !fold_init_cap_.empty()) {
                // ── Fold-mode repetition ─────────────────────────────────────────
                // Accumulator starts from the preceding sequence item.
                // Each iteration: GROUP matches a postfix suffix, builds a new node
                // with RECEIVER=$0 (= fold accumulator), then fold_acc = new node.
                std::string fold_acc = "fold_acc_" + id;
                w.fmt("AnyVal {} = {};", fold_acc, fold_init_cap_);
                // Set cur_fold_var_ so that $0 inside GROUP alt actions resolves correctly.
                cur_fold_var_ = fold_acc;
                w.line("{");
                w.indent();
                w.line("while (true) {");
                w.indent();
                w.line("size_t rep_pos_ = pos_; bool rep_la_ = have_la_; Token rep_tok_ = la_; uint32_t rep_line_ = line_;");
                w.line("size_t rep_doc_ = doc_.arena_checkpoint();");
                if (!item.sub_items.empty()) {
                    w.line("{");
                    w.indent();
                    std::string sub_cap = "rep_item_" + id;
                    emit_item_match(w, item.sub_items[0], sub_cap, fail_lbl, 0);
                    w.fmt("{} = {};", fold_acc, sub_cap);
                    w.line("continue;");
                    w.dedent();
                    w.line("}");
                }
                w.fmt("{}: ;", fail_lbl);
                w.line("pos_ = rep_pos_; have_la_ = rep_la_; la_ = rep_tok_; line_ = rep_line_;");
                w.line("doc_.arena_rollback(rep_doc_);");
                w.line("break;");
                w.dedent();
                w.line("}");
                w.dedent();
                w.line("}");
                w.fmt("[[maybe_unused]] AnyVal {} = {};", cap, fold_acc);
                cur_fold_var_.clear();
                if (item.min > 0)
                    w.fmt("if ({}.is_null()) goto {};", fold_acc, fail_label);
            } else {
                // ── Array-accumulation mode (original behaviour) ─────────────────
                std::string arr_var = "arr_" + id;
                w.fmt("auto {} = doc_.make_array(4).get();", arr_var);
                w.line("{");
                w.indent();
                w.line("while (true) {");
                w.indent();
                w.line("size_t rep_pos_ = pos_; bool rep_la_ = have_la_; Token rep_tok_ = la_; uint32_t rep_line_ = line_;");
                w.line("size_t rep_doc_ = doc_.arena_checkpoint();");
                if (!item.sub_items.empty()) {
                    w.line("{");
                    w.indent();
                    std::string sub_cap = "rep_item_" + id;
                    emit_item_match(w, item.sub_items[0], sub_cap, fail_lbl, 0);
                    w.fmt("if (!{}.is_null()) {}.push_back({}).get();", sub_cap, arr_var, sub_cap);
                    w.line("continue;");
                    w.dedent();
                    w.line("}");
                }
                w.fmt("{}: ;", fail_lbl);
                w.line("pos_ = rep_pos_; have_la_ = rep_la_; la_ = rep_tok_; line_ = rep_line_;");
                w.line("doc_.arena_rollback(rep_doc_);");
                w.line("break;");
                w.dedent();
                w.line("}");
                w.dedent();
                w.line("}");
                w.fmt("[[maybe_unused]] AnyVal {} = {}.to_anyval();", cap, arr_var);
                if (item.min > 0)
                    w.fmt("if ({}.size() < {}) goto {};", arr_var, item.min, fail_label);
            }
            break;
        }

        case int32_t(ast::GROUP): {
            // Inline ordered choice: try each alt, use first that succeeds.
            // Each alt: outer scope holds position save; inner scope holds item captures.
            // All labels use unique IDs from fresh() to avoid collisions.
            // If an alt's action uses $..., a per-alt rcap array is set up for it.
            std::string grp_id   = fresh();
            std::string done_lbl = "grp_done_" + grp_id;
            w.fmt("[[maybe_unused]] AnyVal {} = AnyVal{{}};", cap);
            w.line("{");
            w.indent();
            w.line("size_t grp_pos_; bool grp_la_; Token grp_tok_; size_t grp_doc_; uint32_t grp_line_;");
            for (size_t gi = 0; gi < item.sub_alts.size(); ++gi) {
                const auto& sa = item.sub_alts[gi];
                std::string alt_fail = "grp_fail_" + grp_id + "_" + std::to_string(gi);
                w.fmt("// Group alt {}", gi + 1);
                w.line("{");
                w.indent();
                w.line("grp_pos_ = pos_; grp_la_ = have_la_; grp_tok_ = la_; grp_doc_ = doc_.arena_checkpoint(); grp_line_ = line_;");
                w.line("{"); // inner scope: item captures

                // Per-alt rcap: only when this alt has its own $... action.
                // If no $... action: keep the outer rcap_var_ so that rule refs
                // in this GROUP alt continue to push to the enclosing collector.
                std::string saved_rcap = rcap_var_;
                if (sa.action && action_has_array_capture(*sa.action)) {
                    rcap_var_ = "grp_rcap_" + grp_id + "_" + std::to_string(gi);
                    w.fmt("auto {} = doc_.make_array(4).get();", rcap_var_);
                }

                w.indent();
                // 1-indexed capture slots for this GROUP alt ($1 = first item, etc.).
                std::vector<std::string> sa_caps(1); // sa_caps[0] unused ($0 = FOLD_CAPTURE)
                for (size_t si = 0; si < sa.seq.size(); ++si) {
                    std::string sc = std::format("{}_gi{}_s{}", cap, gi, si);
                    sa_caps.push_back(sc);
                    emit_item_match(w, sa.seq[si], sc, alt_fail, si);
                }
                if (sa.action) {
                    // Emit action: stores result in `cap`; caller emits goto done_lbl.
                    emit_action(w, *sa.action, sa_caps, cap);
                } else if (sa_caps.size() == 2) {
                    w.fmt("{} = {};", cap, sa_caps[1]);   // single-item passthrough
                } else {
                    w.fmt("{} = AnyVal{{}};  // multi-item group alt (no action)", cap);
                }
                w.fmt("goto {};", done_lbl);
                rcap_var_ = saved_rcap;
                w.dedent();
                w.line("}"); // end inner scope
                w.fmt("{}: ;", alt_fail);
                w.line("pos_ = grp_pos_; have_la_ = grp_la_; la_ = grp_tok_; doc_.arena_rollback(grp_doc_); line_ = grp_line_;");
                w.dedent();
                w.line("}");
            }
            // No alt matched → outer fail.
            w.fmt("goto {};  // no group alternative matched", fail_label);
            w.fmt("{}: ;", done_lbl);
            w.dedent();
            w.line("}");
            break;
        }

        case int32_t(ast::LOOKAHEAD): {
            std::string id       = fresh();
            std::string end_lbl  = "la_end_" + id;
            std::string fail_lbl = "la_fail_" + id;
            w.line("{");
            w.indent();
            w.line("size_t la_pos_ = pos_; bool la_la_ = have_la_; Token la_tok_ = la_; uint32_t la_line_ = line_;");
            w.line("size_t la_doc_ = doc_.arena_checkpoint();");
            w.line("{");
            w.indent();
            if (!item.sub_items.empty()) {
                std::string sub_cap = "la_" + id;
                emit_item_match(w, item.sub_items[0], sub_cap, fail_lbl, 0);
            }
            w.line("pos_ = la_pos_; have_la_ = la_la_; la_ = la_tok_; doc_.arena_rollback(la_doc_); line_ = la_line_;");
            w.fmt("[[maybe_unused]] AnyVal {} = AnyVal{{}};  // lookahead result (position restored)", cap);
            w.fmt("goto {};", end_lbl);
            w.dedent();
            w.line("}");
            w.fmt("{}: ;", fail_lbl);
            w.line("pos_ = la_pos_; have_la_ = la_la_; la_ = la_tok_; doc_.arena_rollback(la_doc_); line_ = la_line_;");
            w.fmt("goto {};", fail_label);
            w.fmt("{}: ;", end_lbl);
            w.dedent();
            w.line("}");
            break;
        }

        case int32_t(ast::NEG_AHEAD): {
            std::string id       = fresh();
            std::string ok_lbl   = "na_ok_" + id;
            std::string fail_lbl = "na_fail_" + id;
            w.line("{");
            w.indent();
            w.line("size_t na_pos_ = pos_; bool na_la_ = have_la_; Token na_tok_ = la_; uint32_t na_line_ = line_;");
            w.line("size_t na_doc_ = doc_.arena_checkpoint();");
            w.line("{");
            w.indent();
            if (!item.sub_items.empty()) {
                std::string sub_cap = "na_" + id;
                emit_item_match(w, item.sub_items[0], sub_cap, ok_lbl, 0);
            }
            // Sub-item matched → negation fails.
            w.line("pos_ = na_pos_; have_la_ = na_la_; la_ = na_tok_; doc_.arena_rollback(na_doc_); line_ = na_line_;");
            w.fmt("goto {};", fail_label);
            w.dedent();
            w.line("}");
            w.fmt("{}: ;", fail_lbl);
            w.fmt("{}: ;", ok_lbl);
            w.line("pos_ = na_pos_; have_la_ = na_la_; la_ = na_tok_; doc_.arena_rollback(na_doc_); line_ = na_line_;");
            w.fmt("[[maybe_unused]] AnyVal {} = AnyVal{{}};  // negative lookahead succeeded", cap);
            w.dedent();
            w.line("}");
            break;
        }

        default:
            w.fmt("[[maybe_unused]] AnyVal {} = AnyVal{{}}; // TODO: item kind {}", cap, item.kind);
            break;
        }
    }

    // ── Action ───────────────────────────────────────────────────────────────
    // out_cap: if empty → emit "return result_" (rule-level alt);
    //          if non-empty → emit "out_cap = result_" (GROUP sub-alt, caller emits goto).

    void emit_action(CodeWriter& w, const Action& action,
                     const std::vector<std::string>& captures,
                     const std::string& out_cap = "") {
        int slot_count = int(action.fields.size()) + 2; // +1 for CODE, +1 for SRC_LINE
        w.fmt("auto* node = logos::hermes::HermesCtrAccess::raw_tiny_map(doc_, {}).get();", slot_count);

        for (const auto& field : action.fields) {
            const auto& expr = field.expr;
            // Resolve field name: prefer the rule's group, fall back to global.
            std::string field_const;
            bool found_in_group = false;
            if (!cur_rule_group_.empty()) {
                for (const auto& f : g_.fields) {
                    if (f.group == cur_rule_group_ && f.name == field.name) {
                        field_const = ast_ns_ + "::" + cur_rule_group_ + "::" + field.name;
                        found_in_group = true;
                        break;
                    }
                }
            }
            if (!found_in_group)
                field_const = ast_ns_ + "::" + field.name;

            switch (expr.kind) {

            case int32_t(ast::CAPTURE): {
                // All captures are AnyVal — token captures hold intern'd string offsets,
                // rule captures hold arena object offsets.
                size_t idx = size_t(expr.index);
                if (idx < captures.size() && !captures[idx].empty()) {
                    w.fmt("node->put({}, {}, logos::hermes::HermesCtrAccess::arena(doc_)).get();",
                          field_const, captures[idx]);
                } else {
                    w.fmt("// {} : ${}  — capture index out of range", field.name, idx);
                }
                break;
            }

            case int32_t(ast::FOLD_CAPTURE): {
                // $0 — the fold accumulator: the result of the preceding sequence item.
                if (!cur_fold_var_.empty()) {
                    w.fmt("node->put({}, {}, logos::hermes::HermesCtrAccess::arena(doc_)).get();",
                          field_const, cur_fold_var_);
                } else {
                    w.fmt("// {} : $0  — no fold context (FOLD_CAPTURE outside fold REP)", field.name);
                }
                break;
            }

            case int32_t(ast::ARRAY_CAPTURE): {
                // $... — use the rule-captures collector built during item matching.
                // rcap_VAR was declared before the items and populated by every RULE_REF.
                // TOKEN_REF captures are NOT included — they're structural punctuation.
                w.fmt("node->put({}, {}.to_anyval(), logos::hermes::HermesCtrAccess::arena(doc_)).get();",
                      field_const, rcap_var_);
                break;
            }

            case int32_t(ast::STR_LIT): {
                // Symbolic name (e.g. MAP_NODE) → NamedCode value.
                w.fmt("node->put({}, AnyVal::from_value({}::{}), logos::hermes::HermesCtrAccess::arena(doc_)).get();",
                      field_const, ast_ns_, expr.value);
                break;
            }

            case int32_t(ast::INT_LIT): {
                w.fmt("node->put({}, AnyVal::from_value(int32_t({})), logos::hermes::HermesCtrAccess::arena(doc_)).get();",
                      field_const, expr.int_val);
                break;
            }

            case int32_t(ast::BOOL_LIT): {
                w.fmt("node->put({}, AnyVal::from_value(uint8_t({})), logos::hermes::HermesCtrAccess::arena(doc_)).get();",
                      field_const, expr.int_val);
                break;
            }

            default:
                w.fmt("// TODO: action expr kind {} for field {}", expr.kind, field.name);
                break;
            }
        }
        // Emit SRC_LINE (source line number of the first token — always present).
        w.fmt("node->put({}::SRC_LINE, AnyVal::from_value(first_line_), logos::hermes::HermesCtrAccess::arena(doc_)).get();",
              ast_ns_);
        w.line("{");
        w.indent();
        w.line("AnyVal result_;");
        w.line("result_.set_pointer(node, logos::hermes::HermesCtrAccess::base(doc_));");
        if (out_cap.empty())
            w.line("return result_;");
        else
            w.fmt("{} = result_;", out_cap);
        w.dedent();
        w.line("}");
    }

    // ── Pratt ─────────────────────────────────────────────────────────────────

    void emit_pratt(CodeWriter& w) {
        w.line("// ── Pratt expression parser ───────────────────────────────────────────────");
        w.line("// Precedence table (low → high):");
        for (size_t i = 0; i < g_.prec.size(); ++i) {
            const auto& level = g_.prec[i];
            std::string assoc_str =
                (level.assoc == int32_t(ast::ASSOC_LEFT))  ? "left"  :
                (level.assoc == int32_t(ast::ASSOC_RIGHT)) ? "right" : "none";
            std::string toks;
            for (const auto& t : level.tokens) toks += t + " ";
            w.fmt("//   prec {}  {}  {}", i + 1, assoc_str, toks);
        }
        w.line();

        // Token prec lookup.
        w.fmt("static int {}_token_prec(TK_{} kind) {{", g_.name, to_upper(g_.name));
        w.indent();
        w.line("switch (kind) {");
        for (size_t i = 0; i < g_.prec.size(); ++i) {
            for (const auto& t : g_.prec[i].tokens)
                w.fmt("case TK_{}::{}: return {};", to_upper(g_.name), safe_tok_name(t), int(i + 1));
        }
        w.line("default: return 0;");
        w.line("}");
        w.dedent();
        w.line("}");
        w.line();

        // Right-associative check.
        w.fmt("static bool {}_is_right_assoc(TK_{} kind) {{", g_.name, to_upper(g_.name));
        w.indent();
        w.line("switch (kind) {");
        for (size_t i = 0; i < g_.prec.size(); ++i) {
            if (g_.prec[i].assoc == int32_t(ast::ASSOC_RIGHT)) {
                for (const auto& t : g_.prec[i].tokens)
                    w.fmt("case TK_{}::{}: return true;", to_upper(g_.name), safe_tok_name(t));
            }
        }
        w.line("default: return false;");
        w.line("}");
        w.dedent();
        w.line("}");
        w.line();

        w.fmt("AnyVal {}::pratt_expr(int min_prec) {{", parser_class_);
        w.indent();
        w.line("AnyVal left = pratt_atom();");
        w.line("if (left.is_null()) return AnyVal{};");
        w.line("while (true) {");
        w.indent();
        w.line("TK t = peek_token().kind;");
        w.fmt("int prec = {}_token_prec(t);", g_.name);
        w.line("if (prec < min_prec || prec == 0) break;");
        w.fmt("int next_min = {}_is_right_assoc(t) ? prec : prec + 1;", g_.name);
        w.line("next_token(); // consume operator");
        w.line("AnyVal right = pratt_expr(next_min);");
        w.line("// TODO: build BINARY_EXPR node for (left op right)");
        w.line("left = right; // placeholder");
        w.dedent();
        w.line("}");
        w.line("return left;");
        w.dedent();
        w.line("}");
        w.line();

        w.fmt("AnyVal {}::pratt_atom() {{", parser_class_);
        w.indent();
        w.line("// TODO: delegate to the lowest-precedence non-infix rule.");
        w.line("return AnyVal{};");
        w.dedent();
        w.line("}");
        w.line();
    }

    // ── Helpers ───────────────────────────────────────────────────────────────

    static std::string_view unquote(const std::string& s) {
        std::string_view sv = s;
        if (sv.size() >= 2 && sv.front() == '"' && sv.back() == '"')
            return sv.substr(1, sv.size() - 2);
        return sv;
    }

    static std::string to_upper(const std::string& s) {
        std::string r = s;
        for (char& c : r) c = char(std::toupper(c));
        return r;
    }

    static char escape_char(char c) { return c; }

    static std::string peg_text(const Item::Alt& alt) {
        std::string out;
        for (const auto& item : alt.seq) {
            if (!out.empty()) out += ' ';
            out += item_text(item);
        }
        if (alt.action) out += " => {...}";
        return out;
    }

    // Token names that clash with C macros or C++ keywords — suffix with _KW.
    static std::string safe_tok_name(const std::string& name) {
        static const std::unordered_set<std::string> reserved = {
            "NULL", "TRUE", "FALSE", "EOF", "OVERFLOW", "UNDERFLOW",
            "ERANGE", "EDOM", "ERRNO", "NAN", "INFINITY",
        };
        return reserved.count(name) ? name + "_KW" : name;
    }

    static std::string item_text(const Item& item) {
        switch (item.kind) {
        case int32_t(ast::TOKEN_REF):  return item.name;
        case int32_t(ast::RULE_REF):
            return item.grammar_alias.empty() ? item.name : item.grammar_alias + "::" + item.name;
        case int32_t(ast::LITERAL):    return '"' + item.name + '"';
        case int32_t(ast::OPT):
            return (!item.sub_items.empty() ? item_text(item.sub_items[0]) : "") + "?";
        case int32_t(ast::REP):
            return (!item.sub_items.empty() ? item_text(item.sub_items[0]) : "") +
                   (item.min == 0 ? "*" : "+");
        case int32_t(ast::GROUP):      return "(...)";
        case int32_t(ast::LOOKAHEAD):  return "&" + (!item.sub_items.empty() ? item_text(item.sub_items[0]) : "");
        case int32_t(ast::NEG_AHEAD):  return "!" + (!item.sub_items.empty() ? item_text(item.sub_items[0]) : "");
        default: return "?";
        }
    }
};

// ═══════════════════════════════════════════════════════════════════════════
// Public API
// ═══════════════════════════════════════════════════════════════════════════

void codegen(const std::vector<ResolvedModule>& modules, const CodegenOptions& opts) {
    fs::create_directories(opts.output_dir);

    for (const auto& mod : modules) {
        GrammarInfo g = GrammarReader::read(mod.grammar, modules);
        if (g.output.empty()) {
            std::println(stderr, "peg_gen: module '{}' has no %%meta output name — skipping",
                mod.path);
            continue;
        }

        std::println("peg_gen: generating {}.hpp / .cpp  ({})",
            g.output, mod.path);

        CodeGen cg(g, opts.output_dir);
        cg.emit_all();
    }
}

} // namespace logos::peg_gen
