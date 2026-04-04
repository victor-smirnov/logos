// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos

#include <logos/hermes/path.hpp>
#include <logos/hermes/access.hpp>
#include <logos/hermes/text_parser.hpp>
#include <logos/hermes/arena_string.hpp>
#include <logos/hermes/arena_value.hpp>
#include <logos/hermes/object_array.hpp>
#include <logos/hermes/object_map.hpp>
#include <logos/hermes/tiny_object_map.hpp>
#include <logos/hermes/compound_types.hpp>

#include <charconv>
#include <string>
#include <cstring>
#include <cmath>
#include <algorithm>

namespace logos::hermes {

using namespace path_ast;

// ============================================================================
// HermesPath Recursive Descent Parser
//
// Precedence (low to high):
//   pipe (|) → or (||) → and (&&) → comparator → not (!) → postfix (. [] .*)
//
// All binary ops are left-associative.
// ============================================================================

class PathParser {
public:
    PathParser(std::string_view text, HermesCtr& doc)
        : text_(text), pos_(0), doc_(doc) {}

    logos::expected<void*> parse() noexcept {
        skip();
        if (at_end()) return make_node(CURRENT_NODE); // Empty → identity
        LOGOS_TRY(void* result, parse_pipe());
        skip();
        return result;
    }

private:
    std::string_view text_;
    size_t pos_;
    HermesCtr& doc_;

    // --- Lexer helpers ---

    bool at_end() const noexcept { return pos_ >= text_.size(); }
    char peek() const noexcept { return at_end() ? '\0' : text_[pos_]; }
    char peek(size_t off) const noexcept { size_t p = pos_ + off; return p >= text_.size() ? '\0' : text_[p]; }
    char advance() noexcept { return text_[pos_++]; }

    void skip() noexcept {
        while (pos_ < text_.size() && (text_[pos_] == ' ' || text_[pos_] == '\t' || text_[pos_] == '\n' || text_[pos_] == '\r'))
            ++pos_;
    }

    logos::expected<void> error() noexcept {
        return std::unexpected(logos::err(hermes::ErrCode::parse_error));
    }

    logos::expected<void> expect(char c) noexcept {
        skip();
        if (at_end() || text_[pos_] != c) return std::unexpected(logos::err(hermes::ErrCode::parse_error));
        ++pos_;
        return {};
    }

    bool try_char(char c) noexcept {
        skip();
        if (!at_end() && text_[pos_] == c) { ++pos_; return true; }
        return false;
    }

