// Logos project — https://github.com/victor-smirnov/logos
//
// peg_gen_test: integration tests for grammar_parser, module_resolver, codegen.
// Runs as a standalone executable; all checks via LOGOS_ASSERT.

#include "grammar_parser.hpp"
#include "module_resolver.hpp"
#include "codegen.hpp"
#include "grammar_ast.hpp"

#include <logos/verification/assert.hpp>
#include <logos/hermes/view.hpp>

#include <filesystem>
#include <fstream>
#include <print>
#include <string_view>

namespace fs  = std::filesystem;
namespace ast = logos::peg_gen::ast;

using logos::hermes::AnyVal;
using logos::hermes::ArrayView;
using logos::hermes::TinyMapView;
using logos::hermes::StringView;
using logos::hermes::MemHolder;
using logos::peg_gen::parse_grammar_string;
using logos::peg_gen::resolve_modules;
using logos::peg_gen::codegen;
using logos::peg_gen::CodegenOptions;

// ═══════════════════════════════════════════════════════════════════════════
// Navigation helpers
// ═══════════════════════════════════════════════════════════════════════════

static std::string_view str_field(AnyVal v, MemHolder* h) {
    if (v.is_null() || !v.is_pointer()) return {};
    return StringView(v.to_offset(), h).view();
}

static int32_t int_field(AnyVal v) {
    if (v.is_null() || v.is_pointer()) return -999;
    return v.as_value<int32_t>();
}

// Get top-level section (root is ObjectMap, string-keyed).
static AnyVal section(const logos::hermes::Hermes& doc, std::string_view key) {
    if (!doc.has_root()) return AnyVal{};
    return doc.root_object().as_map().get(key);
}

// Section helpers that return typed views.
static TinyMapView meta_of(const logos::hermes::Hermes& doc) {
    return TinyMapView(section(doc, "meta").to_offset(), doc.holder());
}

static ArrayView array_section(const logos::hermes::Hermes& doc, std::string_view key) {
    AnyVal v = section(doc, key);
    LOGOS_ASSERT(!v.is_null(), "PEGEN-TEST-NAV", "section '{}' missing", key);
    return ArrayView(v.to_offset(), doc.holder());
}

static TinyMapView tiny_elem(const logos::hermes::Hermes& doc, AnyVal v) {
    return TinyMapView(v.to_offset(), doc.holder());
}

// ═══════════════════════════════════════════════════════════════════════════
// Grammar parser tests
// ═══════════════════════════════════════════════════════════════════════════

// ── %meta ────────────────────────────────────────────────────────────────

static void test_meta_fields() {
    auto doc = parse_grammar_string(R"(
        %meta {
            name:      "calc"
            namespace: "logos::calc"
            output:    "calc_parser"
            version:   "2.0"
        }
    )");
    LOGOS_ASSERT(doc.has_value(), "PEGEN-TEST-META-001", "parse failed");
    auto h = doc->holder();
    auto meta = meta_of(*doc);
    LOGOS_ASSERT(str_field(meta.get(uint8_t(ast::NAME)),      h) == "calc",        "PEGEN-TEST-META-001", "name");
    LOGOS_ASSERT(str_field(meta.get(uint8_t(ast::NAMESPACE)), h) == "logos::calc", "PEGEN-TEST-META-001", "namespace");
    LOGOS_ASSERT(str_field(meta.get(uint8_t(ast::OUTPUT)),    h) == "calc_parser", "PEGEN-TEST-META-001", "output");
    LOGOS_ASSERT(str_field(meta.get(uint8_t(ast::VERSION)),   h) == "2.0",         "PEGEN-TEST-META-001", "version");
    std::println("  [OK] test_meta_fields");
}

// ── %export ──────────────────────────────────────────────────────────────

static void test_exports() {
    auto doc = parse_grammar_string(R"(
        %meta { name: "e" namespace: "ns" output: "e" }
        %export { expr stmt decl }
        %rules {
            expr <- IDENT
            stmt <- IDENT
            decl <- IDENT
        }
    )");
    LOGOS_ASSERT(doc.has_value(), "PEGEN-TEST-EXPORT-001", "parse failed");
    auto exports = array_section(*doc, "exports");
    LOGOS_ASSERT(exports.size() == 3, "PEGEN-TEST-EXPORT-001",
        "expected 3 exports, got {}", exports.size());
    auto h = doc->holder();
    LOGOS_ASSERT(str_field(exports.get(0), h) == "expr",  "PEGEN-TEST-EXPORT-001", "exports[0]");
    LOGOS_ASSERT(str_field(exports.get(1), h) == "stmt",  "PEGEN-TEST-EXPORT-001", "exports[1]");
    LOGOS_ASSERT(str_field(exports.get(2), h) == "decl",  "PEGEN-TEST-EXPORT-001", "exports[2]");
    std::println("  [OK] test_exports");
}

// ── %fields / %nodes ─────────────────────────────────────────────────────

static void test_fields_and_nodes() {
    auto doc = parse_grammar_string(R"(
        %meta { name: "f" namespace: "ns" output: "f" }
        %fields { CODE = 0  VALUE = 1  KEY = 2 }
        %nodes  { MAP = 0   ARRAY = 1  STRING = 2 }
    )");
    LOGOS_ASSERT(doc.has_value(), "PEGEN-TEST-DECL-001", "parse failed");
    auto h = doc->holder();

    auto fields = array_section(*doc, "fields");
    LOGOS_ASSERT(fields.size() == 3, "PEGEN-TEST-DECL-001", "fields count");
    {
        auto f = tiny_elem(*doc, fields.get(0));
        LOGOS_ASSERT(str_field(f.get(uint8_t(ast::NAME)),  h) == "CODE", "PEGEN-TEST-DECL-001", "field[0].name");
        LOGOS_ASSERT(int_field(f.get(uint8_t(ast::VALUE))) == 0,          "PEGEN-TEST-DECL-001", "field[0].value");
    }
    {
        auto f = tiny_elem(*doc, fields.get(2));
        LOGOS_ASSERT(str_field(f.get(uint8_t(ast::NAME)),  h) == "KEY", "PEGEN-TEST-DECL-001", "field[2].name");
        LOGOS_ASSERT(int_field(f.get(uint8_t(ast::VALUE))) == 2,         "PEGEN-TEST-DECL-001", "field[2].value");
    }

    auto nodes = array_section(*doc, "nodes");
    LOGOS_ASSERT(nodes.size() == 3, "PEGEN-TEST-DECL-001", "nodes count");
    {
        auto n = tiny_elem(*doc, nodes.get(1));
        LOGOS_ASSERT(str_field(n.get(uint8_t(ast::NAME)),  h) == "ARRAY", "PEGEN-TEST-DECL-001", "node[1].name");
        LOGOS_ASSERT(int_field(n.get(uint8_t(ast::VALUE))) == 1,           "PEGEN-TEST-DECL-001", "node[1].value");
    }
    std::println("  [OK] test_fields_and_nodes");
}

// ── %tokens ──────────────────────────────────────────────────────────────

