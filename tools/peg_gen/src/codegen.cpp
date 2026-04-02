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

struct NameDecl  { std::string name; int32_t code; };
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
            out.push_back({
                read_str(node.get(uint8_t(ast::NAME)),  h),
                read_int(node.get(uint8_t(ast::VALUE)))
            });
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
    int                lc_ = 0;  // label counter — reset per rule, always increasing

    // Returns a unique label suffix string within the current rule.
    std::string fresh() { return std::to_string(lc_++); }

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
            w.line("// Field keys (TinyObjectMap slot indices)");
            for (const auto& f : g_.fields)
                w.fmt("inline constexpr Key  {:20s} {{\"{}\", {}}};", f.name, f.name, f.code);
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
            w.line("struct Token { TK kind; std::string_view text; };");
            w.line("Token lex_one();");
            w.line("Token next_token();");
            w.line("Token peek_token();");
            w.line("bool  try_token(TK kind);");
            w.line("Token expect_token(TK kind, std::string_view what);");
        }

        w.line();
        w.line("logos::hermes::HermesCtr doc_;");
        w.line("std::string_view         source_;");
        w.line("size_t                   pos_ = 0;");
        if (!g_.tokens.empty()) {
            w.line("Token                    la_{};");
            w.line("bool                     have_la_ = false;");
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
        w.line("if (have_la_) { have_la_ = false; return la_; }");
        w.line("return lex_one();");
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
        w.line("if (peek_token().kind == kind) { next_token(); return true; }");
        w.line("return false;");
        w.dedent();
        w.line("}");
        w.line();

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
        w.line("restart:");
        w.line("while (pos_ < source_.size()) {");
        w.indent();
        w.line("char c = source_[pos_];");
        // Common whitespace
        bool has_ws_skip = false;
        for (const auto& t : g_.tokens) {
            if (t.kind == skip_code && (t.pattern == "/[ \\t\\n\\r]+/" ||
                t.pattern == "/[ \t\n\r]+/")) {
                has_ws_skip = true; break;
            }
        }
        if (has_ws_skip) {
            w.line("if (c == ' ' || c == '\\t' || c == '\\n' || c == '\\r') { ++pos_; continue; }");
        }
        // Line comment skip: //[^\n]*
        for (const auto& t : g_.tokens) {
            if (t.kind == skip_code && t.pattern.find("//") != std::string::npos) {
                w.line("if (c == '/' && pos_+1 < source_.size() && source_[pos_+1] == '/') {");
                w.line("    while (pos_ < source_.size() && source_[pos_] != '\\n') ++pos_;");
                w.line("    continue; }");
                break;
            }
        }
        // Block comment: /* ... */
        for (const auto& t : g_.tokens) {
            if (t.kind == skip_code && t.pattern.find("/*") != std::string::npos) {
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
        w.line("if (pos_ >= source_.size()) return {TK::Eof, {}};");
        w.line("size_t start = pos_;");
        w.line("char   c     = source_[pos_];");
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
                    w.fmt("if (c == '{}') {{ ++pos_; return {{TK::{}, source_.substr(start, 1)}}; }}",
                          escape_char(pat[0]), safe_tok_name(t->name));
                } else {
                    w.fmt("if (source_.substr(pos_, {0}).size() == {0} &&", pat.size());
                    w.fmt("    source_.substr(pos_, {}) == \"{}\") {{", pat.size(), pat);
                    w.fmt("    pos_ += {}; return {{TK::{}, source_.substr(start, {})}}; }}",
                          pat.size(), safe_tok_name(t->name), pat.size());
                }
            }
            w.line();
        }

        // Well-known regex patterns — generated as hand-coded matchers.
        emit_regex_tokens(w);

        w.line("++pos_; // unknown character — skip");
        w.line("goto restart;");
        w.dedent();
        w.line("}");
        w.line();
    }

    void emit_regex_tokens(CodeWriter& w) {
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
                w.fmt("return {{TK::{}, source_.substr(start, pos_ - start)}};", safe_tok_name(t.name));
                w.dedent();
                w.line("}");
            }
            // INTEGER: [0-9]+  or  [-]?[0-9]+
            else if (pat.find("[0-9]+") != std::string::npos && pat.find('.') == std::string::npos) {
                w.fmt("// {} = /{}/", t.name, pat);
                w.fmt("if (std::isdigit(c) || (c == '-' && pos_+1 < source_.size() && std::isdigit(source_[pos_+1]))) {{");
                w.indent();
                w.line("if (c == '-') ++pos_;");
                w.line("while (pos_ < source_.size() && std::isdigit(source_[pos_])) ++pos_;");
                w.fmt("return {{TK::{}, source_.substr(start, pos_ - start)}};", safe_tok_name(t.name));
                w.dedent();
                w.line("}");
            }
            // FLOAT: pattern contains '.' and [0-9]
            else if (pat.find('.') != std::string::npos && pat.find("[0-9]") != std::string::npos) {
                w.fmt("// {} = /{}/", t.name, pat);
                w.line("// (FLOAT matched by INTEGER handler if needed — order tokens carefully)");
            }
            // STRING: \"([^\"\\\\]|\\\\.)*\"
            else if (pat.find('"') != std::string::npos) {
                w.fmt("// {} = /{}/", t.name, pat);
                w.fmt("if (c == '\"') {{");
                w.indent();
                w.line("++pos_;");
                w.line("while (pos_ < source_.size() && source_[pos_] != '\"') {");
                w.line("    if (source_[pos_] == '\\\\') ++pos_;");
                w.line("    ++pos_;");
                w.line("}");
                w.line("if (pos_ < source_.size()) ++pos_;");
                w.fmt("return {{TK::{}, source_.substr(start, pos_ - start)}};", safe_tok_name(t.name));
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
            w.line("doc_ = logos::hermes::make_doc();");
            w.fmt("AnyVal root = rule_{}();", e);
            w.fmt("LOGOS_ASSERT(!root.is_null(), \"{}-PARSE-001\", \"parse_{}: expected {}\");",
                  to_upper(g_.name), e, e);
            w.line("doc_.set_root_offset(root.to_offset());");
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
        w.fmt("AnyVal {}::rule_{}() {{", parser_class_, rule.name);
        w.indent();
        w.line("size_t saved_pos;");
        w.line("bool   saved_la;");
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
        w.line("saved_pos = pos_;");
        w.line("saved_la  = have_la_;");
        w.line("Token saved_tok_ = la_;");
        w.line();
        // Inner block: all captures and node pointers are scoped here.
        // The backtrack label below is OUTSIDE this block so gotos don't cross inits.
        w.line("{");
        w.indent();

        // Backtrack label for this alternative — used as fail_label for all items.
        std::string alt_fail = std::format("bt_{}_{}", rule.name, idx);

        // Capture slots (one per item in the sequence).
        // We number them from 1 ($1, $2, ...) to match grammar action syntax.
        // All captures have type AnyVal — tokens are interned as arena strings,
        // rule results are stored as arena object offsets.
        std::vector<std::string> captures(alt.seq.size() + 1); // 1-indexed
        for (size_t i = 0; i < alt.seq.size(); ++i) {
            std::string cap = std::format("cap{}", i + 1);
            captures[i + 1] = cap;
            emit_item_match(w, alt.seq[i], cap, alt_fail, i);
        }

        // Action: build AST node.
        if (alt.action) {
            emit_action(w, *alt.action, captures);
        } else if (alt.seq.size() == 1) {
            // No action + single item → pass through.
            w.fmt("return {};", captures[1]);
        } else {
            w.line("return AnyVal{}; // TODO: add => { ... } action for multi-item alt");
        }

        w.dedent();
        w.line("}"); // end inner block
        // Backtrack label is here, AFTER the inner block — no initialization is crossed.
        w.fmt("bt_{}_{}:", rule.name, idx);
        w.line("pos_      = saved_pos;");
        w.line("have_la_  = saved_la;");
        w.line("la_       = saved_tok_;");
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
            // Match token, intern text as arena string → AnyVal offset.
            w.fmt("if (peek_token().kind != TK::{}) goto {};",
                  safe_tok_name(item.name), fail_label);
            w.fmt("Token tok_{0}_ = next_token();", cap);
            w.fmt("AnyVal {0} = AnyVal::from_offset(doc_.make_string(tok_{0}_.text).offset());", cap);
            break;
        }

        case int32_t(ast::RULE_REF): {
            std::string call;
            if (item.grammar_alias.empty())
                call = std::format("rule_{}()", item.name);
            else
                call = std::format("{0}_.rule_{1}()", item.grammar_alias, item.name);
            w.fmt("AnyVal {} = {};", cap, call);
            w.fmt("if ({}.is_null()) goto {};", cap, fail_label);
            break;
        }

        case int32_t(ast::LITERAL): {
            // Match literal string, intern as arena string → AnyVal offset.
            size_t n = item.name.size();
            w.fmt("if (pos_ + {0} > source_.size() || source_.substr(pos_, {0}) != \"{1}\") goto {2};",
                  n, item.name, fail_label);
            w.fmt("AnyVal {0} = AnyVal::from_offset(doc_.make_string(source_.substr(pos_, {1})).offset());", cap, n);
            w.fmt("pos_ += {};", n);
            break;
        }

        case int32_t(ast::OPT): {
            // Optional: try sub-item, silently ignore failure. cap = null AnyVal if no match.
            std::string id       = fresh();
            std::string done_lbl = "opt_done_" + id;
            std::string fail_lbl = "opt_fail_" + id;
            w.fmt("AnyVal {} = AnyVal{{}};", cap);
            w.line("{");
            w.indent();
            w.line("size_t opt_pos_ = pos_; bool opt_la_ = have_la_; Token opt_tok_ = la_;");
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
            w.line("pos_ = opt_pos_; have_la_ = opt_la_; la_ = opt_tok_;");
            w.fmt("{}: ;", done_lbl);
            w.dedent();
            w.line("}");
            break;
        }

        case int32_t(ast::REP): {
            // Repetition: collect sub-items into array. cap = AnyVal pointing to array.
            std::string id       = fresh();
            std::string arr_var  = "arr_" + id;
            std::string fail_lbl = "rep_fail_" + id;
            w.fmt("auto {} = doc_.make_array(4);", arr_var);
            w.line("{");
            w.indent();
            w.line("while (true) {");
            w.indent();
            w.line("size_t rep_pos_ = pos_; bool rep_la_ = have_la_; Token rep_tok_ = la_;");
            if (!item.sub_items.empty()) {
                w.line("{"); // inner scope for sub-item
                w.indent();
                std::string sub_cap = "rep_item_" + id;
                emit_item_match(w, item.sub_items[0], sub_cap, fail_lbl, 0);
                w.fmt("if (!{}.is_null()) {}.push_back({});", sub_cap, arr_var, sub_cap);
                w.line("continue;");
                w.dedent();
                w.line("}");
            }
            w.fmt("{}: ;", fail_lbl);
            w.line("pos_ = rep_pos_; have_la_ = rep_la_; la_ = rep_tok_;");
            w.line("break;");
            w.dedent();
            w.line("}");
            w.dedent();
            w.line("}");
            w.fmt("AnyVal {} = AnyVal::from_offset({}.offset());", cap, arr_var);
            if (item.min > 0)
                w.fmt("if ({}.size() < {}) goto {};", arr_var, item.min, fail_label);
            break;
        }

        case int32_t(ast::GROUP): {
            // Inline ordered choice: try each alt, use first that succeeds.
            // Each alt: outer scope holds position save; inner scope holds item captures.
            // All labels use unique IDs from fresh() to avoid collisions.
            std::string grp_id   = fresh();
            std::string done_lbl = "grp_done_" + grp_id;
            w.fmt("AnyVal {} = AnyVal{{}};", cap);
            w.line("{");
            w.indent();
            w.line("size_t grp_pos_; bool grp_la_; Token grp_tok_;");
            for (size_t gi = 0; gi < item.sub_alts.size(); ++gi) {
                const auto& sa = item.sub_alts[gi];
                std::string alt_fail = "grp_fail_" + grp_id + "_" + std::to_string(gi);
                w.fmt("// Group alt {}", gi + 1);
                w.line("{");
                w.indent();
                w.line("grp_pos_ = pos_; grp_la_ = have_la_; grp_tok_ = la_;");
                w.line("{"); // inner scope: item captures
                w.indent();
                std::vector<std::string> sub_caps;
                for (size_t si = 0; si < sa.seq.size(); ++si) {
                    std::string sc = std::format("{}_gi{}_s{}", cap, gi, si);
                    sub_caps.push_back(sc);
                    emit_item_match(w, sa.seq[si], sc, alt_fail, si);
                }
                if (sub_caps.size() == 1)
                    w.fmt("{} = {};", cap, sub_caps[0]);
                else
                    w.fmt("{} = AnyVal{{}};  // multi-item group alt", cap);
                w.fmt("goto {};", done_lbl);
                w.dedent();
                w.line("}"); // end inner scope
                w.fmt("{}: ;", alt_fail);
                w.line("pos_ = grp_pos_; have_la_ = grp_la_; la_ = grp_tok_;");
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
            w.line("size_t la_pos_ = pos_; bool la_la_ = have_la_; Token la_tok_ = la_;");
            w.line("{");
            w.indent();
            if (!item.sub_items.empty()) {
                std::string sub_cap = "la_" + id;
                emit_item_match(w, item.sub_items[0], sub_cap, fail_lbl, 0);
            }
            w.line("pos_ = la_pos_; have_la_ = la_la_; la_ = la_tok_;");
            w.fmt("AnyVal {} = AnyVal{{}};  // lookahead result (position restored)", cap);
            w.fmt("goto {};", end_lbl);
            w.dedent();
            w.line("}");
            w.fmt("{}: ;", fail_lbl);
            w.line("pos_ = la_pos_; have_la_ = la_la_; la_ = la_tok_;");
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
            w.line("size_t na_pos_ = pos_; bool na_la_ = have_la_; Token na_tok_ = la_;");
            w.line("{");
            w.indent();
            if (!item.sub_items.empty()) {
                std::string sub_cap = "na_" + id;
                emit_item_match(w, item.sub_items[0], sub_cap, ok_lbl, 0);
            }
            // Sub-item matched → negation fails.
            w.line("pos_ = na_pos_; have_la_ = na_la_; la_ = na_tok_;");
            w.fmt("goto {};", fail_label);
            w.dedent();
            w.line("}");
            w.fmt("{}: ;", fail_lbl);
            w.fmt("{}: ;", ok_lbl);
            w.line("pos_ = na_pos_; have_la_ = na_la_; la_ = na_tok_;");
            w.fmt("AnyVal {} = AnyVal{{}};  // negative lookahead succeeded", cap);
            w.dedent();
            w.line("}");
            break;
        }

        default:
            w.fmt("AnyVal {} = AnyVal{{}}; // TODO: item kind {}", cap, item.kind);
            break;
        }
    }

    // ── Action ───────────────────────────────────────────────────────────────

    void emit_action(CodeWriter& w, const Action& action,
                     const std::vector<std::string>& captures) {
        int slot_count = int(action.fields.size()) + 1; // +1 for CODE
        w.fmt("auto* node = doc_.raw_tiny_map({});", slot_count);

        for (const auto& field : action.fields) {
            const auto& expr = field.expr;
            std::string field_const = ast_ns_ + "::" + field.name;

            switch (expr.kind) {

            case int32_t(ast::CAPTURE): {
                // All captures are AnyVal — token captures hold intern'd string offsets,
                // rule captures hold arena object offsets.
                size_t idx = size_t(expr.index);
                if (idx < captures.size() && !captures[idx].empty()) {
                    w.fmt("node->put({}, {}, doc_.arena());",
                          field_const, captures[idx]);
                } else {
                    w.fmt("// {} : ${}  — capture index out of range", field.name, idx);
                }
                break;
            }

            case int32_t(ast::ARRAY_CAPTURE): {
                // $... — collect all non-null AnyVal captures into an array.
                w.fmt("// {} : $...", field.name);
                w.line("{");
                w.indent();
                w.fmt("auto items_arr = doc_.make_array({});", captures.size());
                for (size_t i = 1; i < captures.size(); ++i) {
                    if (!captures[i].empty()) {
                        w.fmt("if (!{0}.is_null()) items_arr.push_back({0});", captures[i]);
                    }
                }
                w.fmt("node->put({}, AnyVal::from_offset(items_arr.offset()), doc_.arena());",
                      field_const);
                w.dedent();
                w.line("}");
                break;
            }

            case int32_t(ast::STR_LIT): {
                // Symbolic name (e.g. MAP_NODE) → NamedCode value.
                w.fmt("node->put({}, AnyVal::from_value({}::{}), doc_.arena());",
                      field_const, ast_ns_, expr.value);
                break;
            }

            case int32_t(ast::INT_LIT): {
                w.fmt("node->put({}, AnyVal::from_value(int32_t({})), doc_.arena());",
                      field_const, expr.int_val);
                break;
            }

            case int32_t(ast::BOOL_LIT): {
                w.fmt("node->put({}, AnyVal::from_value(uint8_t({})), doc_.arena());",
                      field_const, expr.int_val);
                break;
            }

            default:
                w.fmt("// TODO: action expr kind {} for field {}", expr.kind, field.name);
                break;
            }
        }
        // Emit CODE field (the node type discriminant — always the last field so it
        // doesn't shift earlier slot indices, but we write it after all value fields).
        // (already handled per-field above via STR_LIT/INT_LIT for the CODE field)
        w.line("{");
        w.indent();
        w.line("AnyVal result_;");
        w.line("result_.set_pointer(node, doc_.base());");
        w.line("return result_;");
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