    bool is_ident_start(char c) noexcept { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_'; }
    bool is_ident_char(char c) noexcept { return is_ident_start(c) || (c >= '0' && c <= '9'); }
    bool is_digit(char c) noexcept { return c >= '0' && c <= '9'; }

    logos::expected<std::string> read_identifier() noexcept {
        skip();
        size_t start = pos_;
        if (at_end() || !is_ident_start(peek())) return std::unexpected(logos::err(hermes::ErrCode::parse_error));
        while (pos_ < text_.size() && is_ident_char(text_[pos_])) ++pos_;
        return std::string(text_.substr(start, pos_ - start));
    }

    int32_t try_comparator() noexcept {
        if (at_end()) return -1;
        if (text_[pos_] == '<' && peek(1) == '=') { pos_ += 2; return CMP_LE; }
        if (text_[pos_] == '>' && peek(1) == '=') { pos_ += 2; return CMP_GE; }
        if (text_[pos_] == '=' && peek(1) == '=') { pos_ += 2; return CMP_EQ; }
        if (text_[pos_] == '!' && peek(1) == '=') { pos_ += 2; return CMP_NE; }
        if (text_[pos_] == '<') { ++pos_; return CMP_LT; }
        if (text_[pos_] == '>') { ++pos_; return CMP_GT; }
        return -1;
    }

    // --- AST node builders ---

    logos::expected<void*> make_node(int32_t code) noexcept {
        LOGOS_TRY(auto* m, HermesCtrAccess::raw_tiny_map(doc_, 4));
        LOGOS_TRY_VOID(m->put(CODE, AnyVal::from_value(code), HermesCtrAccess::arena(doc_)));
        return m;
    }

    logos::expected<void*> make_binary(int32_t code, void* left, void* right) noexcept {
        LOGOS_TRY(auto* m, HermesCtrAccess::raw_tiny_map(doc_, 4));
        LOGOS_TRY_VOID(m->put(CODE, AnyVal::from_value(code), HermesCtrAccess::arena(doc_)));
        LOGOS_TRY_VOID(m->put(LEFT, AnyVal{}, HermesCtrAccess::arena(doc_)));
        m->slot(LEFT, HermesCtrAccess::base(doc_))->set_pointer(left, HermesCtrAccess::base(doc_));
        LOGOS_TRY_VOID(m->put(RIGHT, AnyVal{}, HermesCtrAccess::arena(doc_)));
        m->slot(RIGHT, HermesCtrAccess::base(doc_))->set_pointer(right, HermesCtrAccess::base(doc_));
        return m;
    }

    logos::expected<void*> make_named(int32_t code, std::string_view name) noexcept {
        LOGOS_TRY(auto* m, HermesCtrAccess::raw_tiny_map(doc_, 4));
        LOGOS_TRY_VOID(m->put(CODE, AnyVal::from_value(code), HermesCtrAccess::arena(doc_)));
        LOGOS_TRY(auto* s, HermesCtrAccess::raw_string(doc_, name));
        LOGOS_TRY_VOID(m->put(NAME, AnyVal{}, HermesCtrAccess::arena(doc_)));
        m->slot(NAME, HermesCtrAccess::base(doc_))->set_pointer(s, HermesCtrAccess::base(doc_));
        return m;
    }

    // --- Precedence levels ---

    // pipe: or ('|' or)*
    logos::expected<void*> parse_pipe() noexcept {
        LOGOS_TRY(void* left, parse_or());
        while (true) {
            skip();
            // | but not ||
            if (!at_end() && text_[pos_] == '|' && peek(1) != '|') {
                ++pos_;
                LOGOS_TRY(void* rhs, parse_or());
                LOGOS_TRY(left, make_binary(PIPE, left, rhs));
            } else break;
        }
        return left;
    }

    // or: and ('||' and)*
    logos::expected<void*> parse_or() noexcept {
        LOGOS_TRY(void* left, parse_and());
        while (true) {
            skip();
            if (pos_ + 1 < text_.size() && text_[pos_] == '|' && text_[pos_ + 1] == '|') {
                pos_ += 2;
                LOGOS_TRY(void* rhs, parse_and());
                LOGOS_TRY(left, make_binary(OR_EXPR, left, rhs));
            } else break;
        }
        return left;
    }

    // and: comparator ('&&' comparator)*
    logos::expected<void*> parse_and() noexcept {
        LOGOS_TRY(void* left, parse_comparator());
        while (true) {
            skip();
            if (pos_ + 1 < text_.size() && text_[pos_] == '&' && text_[pos_ + 1] == '&') {
                pos_ += 2;
                LOGOS_TRY(void* rhs, parse_comparator());
                LOGOS_TRY(left, make_binary(AND_EXPR, left, rhs));
            } else break;
        }
        return left;
    }

    // comparator: not (('<'|'<='|'=='|'>='|'>'|'!=') not)*
    logos::expected<void*> parse_comparator() noexcept {
        LOGOS_TRY(void* left, parse_not());
        while (true) {
            skip();
            int32_t cmp = try_comparator();
            if (cmp >= 0) {
                LOGOS_TRY(void* right, parse_not());
                LOGOS_TRY(auto* m, HermesCtrAccess::raw_tiny_map(doc_, 6));
                LOGOS_TRY_VOID(m->put(CODE, AnyVal::from_value(int32_t(COMPARATOR_EXPR)), HermesCtrAccess::arena(doc_)));
                LOGOS_TRY_VOID(m->put(LEFT, AnyVal{}, HermesCtrAccess::arena(doc_)));
                m->slot(LEFT, HermesCtrAccess::base(doc_))->set_pointer(left, HermesCtrAccess::base(doc_));
                LOGOS_TRY_VOID(m->put(RIGHT, AnyVal{}, HermesCtrAccess::arena(doc_)));
                m->slot(RIGHT, HermesCtrAccess::base(doc_))->set_pointer(right, HermesCtrAccess::base(doc_));
                LOGOS_TRY_VOID(m->put(COMPARATOR, AnyVal::from_value(cmp), HermesCtrAccess::arena(doc_)));
                left = m;
            } else break;
        }
        return left;
    }

    // not: '!' not | postfix
    logos::expected<void*> parse_not() noexcept {
        skip();
        if (!at_end() && text_[pos_] == '!' && peek(1) != '=') {
            ++pos_;
            LOGOS_TRY(void* expr, parse_not());
            LOGOS_TRY(auto* m, HermesCtrAccess::raw_tiny_map(doc_, 4));
            LOGOS_TRY_VOID(m->put(CODE, AnyVal::from_value(int32_t(NOT_EXPR)), HermesCtrAccess::arena(doc_)));
            LOGOS_TRY_VOID(m->put(RIGHT, AnyVal{}, HermesCtrAccess::arena(doc_)));
            m->slot(RIGHT, HermesCtrAccess::base(doc_))->set_pointer(expr, HermesCtrAccess::base(doc_));
            return static_cast<void*>(m);
        }
        return parse_postfix();
    }

    // postfix: primary ( '.' ident | '[' bracket ']' | '.*' )*
    logos::expected<void*> parse_postfix() noexcept {
        LOGOS_TRY(void* node, parse_primary());
        while (true) {
            skip();
            if (at_end()) break;

            if (text_[pos_] == '.' && peek(1) == '*') {
                pos_ += 2;
                LOGOS_TRY(void* cur, make_node(CURRENT_NODE));
                LOGOS_TRY(node, make_binary(HASH_WILDCARD, node, cur));
            } else if (text_[pos_] == '.' && (is_ident_start(peek(1)) || peek(1) == '"' || peek(1) == '\'')) {
                ++pos_;
                LOGOS_TRY(void* right, parse_dot_rhs());
                LOGOS_TRY(node, make_binary(SUBEXPRESSION, node, right));
            } else if (text_[pos_] == '[') {
                LOGOS_TRY(void* bracket, parse_bracket());
                LOGOS_TRY(node, make_binary(INDEX_EXPRESSION, node, bracket));
            } else break;
        }
        return node;
    }

    logos::expected<void*> parse_dot_rhs() noexcept {
        skip();
        // function call or identifier
        if (is_ident_start(peek())) {
            LOGOS_TRY(std::string name, read_identifier());
            skip();
            if (!at_end() && peek() == '(') {
                return parse_function_args(name);
            }
            return make_named(IDENTIFIER, name);
        }
        if (peek() == '"') {
            ++pos_;
            LOGOS_TRY(std::string s, parse_qstring());
            return make_named(RAW_STRING, s);
        }
        if (peek() == '\'') {
            ++pos_;
            LOGOS_TRY(std::string s, parse_raw_str());
            return make_named(RAW_STRING, s);
        }
        return std::unexpected(logos::err(hermes::ErrCode::parse_error));
    }

    // primary: identifier | function | '*' | '@' | literal | '(' expr ')' | '[' multiselect ']' | '{' hash '}'
    logos::expected<void*> parse_primary() noexcept {
        skip();
        if (at_end()) return std::unexpected(logos::err(hermes::ErrCode::parse_error));
        char c = peek();

        if (c == '*') {
            ++pos_;
            return make_node(HASH_WILDCARD);
        }
        if (c == '@') {
            ++pos_;
            return make_node(CURRENT_NODE);
        }
        if (c == '(') {
            ++pos_;
            LOGOS_TRY(void* expr, parse_pipe());
            LOGOS_TRY_VOID(expect(')'));
            LOGOS_TRY(auto* m, HermesCtrAccess::raw_tiny_map(doc_, 4));
            LOGOS_TRY_VOID(m->put(CODE, AnyVal::from_value(int32_t(PAREN)), HermesCtrAccess::arena(doc_)));
            LOGOS_TRY_VOID(m->put(RIGHT, AnyVal{}, HermesCtrAccess::arena(doc_)));
            m->slot(RIGHT, HermesCtrAccess::base(doc_))->set_pointer(expr, HermesCtrAccess::base(doc_));
            return static_cast<void*>(m);
        }
        if (c == '{') {
            return parse_multiselect_hash();
        }
        if (c == '[') {
            // Could be multiselect list or bracket specifier on identity
            if (peek(1) == ']') { pos_ += 2; return make_node(FLATTEN); }
            if (peek(1) == '?') { return parse_bracket(); }
            // Multiselect list
            return parse_multiselect_list();
        }
        if (c == '^') {
            // Hermes value literal
            ++pos_;
            return parse_hermes_value_literal();
        }
        if (c == '"') {
            ++pos_;
            LOGOS_TRY(std::string s, parse_qstring());
            return make_named(RAW_STRING, s);
        }
        if (c == '\'') {
            ++pos_;
            LOGOS_TRY(std::string s, parse_raw_str());
            return make_named(RAW_STRING, s);
        }
        if (is_ident_start(c)) {
            LOGOS_TRY(std::string name, read_identifier());
            // Check for function call.
            skip();
            if (!at_end() && peek() == '(') {
                return parse_function_args(name);
            }
            // Keywords: true, false, null as literals.
            if (name == "true" || name == "false" || name == "null") {
                LOGOS_TRY(auto* m, HermesCtrAccess::raw_tiny_map(doc_, 4));
                LOGOS_TRY_VOID(m->put(CODE, AnyVal::from_value(int32_t(HERMES_VALUE)), HermesCtrAccess::arena(doc_)));
                if (name == "true") {
                    LOGOS_TRY(auto* v, HermesCtrAccess::make_value<uint8_t>(doc_, 1));
                    LOGOS_TRY_VOID(m->put(VALUE, AnyVal{}, HermesCtrAccess::arena(doc_)));
                    m->slot(VALUE, HermesCtrAccess::base(doc_))->set_pointer(v, HermesCtrAccess::base(doc_));
                } else if (name == "false") {
                    LOGOS_TRY(auto* v, HermesCtrAccess::make_value<uint8_t>(doc_, 0));
                    LOGOS_TRY_VOID(m->put(VALUE, AnyVal{}, HermesCtrAccess::arena(doc_)));
                    m->slot(VALUE, HermesCtrAccess::base(doc_))->set_pointer(v, HermesCtrAccess::base(doc_));
                }
                // null: VALUE stays null
                return static_cast<void*>(m);
            }
            return make_named(IDENTIFIER, name);
        }
        if (is_digit(c) || c == '-') {
            return parse_number_literal();
        }

        return std::unexpected(logos::err(hermes::ErrCode::parse_error));
    }

    // --- Bracket specifiers ---

    logos::expected<void*> parse_bracket() noexcept {
        LOGOS_TRY_VOID(expect('['));
        skip();

        // Filter: [? expr ]
        if (peek() == '?') {
            ++pos_;
            LOGOS_TRY(void* expr, parse_pipe());
            LOGOS_TRY_VOID(expect(']'));
            LOGOS_TRY(auto* m, HermesCtrAccess::raw_tiny_map(doc_, 4));
            LOGOS_TRY_VOID(m->put(CODE, AnyVal::from_value(int32_t(FILTER)), HermesCtrAccess::arena(doc_)));
            LOGOS_TRY_VOID(m->put(RIGHT, AnyVal{}, HermesCtrAccess::arena(doc_)));
            m->slot(RIGHT, HermesCtrAccess::base(doc_))->set_pointer(expr, HermesCtrAccess::base(doc_));
            return static_cast<void*>(m);
        }

        // Flatten: []
        if (peek() == ']') {
            ++pos_;
            return make_node(FLATTEN);
        }

        // Wildcard: [*]
        if (peek() == '*') {
            ++pos_;
            LOGOS_TRY_VOID(expect(']'));
            return make_node(LIST_WILDCARD);
        }

        // Slice or array item.
        // Try to parse optional int, then check for ':'.
        bool has_start = false;
        int64_t start_val = 0;
        if (peek() == ':' || is_digit(peek()) || peek() == '-') {
            if (peek() != ':') {
                LOGOS_TRY(start_val, parse_int64());
                has_start = true;
            }
        }

        skip();
        if (peek() == ':') {
            // Slice: [start:stop:step]
            return parse_slice(has_start, start_val);
        }

        // Array item: [index]
        if (!has_start) return std::unexpected(logos::err(hermes::ErrCode::parse_error));
        LOGOS_TRY_VOID(expect(']'));
        LOGOS_TRY(auto* m, HermesCtrAccess::raw_tiny_map(doc_, 4));
        LOGOS_TRY_VOID(m->put(CODE, AnyVal::from_value(int32_t(ARRAY_ITEM)), HermesCtrAccess::arena(doc_)));
        LOGOS_TRY_VOID(m->put(VALUE, AnyVal::from_value(int32_t(start_val)), HermesCtrAccess::arena(doc_)));
        return static_cast<void*>(m);
    }

    logos::expected<void*> parse_slice(bool has_start, int64_t start_val) noexcept {
        LOGOS_TRY(auto* m, HermesCtrAccess::raw_tiny_map(doc_, 6));
        LOGOS_TRY_VOID(m->put(CODE, AnyVal::from_value(int32_t(SLICE)), HermesCtrAccess::arena(doc_)));
        if (has_start) {
            LOGOS_TRY_VOID(m->put(START, AnyVal::from_value(int32_t(start_val)), HermesCtrAccess::arena(doc_)));
        }

        LOGOS_TRY_VOID(expect(':')); // First colon.
        skip();

        // Optional stop.
        if (peek() != ':' && peek() != ']') {
            LOGOS_TRY(int64_t stop, parse_int64());
            LOGOS_TRY_VOID(m->put(STOP, AnyVal::from_value(int32_t(stop)), HermesCtrAccess::arena(doc_)));
        }

        skip();
        if (peek() == ':') {
            ++pos_;
            skip();
            if (peek() != ']') {
                LOGOS_TRY(int64_t step, parse_int64());
                LOGOS_TRY_VOID(m->put(STEP, AnyVal::from_value(int32_t(step)), HermesCtrAccess::arena(doc_)));
            }
        }

        LOGOS_TRY_VOID(expect(']'));
        return static_cast<void*>(m);
    }

    logos::expected<int64_t> parse_int64() noexcept {
        skip();
        bool neg = false;
        if (peek() == '-') { neg = true; ++pos_; }
        int64_t val = 0;
        if (!is_digit(peek())) return std::unexpected(logos::err(hermes::ErrCode::parse_error));
        while (!at_end() && is_digit(peek())) {
            val = val * 10 + (advance() - '0');
        }
        return neg ? -val : val;
    }

    // --- Function calls ---

    logos::expected<void*> parse_function_args(const std::string& name) noexcept {
        LOGOS_TRY_VOID(expect('('));
        LOGOS_TRY(auto* m, HermesCtrAccess::raw_tiny_map(doc_, 4));
        LOGOS_TRY_VOID(m->put(CODE, AnyVal::from_value(int32_t(FUNCTION_CALL)), HermesCtrAccess::arena(doc_)));
        LOGOS_TRY(auto* fname, HermesCtrAccess::raw_string(doc_, name));
        LOGOS_TRY_VOID(m->put(NAME, AnyVal{}, HermesCtrAccess::arena(doc_)));
        m->slot(NAME, HermesCtrAccess::base(doc_))->set_pointer(fname, HermesCtrAccess::base(doc_));

        LOGOS_TRY(auto* args, HermesCtrAccess::raw_array(doc_));
        skip();
        if (peek() != ')') {
            while (true) {
                skip();
                void* arg;
                if (peek() == '&') {
                    ++pos_;
                    LOGOS_TRY(void* expr, parse_pipe());
                    LOGOS_TRY(auto* ea, HermesCtrAccess::raw_tiny_map(doc_, 4));
                    LOGOS_TRY_VOID(ea->put(CODE, AnyVal::from_value(int32_t(EXPR_ARGUMENT)), HermesCtrAccess::arena(doc_)));
                    LOGOS_TRY_VOID(ea->put(RIGHT, AnyVal{}, HermesCtrAccess::arena(doc_)));
                    ea->slot(RIGHT, HermesCtrAccess::base(doc_))->set_pointer(expr, HermesCtrAccess::base(doc_));
                    arg = ea;
                } else {
                    LOGOS_TRY(arg, parse_pipe());
                }
                LOGOS_TRY_VOID(args->push_back(AnyVal{}, HermesCtrAccess::arena(doc_)));
                args->slot(args->size() - 1, HermesCtrAccess::base(doc_))->set_pointer(arg, HermesCtrAccess::base(doc_));
                skip();
                if (peek() == ')') break;
                LOGOS_TRY_VOID(expect(','));
            }
        }
        LOGOS_TRY_VOID(expect(')'));
        LOGOS_TRY_VOID(m->put(ARGS, AnyVal{}, HermesCtrAccess::arena(doc_)));
        m->slot(ARGS, HermesCtrAccess::base(doc_))->set_pointer(args, HermesCtrAccess::base(doc_));
        return static_cast<void*>(m);
    }

    // --- Multiselect ---

    logos::expected<void*> parse_multiselect_list() noexcept {
        LOGOS_TRY_VOID(expect('['));
        LOGOS_TRY(auto* exprs, HermesCtrAccess::raw_array(doc_));
        while (true) {
            LOGOS_TRY(void* e, parse_pipe());
            LOGOS_TRY_VOID(exprs->push_back(AnyVal{}, HermesCtrAccess::arena(doc_)));
            exprs->slot(exprs->size() - 1, HermesCtrAccess::base(doc_))->set_pointer(e, HermesCtrAccess::base(doc_));
            skip();
            if (peek() == ']') { ++pos_; break; }
            LOGOS_TRY_VOID(expect(','));
        }
        LOGOS_TRY(auto* m, HermesCtrAccess::raw_tiny_map(doc_, 4));
        LOGOS_TRY_VOID(m->put(CODE, AnyVal::from_value(int32_t(MULTISELECT_LIST)), HermesCtrAccess::arena(doc_)));
        LOGOS_TRY_VOID(m->put(EXPRESSIONS, AnyVal{}, HermesCtrAccess::arena(doc_)));
        m->slot(EXPRESSIONS, HermesCtrAccess::base(doc_))->set_pointer(exprs, HermesCtrAccess::base(doc_));
        return static_cast<void*>(m);
    }

    logos::expected<void*> parse_multiselect_hash() noexcept {
        LOGOS_TRY_VOID(expect('{'));
        LOGOS_TRY(auto* keys_arr, HermesCtrAccess::raw_array(doc_));
        LOGOS_TRY(auto* vals_arr, HermesCtrAccess::raw_array(doc_));
        while (true) {
            skip();
            LOGOS_TRY(std::string key, read_identifier());
            LOGOS_TRY_VOID(expect(':'));
            LOGOS_TRY(void* val, parse_pipe());

            LOGOS_TRY(auto* ks, HermesCtrAccess::raw_string(doc_, key));
            LOGOS_TRY_VOID(keys_arr->push_back(AnyVal{}, HermesCtrAccess::arena(doc_)));
            keys_arr->slot(keys_arr->size() - 1, HermesCtrAccess::base(doc_))->set_pointer(ks, HermesCtrAccess::base(doc_));
            LOGOS_TRY_VOID(vals_arr->push_back(AnyVal{}, HermesCtrAccess::arena(doc_)));
            vals_arr->slot(vals_arr->size() - 1, HermesCtrAccess::base(doc_))->set_pointer(val, HermesCtrAccess::base(doc_));

            skip();
            if (peek() == '}') { ++pos_; break; }
            LOGOS_TRY_VOID(expect(','));
        }
        LOGOS_TRY(auto* m, HermesCtrAccess::raw_tiny_map(doc_, 6));
        LOGOS_TRY_VOID(m->put(CODE, AnyVal::from_value(int32_t(MULTISELECT_HASH)), HermesCtrAccess::arena(doc_)));
        LOGOS_TRY_VOID(m->put(KEYS, AnyVal{}, HermesCtrAccess::arena(doc_)));
        m->slot(KEYS, HermesCtrAccess::base(doc_))->set_pointer(keys_arr, HermesCtrAccess::base(doc_));
        LOGOS_TRY_VOID(m->put(EXPRESSIONS, AnyVal{}, HermesCtrAccess::arena(doc_)));
        m->slot(EXPRESSIONS, HermesCtrAccess::base(doc_))->set_pointer(vals_arr, HermesCtrAccess::base(doc_));
        return static_cast<void*>(m);
    }

    // --- Literals ---

    logos::expected<void*> parse_hermes_value_literal() noexcept {
        // Capture remaining text and parse as Hermes value.
        // We need to find how much the Hermes parser consumes.
        // Simple approach: parse inline and wrap.
        // For now, support basic literals: numbers, strings, bools, arrays, maps.
        skip();
        size_t start = pos_;
        // Read until we hit something that doesn't belong in a value.
        // This is tricky — delegate to the Hermes value parser mentally.
        // For simplicity, treat ^value as parse_primary of the hermes format.
        LOGOS_TRY(void* val, parse_primary()); // Reuse our primary for basic types.
        LOGOS_TRY(auto* m, HermesCtrAccess::raw_tiny_map(doc_, 4));
        LOGOS_TRY_VOID(m->put(CODE, AnyVal::from_value(int32_t(HERMES_VALUE)), HermesCtrAccess::arena(doc_)));
        LOGOS_TRY_VOID(m->put(VALUE, AnyVal{}, HermesCtrAccess::arena(doc_)));
        m->slot(VALUE, HermesCtrAccess::base(doc_))->set_pointer(val, HermesCtrAccess::base(doc_));
        (void)start;
        return static_cast<void*>(m);
    }

    logos::expected<void*> parse_number_literal() noexcept {
        bool neg = false;
        if (peek() == '-') { neg = true; ++pos_; }
        int64_t val = 0;
        bool has_dot = false;
        size_t start = pos_ - (neg ? 1 : 0);
        while (!at_end() && is_digit(peek())) val = val * 10 + (advance() - '0');
        if (!at_end() && peek() == '.') {
            has_dot = true;
            ++pos_;
            while (!at_end() && is_digit(peek())) ++pos_;
        }
        if (has_dot) {
            std::string_view ns = text_.substr(start, pos_ - start);
            double dval;
            std::from_chars(ns.data(), ns.data() + ns.size(), dval);
            LOGOS_TRY(auto* v, HermesCtrAccess::make_value<double>(doc_, dval));
            LOGOS_TRY(auto* m, HermesCtrAccess::raw_tiny_map(doc_, 4));
            LOGOS_TRY_VOID(m->put(CODE, AnyVal::from_value(int32_t(HERMES_VALUE)), HermesCtrAccess::arena(doc_)));
            LOGOS_TRY_VOID(m->put(VALUE, AnyVal{}, HermesCtrAccess::arena(doc_)));
            m->slot(VALUE, HermesCtrAccess::base(doc_))->set_pointer(v, HermesCtrAccess::base(doc_));
            return static_cast<void*>(m);
        }
        int32_t ival = neg ? -int32_t(val) : int32_t(val);
        LOGOS_TRY(auto* m, HermesCtrAccess::raw_tiny_map(doc_, 4));
        LOGOS_TRY_VOID(m->put(CODE, AnyVal::from_value(int32_t(HERMES_VALUE)), HermesCtrAccess::arena(doc_)));
        LOGOS_TRY_VOID(m->put(VALUE, AnyVal::from_value(ival), HermesCtrAccess::arena(doc_)));
        return static_cast<void*>(m);
    }

    logos::expected<std::string> parse_qstring() noexcept {
        std::string result;
        while (!at_end() && peek() != '"') {
            if (peek() == '\\') { ++pos_; result += advance(); }
            else result += advance();
        }
        if (at_end()) return std::unexpected(logos::err(hermes::ErrCode::parse_error));
        ++pos_;
        return result;
    }

    logos::expected<std::string> parse_raw_str() noexcept {
        std::string result;
        while (!at_end() && peek() != '\'') {
            if (peek() == '\\' && peek(1) == '\'') { pos_ += 2; result += '\''; }
            else result += advance();
        }
        if (at_end()) return std::unexpected(logos::err(hermes::ErrCode::parse_error));
        ++pos_;
        return result;
    }
};

// ============================================================================
// HermesPath Evaluator
//
// Walks AST nodes (TinyObjectMap) and evaluates against data.
// Data and results live in the result document's arena.
// ============================================================================

class PathEvaluator {
public:
    PathEvaluator(HermesCtr& result, uint8_t* data_base, uint8_t* ast_base)
        : result_(result), data_base_(data_base), ast_base_(ast_base) {}

