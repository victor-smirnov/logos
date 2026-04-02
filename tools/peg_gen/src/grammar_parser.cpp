// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos

#include "grammar_parser.hpp"
#include "grammar_ast.hpp"

#include <charconv>
#include <fstream>
#include <format>
#include <print>
#include <stdexcept>
#include <string>

namespace logos::peg_gen {

namespace ast = logos::peg_gen::ast;
using logos::hermes::AnyVal;
using logos::hermes::HermesCtr;
using logos::hermes::HermesCtrView;
using logos::hermes::ArrayView;
using logos::hermes::MapView;
using logos::hermes::TinyMapView;
using logos::hermes::TinyMap;
using logos::hermes::Array;
using logos::hermes::Map;
using logos::hermes::String;

// ═══════════════════════════════════════════════════════════════════════════
// Lexer
// ═══════════════════════════════════════════════════════════════════════════

enum class TK {
    Eof,
    PercentIdent,   // %meta, %import, %export, %fields, %nodes, %tokens, %prec, %rules, %skip
    Ident,          // [a-zA-Z_][a-zA-Z0-9_]*
    String,         // "..."
    Regex,          // /pattern/
    Integer,        // [0-9]+

    LBrace,         // {
    RBrace,         // }
    LParen,         // (
    RParen,         // )
    Arrow,          // <-
    Slash,          // /  (alternative separator in rules)
    Question,       // ?
    Star,           // *
    Plus,           // +
    Amp,            // &
    Bang,           // !
    Comma,          // ,
    Colon,          // :
    Equals,         // =
    FatArrow,       // =>
    ColonColon,     // ::
    DollarN,        // $1, $2, ... (digits follow $)
    DollarDots,     // $...
};

struct Token {
    TK               kind;
    std::string_view text;
    size_t           line;
    size_t           col;
};

class Lexer {
public:
    Lexer(std::string_view src, std::string_view name)
        : src_(src), name_(name) {}

    // Consume and return next token.
    Token next() {
        if (la_count_ > 0) {
            Token t = la_[0];
            if (la_count_ == 2) la_[0] = la_[1];
            --la_count_;
            return t;
        }
        return lex_one();
    }

    // Look ahead without consuming. ahead=0 → next token, ahead=1 → token after next.
    Token peek(int ahead = 0) {
        while (la_count_ <= ahead)
            la_[la_count_++] = lex_one();
        return la_[ahead];
    }

    bool at_end() { return peek().kind == TK::Eof; }
    std::string_view source_name() const { return name_; }

private:
    std::string_view src_;
    std::string_view name_;
    size_t           pos_  = 0;
    size_t           line_ = 1;
    size_t           col_  = 1;
    Token            la_[2];
    int              la_count_ = 0;

    char cur(size_t off = 0) const {
        size_t p = pos_ + off;
        return p < src_.size() ? src_[p] : '\0';
    }

    char eat() {
        char c = cur();
        ++pos_;
        if (c == '\n') { ++line_; col_ = 1; } else ++col_;
        return c;
    }

    void skip_ws_and_comments() {
        while (pos_ < src_.size()) {
            char c = cur();
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
                eat();
            } else if (c == '/' && cur(1) == '/') {
                while (pos_ < src_.size() && cur() != '\n') eat();
            } else if (c == '/' && cur(1) == '*') {
                eat(); eat();
                while (pos_ < src_.size()) {
                    if (cur() == '*' && cur(1) == '/') { eat(); eat(); break; }
                    eat();
                }
            } else {
                break;
            }
        }
    }

    Token make(TK kind, size_t start, size_t ln, size_t cn) {
        return Token{kind, src_.substr(start, pos_ - start), ln, cn};
    }