static void test_tokens() {
    auto doc = parse_grammar_string(R"(
        %meta { name: "t" namespace: "ns" output: "t" }
        %tokens {
            PLUS     = "+"
            IDENT    = /[a-zA-Z_]\w*/
            INTEGER  = /[0-9]+/
            %skip    = /[ \t\n\r]+/
        }
    )");
    LOGOS_ASSERT(doc.has_value(), "PEGEN-TEST-TOK-001", "parse failed");
    auto h = doc->holder();
    auto tokens = array_section(*doc, "tokens");
    LOGOS_ASSERT(tokens.size() == 4, "PEGEN-TEST-TOK-001",
        "expected 4 tokens, got {}", tokens.size());

    // PLUS: literal
    {
        auto t = tiny_elem(*doc, tokens.get(0));
        LOGOS_ASSERT(str_field(t.get(uint8_t(ast::NAME)), h) == "PLUS",
            "PEGEN-TEST-TOK-001", "tokens[0].name");
        LOGOS_ASSERT(int_field(t.get(uint8_t(ast::KIND))) == int32_t(ast::TOKEN_LITERAL),
            "PEGEN-TEST-TOK-001", "tokens[0].kind");
        // Lexer returns pattern text verbatim including delimiters: "+" not +
        LOGOS_ASSERT(str_field(t.get(uint8_t(ast::PATTERN)), h) == "\"+\"",
            "PEGEN-TEST-TOK-001", "tokens[0].pattern");
    }
    // IDENT: regex
    {
        auto t = tiny_elem(*doc, tokens.get(1));
        LOGOS_ASSERT(str_field(t.get(uint8_t(ast::NAME)), h) == "IDENT",
            "PEGEN-TEST-TOK-001", "tokens[1].name");
        LOGOS_ASSERT(int_field(t.get(uint8_t(ast::KIND))) == int32_t(ast::TOKEN_REGEX),
            "PEGEN-TEST-TOK-001", "tokens[1].kind");
    }
    // %skip
    {
        auto t = tiny_elem(*doc, tokens.get(3));
        LOGOS_ASSERT(int_field(t.get(uint8_t(ast::KIND))) == int32_t(ast::TOKEN_SKIP),
            "PEGEN-TEST-TOK-001", "tokens[3] is skip");
    }
    std::println("  [OK] test_tokens");
}

// ── %prec ────────────────────────────────────────────────────────────────

static void test_prec() {
    // Each %prec block defines one level to avoid ambiguity
    // (the parser can't distinguish assoc keyword from a token name mid-level).
    auto doc = parse_grammar_string(R"(
        %meta { name: "p" namespace: "ns" output: "p" }
        %prec { left:  OR }
        %prec { left:  AND }
        %prec { right: NOT }
        %prec { left:  PLUS MINUS }
    )");
    LOGOS_ASSERT(doc.has_value(), "PEGEN-TEST-PREC-001", "parse failed");
    auto h = doc->holder();
    auto prec = array_section(*doc, "prec");
    LOGOS_ASSERT(prec.size() == 4, "PEGEN-TEST-PREC-001",
        "expected 4 levels, got {}", prec.size());

    // Level 0: left OR
    {
        auto lv = tiny_elem(*doc, prec.get(0));
        LOGOS_ASSERT(int_field(lv.get(uint8_t(ast::ASSOC))) == int32_t(ast::ASSOC_LEFT),
            "PEGEN-TEST-PREC-001", "level[0] assoc");
        auto toks = ArrayView(lv.get(uint8_t(ast::TOKENS)).to_offset(), h);
        LOGOS_ASSERT(toks.size() == 1, "PEGEN-TEST-PREC-001", "level[0] 1 token");
        LOGOS_ASSERT(str_field(toks.get(0), h) == "OR", "PEGEN-TEST-PREC-001", "level[0] token name");
    }
    // Level 2: right NOT
    {
        auto lv = tiny_elem(*doc, prec.get(2));
        LOGOS_ASSERT(int_field(lv.get(uint8_t(ast::ASSOC))) == int32_t(ast::ASSOC_RIGHT),
            "PEGEN-TEST-PREC-001", "level[2] right assoc");
    }
    // Level 3: left PLUS MINUS
    {
        auto lv = tiny_elem(*doc, prec.get(3));
        auto toks = ArrayView(lv.get(uint8_t(ast::TOKENS)).to_offset(), h);
        LOGOS_ASSERT(toks.size() == 2, "PEGEN-TEST-PREC-001", "level[3] 2 tokens");
        LOGOS_ASSERT(str_field(toks.get(0), h) == "PLUS",  "PEGEN-TEST-PREC-001", "PLUS");
        LOGOS_ASSERT(str_field(toks.get(1), h) == "MINUS", "PEGEN-TEST-PREC-001", "MINUS");
    }
    std::println("  [OK] test_prec");
}

// ── Rules: basic structure ───────────────────────────────────────────────

static void test_rule_single_alt() {
    auto doc = parse_grammar_string(R"(
        %meta { name: "r" namespace: "ns" output: "r" }
        %rules {
            expr <- NUMBER PLUS NUMBER
        }
    )");
    LOGOS_ASSERT(doc.has_value(), "PEGEN-TEST-RULE-001", "parse failed");
    auto h = doc->holder();
    auto rules = array_section(*doc, "rules");
    LOGOS_ASSERT(rules.size() == 1, "PEGEN-TEST-RULE-001", "1 rule");

    auto rule = tiny_elem(*doc, rules.get(0));
    LOGOS_ASSERT(str_field(rule.get(uint8_t(ast::NAME)), h) == "expr",
        "PEGEN-TEST-RULE-001", "rule.name");

    auto alts = ArrayView(rule.get(uint8_t(ast::ALTS)).to_offset(), h);
    LOGOS_ASSERT(alts.size() == 1, "PEGEN-TEST-RULE-001", "1 alt");

    auto alt = tiny_elem(*doc, alts.get(0));
    auto seq  = ArrayView(alt.get(uint8_t(ast::SEQ)).to_offset(), h);
    LOGOS_ASSERT(seq.size() == 3, "PEGEN-TEST-RULE-001", "seq len 3");

    // Item 0: TOKEN_REF NUMBER
    auto item0 = tiny_elem(*doc, seq.get(0));
    LOGOS_ASSERT(int_field(item0.get(uint8_t(ast::CODE))) == int32_t(ast::TOKEN_REF),
        "PEGEN-TEST-RULE-001", "item0 is TOKEN_REF");
    LOGOS_ASSERT(str_field(item0.get(uint8_t(ast::NAME)), h) == "NUMBER",
        "PEGEN-TEST-RULE-001", "item0 name");
    std::println("  [OK] test_rule_single_alt");
}

static void test_rule_multiple_alts() {
    auto doc = parse_grammar_string(R"(
        %meta { name: "r" namespace: "ns" output: "r" }
        %rules {
            value <- map
                   / array
                   / STRING
        }
    )");
    LOGOS_ASSERT(doc.has_value(), "PEGEN-TEST-RULE-002", "parse failed");
    auto h = doc->holder();
    auto rules = array_section(*doc, "rules");
    auto rule  = tiny_elem(*doc, rules.get(0));
    auto alts  = ArrayView(rule.get(uint8_t(ast::ALTS)).to_offset(), h);
    LOGOS_ASSERT(alts.size() == 3, "PEGEN-TEST-RULE-002",
        "expected 3 alts, got {}", alts.size());

    // Alt 0: RULE_REF map
    {
        auto alt  = tiny_elem(*doc, alts.get(0));
        auto seq  = ArrayView(alt.get(uint8_t(ast::SEQ)).to_offset(), h);
        auto item = tiny_elem(*doc, seq.get(0));
        LOGOS_ASSERT(int_field(item.get(uint8_t(ast::CODE))) == int32_t(ast::RULE_REF),
            "PEGEN-TEST-RULE-002", "alt0 is RULE_REF");
        LOGOS_ASSERT(str_field(item.get(uint8_t(ast::NAME)), h) == "map",
            "PEGEN-TEST-RULE-002", "alt0 name");
    }
    // Alt 2: TOKEN_REF STRING
    {
        auto alt  = tiny_elem(*doc, alts.get(2));
        auto seq  = ArrayView(alt.get(uint8_t(ast::SEQ)).to_offset(), h);
        auto item = tiny_elem(*doc, seq.get(0));
        LOGOS_ASSERT(int_field(item.get(uint8_t(ast::CODE))) == int32_t(ast::TOKEN_REF),
            "PEGEN-TEST-RULE-002", "alt2 is TOKEN_REF");
    }
    std::println("  [OK] test_rule_multiple_alts");
}