    // Evaluate an AST node against a data value. Returns arena pointer to result.
    logos::expected<void*> eval(void* data, void* ast_node) noexcept {
        auto* node = static_cast<TinyObjectMap*>(ast_node);
        int32_t code = node->get(CODE, ast_base_).as_value<int32_t>();

        switch (code) {
            case CURRENT_NODE:  return data;
            case IDENTIFIER:    return eval_identifier(data, node);
            case RAW_STRING:    return eval_identifier(data, node);
            case SUBEXPRESSION: return eval_subexpression(data, node);
            case INDEX_EXPRESSION: return eval_index(data, node);
            case ARRAY_ITEM:    return eval_array_item(data, node);
            case FLATTEN:       return eval_flatten(data);
            case SLICE:         return eval_slice(data, node);
            case FILTER:        return eval_filter(data, node);
            case LIST_WILDCARD: return eval_list_wildcard(data);
            case HASH_WILDCARD: return eval_hash_wildcard(data);
            case COMPARATOR_EXPR: return eval_comparator(data, node);
            case NOT_EXPR:      return eval_not(data, node);
            case OR_EXPR:       return eval_or(data, node);
            case AND_EXPR:      return eval_and(data, node);
            case PIPE:          return eval_pipe(data, node);
            case PAREN:         return eval(data, get_child(node, RIGHT));
            case MULTISELECT_LIST: return eval_multiselect_list(data, node);
            case MULTISELECT_HASH: return eval_multiselect_hash(data, node);
            case FUNCTION_CALL: return eval_function(data, node);
            case HERMES_VALUE:  return eval_hermes_value(node);
            default: return nullptr;
        }
    }

private:
    HermesCtr& result_;
    uint8_t* data_base_;  // base of the data document arena
    uint8_t* ast_base_;   // base of the AST document arena