    Token lex_one() {
        skip_ws_and_comments();
        if (pos_ >= src_.size()) return Token{TK::Eof, {}, line_, col_};

        size_t start = pos_, ln = line_, cn = col_;
        char c = cur();

        // ── % directives ─────────────────────────────────────────────────
        if (c == '%') {
            eat();
            while (pos_ < src_.size() && (std::isalnum(cur()) || cur() == '_')) eat();
            return make(TK::PercentIdent, start, ln, cn);
        }

        // ── Quoted string ─────────────────────────────────────────────────
        if (c == '"') {
            eat();
            while (pos_ < src_.size() && cur() != '"') {
                if (cur() == '\\') eat();
                eat();
            }
            if (pos_ < src_.size()) eat();
            return make(TK::String, start, ln, cn);
        }

        // ── Regex or slash ────────────────────────────────────────────────
        // Heuristic: if / is followed by a non-whitespace, non-*, non-/ char → regex.
        if (c == '/') {
            char nc = cur(1);
            bool looks_like_regex = nc != ' ' && nc != '\t' && nc != '\n'
                                 && nc != '*' && nc != ')' && nc != '\0' && nc != '/';
            if (looks_like_regex) {
                eat();
                while (pos_ < src_.size() && cur() != '/') {
                    if (cur() == '\\') eat();
                    eat();
                }
                if (pos_ < src_.size()) eat();
                return make(TK::Regex, start, ln, cn);
            }
            eat();
            return make(TK::Slash, start, ln, cn);
        }

        // ── $ captures ───────────────────────────────────────────────────
        if (c == '$') {
            eat();
            if (cur() == '.' && cur(1) == '.' && cur(2) == '.') {
                eat(); eat(); eat();
                return make(TK::DollarDots, start, ln, cn);
            }
            while (pos_ < src_.size() && std::isdigit(cur())) eat();
            return make(TK::DollarN, start, ln, cn);
        }

        // ── Single-char / two-char punctuation ────────────────────────────
        switch (c) {
        case '{': eat(); return make(TK::LBrace,   start, ln, cn);
        case '}': eat(); return make(TK::RBrace,   start, ln, cn);
        case '(': eat(); return make(TK::LParen,   start, ln, cn);
        case ')': eat(); return make(TK::RParen,   start, ln, cn);
        case '?': eat(); return make(TK::Question, start, ln, cn);
        case '*': eat(); return make(TK::Star,     start, ln, cn);
        case '+': eat(); return make(TK::Plus,     start, ln, cn);
        case '&': eat(); return make(TK::Amp,      start, ln, cn);
        case '!': eat(); return make(TK::Bang,     start, ln, cn);
        case ',': eat(); return make(TK::Comma,    start, ln, cn);
        case ':':
            eat();
            if (cur() == ':') { eat(); return make(TK::ColonColon, start, ln, cn); }
            return make(TK::Colon, start, ln, cn);
        case '=':
            eat();
            if (cur() == '>') { eat(); return make(TK::FatArrow, start, ln, cn); }
            return make(TK::Equals, start, ln, cn);
        case '<':
            eat();
            if (cur() == '-') { eat(); return make(TK::Arrow, start, ln, cn); }
            // bare < (type parameter angle bracket) — re-use Amp slot as placeholder
            return make(TK::Eof, start, ln, cn); // shouldn't appear in .peg
        }

        // ── Integer ───────────────────────────────────────────────────────
        if (std::isdigit(c) || (c == '-' && std::isdigit(cur(1)))) {
            if (c == '-') eat();
            while (pos_ < src_.size() && std::isdigit(cur())) eat();
            return make(TK::Integer, start, ln, cn);
        }

        // ── Identifier ────────────────────────────────────────────────────
        if (std::isalpha(c) || c == '_') {
            while (pos_ < src_.size() && (std::isalnum(cur()) || cur() == '_')) eat();
            return make(TK::Ident, start, ln, cn);
        }

        eat();
        return make(TK::Eof, start, ln, cn); // unknown character
    }
};

// ═══════════════════════════════════════════════════════════════════════════
// Parser
// ═══════════════════════════════════════════════════════════════════════════

struct ParseError { std::string message; };

class PegParser {
public:
    PegParser(std::string_view src, std::string_view name)
        : lex_(src, name)
        , doc_(logos::hermes::make_doc_multi(65536)) {}