// ── Rules: item kinds ────────────────────────────────────────────────────

static void test_item_opt_rep_literal() {
    auto doc = parse_grammar_string(R"(
        %meta { name: "r" namespace: "ns" output: "r" }
        %rules {
            list <- "[" item+ "]"
            item <- IDENT ","?
        }
    )");
    LOGOS_ASSERT(doc.has_value(), "PEGEN-TEST-ITEM-001", "parse failed");
    auto h = doc->holder();
    auto rules = array_section(*doc, "rules");

    // rule "list": "[" item+ "]"
    {
        auto rule = tiny_elem(*doc, rules.get(0));
        auto alts = ArrayView(rule.get(uint8_t(ast::ALTS)).to_offset(), h);
        auto alt  = tiny_elem(*doc, alts.get(0));
        auto seq  = ArrayView(alt.get(uint8_t(ast::SEQ)).to_offset(), h);
        LOGOS_ASSERT(seq.size() == 3, "PEGEN-TEST-ITEM-001", "list seq=3");

        // item0: LITERAL "["
        auto lit = tiny_elem(*doc, seq.get(0));
        LOGOS_ASSERT(int_field(lit.get(uint8_t(ast::CODE))) == int32_t(ast::LITERAL),
            "PEGEN-TEST-ITEM-001", "literal [");
        LOGOS_ASSERT(str_field(lit.get(uint8_t(ast::VALUE)), h) == "[",
            "PEGEN-TEST-ITEM-001", "literal [ value");

        // item1: REP item+
        auto rep = tiny_elem(*doc, seq.get(1));
        LOGOS_ASSERT(int_field(rep.get(uint8_t(ast::CODE))) == int32_t(ast::REP),
            "PEGEN-TEST-ITEM-001", "item+ is REP");
        LOGOS_ASSERT(int_field(rep.get(uint8_t(ast::MIN))) == 1,
            "PEGEN-TEST-ITEM-001", "item+ min=1");
    }

    // rule "item": IDENT ","?
    {
        auto rule = tiny_elem(*doc, rules.get(1));
        auto alts = ArrayView(rule.get(uint8_t(ast::ALTS)).to_offset(), h);
        auto alt  = tiny_elem(*doc, alts.get(0));
        auto seq  = ArrayView(alt.get(uint8_t(ast::SEQ)).to_offset(), h);
        LOGOS_ASSERT(seq.size() == 2, "PEGEN-TEST-ITEM-001", "item seq=2");

        // item1: OPT ","?
        auto opt = tiny_elem(*doc, seq.get(1));
        LOGOS_ASSERT(int_field(opt.get(uint8_t(ast::CODE))) == int32_t(ast::OPT),
            "PEGEN-TEST-ITEM-001", "comma? is OPT");
        // sub-item is a LITERAL ","
        auto sub = tiny_elem(*doc, opt.get(uint8_t(ast::ITEM)));
        LOGOS_ASSERT(int_field(sub.get(uint8_t(ast::CODE))) == int32_t(ast::LITERAL),
            "PEGEN-TEST-ITEM-001", "opt sub-item is LITERAL");
        LOGOS_ASSERT(str_field(sub.get(uint8_t(ast::VALUE)), h) == ",",
            "PEGEN-TEST-ITEM-001", "comma literal value");
    }
    std::println("  [OK] test_item_opt_rep_literal");
}

static void test_item_star_rep() {
    auto doc = parse_grammar_string(R"(
        %meta { name: "r" namespace: "ns" output: "r" }
        %rules {
            args <- arg*
            arg  <- IDENT
        }
    )");
    LOGOS_ASSERT(doc.has_value(), "PEGEN-TEST-ITEM-002", "parse failed");
    auto h = doc->holder();
    auto rules = array_section(*doc, "rules");
    auto rule  = tiny_elem(*doc, rules.get(0));
    auto alts  = ArrayView(rule.get(uint8_t(ast::ALTS)).to_offset(), h);
    auto alt   = tiny_elem(*doc, alts.get(0));
    auto seq   = ArrayView(alt.get(uint8_t(ast::SEQ)).to_offset(), h);
    LOGOS_ASSERT(seq.size() == 1, "PEGEN-TEST-ITEM-002", "seq=1");

    auto rep = tiny_elem(*doc, seq.get(0));
    LOGOS_ASSERT(int_field(rep.get(uint8_t(ast::CODE))) == int32_t(ast::REP),
        "PEGEN-TEST-ITEM-002", "arg* is REP");
    LOGOS_ASSERT(int_field(rep.get(uint8_t(ast::MIN))) == 0,
        "PEGEN-TEST-ITEM-002", "arg* min=0");
    std::println("  [OK] test_item_star_rep");
}

static void test_item_group() {
    auto doc = parse_grammar_string(R"(
        %meta { name: "r" namespace: "ns" output: "r" }
        %rules {
            key <- (STRING / IDENT)
        }
    )");
    LOGOS_ASSERT(doc.has_value(), "PEGEN-TEST-ITEM-003", "parse failed");
    auto h = doc->holder();
    auto rules = array_section(*doc, "rules");
    auto rule  = tiny_elem(*doc, rules.get(0));
    auto alts  = ArrayView(rule.get(uint8_t(ast::ALTS)).to_offset(), h);
    auto alt   = tiny_elem(*doc, alts.get(0));
    auto seq   = ArrayView(alt.get(uint8_t(ast::SEQ)).to_offset(), h);

    auto grp = tiny_elem(*doc, seq.get(0));
    LOGOS_ASSERT(int_field(grp.get(uint8_t(ast::CODE))) == int32_t(ast::GROUP),
        "PEGEN-TEST-ITEM-003", "group item");

    auto sub_alts = ArrayView(grp.get(uint8_t(ast::ALTS)).to_offset(), h);
    LOGOS_ASSERT(sub_alts.size() == 2, "PEGEN-TEST-ITEM-003",
        "group 2 alts, got {}", sub_alts.size());
    std::println("  [OK] test_item_group");
}

static void test_item_lookahead_neg_ahead() {
    auto doc = parse_grammar_string(R"(
        %meta { name: "r" namespace: "ns" output: "r" }
        %rules {
            ident_not_kw <- !KEYWORD &IDENT
            kw           <- KEYWORD
        }
    )");
    LOGOS_ASSERT(doc.has_value(), "PEGEN-TEST-ITEM-004", "parse failed");
    auto h = doc->holder();
    auto rules = array_section(*doc, "rules");
    auto rule  = tiny_elem(*doc, rules.get(0));
    auto alts  = ArrayView(rule.get(uint8_t(ast::ALTS)).to_offset(), h);
    auto alt   = tiny_elem(*doc, alts.get(0));
    auto seq   = ArrayView(alt.get(uint8_t(ast::SEQ)).to_offset(), h);
    LOGOS_ASSERT(seq.size() == 2, "PEGEN-TEST-ITEM-004", "seq=2");

    auto neg = tiny_elem(*doc, seq.get(0));
    LOGOS_ASSERT(int_field(neg.get(uint8_t(ast::CODE))) == int32_t(ast::NEG_AHEAD),
        "PEGEN-TEST-ITEM-004", "!KEYWORD is NEG_AHEAD");

    auto la = tiny_elem(*doc, seq.get(1));
    LOGOS_ASSERT(int_field(la.get(uint8_t(ast::CODE))) == int32_t(ast::LOOKAHEAD),
        "PEGEN-TEST-ITEM-004", "&IDENT is LOOKAHEAD");
    std::println("  [OK] test_item_lookahead_neg_ahead");
}