    void* get_child(TinyObjectMap* node, uint8_t key) noexcept {
        AnyVal* s = node->slot(key, ast_base_);
        if (!s || s->is_null()) return nullptr;
        if (s->is_pointer()) return s->as_ptr<void>(ast_base_);
        return nullptr; // Embedded values are not child nodes.
    }

    bool is_truthy(void* val) noexcept {
        if (!val) return false;
        auto* bytes = static_cast<const uint8_t*>(val);
        TypeTag tag = TypeTag::read_before(bytes);
        uint64_t tc = tag.type_code();
        if (tc == type_hash::Boolean) return *static_cast<const uint8_t*>(val) != 0;
        if (tc == type_hash::Integer) return *static_cast<const int32_t*>(val) != 0;
        if (tc == type_hash::Varchar) return static_cast<const ArenaString*>(val)->length() > 0;
        if (tag.descriptor() == TagDescriptor::Array && tc == type_hash::ObjectArray)
            return static_cast<const ObjectArray*>(val)->size() > 0;
        if (tag.descriptor() == TagDescriptor::Map && tc == type_hash::ObjectMap)
            return static_cast<const ObjectMap*>(val)->size() > 0;
        return true; // Other non-null values are truthy.
    }

    double to_double(void* val) noexcept {
        if (!val) return std::nan("");
        auto* b = static_cast<const uint8_t*>(val);
        TypeTag tag = TypeTag::read_before(b);
        switch (tag.type_code()) {
            case type_hash::Integer: return *static_cast<const int32_t*>(val);
            case type_hash::BigInt:  return static_cast<double>(*static_cast<const int64_t*>(val));
            case type_hash::Real:    return *static_cast<const float*>(val);
            case type_hash::Double:  return *static_cast<const double*>(val);
            default: return std::nan("");
        }
    }