    HermesCtr parse() {
        // Root is a string-keyed ObjectMap with named sections.
        auto root = doc_.make_object_map();

        // Pre-allocate section arrays.
        auto imports = doc_.make_array(4);
        auto exports = doc_.make_array(4);
        auto fields  = doc_.make_array(16);
        auto nodes   = doc_.make_array(16);
        auto tokens  = doc_.make_array(32);
        auto prec    = doc_.make_array(8);
        auto rules   = doc_.make_array(32);

        while (!lex_.at_end()) {
            Token t = lex_.peek();
            if (t.kind != TK::PercentIdent) error(t, "expected % directive");
            lex_.next();

            std::string_view kw = t.text;
            if      (kw == "%meta")    parse_meta(root);
            else if (kw == "%import")  imports.push_back(AnyVal::from_offset(parse_import().offset()));
            else if (kw == "%export")  parse_export(exports);
            else if (kw == "%fields")  parse_name_decls(fields);
            else if (kw == "%nodes")   parse_name_decls(nodes);
            else if (kw == "%tokens")  parse_tokens(tokens);
            else if (kw == "%prec")    parse_prec(prec);
            else if (kw == "%rules")   parse_rules(rules);
            else error(t, std::format("unknown directive '{}'", kw));
        }

        root.put("imports", AnyVal::from_offset(imports.offset()));
        root.put("exports", AnyVal::from_offset(exports.offset()));
        root.put("fields",  AnyVal::from_offset(fields.offset()));
        root.put("nodes",   AnyVal::from_offset(nodes.offset()));
        root.put("tokens",  AnyVal::from_offset(tokens.offset()));
        root.put("prec",    AnyVal::from_offset(prec.offset()));
        root.put("rules",   AnyVal::from_offset(rules.offset()));

        doc_.set_root(root);
        return std::move(doc_);
    }

private:
    Lexer    lex_;
    HermesCtr doc_;

    // ── Error helpers ─────────────────────────────────────────────────────

    [[noreturn]] void error(Token t, std::string_view msg) {
        throw ParseError{std::format("{}:{}:{}: error: {}",
            lex_.source_name(), t.line, t.col, msg)};
    }

    Token expect(TK kind, std::string_view what) {
        Token t = lex_.next();
        if (t.kind != kind)
            error(t, std::format("expected {}, got '{}'", what, t.text));
        return t;
    }

    bool try_eat(TK kind) {
        if (lex_.peek().kind == kind) { lex_.next(); return true; }
        return false;
    }

    // ── Helpers ───────────────────────────────────────────────────────────

    std::string_view unquote(std::string_view s) {
        if (s.size() >= 2 && s.front() == '"' && s.back() == '"')
            return s.substr(1, s.size() - 2);
        return s;
    }

    String make_str(std::string_view s) { return doc_.make_string(s); }

    int32_t parse_int(std::string_view text) {
        int32_t v = 0;
        std::from_chars(text.data(), text.data() + text.size(), v);
        return v;
    }

    // ── Section: %meta ────────────────────────────────────────────────────

    void parse_meta(MapView& root) {
        expect(TK::LBrace, "{");
        auto meta = doc_.make_tiny_map(8);
        meta.put(ast::CODE, AnyVal::from_value(ast::META_INFO));

        while (lex_.peek().kind == TK::Ident) {
            Token key = lex_.next();
            expect(TK::Colon, ":");
            Token val = expect(TK::String, "string value");
            auto  sv  = make_str(unquote(val.text));
            AnyVal ref = AnyVal::from_offset(sv.offset());

            std::string_view k = key.text;
            if      (k == "name")      meta.put(ast::NAME,      ref);
            else if (k == "version")   meta.put(ast::VERSION,   ref);
            else if (k == "namespace") meta.put(ast::NAMESPACE, ref);
            else if (k == "output")    meta.put(ast::OUTPUT,    ref);
            else error(key, std::format("unknown meta key '{}'", k));
        }
        expect(TK::RBrace, "}");
        root.put("meta", AnyVal::from_offset(meta.offset()));
    }

    // ── Section: %import ─────────────────────────────────────────────────

    TinyMap parse_import() {
        Token path_tok  = expect(TK::String, "import path");
        Token as_tok    = expect(TK::Ident,  "as");
        if (as_tok.text != "as") error(as_tok, "expected 'as'");
        Token alias_tok = expect(TK::Ident,  "alias");

        auto node  = doc_.make_tiny_map(4);
        auto path_s  = make_str(unquote(path_tok.text));
        auto alias_s = make_str(alias_tok.text);
        node.put(ast::CODE,  AnyVal::from_value(ast::IMPORT));
        node.put(ast::PATH,  AnyVal::from_offset(path_s.offset()));
        node.put(ast::ALIAS, AnyVal::from_offset(alias_s.offset()));
        return node;
    }

    // ── Section: %export ─────────────────────────────────────────────────

    void parse_export(ArrayView& out) {
        expect(TK::LBrace, "{");
        while (lex_.peek().kind == TK::Ident) {
            Token t = lex_.next();
            auto s = make_str(t.text);
            out.push_back(AnyVal::from_offset(s.offset()));
        }
        expect(TK::RBrace, "}");
    }

    // ── Section: %fields / %nodes ─────────────────────────────────────────