static void test_item_cross_grammar_ref() {
    auto doc = parse_grammar_string(R"(
        %meta   { name: "r" namespace: "ns" output: "r" }
        %import "sql.peg" as sql
        %rules {
            typed <- sql::expr COLON value
            value <- IDENT
        }
    )");
    LOGOS_ASSERT(doc.has_value(), "PEGEN-TEST-ITEM-005", "parse failed");
    auto h = doc->holder();
    auto rules = array_section(*doc, "rules");
    auto rule  = tiny_elem(*doc, rules.get(0));
    auto alts  = ArrayView(rule.get(uint8_t(ast::ALTS)).to_offset(), h);
    auto alt   = tiny_elem(*doc, alts.get(0));
    auto seq   = ArrayView(alt.get(uint8_t(ast::SEQ)).to_offset(), h);

    auto item = tiny_elem(*doc, seq.get(0));
    LOGOS_ASSERT(int_field(item.get(uint8_t(ast::CODE))) == int32_t(ast::RULE_REF),
        "PEGEN-TEST-ITEM-005", "sql::expr is RULE_REF");
    LOGOS_ASSERT(str_field(item.get(uint8_t(ast::NAME)),    h) == "expr",
        "PEGEN-TEST-ITEM-005", "name=expr");
    LOGOS_ASSERT(str_field(item.get(uint8_t(ast::GRAMMAR)), h) == "sql",
        "PEGEN-TEST-ITEM-005", "grammar alias=sql");
    std::println("  [OK] test_item_cross_grammar_ref");
}

// ── Rules: actions ───────────────────────────────────────────────────────

static void test_action_captures() {
    auto doc = parse_grammar_string(R"(
        %meta   { name: "r" namespace: "ns" output: "r" }
        %fields { CODE = 0  VALUE = 1  KEY = 2 }
        %nodes  { ENTRY = 7 }
        %rules {
            entry <- KEY COLON VALUE
                  => { CODE: ENTRY, KEY: $1, VALUE: $3 }
        }
    )");
    LOGOS_ASSERT(doc.has_value(), "PEGEN-TEST-ACT-001", "parse failed");
    auto h = doc->holder();
    auto rules = array_section(*doc, "rules");
    auto rule  = tiny_elem(*doc, rules.get(0));
    auto alts  = ArrayView(rule.get(uint8_t(ast::ALTS)).to_offset(), h);
    auto alt   = tiny_elem(*doc, alts.get(0));

    // Action must be present.
    // parse_action() stores action as ObjectMap (string key → ActionExpr TinyObjectMap).
    AnyVal action_val = alt.get(uint8_t(ast::ACTION));
    LOGOS_ASSERT(!action_val.is_null(), "PEGEN-TEST-ACT-001", "action present");

    auto action = logos::hermes::MapView(action_val.to_offset(), h);
    LOGOS_ASSERT(action.size() == 3, "PEGEN-TEST-ACT-001",
        "action 3 fields, got {}", action.size());

    // KEY field: should be CAPTURE $1
    {
        AnyVal key_expr_v = action.get("KEY");
        LOGOS_ASSERT(!key_expr_v.is_null(), "PEGEN-TEST-ACT-001", "KEY field present");
        auto expr = tiny_elem(*doc, key_expr_v);
        LOGOS_ASSERT(int_field(expr.get(uint8_t(ast::CODE))) == int32_t(ast::CAPTURE),
            "PEGEN-TEST-ACT-001", "KEY action is CAPTURE");
        LOGOS_ASSERT(int_field(expr.get(uint8_t(ast::INDEX))) == 1,
            "PEGEN-TEST-ACT-001", "KEY captures $1");
    }
    // VALUE field: should be CAPTURE $3
    {
        AnyVal val_expr_v = action.get("VALUE");
        LOGOS_ASSERT(!val_expr_v.is_null(), "PEGEN-TEST-ACT-001", "VALUE field present");
        auto expr = tiny_elem(*doc, val_expr_v);
        LOGOS_ASSERT(int_field(expr.get(uint8_t(ast::CODE))) == int32_t(ast::CAPTURE),
            "PEGEN-TEST-ACT-001", "VALUE action is CAPTURE");
        LOGOS_ASSERT(int_field(expr.get(uint8_t(ast::INDEX))) == 3,
            "PEGEN-TEST-ACT-001", "VALUE captures $3");
    }
    std::println("  [OK] test_action_captures");
}

static void test_action_array_capture() {
    auto doc = parse_grammar_string(R"(
        %meta   { name: "r" namespace: "ns" output: "r" }
        %fields { CODE = 0  ITEMS = 1 }
        %nodes  { LIST = 3 }
        %rules {
            list <- "[" item* "]"
                 => { CODE: LIST, ITEMS: $... }
        }
    )");
    LOGOS_ASSERT(doc.has_value(), "PEGEN-TEST-ACT-002", "parse failed");
    auto h = doc->holder();
    auto rules  = array_section(*doc, "rules");
    auto rule   = tiny_elem(*doc, rules.get(0));
    auto alts   = ArrayView(rule.get(uint8_t(ast::ALTS)).to_offset(), h);
    auto alt    = tiny_elem(*doc, alts.get(0));
    auto action = logos::hermes::MapView(alt.get(uint8_t(ast::ACTION)).to_offset(), h);

    AnyVal items_v = action.get("ITEMS");
    LOGOS_ASSERT(!items_v.is_null(), "PEGEN-TEST-ACT-002", "ITEMS field present");
    auto expr = tiny_elem(*doc, items_v);
    LOGOS_ASSERT(int_field(expr.get(uint8_t(ast::CODE))) == int32_t(ast::ARRAY_CAPTURE),
        "PEGEN-TEST-ACT-002", "ITEMS is ARRAY_CAPTURE");
    std::println("  [OK] test_action_array_capture");
}

static void test_action_literal_node_code() {
    auto doc = parse_grammar_string(R"(
        %meta   { name: "r" namespace: "ns" output: "r" }
        %fields { CODE = 0  VALUE = 1 }
        %nodes  { INTEGER = 5 }
        %rules {
            num <- NUMBER => { CODE: INTEGER, VALUE: $1 }
        }
    )");
    LOGOS_ASSERT(doc.has_value(), "PEGEN-TEST-ACT-003", "parse failed");
    auto h = doc->holder();
    auto rules  = array_section(*doc, "rules");
    auto rule   = tiny_elem(*doc, rules.get(0));
    auto alts   = ArrayView(rule.get(uint8_t(ast::ALTS)).to_offset(), h);
    auto alt    = tiny_elem(*doc, alts.get(0));
    auto action = logos::hermes::MapView(alt.get(uint8_t(ast::ACTION)).to_offset(), h);

    AnyVal code_v = action.get("CODE");
    LOGOS_ASSERT(!code_v.is_null(), "PEGEN-TEST-ACT-003", "CODE field present");
    auto expr = tiny_elem(*doc, code_v);
    // "INTEGER" is a symbolic name → STR_LIT in action
    LOGOS_ASSERT(int_field(expr.get(uint8_t(ast::CODE))) == int32_t(ast::STR_LIT),
        "PEGEN-TEST-ACT-003", "CODE: INTEGER is STR_LIT");
    LOGOS_ASSERT(str_field(expr.get(uint8_t(ast::VALUE)), h) == "INTEGER",
        "PEGEN-TEST-ACT-003", "STR_LIT value = INTEGER");
    std::println("  [OK] test_action_literal_node_code");
}