    std::string_view to_string(void* val) noexcept {
        if (!val) return "";
        auto* b = static_cast<const uint8_t*>(val);
        TypeTag tag = TypeTag::read_before(b);
        if (tag.type_code() == type_hash::Varchar)
            return static_cast<const ArenaString*>(val)->view();
        return "";
    }

    bool compare(void* lv, void* rv, int32_t cmp) noexcept {
        if (!lv || !rv) return cmp == CMP_EQ ? (!lv && !rv) : false;
        // Try numeric comparison.
        double ld = to_double(lv), rd = to_double(rv);
        if (!std::isnan(ld) && !std::isnan(rd)) {
            switch (cmp) {
                case CMP_LT: return ld < rd;
                case CMP_LE: return ld <= rd;
                case CMP_EQ: return ld == rd;
                case CMP_GE: return ld >= rd;
                case CMP_GT: return ld > rd;
                case CMP_NE: return ld != rd;
            }
        }
        // String comparison.
        auto ls = to_string(lv), rs = to_string(rv);
        switch (cmp) {
            case CMP_EQ: return ls == rs;
            case CMP_NE: return ls != rs;
            default: return ls < rs; // Lexicographic for < etc.
        }
    }

    // --- Evaluation methods ---

    logos::expected<void*> eval_identifier(void* data, TinyObjectMap* node) noexcept {
        if (!data) return nullptr;
        AnyVal* name_slot = node->slot(NAME, ast_base_);
        if (!name_slot || name_slot->is_null()) return nullptr;
        auto* name = name_slot->as_ptr<ArenaString>(ast_base_);
        auto key = name->view();

        auto* bytes = static_cast<const uint8_t*>(data);
        TypeTag tag = TypeTag::read_before(bytes);
        if (tag.descriptor() == TagDescriptor::Map && tag.type_code() == type_hash::ObjectMap) {
            auto* map = static_cast<ObjectMap*>(data);
            AnyVal* slot = map->get_slot(key, data_base_);
            if (!slot) return nullptr;
            if (slot->is_value()) return resolve_embedded(slot);
            if (slot->is_pointer()) return slot->as_ptr<void>(data_base_);
            return nullptr;
        }
        return nullptr;
    }

    // Resolve a AnyVal slot. For embedded values, materializes in result arena.
    // For pointer-mode, the caller must supply the correct base via the
    // typed resolve methods below; this overload handles embedded-only.
    logos::expected<void*> resolve_embedded(AnyVal* slot) noexcept {
        if (slot->is_null()) return nullptr;
        if (slot->is_pointer()) return nullptr; // Caller should use typed resolve
        // Embedded value — allocate in result arena.
        uint8_t th = slot->value_type_hash();
        switch (th) {
            case type_hash::Integer: {
                LOGOS_TRY(auto* v, HermesCtrAccess::make_value<int32_t>(result_, slot->as_value<int32_t>()));
                return v;
            }
            case type_hash::UInteger: {
                LOGOS_TRY(auto* v, HermesCtrAccess::make_value<uint32_t>(result_, slot->as_value<uint32_t>()));
                return v;
            }
            case type_hash::Boolean: {
                TypeTag tag(type_hash::Boolean, TagDescriptor::Data);
                LOGOS_TRY(void* mem, HermesCtrAccess::arena(result_).allocate(1, 2, tag));
                *static_cast<uint8_t*>(mem) = slot->as_value<uint8_t>();
                return mem;
            }
            case type_hash::Real: {
                LOGOS_TRY(auto* v, HermesCtrAccess::make_value<float>(result_, slot->as_value<float>()));
                return v;
            }
            case type_hash::SmallInt: {
                LOGOS_TRY(auto* v, HermesCtrAccess::make_value<int16_t>(result_, slot->as_value<int16_t>()));
                return v;
            }
            case type_hash::TinyInt: {
                LOGOS_TRY(auto* v, HermesCtrAccess::make_value<int8_t>(result_, slot->as_value<int8_t>()));
                return v;
            }
            default: return nullptr;
        }
    }

    // Resolve a AnyVal slot with a specific arena base for pointer-mode values.
    logos::expected<void*> resolve_slot(AnyVal* slot, uint8_t* base) noexcept {
        if (!slot || slot->is_null()) return nullptr;
        if (slot->is_pointer()) return slot->as_ptr<void>(base);
        return resolve_embedded(slot);
    }

    logos::expected<void*> eval_subexpression(void* data, TinyObjectMap* node) noexcept {
        LOGOS_TRY(void* left_result, eval(data, get_child(node, LEFT)));
        return eval(left_result, get_child(node, RIGHT));
    }

    logos::expected<void*> eval_index(void* data, TinyObjectMap* node) noexcept {
        LOGOS_TRY(void* left_result, eval(data, get_child(node, LEFT)));
        return eval(left_result, get_child(node, RIGHT));
    }

    logos::expected<void*> eval_array_item(void* data, TinyObjectMap* node) noexcept {
        if (!data) return nullptr;
        auto* bytes = static_cast<const uint8_t*>(data);
        TypeTag tag = TypeTag::read_before(bytes);
        if (tag.descriptor() != TagDescriptor::Array || tag.type_code() != type_hash::ObjectArray)
            return nullptr;
        auto* arr = static_cast<ObjectArray*>(data);
        int32_t idx = node->get(VALUE, ast_base_).as_value<int32_t>();
        if (idx < 0) idx += static_cast<int32_t>(arr->size());
        if (idx < 0 || idx >= static_cast<int32_t>(arr->size())) return nullptr;
        return resolve_slot(arr->slot(idx, data_base_), data_base_);
    }

    logos::expected<void*> eval_flatten(void* data) noexcept {
        if (!data) return nullptr;
        auto* bytes = static_cast<const uint8_t*>(data);
        TypeTag tag = TypeTag::read_before(bytes);
        if (tag.descriptor() != TagDescriptor::Array) return nullptr;
        auto* arr = static_cast<ObjectArray*>(data);
        LOGOS_TRY(auto* result, HermesCtrAccess::raw_array(result_));
        for (uint64_t i = 0; i < arr->size(); ++i) {
            LOGOS_TRY(void* elem, resolve_slot(arr->slot(i, data_base_), data_base_));
            if (!elem) continue;
            auto* eb = static_cast<const uint8_t*>(elem);
            TypeTag et = TypeTag::read_before(eb);
            if (et.descriptor() == TagDescriptor::Array && et.type_code() == type_hash::ObjectArray) {
                auto* inner = static_cast<ObjectArray*>(elem);
                for (uint64_t j = 0; j < inner->size(); ++j) {
                    LOGOS_TRY(void* ie, resolve_slot(inner->slot(j, data_base_), data_base_));
                    LOGOS_TRY_VOID(push_value(result, ie));
                }
            } else {
                LOGOS_TRY_VOID(push_value(result, elem));
            }
        }
        return result;
    }

    logos::expected<void*> eval_slice(void* data, TinyObjectMap* node) noexcept {
        if (!data) return nullptr;
        auto* arr = static_cast<ObjectArray*>(data);
        int64_t len = arr->size();
        int64_t start = node->has_key(START) ? node->get(START, ast_base_).as_value<int32_t>() : 0;
        int64_t stop = node->has_key(STOP) ? node->get(STOP, ast_base_).as_value<int32_t>() : len;
        int64_t step = node->has_key(STEP) ? node->get(STEP, ast_base_).as_value<int32_t>() : 1;
        if (step == 0) return nullptr;

        if (start < 0) start += len;
        if (stop < 0) stop += len;
        start = std::clamp(start, int64_t(0), len);
        stop = std::clamp(stop, int64_t(0), len);

        LOGOS_TRY(auto* result, HermesCtrAccess::raw_array(result_));
        if (step > 0) {
            for (int64_t i = start; i < stop; i += step) {
                LOGOS_TRY(void* sv, resolve_slot(arr->slot(i, data_base_), data_base_));
                LOGOS_TRY_VOID(push_value(result, sv));
            }
        } else {
            for (int64_t i = start; i > stop; i += step) {
                LOGOS_TRY(void* sv, resolve_slot(arr->slot(i, data_base_), data_base_));
                LOGOS_TRY_VOID(push_value(result, sv));
            }
        }
        return result;
    }