    void parse_name_decls(ArrayView& out) {
        expect(TK::LBrace, "{");
        while (lex_.peek().kind == TK::Ident) {
            Token name = lex_.next();
            expect(TK::Equals, "=");
            Token num  = expect(TK::Integer, "integer code");

            auto node = doc_.make_tiny_map(4);
            auto ns   = make_str(name.text);
            node.put(ast::CODE,  AnyVal::from_value(ast::NAME_DECL));
            node.put(ast::NAME,  AnyVal::from_offset(ns.offset()));
            node.put(ast::VALUE, AnyVal::from_value(parse_int(num.text)));
            out.push_back(AnyVal::from_offset(node.offset()));

            try_eat(TK::Comma);
        }
        expect(TK::RBrace, "}");
    }

    // ── Section: %tokens ─────────────────────────────────────────────────

    void parse_tokens(ArrayView& out) {
        expect(TK::LBrace, "{");
        while (!lex_.at_end() && lex_.peek().kind != TK::RBrace) {
            Token name = lex_.next();
            bool is_skip = (name.kind == TK::PercentIdent && name.text == "%skip");
            if (!is_skip && name.kind != TK::Ident)
                error(name, "expected token name or %skip");

            expect(TK::Equals, "=");
            Token pat = lex_.next();

            if (is_skip && pat.kind != TK::Regex)
                error(pat, "%skip requires a regex pattern");
            if (!is_skip && pat.kind != TK::String && pat.kind != TK::Regex)
                error(pat, "expected string or regex token pattern");

            ast::Code kind_code =
                is_skip              ? ast::TOKEN_SKIP    :
                pat.kind == TK::String ? ast::TOKEN_LITERAL :
                                         ast::TOKEN_REGEX;

            auto node = doc_.make_tiny_map(4);
            auto ns   = make_str(is_skip ? std::string_view("%skip") : name.text);
            auto ps   = make_str(pat.text);
            node.put(ast::CODE,    AnyVal::from_value(ast::TOKEN_DECL));
            node.put(ast::NAME,    AnyVal::from_offset(ns.offset()));
            node.put(ast::KIND,    AnyVal::from_value(kind_code));
            node.put(ast::PATTERN, AnyVal::from_offset(ps.offset()));
            out.push_back(AnyVal::from_offset(node.offset()));
        }
        expect(TK::RBrace, "}");
    }

    // ── Section: %prec ───────────────────────────────────────────────────

    void parse_prec(ArrayView& out) {
        expect(TK::LBrace, "{");
        while (!lex_.at_end() && lex_.peek().kind != TK::RBrace) {
            Token assoc_tok = expect(TK::Ident, "left, right, or none");
            ast::Code assoc_code =
                (assoc_tok.text == "left")  ? ast::ASSOC_LEFT  :
                (assoc_tok.text == "right") ? ast::ASSOC_RIGHT :
                (assoc_tok.text == "none")  ? ast::ASSOC_NONE  :
                (error(assoc_tok, std::format("expected left/right/none, got '{}'",
                                              assoc_tok.text)), ast::ASSOC_NONE);
            expect(TK::Colon, ":");

            auto toks = doc_.make_array(4);
            while (lex_.peek().kind == TK::Ident) {
                Token tok = lex_.next();
                auto  ts  = make_str(tok.text);
                toks.push_back(AnyVal::from_offset(ts.offset()));
            }

            auto node = doc_.make_tiny_map(4);
            node.put(ast::CODE,   AnyVal::from_value(ast::PREC_LEVEL));
            node.put(ast::ASSOC,  AnyVal::from_value(assoc_code));
            node.put(ast::TOKENS, AnyVal::from_offset(toks.offset()));
            out.push_back(AnyVal::from_offset(node.offset()));
        }
        expect(TK::RBrace, "}");
    }

    // ── Section: %rules ──────────────────────────────────────────────────

    void parse_rules(ArrayView& out) {
        expect(TK::LBrace, "{");
        while (!lex_.at_end() && lex_.peek().kind != TK::RBrace) {
            out.push_back(AnyVal::from_offset(parse_rule().offset()));
        }
        expect(TK::RBrace, "}");
    }