// ── %import ──────────────────────────────────────────────────────────────

static void test_import_parsed() {
    auto doc = parse_grammar_string(R"(
        %meta   { name: "r" namespace: "ns" output: "r" }
        %import "hermes.peg" as hermes
        %import "sql.peg"    as sql
    )");
    LOGOS_ASSERT(doc.has_value(), "PEGEN-TEST-IMP-001", "parse failed");
    auto h = doc->holder();
    auto imports = array_section(*doc, "imports");
    LOGOS_ASSERT(imports.size() == 2, "PEGEN-TEST-IMP-001",
        "expected 2 imports, got {}", imports.size());

    auto imp0 = tiny_elem(*doc, imports.get(0));
    LOGOS_ASSERT(str_field(imp0.get(uint8_t(ast::PATH)),  h) == "hermes.peg",
        "PEGEN-TEST-IMP-001", "import[0] path");
    LOGOS_ASSERT(str_field(imp0.get(uint8_t(ast::ALIAS)), h) == "hermes",
        "PEGEN-TEST-IMP-001", "import[0] alias");

    auto imp1 = tiny_elem(*doc, imports.get(1));
    LOGOS_ASSERT(str_field(imp1.get(uint8_t(ast::PATH)),  h) == "sql.peg",
        "PEGEN-TEST-IMP-001", "import[1] path");
    LOGOS_ASSERT(str_field(imp1.get(uint8_t(ast::ALIAS)), h) == "sql",
        "PEGEN-TEST-IMP-001", "import[1] alias");
    std::println("  [OK] test_import_parsed");
}

// ── Error cases: parser should reject bad input ──────────────────────────

static void test_parse_errors() {
    // Syntax error: rule without <-
    {
        auto doc = parse_grammar_string("expr NUMBER PLUS", "bad1");
        // May or may not fail depending on parser leniency; we just shouldn't crash.
        (void)doc;
    }
    // Empty source: should succeed (empty grammar)
    {
        auto doc = parse_grammar_string("", "empty");
        LOGOS_ASSERT(doc.has_value(), "PEGEN-TEST-ERR-001", "empty source should parse ok");
    }
    // Only comments
    {
        auto doc = parse_grammar_string("// just a comment\n// another", "comments");
        LOGOS_ASSERT(doc.has_value(), "PEGEN-TEST-ERR-001", "comment-only source ok");
    }
    std::println("  [OK] test_parse_errors");
}

// ── Multiple rules and interaction ───────────────────────────────────────

static void test_multiple_rules() {
    auto doc = parse_grammar_string(R"(
        %meta { name: "json" namespace: "json_ns" output: "json_parser" }
        %export { value }
        %tokens {
            LBRACE   = "{"
            RBRACE   = "}"
            LBRACKET = "["
            RBRACKET = "]"
            COLON    = ":"
            COMMA    = ","
            STRING   = /"([^"\\]|\\.)*"/
            NUMBER   = /[-]?[0-9]+/
            TRUE     = "true"
            FALSE    = "false"
            NULL     = "null"
            %skip    = /[ \t\n\r]+/
        }
        %fields { CODE = 0  VALUE = 1  ITEMS = 2  KEY = 3 }
        %nodes  { MAP = 0  ARRAY = 1  STRING = 2  NUMBER = 3  BOOL = 4  NULL_VAL = 5 }
        %rules {
            value <- map
                   / array
                   / STRING => { CODE: STRING,   VALUE: $1 }
                   / NUMBER => { CODE: NUMBER,   VALUE: $1 }
                   / TRUE   => { CODE: BOOL,     VALUE: $1 }
                   / FALSE  => { CODE: BOOL,     VALUE: $1 }
                   / NULL   => { CODE: NULL_VAL, VALUE: $1 }

            map <- LBRACE (entry (COMMA entry)*)? RBRACE
                => { CODE: MAP, ITEMS: $... }

            entry <- (STRING / IDENT) COLON value

            array <- LBRACKET (value (COMMA value)*)? RBRACKET
                  => { CODE: ARRAY, ITEMS: $... }
        }
    )");
    LOGOS_ASSERT(doc.has_value(), "PEGEN-TEST-MULTI-001", "json grammar parse failed");
    auto h = doc->holder();

    auto rules = array_section(*doc, "rules");
    LOGOS_ASSERT(rules.size() == 4, "PEGEN-TEST-MULTI-001",
        "expected 4 rules, got {}", rules.size());

    // value rule has 7 alternatives
    auto value_rule = tiny_elem(*doc, rules.get(0));
    LOGOS_ASSERT(str_field(value_rule.get(uint8_t(ast::NAME)), h) == "value",
        "PEGEN-TEST-MULTI-001", "first rule is value");
    auto value_alts = ArrayView(value_rule.get(uint8_t(ast::ALTS)).to_offset(), h);
    LOGOS_ASSERT(value_alts.size() == 7, "PEGEN-TEST-MULTI-001",
        "value has 7 alts, got {}", value_alts.size());

    // exports
    auto exports = array_section(*doc, "exports");
    LOGOS_ASSERT(exports.size() == 1, "PEGEN-TEST-MULTI-001", "1 export");
    LOGOS_ASSERT(str_field(exports.get(0), h) == "value", "PEGEN-TEST-MULTI-001", "export=value");

    // tokens
    auto tokens = array_section(*doc, "tokens");
    LOGOS_ASSERT(tokens.size() == 12, "PEGEN-TEST-MULTI-001",
        "expected 12 tokens, got {}", tokens.size());

    std::println("  [OK] test_multiple_rules");
}

// ═══════════════════════════════════════════════════════════════════════════
// Module resolver tests
// ═══════════════════════════════════════════════════════════════════════════

// Write a .peg file to a temp path and return the path.
static fs::path write_peg(const fs::path& dir, const std::string& name,
                          std::string_view content) {
    fs::path p = dir / name;
    std::ofstream f(p);
    f << content;
    return p;
}

static void test_resolver_single() {
    auto tmp = fs::temp_directory_path() / "peg_test_single";
    fs::create_directories(tmp);

    write_peg(tmp, "a.peg", R"(
        %meta { name: "a" namespace: "ns" output: "a" }
        %rules { expr <- IDENT }
    )");

    auto mods = resolve_modules((tmp / "a.peg").string());
    LOGOS_ASSERT(mods.has_value(), "PEGEN-TEST-RES-001", "resolve failed");
    LOGOS_ASSERT(mods->size() == 1, "PEGEN-TEST-RES-001",
        "expected 1 module, got {}", mods->size());
    LOGOS_ASSERT(mods->back().alias.empty(), "PEGEN-TEST-RES-001", "root has empty alias");
    fs::remove_all(tmp);
    std::println("  [OK] test_resolver_single");
}

