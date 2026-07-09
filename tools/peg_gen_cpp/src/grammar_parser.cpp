// Logos project — https://github.com/victor-smirnov/logos

#include "grammar_parser.hpp"
#include "grammar_ast.hpp"

#include <charconv>
#include <fstream>
#include <format>
#include <print>
#include <stdexcept>
#include <string>

namespace ast = logos::peg_gen::ast;

namespace logos::peg_gen {
using logos::writ::AnyVal;
using logos::writ::Writ;
using logos::writ::WritView;
using logos::writ::ArrayView;
using logos::writ::MapView;
using logos::writ::TinyMapView;
using TinyMap = logos::writ::TinyMapView;   // view handle (raw node + holder)
using Array   = logos::writ::ObjectArray;
using Map     = logos::writ::MapView;
using String  = logos::writ::StringView;

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
        // Decimal, or `0x`-prefixed hex. Both admit `_` digit separators so a
        // %schema type code can be written exactly as its ADR-0011 `schema`
        // item spells it (e.g. code(0x0010_0000_0000_0004)).
        if (std::isdigit(c) || (c == '-' && std::isdigit(cur(1)))) {
            if (c == '-') eat();
            if (cur() == '0' && (cur(1) == 'x' || cur(1) == 'X')) {
                eat(); eat();
                while (pos_ < src_.size() &&
                       (std::isxdigit(cur()) || cur() == '_')) eat();
            } else {
                while (pos_ < src_.size() &&
                       (std::isdigit(cur()) || cur() == '_')) eat();
            }
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
        // GrowableSingleChunk: a single contiguous buffer that doubles on
        // overflow. Required because peg_gen's parsed-grammar IR contains
        // RelativePtr fields (TinyObjectMap::data_) which can't span chunks
        // — MultiChunk mode produced wild pointers once the IR grew past
        // the initial chunk (sign-truncated cross-chunk delta yielded
        // bogus `vals` addresses; tripped silently as a SIGSEGV in
        // TinyObjectMap::put for grammars that pushed it over the
        // initial-chunk boundary, e.g. anything large enough to need
        // pub_struct_def to grow).
        , doc_(logos::writ::make_doc(524288).get()) {}

    Writ parse() {
        // Root is a string-keyed ObjectMap with named sections.
        auto root = doc_.make_object_map().get();

        // Pre-allocate section arrays.
        auto imports = doc_.make_array(4).get();
        auto exports = doc_.make_array(4).get();
        auto fields  = doc_.make_array(16).get();
        auto nodes   = doc_.make_array(16).get();
        auto tokens  = doc_.make_array(32).get();
        auto prec    = doc_.make_array(8).get();
        auto rules   = doc_.make_array(32).get();
        auto schema  = doc_.make_array(16).get();

        while (!lex_.at_end()) {
            Token t = lex_.peek();
            if (t.kind != TK::PercentIdent) error(t, "expected % directive");
            lex_.next();

            std::string_view kw = t.text;
            if      (kw == "%meta")    parse_meta(root);
            else if (kw == "%import")  imports.push_back(parse_import().to_anyval()).get();
            else if (kw == "%export")  parse_export(exports);
            else if (kw == "%fields")  parse_name_decls(fields);
            else if (kw == "%nodes")   parse_name_decls(nodes);
            else if (kw == "%tokens")  parse_tokens(tokens);
            else if (kw == "%prec")    parse_prec(prec);
            else if (kw == "%rules")   parse_rules(rules);
            else if (kw == "%schema")  parse_schema(root, schema);
            else error(t, std::format("unknown directive '{}'", kw));
        }

        root.put("imports", imports.to_anyval()).get();
        root.put("exports", exports.to_anyval()).get();
        root.put("fields",  fields.to_anyval()).get();
        root.put("nodes",   nodes.to_anyval()).get();
        root.put("tokens",  tokens.to_anyval()).get();
        root.put("prec",    prec.to_anyval()).get();
        root.put("rules",   rules.to_anyval()).get();
        // Empty unless a %schema block was seen — its non-emptiness IS the
        // schema-mode switch for codegen (mirrors peg_gen_logos's cg.schema_mode).
        root.put("schema",  schema.to_anyval()).get();

        doc_.set_root(root.to_anyval());
        return std::move(doc_);
    }

private:
    Lexer    lex_;
    Writ doc_;

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

    logos::writ::StringView make_str(std::string_view s) { return doc_.make_string(s).get(); }
    TinyMap make_tm(uint64_t cap) {
        return TinyMap(doc_.make_tiny_map(cap).get(), doc_.holder());
    }

    // Strip `_` separators and detect a `0x` prefix. Returns the digit body and
    // its base; the leading `-` (if any) is left on the caller's plate.
    static std::pair<std::string, int> num_body(std::string_view text) {
        std::string s;
        s.reserve(text.size());
        for (char ch : text) if (ch != '_') s += ch;
        bool neg = !s.empty() && s[0] == '-';
        std::string_view v = s;
        if (neg) v.remove_prefix(1);
        int base = 10;
        if (v.size() > 2 && v[0] == '0' && (v[1] == 'x' || v[1] == 'X')) {
            base = 16;
            v.remove_prefix(2);
        }
        return {std::string(neg ? "-" : "") + std::string(v), base};
    }

    int32_t parse_int(std::string_view text) {
        auto [body, base] = num_body(text);
        int32_t v = 0;
        std::from_chars(body.data(), body.data() + body.size(), v, base);
        return v;
    }

    uint64_t parse_u64(std::string_view text) {
        auto [body, base] = num_body(text);
        uint64_t v = 0;
        std::from_chars(body.data(), body.data() + body.size(), v, base);
        return v;
    }

    // ── Section: %meta ────────────────────────────────────────────────────

    void parse_meta(MapView& root) {
        expect(TK::LBrace, "{");
        auto meta = make_tm(8);
        meta.put(ast::CODE, AnyVal::from_value(ast::META_INFO)).get();

        while (lex_.peek().kind == TK::Ident) {
            Token key = lex_.next();
            expect(TK::Colon, ":");
            Token val = expect(TK::String, "string value");
            auto  sv  = make_str(unquote(val.text));
            AnyVal ref = sv.to_anyval();

            std::string_view k = key.text;
            if      (k == "name")      meta.put(ast::NAME,      ref).get();
            else if (k == "version")   meta.put(ast::VERSION,   ref).get();
            else if (k == "namespace") meta.put(ast::NAMESPACE, ref).get();
            else if (k == "output")    meta.put(ast::OUTPUT,    ref).get();
            // %schema-dialect meta keys (mirrors peg_gen_logos): `package` names
            // the emitted module, `prefix` disambiguates module-global emitted
            // names so two generated parsers can coexist in one module.
            else if (k == "package")   meta.put(ast::PACKAGE,   ref).get();
            else if (k == "prefix")    meta.put(ast::GPREFIX,   ref).get();
            else error(key, std::format("unknown meta key '{}'", k));
        }
        expect(TK::RBrace, "}");
        root.put("meta", meta.to_anyval()).get();
    }

    // ── Section: %import ─────────────────────────────────────────────────

    TinyMap parse_import() {
        Token path_tok  = expect(TK::String, "import path");
        Token as_tok    = expect(TK::Ident,  "as");
        if (as_tok.text != "as") error(as_tok, "expected 'as'");
        Token alias_tok = expect(TK::Ident,  "alias");

        auto node  = make_tm(4);
        auto path_s  = make_str(unquote(path_tok.text));
        auto alias_s = make_str(alias_tok.text);
        node.put(ast::CODE,  AnyVal::from_value(ast::IMPORT)).get();
        node.put(ast::PATH,  path_s.to_anyval()).get();
        node.put(ast::ALIAS, alias_s.to_anyval()).get();
        return node;
    }

    // ── Section: %export ─────────────────────────────────────────────────

    void parse_export(ArrayView& out) {
        expect(TK::LBrace, "{");
        while (lex_.peek().kind == TK::Ident) {
            Token t = lex_.next();
            auto s = make_str(t.text);
            out.push_back(s.to_anyval()).get();
        }
        expect(TK::RBrace, "}");
    }

    // ── Section: %schema ──────────────────────────────────────────────────
    //
    // The typed-Writ dialect (ADR 0011 schemas). Presence of this block puts
    // the whole grammar in schema mode: actions name a QUOTED schema type in
    // CODE and address that type's declared fields by name, instead of the
    // numeric %fields/%nodes constants.
    //
    //   %schema {
    //       use:      "logos.std.wql.plan"     // repeatable
    //       arena:    external                 // bare ident: external | internal
    //       ref_wrap: "WRef"                   // Logos backend only (see below)
    //
    //       SBin { op: "i32" = 0, lhs: "ref SExpr" = 1, rhs: "ref SExpr" = 2 }
    //   }
    //
    // The explicit `= KEY` is what makes ONE dialect serve both backends. The
    // Logos backend emits `node.lhs = …` and lets logosc resolve field→key
    // against the real `schema` item; C++ has no such second pass and must bake
    // the TOM key in. Parsed as OPTIONAL here so the Logos-only grammars keep
    // parsing; the C++ schema codegen rejects a keyless field with a precise
    // diagnostic at the point the key is actually needed.
    //
    // `ref_wrap` names the Logos edge-wrapper type (default "WRef"); it has no
    // C++ runtime counterpart — a `ref T` field lowers to a plain ref AnyVal.
    void parse_schema(MapView& root, ArrayView& out) {
        expect(TK::LBrace, "{");
        std::string uses;
        while (lex_.peek().kind == TK::Ident) {
            Token head = lex_.next();

            // Header key vs node decl. Dispatch on the NAME, not on a following
            // colon — a node decl may carry one too: `SBin : code(0x…) { … }`.
            std::string_view hk = head.text;
            if (hk == "use" || hk == "arena" || hk == "ref_wrap") {
                expect(TK::Colon, ":");
                if (hk == "use") {
                    Token v = expect(TK::String, "module path string");
                    if (!uses.empty()) uses += ' ';
                    uses += unquote(v.text);
                } else if (hk == "arena") {
                    Token v = expect(TK::Ident, "`external` or `internal`");
                    if (v.text == "external") {
                        root.put("schema_arena_ext", AnyVal::from_value(true)).get();
                    } else if (v.text != "internal") {
                        error(v, std::format("arena must be `external` or "
                                             "`internal`, found '{}'", v.text));
                    }
                } else {
                    Token v = expect(TK::String, "ref-wrapper type name");
                    auto s = make_str(unquote(v.text));
                    root.put("schema_ref_wrap", s.to_anyval()).get();
                }
                try_eat(TK::Comma);
                continue;
            }

            // Node decl: `Name [: code(0x…)] { field: "type" [= KEY], … }`
            // The optional `code(...)` is the ADR-0011 type code, spelled exactly
            // as in the mirrored `schema` item. Only the C++ backend needs it
            // (to stamp set_schema_type_code); the Logos backend gets it from
            // `doc.make::<S>()`, so it stays optional at parse time.
            bool     has_type_code = false;
            uint64_t type_code = 0;
            if (try_eat(TK::Colon)) {
                Token kw = expect(TK::Ident, "`code`");
                if (kw.text != "code")
                    error(kw, std::format("expected `code(...)` after schema node "
                                          "name, found '{}'", kw.text));
                expect(TK::LParen, "(");
                Token num = expect(TK::Integer, "type code");
                type_code = parse_u64(num.text);
                has_type_code = true;
                expect(TK::RParen, ")");
            }

            expect(TK::LBrace, "{");
            auto fields_arr = doc_.make_array(8).get();
            while (lex_.peek().kind == TK::Ident) {
                Token fname = lex_.next();
                expect(TK::Colon, ":");
                Token fty = expect(TK::String, "field-type string");

                auto f   = make_tm(4);
                auto fns = make_str(fname.text);
                auto fts = make_str(unquote(fty.text));
                f.put(ast::CODE,  AnyVal::from_value(ast::SCHEMA_FIELD)).get();
                f.put(ast::NAME,  fns.to_anyval()).get();
                f.put(ast::FTYPE, fts.to_anyval()).get();
                if (try_eat(TK::Equals)) {
                    Token num = expect(TK::Integer, "TOM key");
                    f.put(ast::FKEY,
                          AnyVal::from_value(parse_int(num.text))).get();
                }
                fields_arr.push_back(f.to_anyval()).get();
                try_eat(TK::Comma);
            }
            expect(TK::RBrace, "}");

            auto node = make_tm(4);
            auto ns   = make_str(head.text);
            node.put(ast::CODE,   AnyVal::from_value(ast::SCHEMA_DECL)).get();
            node.put(ast::NAME,   ns.to_anyval()).get();
            node.put(ast::FIELDS, fields_arr.to_anyval()).get();
            if (has_type_code)
                node.put(ast::TYPE_CODE, AnyVal::from_value(type_code)).get();
            out.push_back(node.to_anyval()).get();
            try_eat(TK::Comma);
        }
        expect(TK::RBrace, "}");

        if (!uses.empty()) {
            auto us = make_str(uses);
            root.put("schema_use", us.to_anyval()).get();
        }
    }

    // ── Section: %fields / %nodes ─────────────────────────────────────────

    void parse_name_decls(ArrayView& out) {
        expect(TK::LBrace, "{");
        while (lex_.peek().kind == TK::Ident) {
            Token name = lex_.next();

            // `group X { NAME = N, ... }` — fields scoped to a named group.
            if (name.text == "group") {
                Token gname = expect(TK::Ident, "group name");
                auto fields_arr = doc_.make_array(8).get();
                parse_name_decls(fields_arr);  // recursive — inner block
                auto node = make_tm(4);
                auto ns   = make_str(gname.text);
                node.put(ast::CODE,   AnyVal::from_value(ast::GROUP_DECL)).get();
                node.put(ast::NAME,   ns.to_anyval()).get();
                node.put(ast::FIELDS, fields_arr.to_anyval()).get();
                out.push_back(node.to_anyval()).get();
                try_eat(TK::Comma);
                continue;
            }

            expect(TK::Equals, "=");
            Token num  = expect(TK::Integer, "integer code");

            auto node = make_tm(4);
            auto ns   = make_str(name.text);
            node.put(ast::CODE,  AnyVal::from_value(ast::NAME_DECL)).get();
            node.put(ast::NAME,  ns.to_anyval()).get();
            node.put(ast::VALUE, AnyVal::from_value(parse_int(num.text))).get();
            out.push_back(node.to_anyval()).get();

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

            auto node = make_tm(4);
            auto ns   = make_str(is_skip ? std::string_view("%skip") : name.text);
            auto ps   = make_str(pat.text);
            node.put(ast::CODE,    AnyVal::from_value(ast::TOKEN_DECL)).get();
            node.put(ast::NAME,    ns.to_anyval()).get();
            node.put(ast::KIND,    AnyVal::from_value(kind_code)).get();
            node.put(ast::PATTERN, ps.to_anyval()).get();
            out.push_back(node.to_anyval()).get();
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

            auto toks = doc_.make_array(4).get();
            while (lex_.peek().kind == TK::Ident) {
                Token tok = lex_.next();
                auto  ts  = make_str(tok.text);
                toks.push_back(ts.to_anyval()).get();
            }

            auto node = make_tm(4);
            node.put(ast::CODE,   AnyVal::from_value(ast::PREC_LEVEL)).get();
            node.put(ast::ASSOC,  AnyVal::from_value(assoc_code)).get();
            node.put(ast::TOKENS, toks.to_anyval()).get();
            out.push_back(node.to_anyval()).get();
        }
        expect(TK::RBrace, "}");
    }

    // ── Section: %rules ──────────────────────────────────────────────────

    void parse_rules(ArrayView& out) {
        expect(TK::LBrace, "{");
        while (!lex_.at_end() && lex_.peek().kind != TK::RBrace) {
            out.push_back(parse_rule().to_anyval()).get();
        }
        expect(TK::RBrace, "}");
    }

    TinyMap parse_rule() {
        Token name = expect(TK::Ident, "rule name");

        // Optional group tag: `rule_name :group X: <- …`.  Field names in this
        // rule's action blocks resolve first to the named group, then global.
        std::string group_name;
        if (lex_.peek().kind == TK::Colon) {
            lex_.next();  // :
            Token gk = expect(TK::Ident, "group keyword");
            if (gk.text != "group")
                error(gk, "expected 'group' after ':' in rule tag");
            Token gn = expect(TK::Ident, "group name");
            group_name = std::string(gn.text);
            expect(TK::Colon, ":");
        }
        expect(TK::Arrow, "<-");

        auto alts = doc_.make_array(4).get();
        do {
            alts.push_back(parse_alt().to_anyval()).get();
        } while (try_eat(TK::Slash));

        auto node = make_tm(4);
        auto ns   = make_str(name.text);
        node.put(ast::CODE, AnyVal::from_value(ast::RULE)).get();
        node.put(ast::NAME, ns.to_anyval()).get();
        node.put(ast::ALTS, alts.to_anyval()).get();
        if (!group_name.empty()) {
            auto gs = make_str(group_name);
            node.put(ast::GROUP_NAME, gs.to_anyval()).get();
        }
        return node;
    }

    TinyMap parse_alt() {
        auto seq = doc_.make_array(8).get();

        while (!lex_.at_end()) {
            TK pk = lex_.peek().kind;
            // Stop at alternative separator, action, or end of section.
            if (pk == TK::Slash || pk == TK::FatArrow || pk == TK::RBrace) break;
            // Stop if this ident is the start of the next rule (ident followed by <- or :group tag).
            if (pk == TK::Ident && (lex_.peek(1).kind == TK::Arrow ||
                                    lex_.peek(1).kind == TK::Colon)) break;

            auto item = parse_item();
            if (!item) break;
            seq.push_back(item->to_anyval()).get();
        }

        Map action{};
        bool has_action = false;
        if (try_eat(TK::FatArrow)) {
            action = parse_action();
            has_action = true;
        }

        auto node = make_tm(4);
        node.put(ast::CODE, AnyVal::from_value(ast::ALT)).get();
        node.put(ast::SEQ,  seq.to_anyval()).get();
        if (has_action) node.put(ast::ACTION, action.to_anyval()).get();
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
            auto wrapper = make_tm(6);
            ast::Code wrap_code = (pk == TK::Question) ? ast::OPT : ast::REP;
            wrapper.put(ast::CODE, AnyVal::from_value(wrap_code)).get();
            wrapper.put(ast::ITEM, result.to_anyval()).get();
            if (pk == TK::Star) {
                wrapper.put(ast::MIN, AnyVal::from_value(int32_t(0))).get();
                wrapper.put(ast::MAX, AnyVal::from_value(int32_t(-1))).get();
            } else if (pk == TK::Plus) {
                wrapper.put(ast::MIN, AnyVal::from_value(int32_t(1))).get();
                wrapper.put(ast::MAX, AnyVal::from_value(int32_t(-1))).get();
            }
            result = std::move(wrapper);
        }

        if (is_la || is_neg) {
            auto wrapper = make_tm(2);
            wrapper.put(ast::CODE, AnyVal::from_value(is_la ? ast::LOOKAHEAD : ast::NEG_AHEAD)).get();
            wrapper.put(ast::ITEM, result.to_anyval()).get();
            result = std::move(wrapper);
        }
        return result;
    }