    TinyMap parse_rule() {
        Token name = expect(TK::Ident, "rule name");
        expect(TK::Arrow, "<-");

        auto alts = doc_.make_array(4);
        do {
            alts.push_back(AnyVal::from_offset(parse_alt().offset()));
        } while (try_eat(TK::Slash));

        auto node = doc_.make_tiny_map(4);
        auto ns   = make_str(name.text);
        node.put(ast::CODE, AnyVal::from_value(ast::RULE));
        node.put(ast::NAME, AnyVal::from_offset(ns.offset()));
        node.put(ast::ALTS, AnyVal::from_offset(alts.offset()));
        return node;
    }

    TinyMap parse_alt() {
        auto seq = doc_.make_array(8);

        while (!lex_.at_end()) {
            TK pk = lex_.peek().kind;
            // Stop at alternative separator, action, or end of section.
            if (pk == TK::Slash || pk == TK::FatArrow || pk == TK::RBrace) break;
            // Stop if this ident is the start of the next rule (ident followed by <-).
            if (pk == TK::Ident && lex_.peek(1).kind == TK::Arrow) break;

            auto item = parse_item();
            if (!item) break;
            seq.push_back(AnyVal::from_offset(item->offset()));
        }

        Map action{};
        bool has_action = false;
        if (try_eat(TK::FatArrow)) {
            action = parse_action();
            has_action = true;
        }

        auto node = doc_.make_tiny_map(4);
        node.put(ast::CODE, AnyVal::from_value(ast::ALT));
        node.put(ast::SEQ,  AnyVal::from_offset(seq.offset()));
        if (has_action) node.put(ast::ACTION, AnyVal::from_offset(action.offset()));
        return node;
    }

    // Returns nullopt when no item can start at current position.
    std::optional<TinyMap> parse_item() {
        bool is_la  = try_eat(TK::Amp);
        bool is_neg = !is_la && try_eat(TK::Bang);

        auto primary = parse_primary();
        if (!primary) {
            if (is_la || is_neg) error(lex_.peek(), "expected item after & or !");
            return std::nullopt;
        }

        // Suffix: ? * +
        TinyMap result = std::move(*primary);
        TK pk = lex_.peek().kind;
        if (pk == TK::Question || pk == TK::Star || pk == TK::Plus) {
            lex_.next();
            auto wrapper = doc_.make_tiny_map(6);
            ast::Code wrap_code = (pk == TK::Question) ? ast::OPT : ast::REP;
            wrapper.put(ast::CODE, AnyVal::from_value(wrap_code));
            wrapper.put(ast::ITEM, AnyVal::from_offset(result.offset()));
            if (pk == TK::Star) {
                wrapper.put(ast::MIN, AnyVal::from_value(int32_t(0)));
                wrapper.put(ast::MAX, AnyVal::from_value(int32_t(-1)));
            } else if (pk == TK::Plus) {
                wrapper.put(ast::MIN, AnyVal::from_value(int32_t(1)));
                wrapper.put(ast::MAX, AnyVal::from_value(int32_t(-1)));
            }
            result = std::move(wrapper);
        }

        if (is_la || is_neg) {
            auto wrapper = doc_.make_tiny_map(2);
            wrapper.put(ast::CODE, AnyVal::from_value(is_la ? ast::LOOKAHEAD : ast::NEG_AHEAD));
            wrapper.put(ast::ITEM, AnyVal::from_offset(result.offset()));
            result = std::move(wrapper);
        }
        return result;
    }

    std::optional<TinyMap> parse_primary() {
        Token t = lex_.peek();

        // ( alt / alt )
        if (t.kind == TK::LParen) {
            lex_.next();
            auto alts = doc_.make_array(4);
            do {
                alts.push_back(AnyVal::from_offset(parse_alt().offset()));
            } while (try_eat(TK::Slash));
            expect(TK::RParen, ")");

            auto node = doc_.make_tiny_map(2);
            node.put(ast::CODE, AnyVal::from_value(ast::GROUP));
            node.put(ast::ALTS, AnyVal::from_offset(alts.offset()));
            return node;
        }

        // "literal"
        if (t.kind == TK::String) {
            lex_.next();
            auto node = doc_.make_tiny_map(2);
            auto vs   = make_str(unquote(t.text));
            node.put(ast::CODE,  AnyVal::from_value(ast::LITERAL));
            node.put(ast::VALUE, AnyVal::from_offset(vs.offset()));
            return node;
        }

        // IDENT or alias::name
        // Key rule: if this ident is followed by <-, it's the next rule definition — stop.
        if (t.kind == TK::Ident) {
            if (lex_.peek(1).kind == TK::Arrow) return std::nullopt;
            lex_.next();

            std::string_view grammar_alias;
            std::string_view item_name = t.text;

            if (lex_.peek().kind == TK::ColonColon) {
                lex_.next(); // consume ::
                grammar_alias = t.text;
                Token rn = expect(TK::Ident, "rule or token name after ::");
                item_name = rn.text;
            }

            // Convention: UPPERCASE start → token ref, lowercase → rule ref.
            bool is_token = !item_name.empty() && std::isupper(item_name[0]);

            auto node = doc_.make_tiny_map(4);
            auto ns   = make_str(item_name);
            node.put(ast::CODE, AnyVal::from_value(is_token ? ast::TOKEN_REF : ast::RULE_REF));
            node.put(ast::NAME, AnyVal::from_offset(ns.offset()));
            if (!grammar_alias.empty()) {
                auto gs = make_str(grammar_alias);
                node.put(ast::GRAMMAR, AnyVal::from_offset(gs.offset()));
            }
            return node;
        }

        return std::nullopt;
    }