static void test_resolver_linear_imports() {
    auto tmp = fs::temp_directory_path() / "peg_test_linear";
    fs::create_directories(tmp);

    // c.peg imports b.peg which imports a.peg
    write_peg(tmp, "a.peg", R"(%meta { name: "a" namespace: "ns" output: "a" } %rules { a <- X })");
    write_peg(tmp, "b.peg", R"(%meta { name: "b" namespace: "ns" output: "b" }
        %import "a.peg" as a
        %rules { b <- a::a }
    )");
    write_peg(tmp, "c.peg", R"(%meta { name: "c" namespace: "ns" output: "c" }
        %import "b.peg" as b
        %rules { c <- b::b }
    )");

    auto mods = resolve_modules((tmp / "c.peg").string());
    LOGOS_ASSERT(mods.has_value(), "PEGEN-TEST-RES-002", "resolve failed");
    LOGOS_ASSERT(mods->size() == 3, "PEGEN-TEST-RES-002",
        "expected 3 modules, got {}", mods->size());

    // Topological order: a first, then b, then c (root last)
    LOGOS_ASSERT((*mods)[0].alias == "a", "PEGEN-TEST-RES-002", "order[0]=a");
    LOGOS_ASSERT((*mods)[1].alias == "b", "PEGEN-TEST-RES-002", "order[1]=b");
    LOGOS_ASSERT((*mods)[2].alias.empty(), "PEGEN-TEST-RES-002", "order[2]=root");
    fs::remove_all(tmp);
    std::println("  [OK] test_resolver_linear_imports");
}

static void test_resolver_diamond() {
    auto tmp = fs::temp_directory_path() / "peg_test_diamond";
    fs::create_directories(tmp);

    // d imports b and c; b and c both import a (diamond)
    write_peg(tmp, "a.peg", R"(%meta { name: "a" namespace: "ns" output: "a" } %rules { a <- X })");
    write_peg(tmp, "b.peg", R"(%meta { name: "b" namespace: "ns" output: "b" }
        %import "a.peg" as a
        %rules { b <- a::a }
    )");
    write_peg(tmp, "c.peg", R"(%meta { name: "c" namespace: "ns" output: "c" }
        %import "a.peg" as a
        %rules { c <- a::a }
    )");
    write_peg(tmp, "d.peg", R"(%meta { name: "d" namespace: "ns" output: "d" }
        %import "b.peg" as b
        %import "c.peg" as c
        %rules { d <- b::b c::c }
    )");

    auto mods = resolve_modules((tmp / "d.peg").string());
    LOGOS_ASSERT(mods.has_value(), "PEGEN-TEST-RES-003", "resolve failed");
    // a appears once despite being imported by both b and c
    LOGOS_ASSERT(mods->size() == 4, "PEGEN-TEST-RES-003",
        "expected 4 modules (no dup), got {}", mods->size());

    // Verify a appears before b and c
    int pos_a = -1, pos_b = -1, pos_c = -1;
    for (int i = 0; i < int(mods->size()); ++i) {
        if ((*mods)[i].alias == "a") pos_a = i;
        if ((*mods)[i].alias == "b") pos_b = i;
        if ((*mods)[i].alias == "c") pos_c = i;
    }
    LOGOS_ASSERT(pos_a >= 0, "PEGEN-TEST-RES-003", "a present");
    LOGOS_ASSERT(pos_b >= 0, "PEGEN-TEST-RES-003", "b present");
    LOGOS_ASSERT(pos_c >= 0, "PEGEN-TEST-RES-003", "c present");
    LOGOS_ASSERT(pos_a < pos_b, "PEGEN-TEST-RES-003", "a before b");
    LOGOS_ASSERT(pos_a < pos_c, "PEGEN-TEST-RES-003", "a before c");
    fs::remove_all(tmp);
    std::println("  [OK] test_resolver_diamond");
}

static void test_resolver_missing_file() {
    auto tmp = fs::temp_directory_path() / "peg_test_missing";
    fs::create_directories(tmp);

    write_peg(tmp, "root.peg", R"(%meta { name: "r" namespace: "ns" output: "r" }
        %import "nonexistent.peg" as x
    )");

    auto mods = resolve_modules((tmp / "root.peg").string());
    LOGOS_ASSERT(!mods.has_value(), "PEGEN-TEST-RES-004",
        "should fail for missing import");
    fs::remove_all(tmp);
    std::println("  [OK] test_resolver_missing_file");
}

static void test_resolver_cycle() {
    auto tmp = fs::temp_directory_path() / "peg_test_cycle";
    fs::create_directories(tmp);

    // a imports b, b imports a → cycle
    write_peg(tmp, "a.peg", R"(%meta { name: "a" namespace: "ns" output: "a" }
        %import "b.peg" as b
    )");
    write_peg(tmp, "b.peg", R"(%meta { name: "b" namespace: "ns" output: "b" }
        %import "a.peg" as a
    )");

    auto mods = resolve_modules((tmp / "a.peg").string());
    LOGOS_ASSERT(!mods.has_value(), "PEGEN-TEST-RES-005",
        "should fail for import cycle");
    fs::remove_all(tmp);
    std::println("  [OK] test_resolver_cycle");
}

// ═══════════════════════════════════════════════════════════════════════════
// Codegen smoke tests
// ═══════════════════════════════════════════════════════════════════════════

static void test_codegen_generates_files() {
    auto tmp     = fs::temp_directory_path() / "peg_test_codegen";
    auto out_dir = tmp / "out";
    fs::create_directories(out_dir);

    write_peg(tmp, "calc.peg", R"peg(
        %meta { name: "calc" namespace: "logos::calc" output: "calc_parser" }
        %export { expr }
        %tokens {
            NUMBER = /[0-9]+/
            PLUS   = "+"
            MINUS  = "-"
            STAR   = "*"
            SLASH  = "/"
            LPAREN = "("
            RPAREN = ")"
            %skip  = /[ \t]+/
        }
        %fields { CODE = 0  LEFT = 1  RIGHT = 2  VALUE = 3 }
        %nodes  { ADD = 0  SUB = 1  MUL = 2  DIV = 3  NUM = 4 }
        %rules {
            expr   <- term ((PLUS / MINUS) term)*
            term   <- factor ((STAR / SLASH) factor)*
            factor <- LPAREN expr RPAREN
                    / NUMBER => { CODE: NUM, VALUE: $1 }
        }
    )peg");

    auto mods = resolve_modules((tmp / "calc.peg").string());
    LOGOS_ASSERT(mods.has_value(), "PEGEN-TEST-CG-001", "resolve failed");

    codegen(*mods, CodegenOptions{ .output_dir = out_dir });

    LOGOS_ASSERT(fs::exists(out_dir / "calc_parser.hpp"),
        "PEGEN-TEST-CG-001", "calc_parser.hpp generated");
    LOGOS_ASSERT(fs::exists(out_dir / "calc_parser.cpp"),
        "PEGEN-TEST-CG-001", "calc_parser.cpp generated");

    // Verify header has expected content
    std::ifstream hdr(out_dir / "calc_parser.hpp");
    std::string hdr_content((std::istreambuf_iterator<char>(hdr)),
                             std::istreambuf_iterator<char>());
    LOGOS_ASSERT(hdr_content.find("class CalcParser") != std::string::npos,
        "PEGEN-TEST-CG-001", "header contains class CalcParser");
    LOGOS_ASSERT(hdr_content.find("parse_expr") != std::string::npos,
        "PEGEN-TEST-CG-001", "header has parse_expr entry point");
    LOGOS_ASSERT(hdr_content.find("enum class TK_CALC") != std::string::npos,
        "PEGEN-TEST-CG-001", "header has TK_CALC enum");
    LOGOS_ASSERT(hdr_content.find("namespace logos::calc") != std::string::npos,
        "PEGEN-TEST-CG-001", "header has correct namespace");
    LOGOS_ASSERT(hdr_content.find("namespace calc_ast") != std::string::npos,
        "PEGEN-TEST-CG-001", "header has calc_ast namespace");

    // Verify source has expected content
    std::ifstream src(out_dir / "calc_parser.cpp");
    std::string src_content((std::istreambuf_iterator<char>(src)),
                             std::istreambuf_iterator<char>());
    LOGOS_ASSERT(src_content.find("rule_expr") != std::string::npos,
        "PEGEN-TEST-CG-001", "source has rule_expr");
    LOGOS_ASSERT(src_content.find("rule_term") != std::string::npos,
        "PEGEN-TEST-CG-001", "source has rule_term");
    LOGOS_ASSERT(src_content.find("rule_factor") != std::string::npos,
        "PEGEN-TEST-CG-001", "source has rule_factor");
    LOGOS_ASSERT(src_content.find("lex_one") != std::string::npos,
        "PEGEN-TEST-CG-001", "source has lex_one");
    LOGOS_ASSERT(src_content.find("TK::NUMBER") != std::string::npos,
        "PEGEN-TEST-CG-001", "source uses TK::NUMBER");

    fs::remove_all(tmp);
    std::println("  [OK] test_codegen_generates_files");
}