    std::optional<TinyMap> parse_primary() {
        Token t = lex_.peek();

        // ( alt / alt )
        if (t.kind == TK::LParen) {
            lex_.next();
            auto alts = doc_.make_array(4).get();
            do {
                alts.push_back(parse_alt().to_anyval()).get();
            } while (try_eat(TK::Slash));
            expect(TK::RParen, ")");

            auto node = make_tm(2);
            node.put(ast::CODE, AnyVal::from_value(ast::GROUP)).get();
            node.put(ast::ALTS, alts.to_anyval()).get();
            return node;
        }

        // "literal"
        if (t.kind == TK::String) {
            lex_.next();
            auto node = make_tm(2);
            auto vs   = make_str(unquote(t.text));
            node.put(ast::CODE,  AnyVal::from_value(ast::LITERAL)).get();
            node.put(ast::VALUE, vs.to_anyval()).get();
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

            auto node = make_tm(4);
            auto ns   = make_str(item_name);
            node.put(ast::CODE, AnyVal::from_value(is_token ? ast::TOKEN_REF : ast::RULE_REF)).get();
            node.put(ast::NAME, ns.to_anyval()).get();
            if (!grammar_alias.empty()) {
                auto gs = make_str(grammar_alias);
                node.put(ast::GRAMMAR, gs.to_anyval()).get();
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
        auto action = doc_.make_object_map().get();
        while (!lex_.at_end() && lex_.peek().kind != TK::RBrace) {
            Token field = expect(TK::Ident, "field name");
            expect(TK::Colon, ":");
            auto expr = parse_action_expr();
            action.put(field.text, expr.to_anyval()).get();
            try_eat(TK::Comma);
        }
        expect(TK::RBrace, "}");
        return action;
    }

    TinyMap parse_action_expr() {
        Token t = lex_.next();

        if (t.kind == TK::DollarN) {
            // $0 → fold accumulator (FOLD_CAPTURE); $1, $2, ... → positional CAPTURE
            int32_t idx = parse_int(t.text.substr(1));
            if (idx == 0) {
                auto node = make_tm(1);
                node.put(ast::CODE, AnyVal::from_value(ast::FOLD_CAPTURE)).get();
                return node;
            }
            auto node = make_tm(2);
            node.put(ast::CODE,  AnyVal::from_value(ast::CAPTURE)).get();
            node.put(ast::INDEX, AnyVal::from_value(idx)).get();
            return node;
        }
        if (t.kind == TK::DollarDots) {
            auto node = make_tm(1);
            node.put(ast::CODE, AnyVal::from_value(ast::ARRAY_CAPTURE)).get();
            return node;
        }
        if (t.kind == TK::Ident) {
            if (t.text == "true" || t.text == "false") {
                auto node = make_tm(2);
                node.put(ast::CODE,  AnyVal::from_value(ast::BOOL_LIT)).get();
                node.put(ast::VALUE, AnyVal::from_value(uint8_t(t.text == "true" ? 1 : 0))).get();
                return node;
            }
            // Symbolic name (e.g. MAP_NODE) — stored as a STR_LIT; codegen resolves it.
            auto node = make_tm(2);
            auto vs   = make_str(t.text);
            node.put(ast::CODE,  AnyVal::from_value(ast::STR_LIT)).get();
            node.put(ast::VALUE, vs.to_anyval()).get();
            return node;
        }
        if (t.kind == TK::Integer) {
            auto node = make_tm(2);
            node.put(ast::CODE,  AnyVal::from_value(ast::INT_LIT)).get();
            node.put(ast::VALUE, AnyVal::from_value(parse_int(t.text))).get();
            return node;
        }
        if (t.kind == TK::String) {
            auto node = make_tm(2);
            auto vs   = make_str(unquote(t.text));
            node.put(ast::CODE,  AnyVal::from_value(ast::STR_LIT)).get();
            node.put(ast::VALUE, vs.to_anyval()).get();
            return node;
        }
        error(t, std::format("unexpected token in action expression: '{}'", t.text));
    }
};

// ═══════════════════════════════════════════════════════════════════════════
// Public API
// ═══════════════════════════════════════════════════════════════════════════

std::optional<Writ>
parse_grammar_string(std::string_view source, std::string_view source_name) {
    try {
        PegParser p(source, source_name);
        return p.parse();
    } catch (const ParseError& e) {
        std::println(stderr, "{}", e.message);
        return std::nullopt;
    }
}

std::optional<Writ> parse_grammar(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        std::println(stderr, "peg_gen: cannot open '{}'", path);
        return std::nullopt;
    }
    std::string src(std::istreambuf_iterator<char>(f), {});
    return parse_grammar_string(src, path);
}

} // namespace logos::peg_gen