    // ── Action ────────────────────────────────────────────────────────────
    // Stored as an ObjectMap: field_name (string) → action_expr (TinyObjectMap).
    // Field names are kept as strings here; codegen resolves them to NamedCode codes.

    Map parse_action() {
        expect(TK::LBrace, "{");
        auto action = doc_.make_object_map();
        while (!lex_.at_end() && lex_.peek().kind != TK::RBrace) {
            Token field = expect(TK::Ident, "field name");
            expect(TK::Colon, ":");
            auto expr = parse_action_expr();
            action.put(field.text, AnyVal::from_offset(expr.offset()));
            try_eat(TK::Comma);
        }
        expect(TK::RBrace, "}");
        return action;
    }

    TinyMap parse_action_expr() {
        Token t = lex_.next();

        if (t.kind == TK::DollarN) {
            // $1, $2, ...  — strip the leading $
            int32_t idx = parse_int(t.text.substr(1));
            auto node = doc_.make_tiny_map(2);
            node.put(ast::CODE,  AnyVal::from_value(ast::CAPTURE));
            node.put(ast::INDEX, AnyVal::from_value(idx));
            return node;
        }
        if (t.kind == TK::DollarDots) {
            auto node = doc_.make_tiny_map(1);
            node.put(ast::CODE, AnyVal::from_value(ast::ARRAY_CAPTURE));
            return node;
        }
        if (t.kind == TK::Ident) {
            if (t.text == "true" || t.text == "false") {
                auto node = doc_.make_tiny_map(2);
                node.put(ast::CODE,  AnyVal::from_value(ast::BOOL_LIT));
                node.put(ast::VALUE, AnyVal::from_value(uint8_t(t.text == "true" ? 1 : 0)));
                return node;
            }
            // Symbolic name (e.g. MAP_NODE) — stored as a STR_LIT; codegen resolves it.
            auto node = doc_.make_tiny_map(2);
            auto vs   = make_str(t.text);
            node.put(ast::CODE,  AnyVal::from_value(ast::STR_LIT));
            node.put(ast::VALUE, AnyVal::from_offset(vs.offset()));
            return node;
        }
        if (t.kind == TK::Integer) {
            auto node = doc_.make_tiny_map(2);
            node.put(ast::CODE,  AnyVal::from_value(ast::INT_LIT));
            node.put(ast::VALUE, AnyVal::from_value(parse_int(t.text)));
            return node;
        }
        if (t.kind == TK::String) {
            auto node = doc_.make_tiny_map(2);
            auto vs   = make_str(unquote(t.text));
            node.put(ast::CODE,  AnyVal::from_value(ast::STR_LIT));
            node.put(ast::VALUE, AnyVal::from_offset(vs.offset()));
            return node;
        }
        error(t, std::format("unexpected token in action expression: '{}'", t.text));
    }
};

// ═══════════════════════════════════════════════════════════════════════════
// Public API
// ═══════════════════════════════════════════════════════════════════════════

std::optional<HermesCtr>
parse_grammar_string(std::string_view source, std::string_view source_name) {
    try {
        PegParser p(source, source_name);
        return p.parse();
    } catch (const ParseError& e) {
        std::println(stderr, "{}", e.message);
        return std::nullopt;
    }
}

std::optional<HermesCtr> parse_grammar(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        std::println(stderr, "peg_gen: cannot open '{}'", path);
        return std::nullopt;
    }
    std::string src(std::istreambuf_iterator<char>(f), {});
    return parse_grammar_string(src, path);
}

} // namespace logos::peg_gen