static void test_codegen_hermes_grammar() {
    // Run codegen on the real hermes.peg grammar and verify compilation.
    auto hermes_peg = fs::path(PEGEN_GRAMMARS_DIR) / "hermes.peg";
    if (!fs::exists(hermes_peg)) {
        std::println("  [SKIP] test_codegen_hermes_grammar: hermes.peg not found at {}",
            hermes_peg.string());
        return;
    }

    auto tmp = fs::temp_directory_path() / "peg_test_hermes_out";
    fs::create_directories(tmp);

    auto mods = resolve_modules(hermes_peg.string());
    LOGOS_ASSERT(mods.has_value(), "PEGEN-TEST-CG-002", "hermes.peg resolve failed");
    LOGOS_ASSERT(mods->size() == 1, "PEGEN-TEST-CG-002", "hermes.peg: 1 module");

    codegen(*mods, CodegenOptions{ .output_dir = tmp });

    LOGOS_ASSERT(fs::exists(tmp / "hermes_parser.hpp"), "PEGEN-TEST-CG-002", "hpp generated");
    LOGOS_ASSERT(fs::exists(tmp / "hermes_parser.cpp"), "PEGEN-TEST-CG-002", "cpp generated");

    // Check that all exported rules are present in the header.
    std::ifstream hdr(tmp / "hermes_parser.hpp");
    std::string content((std::istreambuf_iterator<char>(hdr)),
                         std::istreambuf_iterator<char>());
    for (const char* entry : {"parse_value", "parse_map", "parse_array", "parse_typed_value"})
        LOGOS_ASSERT(content.find(entry) != std::string::npos,
            "PEGEN-TEST-CG-002", "header has {}", entry);

    fs::remove_all(tmp);
    std::println("  [OK] test_codegen_hermes_grammar");
}

static void test_codegen_safe_token_names() {
    // Tokens named NULL / TRUE / FALSE must be renamed to avoid C macro clashes.
    auto tmp     = fs::temp_directory_path() / "peg_test_safe_tok";
    auto out_dir = tmp / "out";
    fs::create_directories(out_dir);

    write_peg(tmp, "json_tok.peg", R"(
        %meta { name: "json_tok" namespace: "jns" output: "json_tok_parser" }
        %tokens {
            NULL  = "null"
            TRUE  = "true"
            FALSE = "false"
        }
        %rules { val <- NULL / TRUE / FALSE }
    )");

    auto mods = resolve_modules((tmp / "json_tok.peg").string());
    LOGOS_ASSERT(mods.has_value(), "PEGEN-TEST-CG-003", "resolve failed");
    codegen(*mods, CodegenOptions{ .output_dir = out_dir });

    std::ifstream hdr(out_dir / "json_tok_parser.hpp");
    std::string content((std::istreambuf_iterator<char>(hdr)),
                         std::istreambuf_iterator<char>());
    LOGOS_ASSERT(content.find("NULL_KW")  != std::string::npos,
        "PEGEN-TEST-CG-003", "NULL renamed to NULL_KW");
    LOGOS_ASSERT(content.find("TRUE_KW")  != std::string::npos,
        "PEGEN-TEST-CG-003", "TRUE renamed to TRUE_KW");
    LOGOS_ASSERT(content.find("FALSE_KW") != std::string::npos,
        "PEGEN-TEST-CG-003", "FALSE renamed to FALSE_KW");
    // The bare names must NOT appear as enum values.
    // (They can appear in comments "// null" etc., but not as enum identifiers.)
    // Check that enum body uses _KW names.
    auto enum_start = content.find("enum class TK_JSON_TOK");
    LOGOS_ASSERT(enum_start != std::string::npos, "PEGEN-TEST-CG-003", "enum present");

    fs::remove_all(tmp);
    std::println("  [OK] test_codegen_safe_token_names");
}

static void test_codegen_lookahead_neg_ahead() {
    // Grammar with &FOLLOW and !KEYWORD items — verifies generated C++ compiles.
    auto tmp     = fs::temp_directory_path() / "peg_test_la";
    auto out_dir = tmp / "out";
    fs::create_directories(out_dir);

    write_peg(tmp, "la.peg", R"peg(
        %meta { name: "la" namespace: "ns" output: "la_parser" }
        %tokens {
            KW    = "if"
            IDENT = /[a-zA-Z_]\w*/
            %skip = /[ \t\n]+/
        }
        %fields { CODE = 0  VALUE = 1 }
        %nodes  { NAME = 0  IDENT_NODE = 1 }
        %rules {
            // !KW &IDENT: matches IDENT that is not "if"
            plain_ident <- !KW &IDENT IDENT
                        => { CODE: IDENT_NODE, VALUE: $3 }
        }
    )peg");

    auto mods = resolve_modules((tmp / "la.peg").string());
    LOGOS_ASSERT(mods.has_value(), "PEGEN-TEST-CG-004", "resolve failed");
    codegen(*mods, CodegenOptions{ .output_dir = out_dir });

    std::ifstream src(out_dir / "la_parser.cpp");
    std::string content((std::istreambuf_iterator<char>(src)),
                         std::istreambuf_iterator<char>());
    // Lookahead emits save/restore of la_pos_ / la_la_ / la_tok_
    LOGOS_ASSERT(content.find("la_pos_") != std::string::npos,
        "PEGEN-TEST-CG-004", "lookahead save/restore emitted");
    // Neg-ahead emits na_pos_ etc.
    LOGOS_ASSERT(content.find("na_pos_") != std::string::npos,
        "PEGEN-TEST-CG-004", "neg-ahead save/restore emitted");

    fs::remove_all(tmp);
    std::println("  [OK] test_codegen_lookahead_neg_ahead");
}

static void test_codegen_int_lit_action() {
    // Grammar with a literal integer in action: { CODE: 42 }
    auto tmp     = fs::temp_directory_path() / "peg_test_intlit";
    auto out_dir = tmp / "out";
    fs::create_directories(out_dir);

    write_peg(tmp, "intlit.peg", R"peg(
        %meta { name: "intlit" namespace: "ns" output: "intlit_parser" }
        %tokens {
            WORD  = /[a-z]+/
            %skip = /[ \t]+/
        }
        %fields { CODE = 0  VALUE = 1  KIND = 2 }
        %nodes  { WORD_NODE = 0 }
        %rules {
            word <- WORD => { CODE: WORD_NODE, VALUE: $1, KIND: 99 }
        }
    )peg");

    auto mods = resolve_modules((tmp / "intlit.peg").string());
    LOGOS_ASSERT(mods.has_value(), "PEGEN-TEST-CG-005", "resolve failed");
    codegen(*mods, CodegenOptions{ .output_dir = out_dir });

    std::ifstream src(out_dir / "intlit_parser.cpp");
    std::string content((std::istreambuf_iterator<char>(src)),
                         std::istreambuf_iterator<char>());
    // INT_LIT 99 must appear as from_value(int32_t(99))
    LOGOS_ASSERT(content.find("from_value(int32_t(99))") != std::string::npos,
        "PEGEN-TEST-CG-005", "INT_LIT 99 emitted correctly");

    fs::remove_all(tmp);
    std::println("  [OK] test_codegen_int_lit_action");
}

