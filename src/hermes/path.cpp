// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos

#include <logos/hermes/path.hpp>
#include <logos/hermes/text_parser.hpp>
#include <logos/hermes/arena_string.hpp>
#include <logos/hermes/arena_value.hpp>
#include <logos/hermes/object_array.hpp>
#include <logos/hermes/object_map.hpp>
#include <logos/hermes/tiny_object_map.hpp>
#include <logos/hermes/compound_types.hpp>

#include <charconv>
#include <stdexcept>
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

    void* parse() {
        skip();
        if (at_end()) return make_node(CURRENT_NODE); // Empty → identity
        void* result = parse_pipe();
        skip();
        return result;
    }

private:
    std::string_view text_;
    size_t pos_;
    HermesCtr& doc_;

    // --- Lexer helpers ---

    bool at_end() const { return pos_ >= text_.size(); }
    char peek() const { return at_end() ? '\0' : text_[pos_]; }
    char peek(size_t off) const { size_t p = pos_ + off; return p >= text_.size() ? '\0' : text_[p]; }
    char advance() { return text_[pos_++]; }

    void skip() {
        while (pos_ < text_.size() && (text_[pos_] == ' ' || text_[pos_] == '\t' || text_[pos_] == '\n' || text_[pos_] == '\r'))
            ++pos_;
    }

    void expect(char c) {
        skip();
        if (at_end() || text_[pos_] != c) error(std::string("expected '") + c + "'");
        ++pos_;
    }

    bool try_char(char c) {
        skip();
        if (!at_end() && text_[pos_] == c) { ++pos_; return true; }
        return false;
    }

    bool is_ident_start(char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_'; }
    bool is_ident_char(char c) { return is_ident_start(c) || (c >= '0' && c <= '9'); }
    bool is_digit(char c) { return c >= '0' && c <= '9'; }

    std::string read_identifier() {
        skip();
        size_t start = pos_;
        if (at_end() || !is_ident_start(peek())) error("expected identifier");
        while (pos_ < text_.size() && is_ident_char(text_[pos_])) ++pos_;
        return std::string(text_.substr(start, pos_ - start));
    }

    [[noreturn]] void error(const std::string& msg) {
        throw std::runtime_error("HermesPath parse error at pos " + std::to_string(pos_) + ": " + msg);
    }

    // --- AST node builders ---

    void* make_node(int32_t code) {
        auto* m = doc_.make_tiny_map(4);
        m->put(CODE, TaggedPtr::from_value(code, type_hash::Integer), doc_.arena());
        return m;
    }

    void* make_binary(int32_t code, void* left, void* right) {
        auto* m = doc_.make_tiny_map(4);
        m->put(CODE, TaggedPtr::from_value(code, type_hash::Integer), doc_.arena());
        m->put(LEFT, TaggedPtr{}, doc_.arena());
        m->slot(LEFT)->set_pointer(left);
        m->put(RIGHT, TaggedPtr{}, doc_.arena());
        m->slot(RIGHT)->set_pointer(right);
        return m;
    }

    void* make_named(int32_t code, std::string_view name) {
        auto* m = doc_.make_tiny_map(4);
        m->put(CODE, TaggedPtr::from_value(code, type_hash::Integer), doc_.arena());
        auto* s = doc_.make_string(name);
        m->put(NAME, TaggedPtr{}, doc_.arena());
        m->slot(NAME)->set_pointer(s);
        return m;
    }

    // --- Precedence levels ---

    // pipe: or ('|' or)*
    void* parse_pipe() {
        void* left = parse_or();
        while (true) {
            skip();
            // | but not ||
            if (!at_end() && text_[pos_] == '|' && peek(1) != '|') {
                ++pos_;
                left = make_binary(PIPE, left, parse_or());
            } else break;
        }
        return left;
    }

    // or: and ('||' and)*
    void* parse_or() {
        void* left = parse_and();
        while (true) {
            skip();
            if (pos_ + 1 < text_.size() && text_[pos_] == '|' && text_[pos_ + 1] == '|') {
                pos_ += 2;
                left = make_binary(OR_EXPR, left, parse_and());
            } else break;
        }
        return left;
    }

    // and: comparator ('&&' comparator)*
    void* parse_and() {
        void* left = parse_comparator();
        while (true) {
            skip();
            if (pos_ + 1 < text_.size() && text_[pos_] == '&' && text_[pos_ + 1] == '&') {
                pos_ += 2;
                left = make_binary(AND_EXPR, left, parse_comparator());
            } else break;
        }
        return left;
    }

    // comparator: not (('<'|'<='|'=='|'>='|'>'|'!=') not)*
    void* parse_comparator() {
        void* left = parse_not();
        while (true) {
            skip();
            int32_t cmp = try_comparator();
            if (cmp >= 0) {
                void* right = parse_not();
                auto* m = doc_.make_tiny_map(6);
                m->put(CODE, TaggedPtr::from_value(int32_t(COMPARATOR_EXPR), type_hash::Integer), doc_.arena());
                m->put(LEFT, TaggedPtr{}, doc_.arena());
                m->slot(LEFT)->set_pointer(left);
                m->put(RIGHT, TaggedPtr{}, doc_.arena());
                m->slot(RIGHT)->set_pointer(right);
                m->put(COMPARATOR, TaggedPtr::from_value(cmp, type_hash::Integer), doc_.arena());
                left = m;
            } else break;
        }
        return left;
    }

    int32_t try_comparator() {
        if (at_end()) return -1;
        if (text_[pos_] == '<' && peek(1) == '=') { pos_ += 2; return CMP_LE; }
        if (text_[pos_] == '>' && peek(1) == '=') { pos_ += 2; return CMP_GE; }
        if (text_[pos_] == '=' && peek(1) == '=') { pos_ += 2; return CMP_EQ; }
        if (text_[pos_] == '!' && peek(1) == '=') { pos_ += 2; return CMP_NE; }
        if (text_[pos_] == '<') { ++pos_; return CMP_LT; }
        if (text_[pos_] == '>') { ++pos_; return CMP_GT; }
        return -1;
    }

    // not: '!' not | postfix
    void* parse_not() {
        skip();
        if (!at_end() && text_[pos_] == '!' && peek(1) != '=') {
            ++pos_;
            void* expr = parse_not();
            auto* m = doc_.make_tiny_map(4);
            m->put(CODE, TaggedPtr::from_value(int32_t(NOT_EXPR), type_hash::Integer), doc_.arena());
            m->put(RIGHT, TaggedPtr{}, doc_.arena());
            m->slot(RIGHT)->set_pointer(expr);
            return m;
        }
        return parse_postfix();
    }

    // postfix: primary ( '.' ident | '[' bracket ']' | '.*' )*
    void* parse_postfix() {
        void* node = parse_primary();
        while (true) {
            skip();
            if (at_end()) break;

            if (text_[pos_] == '.' && peek(1) == '*') {
                pos_ += 2;
                node = make_binary(HASH_WILDCARD, node, make_node(CURRENT_NODE));
            } else if (text_[pos_] == '.' && (is_ident_start(peek(1)) || peek(1) == '"' || peek(1) == '\'')) {
                ++pos_;
                void* right = parse_dot_rhs();
                node = make_binary(SUBEXPRESSION, node, right);
            } else if (text_[pos_] == '[') {
                void* bracket = parse_bracket();
                node = make_binary(INDEX_EXPRESSION, node, bracket);
            } else break;
        }
        return node;
    }

    void* parse_dot_rhs() {
        skip();
        // function call or identifier
        if (is_ident_start(peek())) {
            std::string name = read_identifier();
            skip();
            if (!at_end() && peek() == '(') {
                return parse_function_args(name);
            }
            return make_named(IDENTIFIER, name);
        }
        if (peek() == '"') {
            ++pos_;
            return make_named(RAW_STRING, parse_qstring());
        }
        if (peek() == '\'') {
            ++pos_;
            return make_named(RAW_STRING, parse_raw_str());
        }
        error("expected identifier after '.'");
    }

    // primary: identifier | function | '*' | '@' | literal | '(' expr ')' | '[' multiselect ']' | '{' hash '}'
    void* parse_primary() {
        skip();
        if (at_end()) error("unexpected end of expression");
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
            void* expr = parse_pipe();
            expect(')');
            auto* m = doc_.make_tiny_map(4);
            m->put(CODE, TaggedPtr::from_value(int32_t(PAREN), type_hash::Integer), doc_.arena());
            m->put(RIGHT, TaggedPtr{}, doc_.arena());
            m->slot(RIGHT)->set_pointer(expr);
            return m;
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
            return make_named(RAW_STRING, parse_qstring());
        }
        if (c == '\'') {
            ++pos_;
            return make_named(RAW_STRING, parse_raw_str());
        }
        if (is_ident_start(c)) {
            std::string name = read_identifier();
            // Check for function call.
            skip();
            if (!at_end() && peek() == '(') {
                return parse_function_args(name);
            }
            // Keywords: true, false, null as literals.
            if (name == "true" || name == "false" || name == "null") {
                auto* m = doc_.make_tiny_map(4);
                m->put(CODE, TaggedPtr::from_value(int32_t(HERMES_VALUE), type_hash::Integer), doc_.arena());
                if (name == "true") {
                    auto* v = doc_.make_value<uint8_t>(1);
                    m->put(VALUE, TaggedPtr{}, doc_.arena());
                    m->slot(VALUE)->set_pointer(v);
                } else if (name == "false") {
                    auto* v = doc_.make_value<uint8_t>(0);
                    m->put(VALUE, TaggedPtr{}, doc_.arena());
                    m->slot(VALUE)->set_pointer(v);
                }
                // null: VALUE stays null
                return m;
            }
            return make_named(IDENTIFIER, name);
        }
        if (is_digit(c) || c == '-') {
            return parse_number_literal();
        }

        error(std::string("unexpected '") + c + "'");
    }

    // --- Bracket specifiers ---

    void* parse_bracket() {
        expect('[');
        skip();

        // Filter: [? expr ]
        if (peek() == '?') {
            ++pos_;
            void* expr = parse_pipe();
            expect(']');
            auto* m = doc_.make_tiny_map(4);
            m->put(CODE, TaggedPtr::from_value(int32_t(FILTER), type_hash::Integer), doc_.arena());
            m->put(RIGHT, TaggedPtr{}, doc_.arena());
            m->slot(RIGHT)->set_pointer(expr);
            return m;
        }

        // Flatten: []
        if (peek() == ']') {
            ++pos_;
            return make_node(FLATTEN);
        }

        // Wildcard: [*]
        if (peek() == '*') {
            ++pos_;
            expect(']');
            return make_node(LIST_WILDCARD);
        }

        // Slice or array item.
        // Try to parse optional int, then check for ':'.
        bool has_start = false;
        int64_t start_val = 0;
        if (peek() == ':' || is_digit(peek()) || peek() == '-') {
            if (peek() != ':') {
                start_val = parse_int64();
                has_start = true;
            }
        }

        skip();
        if (peek() == ':') {
            // Slice: [start:stop:step]
            return parse_slice(has_start, start_val);
        }

        // Array item: [index]
        if (!has_start) error("expected index or slice");
        expect(']');
        auto* m = doc_.make_tiny_map(4);
        m->put(CODE, TaggedPtr::from_value(int32_t(ARRAY_ITEM), type_hash::Integer), doc_.arena());
        m->put(VALUE, TaggedPtr::from_value(int32_t(start_val), type_hash::Integer), doc_.arena());
        return m;
    }

    void* parse_slice(bool has_start, int64_t start_val) {
        auto* m = doc_.make_tiny_map(6);
        m->put(CODE, TaggedPtr::from_value(int32_t(SLICE), type_hash::Integer), doc_.arena());
        if (has_start) {
            m->put(START, TaggedPtr::from_value(int32_t(start_val), type_hash::Integer), doc_.arena());
        }

        expect(':'); // First colon.
        skip();

        // Optional stop.
        if (peek() != ':' && peek() != ']') {
            int64_t stop = parse_int64();
            m->put(STOP, TaggedPtr::from_value(int32_t(stop), type_hash::Integer), doc_.arena());
        }

        skip();
        if (peek() == ':') {
            ++pos_;
            skip();
            if (peek() != ']') {
                int64_t step = parse_int64();
                m->put(STEP, TaggedPtr::from_value(int32_t(step), type_hash::Integer), doc_.arena());
            }
        }

        expect(']');
        return m;
    }

    int64_t parse_int64() {
        skip();
        bool neg = false;
        if (peek() == '-') { neg = true; ++pos_; }
        int64_t val = 0;
        if (!is_digit(peek())) error("expected integer");
        while (!at_end() && is_digit(peek())) {
            val = val * 10 + (advance() - '0');
        }
        return neg ? -val : val;
    }

    // --- Function calls ---

    void* parse_function_args(const std::string& name) {
        expect('(');
        auto* m = doc_.make_tiny_map(4);
        m->put(CODE, TaggedPtr::from_value(int32_t(FUNCTION_CALL), type_hash::Integer), doc_.arena());
        auto* fname = doc_.make_string(name);
        m->put(NAME, TaggedPtr{}, doc_.arena());
        m->slot(NAME)->set_pointer(fname);

        auto* args = doc_.make_array();
        skip();
        if (peek() != ')') {
            while (true) {
                skip();
                void* arg;
                if (peek() == '&') {
                    ++pos_;
                    void* expr = parse_pipe();
                    auto* ea = doc_.make_tiny_map(4);
                    ea->put(CODE, TaggedPtr::from_value(int32_t(EXPR_ARGUMENT), type_hash::Integer), doc_.arena());
                    ea->put(RIGHT, TaggedPtr{}, doc_.arena());
                    ea->slot(RIGHT)->set_pointer(expr);
                    arg = ea;
                } else {
                    arg = parse_pipe();
                }
                args->push_back(TaggedPtr{}, doc_.arena());
                args->slot(args->size() - 1)->set_pointer(arg);
                skip();
                if (peek() == ')') break;
                expect(',');
            }
        }
        expect(')');
        m->put(ARGS, TaggedPtr{}, doc_.arena());
        m->slot(ARGS)->set_pointer(args);
        return m;
    }

    // --- Multiselect ---

    void* parse_multiselect_list() {
        expect('[');
        auto* exprs = doc_.make_array();
        while (true) {
            void* e = parse_pipe();
            exprs->push_back(TaggedPtr{}, doc_.arena());
            exprs->slot(exprs->size() - 1)->set_pointer(e);
            skip();
            if (peek() == ']') { ++pos_; break; }
            expect(',');
        }
        auto* m = doc_.make_tiny_map(4);
        m->put(CODE, TaggedPtr::from_value(int32_t(MULTISELECT_LIST), type_hash::Integer), doc_.arena());
        m->put(EXPRESSIONS, TaggedPtr{}, doc_.arena());
        m->slot(EXPRESSIONS)->set_pointer(exprs);
        return m;
    }

    void* parse_multiselect_hash() {
        expect('{');
        auto* keys_arr = doc_.make_array();
        auto* vals_arr = doc_.make_array();
        while (true) {
            skip();
            std::string key = read_identifier();
            expect(':');
            void* val = parse_pipe();

            auto* ks = doc_.make_string(key);
            keys_arr->push_back(TaggedPtr{}, doc_.arena());
            keys_arr->slot(keys_arr->size() - 1)->set_pointer(ks);
            vals_arr->push_back(TaggedPtr{}, doc_.arena());
            vals_arr->slot(vals_arr->size() - 1)->set_pointer(val);

            skip();
            if (peek() == '}') { ++pos_; break; }
            expect(',');
        }
        auto* m = doc_.make_tiny_map(6);
        m->put(CODE, TaggedPtr::from_value(int32_t(MULTISELECT_HASH), type_hash::Integer), doc_.arena());
        m->put(KEYS, TaggedPtr{}, doc_.arena());
        m->slot(KEYS)->set_pointer(keys_arr);
        m->put(EXPRESSIONS, TaggedPtr{}, doc_.arena());
        m->slot(EXPRESSIONS)->set_pointer(vals_arr);
        return m;
    }

    // --- Literals ---

    void* parse_hermes_value_literal() {
        // Capture remaining text and parse as Hermes value.
        // We need to find how much the Hermes parser consumes.
        // Simple approach: parse inline and wrap.
        // For now, support basic literals: numbers, strings, bools, arrays, maps.
        skip();
        size_t start = pos_;
        // Read until we hit something that doesn't belong in a value.
        // This is tricky — delegate to the Hermes value parser mentally.
        // For simplicity, treat ^value as parse_primary of the hermes format.
        void* val = parse_primary(); // Reuse our primary for basic types.
        auto* m = doc_.make_tiny_map(4);
        m->put(CODE, TaggedPtr::from_value(int32_t(HERMES_VALUE), type_hash::Integer), doc_.arena());
        m->put(VALUE, TaggedPtr{}, doc_.arena());
        m->slot(VALUE)->set_pointer(val);
        (void)start;
        return m;
    }

    void* parse_number_literal() {
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
            auto* v = doc_.make_value<double>(dval);
            auto* m = doc_.make_tiny_map(4);
            m->put(CODE, TaggedPtr::from_value(int32_t(HERMES_VALUE), type_hash::Integer), doc_.arena());
            m->put(VALUE, TaggedPtr{}, doc_.arena());
            m->slot(VALUE)->set_pointer(v);
            return m;
        }
        int32_t ival = neg ? -int32_t(val) : int32_t(val);
        auto* m = doc_.make_tiny_map(4);
        m->put(CODE, TaggedPtr::from_value(int32_t(HERMES_VALUE), type_hash::Integer), doc_.arena());
        m->put(VALUE, TaggedPtr::from_value(ival, type_hash::Integer), doc_.arena());
        return m;
    }

    std::string parse_qstring() {
        std::string result;
        while (!at_end() && peek() != '"') {
            if (peek() == '\\') { ++pos_; result += advance(); }
            else result += advance();
        }
        if (at_end()) error("unterminated string");
        ++pos_;
        return result;
    }

    std::string parse_raw_str() {
        std::string result;
        while (!at_end() && peek() != '\'') {
            if (peek() == '\\' && peek(1) == '\'') { pos_ += 2; result += '\''; }
            else result += advance();
        }
        if (at_end()) error("unterminated raw string");
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
    PathEvaluator(HermesCtr& result) : result_(result) {}

    // Evaluate an AST node against a data value. Returns arena pointer to result.
    void* eval(void* data, void* ast_node) {
        auto* node = static_cast<TinyObjectMap*>(ast_node);
        int32_t code = node->get(CODE).as_value<int32_t>();

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

    void* get_child(TinyObjectMap* node, uint8_t key) {
        TaggedPtr* slot = node->slot(key);
        if (!slot || slot->is_null()) return nullptr;
        if (slot->is_pointer()) return slot->as_ptr<void>();
        return nullptr; // Embedded values are not child nodes.
    }

    bool is_truthy(void* val) {
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

    // --- Evaluation methods ---

    void* eval_identifier(void* data, TinyObjectMap* node) {
        if (!data) return nullptr;
        TaggedPtr* name_slot = node->slot(NAME);
        if (!name_slot || name_slot->is_null()) return nullptr;
        auto* name = name_slot->as_ptr<ArenaString>();
        auto key = name->view();

        auto* bytes = static_cast<const uint8_t*>(data);
        TypeTag tag = TypeTag::read_before(bytes);
        if (tag.descriptor() == TagDescriptor::Map && tag.type_code() == type_hash::ObjectMap) {
            auto* map = static_cast<ObjectMap*>(data);
            TaggedPtr* slot = map->get_slot(key);
            if (!slot) return nullptr;
            if (slot->is_value()) return resolve_embedded(slot);
            if (slot->is_pointer()) return slot->as_ptr<void>();
            return nullptr;
        }
        return nullptr;
    }

    // Resolve a TaggedPtr slot. For embedded values, materializes in result arena.
    // For pointer-mode, returns the pointed-to object directly (caller must
    // use copy_to_result() if storing in result arena containers).
    void* resolve_embedded(TaggedPtr* slot) {
        if (slot->is_null()) return nullptr;
        if (slot->is_pointer()) return slot->as_ptr<void>();
        // Embedded value — allocate in result arena.
        uint8_t th = slot->value_type_hash();
        switch (th) {
            case type_hash::Integer:
                return result_.make_value<int32_t>(slot->as_value<int32_t>());
            case type_hash::UInteger:
                return result_.make_value<uint32_t>(slot->as_value<uint32_t>());
            case type_hash::Boolean: {
                TypeTag tag(type_hash::Boolean, TagDescriptor::Data);
                void* mem = result_.arena().allocate(1, 2, tag);
                *static_cast<uint8_t*>(mem) = slot->as_value<uint8_t>();
                return mem;
            }
            case type_hash::Real:
                return result_.make_value<float>(slot->as_value<float>());
            case type_hash::SmallInt:
                return result_.make_value<int16_t>(slot->as_value<int16_t>());
            case type_hash::TinyInt:
                return result_.make_value<int8_t>(slot->as_value<int8_t>());
            default: return nullptr;
        }
    }

    void* eval_subexpression(void* data, TinyObjectMap* node) {
        void* left_result = eval(data, get_child(node, LEFT));
        return eval(left_result, get_child(node, RIGHT));
    }

    void* eval_index(void* data, TinyObjectMap* node) {
        void* left_result = eval(data, get_child(node, LEFT));
        return eval(left_result, get_child(node, RIGHT));
    }

    void* eval_array_item(void* data, TinyObjectMap* node) {
        if (!data) return nullptr;
        auto* bytes = static_cast<const uint8_t*>(data);
        TypeTag tag = TypeTag::read_before(bytes);
        if (tag.descriptor() != TagDescriptor::Array || tag.type_code() != type_hash::ObjectArray)
            return nullptr;
        auto* arr = static_cast<ObjectArray*>(data);
        int32_t idx = node->get(VALUE).as_value<int32_t>();
        if (idx < 0) idx += static_cast<int32_t>(arr->size());
        if (idx < 0 || idx >= static_cast<int32_t>(arr->size())) return nullptr;
        return resolve_embedded(arr->slot(idx));
    }

    void* eval_flatten(void* data) {
        if (!data) return nullptr;
        auto* bytes = static_cast<const uint8_t*>(data);
        TypeTag tag = TypeTag::read_before(bytes);
        if (tag.descriptor() != TagDescriptor::Array) return nullptr;
        auto* arr = static_cast<ObjectArray*>(data);
        auto* result = result_.make_array();
        for (uint64_t i = 0; i < arr->size(); ++i) {
            void* elem = resolve_embedded(arr->slot(i));
            if (!elem) continue;
            auto* eb = static_cast<const uint8_t*>(elem);
            TypeTag et = TypeTag::read_before(eb);
            if (et.descriptor() == TagDescriptor::Array && et.type_code() == type_hash::ObjectArray) {
                auto* inner = static_cast<ObjectArray*>(elem);
                for (uint64_t j = 0; j < inner->size(); ++j) {
                    void* ie = resolve_embedded(inner->slot(j));
                    push_value(result, ie);
                }
            } else {
                push_value(result, elem);
            }
        }
        return result;
    }

    void* eval_slice(void* data, TinyObjectMap* node) {
        if (!data) return nullptr;
        auto* arr = static_cast<ObjectArray*>(data);
        int64_t len = arr->size();
        int64_t start = node->has_key(START) ? node->get(START).as_value<int32_t>() : 0;
        int64_t stop = node->has_key(STOP) ? node->get(STOP).as_value<int32_t>() : len;
        int64_t step = node->has_key(STEP) ? node->get(STEP).as_value<int32_t>() : 1;
        if (step == 0) return nullptr;

        if (start < 0) start += len;
        if (stop < 0) stop += len;
        start = std::clamp(start, int64_t(0), len);
        stop = std::clamp(stop, int64_t(0), len);

        auto* result = result_.make_array();
        if (step > 0) {
            for (int64_t i = start; i < stop; i += step)
                push_value(result, resolve_embedded(arr->slot(i)));
        } else {
            for (int64_t i = start; i > stop; i += step)
                push_value(result, resolve_embedded(arr->slot(i)));
        }
        return result;
    }

    void* eval_filter(void* data, TinyObjectMap* node) {
        if (!data) return nullptr;
        auto* arr = static_cast<ObjectArray*>(data);
        auto* result = result_.make_array();
        void* filter_ast = get_child(node, RIGHT);
        for (uint64_t i = 0; i < arr->size(); ++i) {
            void* elem = resolve_embedded(arr->slot(i));
            void* test = eval(elem, filter_ast);
            if (is_truthy(test)) push_value(result, elem);
        }
        return result;
    }

    void* eval_list_wildcard(void* data) {
        if (!data) return nullptr;
        auto* arr = static_cast<ObjectArray*>(data);
        auto* result = result_.make_array();
        for (uint64_t i = 0; i < arr->size(); ++i)
            push_value(result, resolve_embedded(arr->slot(i)));
        return result;
    }

    void* eval_hash_wildcard(void* data) {
        if (!data) return nullptr;
        auto* bytes = static_cast<const uint8_t*>(data);
        TypeTag tag = TypeTag::read_before(bytes);
        if (tag.descriptor() == TagDescriptor::Map && tag.type_code() == type_hash::ObjectMap) {
            auto* map = static_cast<ObjectMap*>(data);
            auto* result = result_.make_array();
            map->for_each([&](ArenaString*, TaggedPtr* val) {
                push_value(result, resolve_embedded(val));
            });
            return result;
        }
        return nullptr;
    }

    void* eval_comparator(void* data, TinyObjectMap* node) {
        void* lv = eval(data, get_child(node, LEFT));
        void* rv = eval(data, get_child(node, RIGHT));
        int32_t cmp = node->get(COMPARATOR).as_value<int32_t>();
        bool result_val = compare(lv, rv, cmp);
        TypeTag tag(type_hash::Boolean, TagDescriptor::Data);
        void* mem = result_.arena().allocate(1, 2, tag);
        *static_cast<uint8_t*>(mem) = result_val ? 1 : 0;
        return mem;
    }

    bool compare(void* lv, void* rv, int32_t cmp) {
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

    double to_double(void* val) {
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

    std::string_view to_string(void* val) {
        if (!val) return "";
        auto* b = static_cast<const uint8_t*>(val);
        TypeTag tag = TypeTag::read_before(b);
        if (tag.type_code() == type_hash::Varchar)
            return static_cast<const ArenaString*>(val)->view();
        return "";
    }

    void* eval_not(void* data, TinyObjectMap* node) {
        void* val = eval(data, get_child(node, RIGHT));
        bool result_val = !is_truthy(val);
        TypeTag tag(type_hash::Boolean, TagDescriptor::Data);
        void* mem = result_.arena().allocate(1, 2, tag);
        *static_cast<uint8_t*>(mem) = result_val ? 1 : 0;
        return mem;
    }

    void* eval_or(void* data, TinyObjectMap* node) {
        void* lv = eval(data, get_child(node, LEFT));
        if (is_truthy(lv)) return lv;
        return eval(data, get_child(node, RIGHT));
    }

    void* eval_and(void* data, TinyObjectMap* node) {
        void* lv = eval(data, get_child(node, LEFT));
        if (!is_truthy(lv)) return lv;
        return eval(data, get_child(node, RIGHT));
    }

    void* eval_pipe(void* data, TinyObjectMap* node) {
        void* lv = eval(data, get_child(node, LEFT));
        return eval(lv, get_child(node, RIGHT));
    }

    void* eval_multiselect_list(void* data, TinyObjectMap* node) {
        auto* exprs = static_cast<ObjectArray*>(get_child(node, EXPRESSIONS));
        auto* result = result_.make_array();
        for (uint64_t i = 0; i < exprs->size(); ++i) {
            void* expr = resolve_embedded(exprs->slot(i));
            void* val = eval(data, expr);
            push_value(result, val);
        }
        return result;
    }

    void* eval_multiselect_hash(void* data, TinyObjectMap* node) {
        auto* keys_arr = static_cast<ObjectArray*>(get_child(node, KEYS));
        auto* vals_arr = static_cast<ObjectArray*>(get_child(node, EXPRESSIONS));
        auto* result = result_.make_object_map();
        for (uint64_t i = 0; i < keys_arr->size(); ++i) {
            auto* key = keys_arr->slot(i)->as_ptr<ArenaString>();
            void* expr = resolve_embedded(vals_arr->slot(i));
            void* val = eval(data, expr);
            put_value(result, key->view(), val);
        }
        return result;
    }

    void* eval_function(void* data, TinyObjectMap* node) {
        auto* fname = node->slot(NAME)->as_ptr<ArenaString>();
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

    void* eval_hermes_value(TinyObjectMap* node) {
        if (!node->has_key(VALUE)) return nullptr;
        TaggedPtr val = node->get(VALUE);
        if (val.is_value()) return resolve_embedded(node->slot(VALUE));
        if (val.is_pointer()) return node->slot(VALUE)->as_ptr<void>();
        return nullptr;
    }

    // --- Built-in functions ---

    void* fn_length(void* data, ObjectArray* args) {
        void* arg = (args && args->size() > 0) ? eval(data, resolve_embedded(args->slot(0))) : data;
        if (!arg) return result_.make_value<int32_t>(0);
        auto* b = static_cast<const uint8_t*>(arg);
        TypeTag tag = TypeTag::read_before(b);
        if (tag.type_code() == type_hash::Varchar)
            return result_.make_value<int32_t>(static_cast<int32_t>(static_cast<const ArenaString*>(arg)->length()));
        if (tag.descriptor() == TagDescriptor::Array)
            return result_.make_value<int32_t>(static_cast<int32_t>(static_cast<const ObjectArray*>(arg)->size()));
        if (tag.descriptor() == TagDescriptor::Map && tag.type_code() == type_hash::ObjectMap)
            return result_.make_value<int32_t>(static_cast<int32_t>(static_cast<const ObjectMap*>(arg)->size()));
        return result_.make_value<int32_t>(0);
    }

    void* fn_type(void* data, ObjectArray* args) {
        void* arg = (args && args->size() > 0) ? eval(data, resolve_embedded(args->slot(0))) : data;
        if (!arg) return result_.make_string("null");
        auto* b = static_cast<const uint8_t*>(arg);
        TypeTag tag = TypeTag::read_before(b);
        switch (tag.type_code()) {
            case type_hash::Varchar: return result_.make_string("string");
            case type_hash::Integer: case type_hash::BigInt:
            case type_hash::Real: case type_hash::Double:
                return result_.make_string("number");
            case type_hash::Boolean: return result_.make_string("boolean");
            default:
                if (tag.descriptor() == TagDescriptor::Array) return result_.make_string("array");
                if (tag.descriptor() == TagDescriptor::Map) return result_.make_string("object");
                return result_.make_string("unknown");
        }
    }

    void* fn_keys(void* data, ObjectArray* args) {
        void* arg = (args && args->size() > 0) ? eval(data, resolve_embedded(args->slot(0))) : data;
        if (!arg) return result_.make_array(0);
        auto* map = static_cast<ObjectMap*>(arg);
        auto* result = result_.make_array();
        map->for_each([&](ArenaString* key, TaggedPtr*) {
            auto* ks = result_.make_string(key->view());
            result->push_back(TaggedPtr{}, result_.arena());
            result->slot(result->size() - 1)->set_pointer(ks);
        });
        return result;
    }

    void* fn_values(void* data, ObjectArray* args) {
        void* arg = (args && args->size() > 0) ? eval(data, resolve_embedded(args->slot(0))) : data;
        return eval_hash_wildcard(arg);
    }

    void* fn_to_string(void* data, ObjectArray* args) {
        void* arg = (args && args->size() > 0) ? eval(data, resolve_embedded(args->slot(0))) : data;
        auto sv = to_string(arg);
        if (!sv.empty()) return result_.make_string(sv);
        double d = to_double(arg);
        if (!std::isnan(d)) {
            char buf[32];
            int n = std::snprintf(buf, sizeof(buf), "%g", d);
            return result_.make_string(std::string_view(buf, n));
        }
        return result_.make_string("null");
    }

    void* fn_not_null(void* data, ObjectArray* args) {
        if (!args) return nullptr;
        for (uint64_t i = 0; i < args->size(); ++i) {
            void* val = eval(data, resolve_embedded(args->slot(i)));
            if (val) return val;
        }
        return nullptr;
    }

    void* fn_abs(void* data, ObjectArray* args) {
        void* arg = (args && args->size() > 0) ? eval(data, resolve_embedded(args->slot(0))) : data;
        double d = to_double(arg);
        if (!std::isnan(d)) return result_.make_value<double>(std::abs(d));
        return nullptr;
    }

    void* fn_sort(void* data, ObjectArray* args) {
        void* arg = (args && args->size() > 0) ? eval(data, resolve_embedded(args->slot(0))) : data;
        if (!arg) return result_.make_array(0);
        auto* arr = static_cast<ObjectArray*>(arg);
        // Collect values, sort, rebuild.
        std::vector<void*> items;
        for (uint64_t i = 0; i < arr->size(); ++i)
            items.push_back(resolve_embedded(arr->slot(i)));
        std::sort(items.begin(), items.end(), [this](void* a, void* b) {
            double da = to_double(a), db = to_double(b);
            if (!std::isnan(da) && !std::isnan(db)) return da < db;
            return to_string(a) < to_string(b);
        });
        auto* result = result_.make_array();
        for (auto* item : items) push_value(result, item);
        return result;
    }

    void* fn_reverse(void* data, ObjectArray* args) {
        void* arg = (args && args->size() > 0) ? eval(data, resolve_embedded(args->slot(0))) : data;
        if (!arg) return result_.make_array(0);
        auto* arr = static_cast<ObjectArray*>(arg);
        auto* result = result_.make_array();
        for (int64_t i = arr->size() - 1; i >= 0; --i)
            push_value(result, resolve_embedded(arr->slot(i)));
        return result;
    }

    void* fn_contains(void* data, ObjectArray* args) {
        if (!args || args->size() < 2) return nullptr;
        void* subject = eval(data, resolve_embedded(args->slot(0)));
        void* search = eval(data, resolve_embedded(args->slot(1)));
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
                    void* elem = resolve_embedded(arr->slot(i));
                    if (!std::isnan(search_d) && to_double(elem) == search_d) { found = true; break; }
                    if (!search_s.empty() && to_string(elem) == search_s) { found = true; break; }
                }
            }
        }
        TypeTag btag(type_hash::Boolean, TagDescriptor::Data);
        void* mem = result_.arena().allocate(1, 2, btag);
        *static_cast<uint8_t*>(mem) = found ? 1 : 0;
        return mem;
    }

    // --- Helpers ---

    // Copy a value into the result arena and push into array.
    // This ensures no cross-arena pointers.
    void push_value(ObjectArray* arr, void* val) {
        if (!val) {
            arr->push_back(TaggedPtr{}, result_.arena());
            return;
        }
        auto* b = static_cast<const uint8_t*>(val);
        TypeTag tag = TypeTag::read_before(b);
        uint64_t tc = tag.type_code();
        if (tc == type_hash::Integer) {
            arr->push_back(TaggedPtr::from_value(*static_cast<const int32_t*>(val), tc), result_.arena());
        } else if (tc == type_hash::Boolean) {
            arr->push_back(TaggedPtr::from_value(*static_cast<const uint8_t*>(val), tc), result_.arena());
        } else if (tc == type_hash::Real) {
            arr->push_back(TaggedPtr::from_value(*static_cast<const float*>(val), tc), result_.arena());
        } else if (tc == type_hash::Varchar) {
            auto* s = static_cast<const ArenaString*>(val);
            auto* copy = result_.make_string(s->view());
            arr->push_back(TaggedPtr{}, result_.arena());
            arr->slot(arr->size() - 1)->set_pointer(copy);
        } else {
            // For complex objects (arrays, maps), store pointer directly.
            // This is safe only if val is already in result_ arena.
            arr->push_back(TaggedPtr{}, result_.arena());
            arr->slot(arr->size() - 1)->set_pointer(val);
        }
    }

    void put_value(ObjectMap* map, std::string_view key, void* val) {
        if (!val) {
            map->put(key, TaggedPtr{}, result_.arena());
            return;
        }
        auto* b = static_cast<const uint8_t*>(val);
        TypeTag tag = TypeTag::read_before(b);
        uint64_t tc = tag.type_code();
        if (tc == type_hash::Integer) {
            map->put(key, TaggedPtr::from_value(*static_cast<const int32_t*>(val), tc), result_.arena());
        } else if (tc == type_hash::Boolean) {
            map->put(key, TaggedPtr::from_value(*static_cast<const uint8_t*>(val), tc), result_.arena());
        } else if (tc == type_hash::Real) {
            map->put(key, TaggedPtr::from_value(*static_cast<const float*>(val), tc), result_.arena());
        } else if (tc == type_hash::Varchar) {
            auto* s = static_cast<const ArenaString*>(val);
            auto* copy = result_.make_string(s->view());
            map->put(key, TaggedPtr{}, result_.arena());
            map->get_slot(key)->set_pointer(copy);
        } else {
            map->put(key, TaggedPtr{}, result_.arena());
            map->get_slot(key)->set_pointer(val);
        }
    }
};

// ============================================================================
// Public API
// ============================================================================

HermesCtr parse_path(std::string_view expr) {
    // Use MultiChunk to avoid pointer invalidation on realloc.
    // Cross-chunk relative pointers are a known issue — use large initial chunk.
    auto doc = HermesCtr::create(ArenaMode::MultiChunk, 65536);
    PathParser parser(expr, doc);
    void* ast = parser.parse();
    doc.set_root_offset(doc.offset_of(ast));
    return doc;
}

HermesCtr eval_path(const HermesCtr& data, std::string_view expr) {
    auto ast_doc = parse_path(expr);
    auto result = HermesCtr::create();
    PathEvaluator evaluator(result);
    void* data_root = const_cast<void*>(static_cast<const void*>(
        data.header()->root.get()));
    void* ast_root = ast_doc.root<void>();
    void* val = evaluator.eval(data_root, ast_root);
    if (val) result.set_root_offset(result.offset_of(val));
    return result;
}

HermesCtr eval_path_ast(void* data_root, void* ast_root, Arena& /*data_arena*/) {
    auto result = HermesCtr::create();
    PathEvaluator evaluator(result);
    void* val = evaluator.eval(data_root, ast_root);
    if (val) result.set_root_offset(result.offset_of(val));
    return result;
}

} // namespace logos::hermes