    logos::expected<void*> eval_filter(void* data, TinyObjectMap* node) noexcept {
        if (!data) return nullptr;
        auto* arr = static_cast<ObjectArray*>(data);
        LOGOS_TRY(auto* result, HermesCtrAccess::raw_array(result_));
        void* filter_ast = get_child(node, RIGHT);
        for (uint64_t i = 0; i < arr->size(); ++i) {
            LOGOS_TRY(void* elem, resolve_slot(arr->slot(i, data_base_), data_base_));
            LOGOS_TRY(void* test, eval(elem, filter_ast));
            if (is_truthy(test)) {
                LOGOS_TRY_VOID(push_value(result, elem));
            }
        }
        return result;
    }

    logos::expected<void*> eval_list_wildcard(void* data) noexcept {
        if (!data) return nullptr;
        auto* arr = static_cast<ObjectArray*>(data);
        LOGOS_TRY(auto* result, HermesCtrAccess::raw_array(result_));
        for (uint64_t i = 0; i < arr->size(); ++i) {
            LOGOS_TRY(void* sv, resolve_slot(arr->slot(i, data_base_), data_base_));
            LOGOS_TRY_VOID(push_value(result, sv));
        }
        return result;
    }

    logos::expected<void*> eval_hash_wildcard(void* data) noexcept {
        if (!data) return nullptr;
        auto* bytes = static_cast<const uint8_t*>(data);
        TypeTag tag = TypeTag::read_before(bytes);
        if (tag.descriptor() == TagDescriptor::Map && tag.type_code() == type_hash::ObjectMap) {
            auto* map = static_cast<ObjectMap*>(data);
            LOGOS_TRY(auto* result, HermesCtrAccess::raw_array(result_));
            logos::expected<void> status{};
            map->for_each([&](ArenaString*, AnyVal* val) {
                if (!status) return;
                auto slot_res = resolve_slot(val, data_base_);
                if (!slot_res) { status = std::unexpected(std::move(slot_res.error())); return; }
                auto push_res = push_value(result, *slot_res);
                if (!push_res) { status = std::unexpected(std::move(push_res.error())); return; }
            }, data_base_);
            LOGOS_TRY_VOID(std::move(status));
            return static_cast<void*>(result);
        }
        return nullptr;
    }

    logos::expected<void*> eval_comparator(void* data, TinyObjectMap* node) noexcept {
        LOGOS_TRY(void* lv, eval(data, get_child(node, LEFT)));
        LOGOS_TRY(void* rv, eval(data, get_child(node, RIGHT)));
        int32_t cmp = node->get(COMPARATOR, ast_base_).as_value<int32_t>();
        bool result_val = compare(lv, rv, cmp);
        TypeTag tag(type_hash::Boolean, TagDescriptor::Data);
        LOGOS_TRY(void* mem, HermesCtrAccess::arena(result_).allocate(1, 2, tag));
        *static_cast<uint8_t*>(mem) = result_val ? 1 : 0;
        return mem;
    }

    logos::expected<void*> eval_not(void* data, TinyObjectMap* node) noexcept {
        LOGOS_TRY(void* val, eval(data, get_child(node, RIGHT)));
        bool result_val = !is_truthy(val);
        TypeTag tag(type_hash::Boolean, TagDescriptor::Data);
        LOGOS_TRY(void* mem, HermesCtrAccess::arena(result_).allocate(1, 2, tag));
        *static_cast<uint8_t*>(mem) = result_val ? 1 : 0;
        return mem;
    }

    logos::expected<void*> eval_or(void* data, TinyObjectMap* node) noexcept {
        LOGOS_TRY(void* lv, eval(data, get_child(node, LEFT)));
        if (is_truthy(lv)) return lv;
        return eval(data, get_child(node, RIGHT));
    }

    logos::expected<void*> eval_and(void* data, TinyObjectMap* node) noexcept {
        LOGOS_TRY(void* lv, eval(data, get_child(node, LEFT)));
        if (!is_truthy(lv)) return lv;
        return eval(data, get_child(node, RIGHT));
    }

    logos::expected<void*> eval_pipe(void* data, TinyObjectMap* node) noexcept {
        LOGOS_TRY(void* lv, eval(data, get_child(node, LEFT)));
        return eval(lv, get_child(node, RIGHT));
    }

    logos::expected<void*> eval_multiselect_list(void* data, TinyObjectMap* node) noexcept {
        auto* exprs = static_cast<ObjectArray*>(get_child(node, EXPRESSIONS));
        LOGOS_TRY(auto* result, HermesCtrAccess::raw_array(result_));
        for (uint64_t i = 0; i < exprs->size(); ++i) {
            LOGOS_TRY(void* expr, resolve_slot(exprs->slot(i, ast_base_), ast_base_));
            LOGOS_TRY(void* val, eval(data, expr));
            LOGOS_TRY_VOID(push_value(result, val));
        }
        return result;
    }

    logos::expected<void*> eval_multiselect_hash(void* data, TinyObjectMap* node) noexcept {
        auto* keys_arr = static_cast<ObjectArray*>(get_child(node, KEYS));
        auto* vals_arr = static_cast<ObjectArray*>(get_child(node, EXPRESSIONS));
        LOGOS_TRY(auto* result, HermesCtrAccess::raw_object_map(result_));
        for (uint64_t i = 0; i < keys_arr->size(); ++i) {
            auto* key = keys_arr->slot(i, ast_base_)->as_ptr<ArenaString>(ast_base_);
            LOGOS_TRY(void* expr, resolve_slot(vals_arr->slot(i, ast_base_), ast_base_));
            LOGOS_TRY(void* val, eval(data, expr));
            LOGOS_TRY_VOID(put_value(result, key->view(), val));
        }
        return result;
    }

    logos::expected<void*> eval_function(void* data, TinyObjectMap* node) noexcept {
        auto* fname = node->slot(NAME, ast_base_)->as_ptr<ArenaString>(ast_base_);
        auto name = fname->view();
        auto* args = static_cast<ObjectArray*>(get_child(node, ARGS));

        if (name == "length") return fn_length(data, args);
        if (name == "type")   return fn_type(data, args);
        if (name == "keys")   return fn_keys(data, args);
        if (name == "values") return fn_values(data, args);
        if (name == "to_string") return fn_to_string(data, args);
        if (name == "not_null")  return fn_not_null(data, args);
        if (name == "abs")       return fn_abs(data, args);
        if (name == "sort")      return fn_sort(data, args);
        if (name == "reverse")   return fn_reverse(data, args);
        if (name == "contains")  return fn_contains(data, args);

        return nullptr; // Unknown function.
    }

    logos::expected<void*> eval_hermes_value(TinyObjectMap* node) noexcept {
        if (!node->has_key(VALUE)) return nullptr;
        AnyVal val = node->get(VALUE, ast_base_);
        if (val.is_value()) return resolve_embedded(node->slot(VALUE, ast_base_));
        if (val.is_pointer()) return node->slot(VALUE, ast_base_)->as_ptr<void>(ast_base_);
        return nullptr;
    }

    // --- Built-in functions ---

    logos::expected<void*> fn_length(void* data, ObjectArray* args) noexcept {
        void* arg_raw = (args && args->size() > 0) ? resolve_slot(args->slot(0, ast_base_), ast_base_).value_or(nullptr) : nullptr;
        void* arg;
        if (arg_raw) {
            LOGOS_TRY(arg, eval(data, arg_raw));
        } else {
            arg = data;
        }
        if (!arg) {
            LOGOS_TRY(auto* v, HermesCtrAccess::make_value<int32_t>(result_, 0));
            return v;
        }
        auto* b = static_cast<const uint8_t*>(arg);
        TypeTag tag = TypeTag::read_before(b);
        if (tag.type_code() == type_hash::Varchar) {
            LOGOS_TRY(auto* v, HermesCtrAccess::make_value<int32_t>(result_, static_cast<int32_t>(static_cast<const ArenaString*>(arg)->length())));
            return v;
        }
        if (tag.descriptor() == TagDescriptor::Array) {
            LOGOS_TRY(auto* v, HermesCtrAccess::make_value<int32_t>(result_, static_cast<int32_t>(static_cast<const ObjectArray*>(arg)->size())));
            return v;
        }
        if (tag.descriptor() == TagDescriptor::Map && tag.type_code() == type_hash::ObjectMap) {
            LOGOS_TRY(auto* v, HermesCtrAccess::make_value<int32_t>(result_, static_cast<int32_t>(static_cast<const ObjectMap*>(arg)->size())));
            return v;
        }
        LOGOS_TRY(auto* v, HermesCtrAccess::make_value<int32_t>(result_, 0));
        return v;
    }