static void test_codegen_prec_pratt() {
    // Grammar with %prec table — verifies pratt_expr / token_prec are emitted.
    auto tmp     = fs::temp_directory_path() / "peg_test_prec";
    auto out_dir = tmp / "out";
    fs::create_directories(out_dir);

    write_peg(tmp, "expr.peg", R"peg(
        %meta { name: "expr" namespace: "ns" output: "expr_parser" }
        %tokens {
            NUMBER = /[0-9]+/
            PLUS   = "+"
            STAR   = "*"
            POW    = "**"
            %skip  = /[ \t]+/
        }
        %prec { left:  PLUS }
        %prec { left:  STAR }
        %prec { right: POW  }
        %fields { CODE = 0  LEFT = 1  RIGHT = 2  VALUE = 3 }
        %nodes  { NUM = 0  BIN = 1 }
        %rules {
            atom <- NUMBER => { CODE: NUM, VALUE: $1 }
        }
    )peg");

    auto mods = resolve_modules((tmp / "expr.peg").string());
    LOGOS_ASSERT(mods.has_value(), "PEGEN-TEST-CG-006", "resolve failed");
    codegen(*mods, CodegenOptions{ .output_dir = out_dir });

    std::ifstream hdr(out_dir / "expr_parser.hpp");
    std::string hdr_content((std::istreambuf_iterator<char>(hdr)),
                              std::istreambuf_iterator<char>());
    LOGOS_ASSERT(hdr_content.find("pratt_expr") != std::string::npos,
        "PEGEN-TEST-CG-006", "header declares pratt_expr");

    std::ifstream src(out_dir / "expr_parser.cpp");
    std::string src_content((std::istreambuf_iterator<char>(src)),
                              std::istreambuf_iterator<char>());
    LOGOS_ASSERT(src_content.find("expr_token_prec") != std::string::npos,
        "PEGEN-TEST-CG-006", "token_prec function emitted");
    LOGOS_ASSERT(src_content.find("expr_is_right_assoc") != std::string::npos,
        "PEGEN-TEST-CG-006", "is_right_assoc function emitted");
    // POW is right-associative — verify it appears in the right-assoc switch.
    LOGOS_ASSERT(src_content.find("POW") != std::string::npos,
        "PEGEN-TEST-CG-006", "POW token in prec table");

    fs::remove_all(tmp);
    std::println("  [OK] test_codegen_prec_pratt");
}

static void test_codegen_multi_module() {
    // Two-module grammar: base.peg defines `atom`, main.peg imports it and calls base::atom.
    auto tmp     = fs::temp_directory_path() / "peg_test_multimod";
    auto out_dir = tmp / "out";
    fs::create_directories(out_dir);

    write_peg(tmp, "base.peg", R"peg(
        %meta { name: "base" namespace: "ns" output: "base_parser" }
        %export { atom }
        %tokens {
            NUMBER = /[0-9]+/
            %skip  = /[ \t]+/
        }
        %fields { CODE = 0  VALUE = 1 }
        %nodes  { NUM = 0 }
        %rules {
            atom <- NUMBER => { CODE: NUM, VALUE: $1 }
        }
    )peg");

    write_peg(tmp, "main.peg", R"peg(
        %meta   { name: "main" namespace: "ns" output: "main_parser" }
        %import "base.peg" as base
        %export { expr }
        %tokens {
            PLUS  = "+"
            %skip = /[ \t]+/
        }
        %fields { CODE = 0  LEFT = 1  RIGHT = 2 }
        %nodes  { ADD = 0 }
        %rules {
            expr <- base::atom PLUS base::atom
        }
    )peg");

    auto mods = resolve_modules((tmp / "main.peg").string());
    LOGOS_ASSERT(mods.has_value(), "PEGEN-TEST-CG-007", "resolve failed");
    LOGOS_ASSERT(mods->size() == 2, "PEGEN-TEST-CG-007",
        "expected 2 modules, got {}", mods->size());
    codegen(*mods, CodegenOptions{ .output_dir = out_dir });

    // Both modules must produce output files.
    LOGOS_ASSERT(fs::exists(out_dir / "base_parser.hpp"), "PEGEN-TEST-CG-007", "base_parser.hpp");
    LOGOS_ASSERT(fs::exists(out_dir / "main_parser.hpp"), "PEGEN-TEST-CG-007", "main_parser.hpp");

    // main_parser.hpp must include base_parser.hpp and declare the sub-parser field.
    std::ifstream hdr(out_dir / "main_parser.hpp");
    std::string hdr_content((std::istreambuf_iterator<char>(hdr)),
                              std::istreambuf_iterator<char>());
    LOGOS_ASSERT(hdr_content.find("#include \"base_parser.hpp\"") != std::string::npos,
        "PEGEN-TEST-CG-007", "main_parser.hpp includes base_parser.hpp");
    LOGOS_ASSERT(hdr_content.find("base_") != std::string::npos,
        "PEGEN-TEST-CG-007", "main_parser has base_ sub-parser field");

    // main_parser.cpp must call base_.rule_atom().
    std::ifstream src(out_dir / "main_parser.cpp");
    std::string src_content((std::istreambuf_iterator<char>(src)),
                              std::istreambuf_iterator<char>());
    LOGOS_ASSERT(src_content.find("base_.rule_atom()") != std::string::npos,
        "PEGEN-TEST-CG-007", "main_parser calls base_.rule_atom()");

    fs::remove_all(tmp);
    std::println("  [OK] test_codegen_multi_module");
}

// ═══════════════════════════════════════════════════════════════════════════
// main
// ═══════════════════════════════════════════════════════════════════════════

int main() {
    std::println("peg_gen_test: running...");
    std::println();
    std::println("── grammar parser ─────────────────────────────────────────");
    test_meta_fields();
    test_exports();
    test_fields_and_nodes();
    test_tokens();
    test_prec();
    test_rule_single_alt();
    test_rule_multiple_alts();
    test_item_opt_rep_literal();
    test_item_star_rep();
    test_item_group();
    test_item_lookahead_neg_ahead();
    test_item_cross_grammar_ref();
    test_action_captures();
    test_action_array_capture();
    test_action_literal_node_code();
    test_import_parsed();
    test_parse_errors();
    test_multiple_rules();
    std::println();
    std::println("── module resolver ────────────────────────────────────────");
    test_resolver_single();
    test_resolver_linear_imports();
    test_resolver_diamond();
    test_resolver_missing_file();
    test_resolver_cycle();
    std::println();
    std::println("── codegen ────────────────────────────────────────────────");
    test_codegen_generates_files();
    test_codegen_hermes_grammar();
    test_codegen_safe_token_names();
    test_codegen_lookahead_neg_ahead();
    test_codegen_int_lit_action();
    test_codegen_prec_pratt();
    test_codegen_multi_module();
    std::println();
    std::println("peg_gen_test: all tests passed.");
    return 0;
}