    logos::expected<void*> fn_type(void* data, ObjectArray* args) noexcept {
        void* arg_raw = (args && args->size() > 0) ? resolve_slot(args->slot(0, ast_base_), ast_base_).value_or(nullptr) : nullptr;
        void* arg;
        if (arg_raw) {
            LOGOS_TRY(arg, eval(data, arg_raw));
        } else {
            arg = data;
        }
        if (!arg) {
            LOGOS_TRY(auto* s, HermesCtrAccess::raw_string(result_, "null"));
            return s;
        }
        auto* b = static_cast<const uint8_t*>(arg);
        TypeTag tag = TypeTag::read_before(b);
        switch (tag.type_code()) {
            case type_hash::Varchar: {
                LOGOS_TRY(auto* s, HermesCtrAccess::raw_string(result_, "string"));
                return s;
            }
            case type_hash::Integer: case type_hash::BigInt:
            case type_hash::Real: case type_hash::Double: {
                LOGOS_TRY(auto* s, HermesCtrAccess::raw_string(result_, "number"));
                return s;
            }
            case type_hash::Boolean: {
                LOGOS_TRY(auto* s, HermesCtrAccess::raw_string(result_, "boolean"));
                return s;
            }
            default:
                if (tag.descriptor() == TagDescriptor::Array) {
                    LOGOS_TRY(auto* s, HermesCtrAccess::raw_string(result_, "array"));
                    return s;
                }
                if (tag.descriptor() == TagDescriptor::Map) {
                    LOGOS_TRY(auto* s, HermesCtrAccess::raw_string(result_, "object"));
                    return s;
                }
                LOGOS_TRY(auto* s, HermesCtrAccess::raw_string(result_, "unknown"));
                return s;
        }
    }

    logos::expected<void*> fn_keys(void* data, ObjectArray* args) noexcept {
        void* arg_raw = (args && args->size() > 0) ? resolve_slot(args->slot(0, ast_base_), ast_base_).value_or(nullptr) : nullptr;
        void* arg;
        if (arg_raw) {
            LOGOS_TRY(arg, eval(data, arg_raw));
        } else {
            arg = data;
        }
        if (!arg) {
            LOGOS_TRY(auto* arr, HermesCtrAccess::raw_array(result_, 0));
            return arr;
        }
        auto* map = static_cast<ObjectMap*>(arg);
        LOGOS_TRY(auto* result, HermesCtrAccess::raw_array(result_));
        logos::expected<void> status{};
        map->for_each([&](ArenaString* key, AnyVal*) {
            if (!status) return;
            auto ks_res = HermesCtrAccess::raw_string(result_, key->view());
            if (!ks_res) { status = std::unexpected(std::move(ks_res.error())); return; }
            auto push_res = result->push_back(AnyVal{}, HermesCtrAccess::arena(result_));
            if (!push_res) { status = std::unexpected(std::move(push_res.error())); return; }
            result->slot(result->size() - 1, HermesCtrAccess::base(result_))->set_pointer(*ks_res, HermesCtrAccess::base(result_));
        }, data_base_);
        LOGOS_TRY_VOID(std::move(status));
        return static_cast<void*>(result);
    }

    logos::expected<void*> fn_values(void* data, ObjectArray* args) noexcept {
        void* arg_raw = (args && args->size() > 0) ? resolve_slot(args->slot(0, ast_base_), ast_base_).value_or(nullptr) : nullptr;
        void* arg;
        if (arg_raw) {
            LOGOS_TRY(arg, eval(data, arg_raw));
        } else {
            arg = data;
        }
        return eval_hash_wildcard(arg);
    }

    logos::expected<void*> fn_to_string(void* data, ObjectArray* args) noexcept {
        void* arg_raw = (args && args->size() > 0) ? resolve_slot(args->slot(0, ast_base_), ast_base_).value_or(nullptr) : nullptr;
        void* arg;
        if (arg_raw) {
            LOGOS_TRY(arg, eval(data, arg_raw));
        } else {
            arg = data;
        }
        auto sv = to_string(arg);
        if (!sv.empty()) {
            LOGOS_TRY(auto* s, HermesCtrAccess::raw_string(result_, sv));
            return s;
        }
        double d = to_double(arg);
        if (!std::isnan(d)) {
            char buf[32];
            int n = std::snprintf(buf, sizeof(buf), "%g", d);
            LOGOS_TRY(auto* s, HermesCtrAccess::raw_string(result_, std::string_view(buf, n)));
            return s;
        }
        LOGOS_TRY(auto* s, HermesCtrAccess::raw_string(result_, "null"));
        return s;
    }

    logos::expected<void*> fn_not_null(void* data, ObjectArray* args) noexcept {
        if (!args) return nullptr;
        for (uint64_t i = 0; i < args->size(); ++i) {
            LOGOS_TRY(void* slot_val, resolve_slot(args->slot(i, ast_base_), ast_base_));
            LOGOS_TRY(void* val, eval(data, slot_val));
            if (val) return val;
        }
        return nullptr;
    }

    logos::expected<void*> fn_abs(void* data, ObjectArray* args) noexcept {
        void* arg_raw = (args && args->size() > 0) ? resolve_slot(args->slot(0, ast_base_), ast_base_).value_or(nullptr) : nullptr;
        void* arg;
        if (arg_raw) {
            LOGOS_TRY(arg, eval(data, arg_raw));
        } else {
            arg = data;
        }
        double d = to_double(arg);
        if (!std::isnan(d)) {
            LOGOS_TRY(auto* v, HermesCtrAccess::make_value<double>(result_, std::abs(d)));
            return v;
        }
        return nullptr;
    }

    logos::expected<void*> fn_sort(void* data, ObjectArray* args) noexcept {
        void* arg_raw = (args && args->size() > 0) ? resolve_slot(args->slot(0, ast_base_), ast_base_).value_or(nullptr) : nullptr;
        void* arg;
        if (arg_raw) {
            LOGOS_TRY(arg, eval(data, arg_raw));
        } else {
            arg = data;
        }
        if (!arg) {
            LOGOS_TRY(auto* arr, HermesCtrAccess::raw_array(result_, 0));
            return arr;
        }
        auto* arr = static_cast<ObjectArray*>(arg);
        // Collect values, sort, rebuild.
        std::vector<void*> items;
        for (uint64_t i = 0; i < arr->size(); ++i) {
            auto slot_res = resolve_slot(arr->slot(i, data_base_), data_base_);
            items.push_back(slot_res.value_or(nullptr));
        }
        std::sort(items.begin(), items.end(), [this](void* a, void* b) {
            double da = to_double(a), db = to_double(b);
            if (!std::isnan(da) && !std::isnan(db)) return da < db;
            return to_string(a) < to_string(b);
        });
        LOGOS_TRY(auto* result, HermesCtrAccess::raw_array(result_));
        for (auto* item : items) {
            LOGOS_TRY_VOID(push_value(result, item));
        }
        return static_cast<void*>(result);
    }

    logos::expected<void*> fn_reverse(void* data, ObjectArray* args) noexcept {
        void* arg_raw = (args && args->size() > 0) ? resolve_slot(args->slot(0, ast_base_), ast_base_).value_or(nullptr) : nullptr;
        void* arg;
        if (arg_raw) {
            LOGOS_TRY(arg, eval(data, arg_raw));
        } else {
            arg = data;
        }
        if (!arg) {
            LOGOS_TRY(auto* arr, HermesCtrAccess::raw_array(result_, 0));
            return arr;
        }
        auto* arr = static_cast<ObjectArray*>(arg);
        LOGOS_TRY(auto* result, HermesCtrAccess::raw_array(result_));
        for (int64_t i = arr->size() - 1; i >= 0; --i) {
            LOGOS_TRY(void* sv, resolve_slot(arr->slot(i, data_base_), data_base_));
            LOGOS_TRY_VOID(push_value(result, sv));
        }
        return static_cast<void*>(result);
    }

    logos::expected<void*> fn_contains(void* data, ObjectArray* args) noexcept {
        if (!args || args->size() < 2) return nullptr;
        LOGOS_TRY(void* subj_raw, resolve_slot(args->slot(0, ast_base_), ast_base_));
        LOGOS_TRY(void* subject, eval(data, subj_raw));
        LOGOS_TRY(void* srch_raw, resolve_slot(args->slot(1, ast_base_), ast_base_));
        LOGOS_TRY(void* search, eval(data, srch_raw));
        bool found = false;
        if (subject) {
            auto* b = static_cast<const uint8_t*>(subject);
            TypeTag tag = TypeTag::read_before(b);
            if (tag.type_code() == type_hash::Varchar && search) {
                auto sv = static_cast<const ArenaString*>(subject)->view();
                auto needle = to_string(search);
                found = sv.find(needle) != std::string_view::npos;
            } else if (tag.descriptor() == TagDescriptor::Array) {
                auto* arr = static_cast<ObjectArray*>(subject);
                double search_d = to_double(search);
                auto search_s = to_string(search);
                for (uint64_t i = 0; i < arr->size(); ++i) {
                    LOGOS_TRY(void* elem, resolve_slot(arr->slot(i, data_base_), data_base_));
                    if (!std::isnan(search_d) && to_double(elem) == search_d) { found = true; break; }
                    if (!search_s.empty() && to_string(elem) == search_s) { found = true; break; }
                }
            }
        }
        TypeTag btag(type_hash::Boolean, TagDescriptor::Data);
        LOGOS_TRY(void* mem, HermesCtrAccess::arena(result_).allocate(1, 2, btag));
        *static_cast<uint8_t*>(mem) = found ? 1 : 0;
        return mem;
    }

    // --- Helpers ---

    // Copy a value into the result arena and push into array.
    // This ensures no cross-arena pointers.
    logos::expected<void> push_value(ObjectArray* arr, void* val) noexcept {
        if (!val) {
            LOGOS_TRY_VOID(arr->push_back(AnyVal{}, HermesCtrAccess::arena(result_)));
            return {};
        }
        auto* b = static_cast<const uint8_t*>(val);
        TypeTag tag = TypeTag::read_before(b);
        uint64_t tc = tag.type_code();
        if (tc == type_hash::Integer) {
            LOGOS_TRY_VOID(arr->push_back(AnyVal::from_value(*static_cast<const int32_t*>(val), tc), HermesCtrAccess::arena(result_)));
        } else if (tc == type_hash::Boolean) {
            LOGOS_TRY_VOID(arr->push_back(AnyVal::from_value(*static_cast<const uint8_t*>(val), tc), HermesCtrAccess::arena(result_)));
        } else if (tc == type_hash::Real) {
            LOGOS_TRY_VOID(arr->push_back(AnyVal::from_value(*static_cast<const float*>(val), tc), HermesCtrAccess::arena(result_)));
        } else if (tc == type_hash::Varchar) {
            auto* s = static_cast<const ArenaString*>(val);
            LOGOS_TRY(auto* copy, HermesCtrAccess::raw_string(result_, s->view()));
            LOGOS_TRY_VOID(arr->push_back(AnyVal{}, HermesCtrAccess::arena(result_)));
            arr->slot(arr->size() - 1, HermesCtrAccess::base(result_))->set_pointer(copy, HermesCtrAccess::base(result_));
        } else {
            // For complex objects (arrays, maps), store pointer directly.
            // This is safe only if val is already in result_ arena.
            LOGOS_TRY_VOID(arr->push_back(AnyVal{}, HermesCtrAccess::arena(result_)));
            arr->slot(arr->size() - 1, HermesCtrAccess::base(result_))->set_pointer(val, HermesCtrAccess::base(result_));
        }
        return {};
    }

    logos::expected<void> put_value(ObjectMap* map, std::string_view key, void* val) noexcept {
        if (!val) {
            LOGOS_TRY_VOID(map->put(key, AnyVal{}, HermesCtrAccess::arena(result_)));
            return {};
        }
        auto* b = static_cast<const uint8_t*>(val);
        TypeTag tag = TypeTag::read_before(b);
        uint64_t tc = tag.type_code();
        if (tc == type_hash::Integer) {
            LOGOS_TRY_VOID(map->put(key, AnyVal::from_value(*static_cast<const int32_t*>(val), tc), HermesCtrAccess::arena(result_)));
        } else if (tc == type_hash::Boolean) {
            LOGOS_TRY_VOID(map->put(key, AnyVal::from_value(*static_cast<const uint8_t*>(val), tc), HermesCtrAccess::arena(result_)));
        } else if (tc == type_hash::Real) {
            LOGOS_TRY_VOID(map->put(key, AnyVal::from_value(*static_cast<const float*>(val), tc), HermesCtrAccess::arena(result_)));
        } else if (tc == type_hash::Varchar) {
            auto* s = static_cast<const ArenaString*>(val);
            LOGOS_TRY(auto* copy, HermesCtrAccess::raw_string(result_, s->view()));
            LOGOS_TRY_VOID(map->put(key, AnyVal{}, HermesCtrAccess::arena(result_)));
            map->get_slot(key, HermesCtrAccess::base(result_))->set_pointer(copy, HermesCtrAccess::base(result_));
        } else {
            LOGOS_TRY_VOID(map->put(key, AnyVal{}, HermesCtrAccess::arena(result_)));
            map->get_slot(key, HermesCtrAccess::base(result_))->set_pointer(val, HermesCtrAccess::base(result_));
        }
        return {};
    }
};

// ============================================================================
// Public API
// ============================================================================

logos::expected<HermesCtr> parse_path(std::string_view expr) noexcept {
    LOGOS_TRY(auto doc, make_doc(65536));
    PathParser parser(expr, doc);
    LOGOS_TRY(void* ast, parser.parse());
    HermesCtrAccess::set_root_offset(doc, HermesCtrAccess::offset_of(doc, ast));
    return doc;
}

logos::expected<HermesCtr> eval_path(const HermesCtr& data,
                                      std::string_view expr) noexcept {
    LOGOS_TRY(auto ast_doc, parse_path(expr));

    LOGOS_TRY(HermesCtr result, make_doc());
    PathEvaluator evaluator(result, HermesCtrAccess::base(data),
                                    HermesCtrAccess::base(ast_doc));
    void* data_root = HermesCtrAccess::root<void>(data);
    void* ast_root  = HermesCtrAccess::root<void>(ast_doc);
    LOGOS_TRY(void* val, evaluator.eval(data_root, ast_root));
    if (val) {
        auto* vp = static_cast<uint8_t*>(val);
        auto* db = HermesCtrAccess::base(data);
        size_t data_used = HermesCtrAccess::arena(data).total_used();
        if (vp >= db && vp < db + data_used) {
            arena_offset_t off{static_cast<arena_offset_t::value_type>(vp - db)};
            result = HermesCtr(HermesCtrView(data.holder()));
            HermesCtrAccess::set_root_override(result, off);
        } else {
            auto* rb = HermesCtrAccess::base(result);
            arena_offset_t off{static_cast<arena_offset_t::value_type>(vp - rb)};
            HermesCtrAccess::set_root_offset(result, off);
        }
    }
    return result;
}

logos::expected<HermesCtr> eval_path_ast(void* data_root, void* ast_root,
                                          Arena& /*data_arena*/) noexcept {
    LOGOS_TRY(auto result, make_doc());
    PathEvaluator evaluator(result, nullptr, nullptr);
    LOGOS_TRY(void* val, evaluator.eval(data_root, ast_root));
    if (val) HermesCtrAccess::set_root_offset(result, HermesCtrAccess::offset_of(result, val));
    return result;
}

} // namespace logos::hermes
