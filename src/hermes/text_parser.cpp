// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos

#include <logos/hermes/text_parser.hpp>
#include <logos/hermes/access.hpp>
#include <logos/hermes/arena_value.hpp>
#include <logos/hermes/compound_types.hpp>
#include <logos/core/expected.hpp>

#include <charconv>
#include <string>
#include <unordered_set>

namespace logos::hermes {

// ============================================================================
// Recursive descent parser for Hermes text format.
//
// Grammar (from Memoria Boost.Spirit source):
//
//   document       := type_directory? value
//   type_directory  := '#{' (identifier ':' type_declaration) % ',' '}'
//   value          := string_or_typed | fp | integer | map | null | array
//                   | type_declaration | typed_value | bool | parameter | typed_ctr
//   string_or_typed := string ('@' type_decl_or_ref)?
//   string         := quoted_string | raw_string
//   integer        := (hex|bin|oct|dec) suffix?
//   fp             := strict_float ('d'|'f'?)
//   array          := '[' (value % ',')? ']'
//   map            := '{' (map_entry % ',')? '}'
//   map_entry      := (string | identifier) ':' value
//   type_declaration := datatype_name ('<' type_param % ',' '>')? ('(' value % ',' ')')? qualifiers?
//   datatype_name  := -(struct|class|union) identifier % '::' | cxx_basic_type
//   type_param     := type_declaration | bool | numeric_type_param | type_ref
//   type_ref       := '#' identifier
//   typed_value    := '@' type_decl_or_ref '=' value
//   typed_ctr      := '<' type_param % ',' '>' ('[' value% ',' ']' | '{' ... '}')
//   parameter      := '?' identifier
//   skipper        := whitespace | '//' ... eol
// ============================================================================

// Reserved words that cannot be bare identifiers (used as type names).
static const std::unordered_set<std::string_view> KEYWORDS = {
    "null", "true", "false", "const", "volatile", "signed", "unsigned",
    "int", "long", "char", "double", "float", "short", "bool",
    "struct", "class", "union"
};

class Parser {
public:
    Parser(std::string_view text, Hermes& doc)
        : text_(text), pos_(0), doc_(doc) {}

    logos::expected<void*> parse_document() noexcept {
        skip();
        // Optional type directory.
        if (text_.substr(pos_).starts_with("#{")) {
            LOGOS_TRY_VOID(parse_type_directory());
        }
        skip();
        LOGOS_TRY(void* root, parse_value());
        skip();
        if (pos_ < text_.size()) {
            return std::unexpected(logos::err(ErrCode::parse_error));
        }
        return root;
    }

private:
    std::string_view text_;
    size_t pos_;
    Hermes& doc_;

    // Type directory: maps names to DatatypeData* for #TypeRef resolution.
    struct TypeDirEntry {
        std::string name;
        DatatypeData* datatype;
    };
    std::vector<TypeDirEntry> type_dir_;

    // --- Character access ---

    bool at_end() const noexcept { return pos_ >= text_.size(); }
    char peek() const noexcept { return at_end() ? '\0' : text_[pos_]; }
    char peek(size_t offset) const noexcept {
        size_t p = pos_ + offset;
        return p >= text_.size() ? '\0' : text_[p];
    }
    char advance() noexcept { return text_[pos_++]; }

    logos::expected<void> expect(char c) noexcept {
        skip();
        if (at_end() || text_[pos_] != c) {
            return std::unexpected(logos::err(ErrCode::parse_error));
        }
        ++pos_;
        return {};
    }

    bool try_char(char c) noexcept {
        skip();
        if (!at_end() && text_[pos_] == c) { ++pos_; return true; }
        return false;
    }

    // Try to consume a literal string at current position (no skip before).
    bool try_consume(std::string_view s) noexcept {
        if (pos_ + s.size() <= text_.size() && text_.substr(pos_, s.size()) == s) {
            // For suffixes, check that the next char isn't alphanumeric (avoid partial match).
            size_t after = pos_ + s.size();
            if (after < text_.size() && is_ident_char(text_[after])) return false;
            pos_ += s.size();
            return true;
        }
        return false;
    }

    // Try to consume a literal string at current position, no boundary check.
    bool try_consume_raw(std::string_view s) noexcept {
        if (pos_ + s.size() <= text_.size() && text_.substr(pos_, s.size()) == s) {
            pos_ += s.size();
            return true;
        }
        return false;
    }

    // --- Whitespace & comments ---

    void skip() noexcept {
        while (pos_ < text_.size()) {
            char c = text_[pos_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                ++pos_;
            } else if (c == '/' && peek(1) == '/') {
                pos_ += 2;
                while (pos_ < text_.size() && text_[pos_] != '\n') ++pos_;
            } else {
                break;
            }
        }
    }

    // --- Identifier ---

    bool is_ident_start(char c) noexcept {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
    }
    bool is_ident_char(char c) noexcept {
        return is_ident_start(c) || (c >= '0' && c <= '9');
    }
    bool is_digit(char c) noexcept { return c >= '0' && c <= '9'; }

    logos::expected<std::string_view> read_identifier() noexcept {
        size_t start = pos_;
        if (at_end() || !is_ident_start(peek()))
            return std::unexpected(logos::err(ErrCode::parse_error));
        while (pos_ < text_.size() && is_ident_char(text_[pos_])) ++pos_;
        return text_.substr(start, pos_ - start);
    }

    // Peek identifier without consuming.
    logos::expected<std::string_view> peek_identifier() noexcept {
        size_t saved = pos_;
        LOGOS_TRY(auto id, read_identifier());
        pos_ = saved;
        return id;
    }

    bool is_keyword(std::string_view id) noexcept {
        return KEYWORDS.contains(id);
    }

    // --- Type directory ---

    logos::expected<void> parse_type_directory() noexcept {
        pos_ += 2; // skip '#{'
        skip();
        if (peek() == '}') { ++pos_; return {}; }

        while (true) {
            skip();
            LOGOS_TRY(auto name_sv, read_identifier());
            std::string name(name_sv);
            LOGOS_TRY_VOID(expect(':'));
            LOGOS_TRY(DatatypeData* dt, parse_type_declaration());
            type_dir_.push_back({std::move(name), dt});

            skip();
            if (peek() == '}') { ++pos_; break; }
            LOGOS_TRY_VOID(expect(','));
        }
        return {};
    }

    logos::expected<DatatypeData*> resolve_typeref(std::string_view name) noexcept {
        for (auto& entry : type_dir_) {
            if (entry.name == name) return entry.datatype;
        }
        return std::unexpected(logos::err(ErrCode::parse_error));
    }

    // --- Value dispatch ---

    logos::expected<void*> parse_value() noexcept {
        skip();
        if (at_end()) return std::unexpected(logos::err(ErrCode::parse_error));
        char c = peek();

        // String (quoted/raw), possibly with type annotation
        if (c == '"' || c == '\'') return parse_string_or_typed();

        // Array
        if (c == '[') return parse_array();

        // Map
        if (c == '{') return parse_map();

        // Typed value: @Type = value
        if (c == '@') return parse_typed_value();

        // Parameter: ?name
        if (c == '?') return parse_parameter();

        // Typed container: <Type>[...] or <Type>{...}
        if (c == '<') return parse_typed_container();

        // Number (starts with digit or sign)
        if (is_digit(c) || ((c == '-' || c == '+') && is_digit(peek(1)))) {
            return parse_number();
        }

        // Keywords, type declarations, or bare identifier
        if (is_ident_start(c)) {
            return parse_keyword_or_type_or_identifier();
        }

        // Type reference: #Name
        if (c == '#' && peek(1) != '{') {
            ++pos_;
            LOGOS_TRY(auto name, read_identifier());
            return resolve_typeref(name);
        }

        return std::unexpected(logos::err(ErrCode::parse_error));
    }

    // --- Strings ---

    logos::expected<void*> parse_string_or_typed() noexcept {
        logos::expected<std::string> str_res;
        if (peek() == '"') {
            str_res = parse_quoted_string();
        } else {
            str_res = parse_raw_string();
        }
        if (!str_res) return std::unexpected(std::move(str_res.error()));
        std::string str = std::move(*str_res);

        // Check for type annotation: "str"@Type
        skip();
        if (!at_end() && peek() == '@') {
            ++pos_;
            skip();
            LOGOS_TRY(DatatypeData* dt, parse_type_declaration());
            LOGOS_TRY(auto* arena_str, HermesAccess::raw_string(doc_, str));
            LOGOS_TRY(auto* tv, TypedValueData::create(HermesAccess::arena(doc_), dt));
            tv->value.set_pointer(arena_str, HermesAccess::base(doc_));
            return tv;
        }

        LOGOS_TRY(auto* s, HermesAccess::raw_string(doc_, str));
        return s;
    }

    logos::expected<std::string> parse_quoted_string() noexcept {
        LOGOS_TRY_VOID(expect('"'));
        std::string result;
        while (!at_end() && peek() != '"') {
            if (peek() == '\\') {
                ++pos_;
                if (at_end()) return std::unexpected(logos::err(ErrCode::parse_error));
                char esc = advance();
                switch (esc) {
                    case '"':  result += '"'; break;
                    case '\\': result += '\\'; break;
                    case '/':  result += '/'; break;
                    case 'b':  result += '\b'; break;
                    case 'f':  result += '\f'; break;
                    case 'n':  result += '\n'; break;
                    case 'r':  result += '\r'; break;
                    case 't':  result += '\t'; break;
                    case 'u': {
                        LOGOS_TRY(auto u, parse_unicode_escape());
                        result += u;
                        break;
                    }
                    default:
                        return std::unexpected(logos::err(ErrCode::parse_error));
                }
            } else {
                result += advance();
            }
        }
        if (at_end()) return std::unexpected(logos::err(ErrCode::parse_error));
        ++pos_;
        return result;
    }

    logos::expected<std::string> parse_raw_string() noexcept {
        LOGOS_TRY_VOID(expect('\''));
        std::string result;
        while (!at_end() && peek() != '\'') {
            if (peek() == '\\' && peek(1) == '\'') {
                pos_ += 2;
                result += '\'';
            } else {
                result += advance();
            }
        }
        if (at_end()) return std::unexpected(logos::err(ErrCode::parse_error));
        ++pos_;
        return result;
    }

    logos::expected<uint32_t> read_hex4() noexcept {
        uint32_t cp = 0;
        for (int i = 0; i < 4; ++i) {
            if (at_end()) return std::unexpected(logos::err(ErrCode::parse_error));
            char h = advance();
            cp <<= 4;
            if (h >= '0' && h <= '9') cp |= (h - '0');
            else if (h >= 'a' && h <= 'f') cp |= (h - 'a' + 10);
            else if (h >= 'A' && h <= 'F') cp |= (h - 'A' + 10);
            else return std::unexpected(logos::err(ErrCode::parse_error));
        }
        return cp;
    }

    logos::expected<std::string> parse_unicode_escape() noexcept {
        LOGOS_TRY(uint32_t cp, read_hex4());

        // Handle surrogate pairs: \uD800-\uDBFF followed by \uDC00-\uDFFF.
        if (cp >= 0xD800 && cp <= 0xDBFF) {
            if (pos_ + 1 < text_.size() && text_[pos_] == '\\' && text_[pos_ + 1] == 'u') {
                pos_ += 2; // skip \u
                LOGOS_TRY(uint32_t lo, read_hex4());
                if (lo >= 0xDC00 && lo <= 0xDFFF) {
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                } else {
                    return std::unexpected(logos::err(ErrCode::parse_error));
                }
            } else {
                return std::unexpected(logos::err(ErrCode::parse_error));
            }
        }

        return encode_utf8(cp);
    }

    static logos::expected<std::string> encode_utf8(uint32_t cp) noexcept {
        std::string r;
        if (cp < 0x80) {
            r += static_cast<char>(cp);
        } else if (cp < 0x800) {
            r += static_cast<char>(0xC0 | (cp >> 6));
            r += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            r += static_cast<char>(0xE0 | (cp >> 12));
            r += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            r += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            r += static_cast<char>(0xF0 | (cp >> 18));
            r += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            r += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            r += static_cast<char>(0x80 | (cp & 0x3F));
        }
        return r;
    }

    // --- Numbers ---

    logos::expected<void*> parse_number() noexcept {
        size_t num_start = pos_;
        bool negative = false;
        if (peek() == '-' || peek() == '+') {
            negative = (peek() == '-');
            ++pos_;
        }

        int base = 10;
        if (peek() == '0' && pos_ + 1 < text_.size()) {
            char next = text_[pos_ + 1];
            if (next == 'x' || next == 'X') { base = 16; pos_ += 2; }
            else if (next == 'b' || next == 'B') { base = 2; pos_ += 2; }
            else if (next == 'o' || next == 'O') { base = 8; pos_ += 2; }
            // Memoria-style: 0 followed by octal digit → octal (no 'o' prefix).
            else if (next >= '0' && next <= '7') { base = 8; ++pos_; }
        }

        size_t digits_start = pos_;
        bool has_dot = false, has_exp = false;

        while (pos_ < text_.size()) {
            char c = text_[pos_];
            bool valid_digit = (base == 16 && ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')))
                || (base == 2 && (c == '0' || c == '1'))
                || (base == 8 && c >= '0' && c <= '7')
                || (base == 10 && c >= '0' && c <= '9');
            if (valid_digit) { ++pos_; continue; }
            if (base == 10 && c == '.' && !has_dot && !has_exp && peek(1) >= '0' && peek(1) <= '9') {
                has_dot = true; ++pos_; continue;
            }
            if (base == 10 && (c == 'e' || c == 'E') && !has_exp) {
                has_exp = true; ++pos_;
                if (!at_end() && (peek() == '+' || peek() == '-')) ++pos_;
                continue;
            }
            break;
        }

        std::string_view digits = text_.substr(digits_start, pos_ - digits_start);
        if (digits.empty()) return std::unexpected(logos::err(ErrCode::parse_error));

        if (has_dot || has_exp) return parse_float_suffix(num_start);
        return parse_integer_with_suffix(digits, base, negative);
    }

    logos::expected<void*> parse_float_suffix(size_t num_start) noexcept {
        std::string_view num_text = text_.substr(num_start, pos_ - num_start);
        if (try_consume_raw("d")) {
            double val;
            std::from_chars(num_text.data(), num_text.data() + num_text.size(), val);
            return HermesAccess::make_value<double>(doc_, val);
        }
        try_consume_raw("f");
        double dval;
        std::from_chars(num_text.data(), num_text.data() + num_text.size(), dval);
        return HermesAccess::make_value<float>(doc_, static_cast<float>(dval));
    }

    logos::expected<void*> parse_integer_with_suffix(std::string_view digits, int base, bool negative) noexcept {
        uint64_t uval = 0;
        for (char c : digits) {
            uint64_t d;
            if (c >= '0' && c <= '9') d = c - '0';
            else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
            else return std::unexpected(logos::err(ErrCode::parse_error));
            uval = uval * base + d;
        }

        if (try_consume("ull") || try_consume("ul") || try_consume("_u64"))
            return HermesAccess::make_value<uint64_t>(doc_, uval);
        if (try_consume("ll") || try_consume("_s64"))
            return HermesAccess::make_value<int64_t>(doc_, negative ? -int64_t(uval) : int64_t(uval));
        if (try_consume("_u32") || try_consume_raw("u"))
            return HermesAccess::make_value<uint32_t>(doc_, uint32_t(uval));
        if (try_consume("_u16")) return HermesAccess::make_value<uint16_t>(doc_, uint16_t(uval));
        if (try_consume("_u8"))  return HermesAccess::make_value<uint8_t>(doc_, uint8_t(uval));
        if (try_consume("_s16")) return HermesAccess::make_value<int16_t>(doc_, negative ? -int16_t(uval) : int16_t(uval));
        if (try_consume("_s8"))  return HermesAccess::make_value<int8_t>(doc_, negative ? -int8_t(uval) : int8_t(uval));
        if (try_consume("_s32")) return HermesAccess::make_value<int32_t>(doc_, negative ? -int32_t(uval) : int32_t(uval));

        if (!at_end() && peek() == 'f') { advance(); return HermesAccess::make_value<float>(doc_, negative ? -float(uval) : float(uval)); }
        if (!at_end() && peek() == 'd') { advance(); return HermesAccess::make_value<double>(doc_, negative ? -double(uval) : double(uval)); }

        return HermesAccess::make_value<int32_t>(doc_, negative ? -int32_t(uval) : int32_t(uval));
    }

    // --- Array ---

    logos::expected<void*> parse_array() noexcept {
        LOGOS_TRY_VOID(expect('['));
        LOGOS_TRY(auto* arr, HermesAccess::raw_array(doc_));
        skip();
        if (peek() == ']') { ++pos_; return arr; }
        while (true) {
            LOGOS_TRY(void* elem, parse_value());
            LOGOS_TRY_VOID(push_element(arr, elem));
            skip();
            if (peek() == ']') { ++pos_; break; }
            LOGOS_TRY_VOID(expect(','));
        }
        return arr;
    }

    // --- Map ---

    logos::expected<void*> parse_map() noexcept {
        LOGOS_TRY_VOID(expect('{'));
        LOGOS_TRY(auto* map, HermesAccess::raw_object_map(doc_));
        skip();
        if (peek() == '}') { ++pos_; return map; }
        while (true) {
            skip();
            logos::expected<std::string> key_res;
            if (peek() == '"') {
                key_res = parse_quoted_string();
            } else if (peek() == '\'') {
                key_res = parse_raw_string();
            } else {
                LOGOS_TRY(auto sv, read_identifier());
                key_res = std::string(sv);
            }
            if (!key_res) return std::unexpected(std::move(key_res.error()));

            LOGOS_TRY_VOID(expect(':'));
            LOGOS_TRY(void* val, parse_value());
            LOGOS_TRY_VOID(put_map_entry(map, *key_res, val));
            skip();
            if (peek() == '}') { ++pos_; break; }
            LOGOS_TRY_VOID(expect(','));
        }
        return map;
    }

    // --- Type declaration ---
    // datatype_name ('<' type_param % ',' '>')? ('(' value % ',' ')')? qualifiers?

    logos::expected<DatatypeData*> parse_type_declaration() noexcept {
        skip();
        LOGOS_TRY(std::string name, parse_datatype_name());
        LOGOS_TRY(auto* arena_name, HermesAccess::raw_string(doc_, name));

        ObjectArray* params = nullptr;
        ObjectArray* ctr_args = nullptr;

        skip();
        // Type parameters: <...>
        if (!at_end() && peek() == '<') {
            ++pos_;
            LOGOS_TRY(params, HermesAccess::raw_array(doc_));
            skip();
            if (peek() != '>') {
                while (true) {
                    LOGOS_TRY(void* tp, parse_type_param());
                    LOGOS_TRY_VOID(push_element(params, tp));
                    skip();
                    if (peek() == '>') break;
                    LOGOS_TRY_VOID(expect(','));
                }
            }
            LOGOS_TRY_VOID(expect('>'));
        }

        skip();
        // Constructor args: (...)
        if (!at_end() && peek() == '(') {
            ++pos_;
            LOGOS_TRY(ctr_args, HermesAccess::raw_array(doc_));
            skip();
            if (peek() != ')') {
                while (true) {
                    LOGOS_TRY(void* v, parse_value());
                    LOGOS_TRY_VOID(push_element(ctr_args, v));
                    skip();
                    if (peek() == ')') break;
                    LOGOS_TRY_VOID(expect(','));
                }
            }
            LOGOS_TRY_VOID(expect(')'));
        }

        LOGOS_TRY(auto* dt, DatatypeData::create(HermesAccess::arena(doc_), arena_name, params, ctr_args));

        // Optional qualifiers: const, volatile, *, &
        skip();
        parse_qualifiers(dt);

        return dt;
    }

    logos::expected<std::string> parse_datatype_name() noexcept {
        skip();
        // Skip optional struct/class/union prefix.
        size_t saved = pos_;
        if (is_ident_start(peek())) {
            LOGOS_TRY(auto kw, read_identifier());
            if (kw == "struct" || kw == "class" || kw == "union") {
                skip();
            } else {
                pos_ = saved;
            }
        }

        // Try C++ basic types first.
        LOGOS_TRY(std::string basic, try_cxx_basic_type());
        if (!basic.empty()) return basic;

        // Qualified name: identifier (:: identifier)*
        LOGOS_TRY(auto first, read_identifier());
        std::string result(first);
        while (pos_ + 1 < text_.size() && text_[pos_] == ':' && text_[pos_ + 1] == ':') {
            pos_ += 2;
            LOGOS_TRY(auto next, read_identifier());
            result += "::";
            result += next;
        }
        return result;
    }

    logos::expected<std::string> try_cxx_basic_type() noexcept {
        size_t saved = pos_;

        // Try multi-word basic types (order: longest match first).
        // This mirrors the Memoria Spirit grammar.
        struct { const char* text; const char* canonical; } basics[] = {
            {"long double", "long double"},
            {"unsigned long long", "unsigned long long"},
            {"unsigned long int", "unsigned long"},
            {"unsigned long", "unsigned long"},
            {"unsigned short int", "unsigned short"},
            {"unsigned short", "unsigned short"},
            {"unsigned char", "unsigned char"},
            {"unsigned int", "unsigned int"},
            {"unsigned", "unsigned int"},
            {"signed long long", "long long"},
            {"signed long int", "long"},
            {"signed long", "long"},
            {"signed short int", "short"},
            {"signed short", "short"},
            {"signed char", "char"},
            {"signed int", "int"},
            {"signed", "int"},
            {"long long", "long long"},
            {"long int", "long"},
            {"long", "long"},
            {"short int", "short"},
            {"short", "short"},
            {"int", "int"},
            {"char", "char"},
            {"double", "double"},
            {"float", "float"},
            {"bool", "bool"},
        };

        for (auto& b : basics) {
            pos_ = saved;
            if (try_match_words(b.text)) {
                return std::string(b.canonical);
            }
        }
        pos_ = saved;
        return std::string{};
    }

    // Match space-separated words like "unsigned long long".
    bool try_match_words(const char* pattern) noexcept {
        size_t saved = pos_;
        const char* p = pattern;
        while (*p) {
            if (*p == ' ') {
                skip();
                ++p;
                continue;
            }
            const char* word_start = p;
            while (*p && *p != ' ') ++p;
            std::string_view word(word_start, p - word_start);

            if (!is_ident_start(peek())) { pos_ = saved; return false; }
            auto id_res = read_identifier();
            if (!id_res) { pos_ = saved; return false; }
            if (*id_res != word) { pos_ = saved; return false; }
        }
        // Check that the next char isn't ident (avoid partial match like "integer" matching "int").
        if (!at_end() && is_ident_char(peek())) { pos_ = saved; return false; }
        return true;
    }

    logos::expected<void*> parse_type_param() noexcept {
        skip();
        char c = peek();

        // Type reference: #Name
        if (c == '#') {
            ++pos_;
            LOGOS_TRY(auto name, read_identifier());
            return resolve_typeref(name);
        }

        // Boolean literal as type param
        if (is_ident_start(c)) {
            size_t saved = pos_;
            LOGOS_TRY(auto id, read_identifier());
            if (id == "true") return make_boolean(1);
            if (id == "false") return make_boolean(0);
            pos_ = saved;
        }

        // Numeric type param: (TypeName) integer  or just integer
        if (c == '(' || is_digit(c) || c == '-' || c == '+') {
            if (c == '(') {
                // Skip the cast: (TypeName) integer
                skip_balanced('(', ')');
                skip();
            }
            if (is_digit(peek()) || peek() == '-' || peek() == '+') {
                return parse_number();
            }
        }

        // Default: nested type declaration
        return parse_type_declaration();
    }

    void parse_qualifiers(DatatypeData* dt) noexcept {
        // Qualifiers: optional (const? volatile? *)* then optional (const? volatile? & &?)
        while (!at_end()) {
            skip();
            if (peek() == '*') { ++pos_; dt->add_ptr(); continue; }
            if (peek() == '&') {
                ++pos_;
                dt->set_refs(1);
                if (!at_end() && peek() == '&') { ++pos_; dt->set_refs(2); }
                break; // & is always last
            }

            size_t saved = pos_;
            if (is_ident_start(peek())) {
                auto kw_res = read_identifier();
                if (!kw_res) break;
                if (*kw_res == "const") { dt->set_const(true); continue; }
                if (*kw_res == "volatile") { dt->set_volatile(true); continue; }
                pos_ = saved;
            }
            break;
        }
    }

    // --- Keywords, type declarations, or identifier ---

    logos::expected<void*> parse_keyword_or_type_or_identifier() noexcept {
        LOGOS_TRY(auto id, peek_identifier());

        if (id == "null") { (void)read_identifier(); return make_boolean(0); } // TODO: proper null
        if (id == "true") { (void)read_identifier(); return make_boolean(1); }
        if (id == "false") { (void)read_identifier(); return make_boolean(0); }

        // Try as type declaration: identifier followed by < or ( or :: or qualifier
        // or just a known type keyword.
        LOGOS_TRY(bool is_type, looks_like_type_declaration());
        if (is_type) {
            return parse_type_declaration();
        }

        // Bare identifier — treat as string.
        LOGOS_TRY(auto name, read_identifier());
        return HermesAccess::raw_string(doc_, name);
    }

    logos::expected<bool> looks_like_type_declaration() noexcept {
        size_t saved = pos_;

        if (!is_ident_start(peek())) { pos_ = saved; return false; }

        LOGOS_TRY(auto id, read_identifier());

        // struct/class/union prefix → definitely a type.
        if (id == "struct" || id == "class" || id == "union") {
            pos_ = saved; return true;
        }

        // C++ basic type keyword → definitely a type.
        if (id == "int" || id == "long" || id == "short" || id == "char" ||
            id == "double" || id == "float" || id == "bool" ||
            id == "signed" || id == "unsigned") {
            pos_ = saved; return true;
        }

        // Check what follows the identifier.
        skip();
        char next = peek();
        pos_ = saved;

        // Followed by < or ( or :: → type declaration.
        if (next == '<' || next == '(') return true;
        if (next == ':' && peek_at(saved + id.size()) == ':') return true;

        // Capital-first identifier at top level (not followed by : which means map key)
        // → treat as type declaration. This matches Memoria behavior where bare type
        // names like "Integer", "Array" are valid values.
        if (id[0] >= 'A' && id[0] <= 'Z' && next != ':') return true;

        return false;
    }

    char peek_at(size_t p) noexcept { return p < text_.size() ? text_[p] : '\0'; }

    // --- Typed value: @Type = value ---

    logos::expected<void*> parse_typed_value() noexcept {
        LOGOS_TRY_VOID(expect('@'));
        skip();

        DatatypeData* dt;
        // Type reference: @#Name = value
        if (peek() == '#') {
            ++pos_;
            LOGOS_TRY(auto name, read_identifier());
            LOGOS_TRY(dt, resolve_typeref(name));
        } else {
            LOGOS_TRY(dt, parse_type_declaration());
        }

        skip();
        LOGOS_TRY_VOID(expect('='));
        LOGOS_TRY(void* val, parse_value());

        LOGOS_TRY(auto* tv, TypedValueData::create(HermesAccess::arena(doc_), dt));
        // Set value in-place.
        auto* bytes = static_cast<const uint8_t*>(val);
        TypeTag tag = TypeTag::read_before(bytes);
        if (can_embed(tag.type_code())) {
            tv->value = make_embedded(val, tag.type_code());
        } else {
            tv->value.set_pointer(val, HermesAccess::base(doc_));
        }
        return tv;
    }

    // --- Typed container: <Type>[...] or <Type>{...} ---
    // Creates a TypedValue where the datatype encodes the container element type(s)
    // and the value is the array or map.

    logos::expected<void*> parse_typed_container() noexcept {
        LOGOS_TRY_VOID(expect('<'));
        // Collect type params into a DatatypeData.
        // For <T>[...] → Datatype name="Array", params=[T]
        // For <K,V>{...} → Datatype name="Map", params=[K,V]
        LOGOS_TRY(auto* type_params, HermesAccess::raw_array(doc_));
        while (true) {
            LOGOS_TRY(void* tp, parse_type_param());
            LOGOS_TRY_VOID(push_element(type_params, tp));
            skip();
            if (peek() == '>') break;
            LOGOS_TRY_VOID(expect(','));
        }
        LOGOS_TRY_VOID(expect('>'));

        skip();
        void* container;
        const char* container_kind;
        if (peek() == '[') {
            LOGOS_TRY(container, parse_array());
            container_kind = "Array";
        } else if (peek() == '{') {
            LOGOS_TRY(container, parse_map());
            container_kind = "Map";
        } else {
            return std::unexpected(logos::err(ErrCode::parse_error));
        }

        // Wrap: @Array<T> = [...]  or  @Map<K,V> = {...}
        LOGOS_TRY(auto* kind_name, HermesAccess::raw_string(doc_, container_kind));
        LOGOS_TRY(auto* dt, DatatypeData::create(HermesAccess::arena(doc_), kind_name, type_params));
        LOGOS_TRY(auto* tv, TypedValueData::create(HermesAccess::arena(doc_), dt));
        tv->value.set_pointer(container, HermesAccess::base(doc_));
        return tv;
    }

    // --- Parameter: ?name ---

    logos::expected<void*> parse_parameter() noexcept {
        LOGOS_TRY_VOID(expect('?'));
        LOGOS_TRY(auto name, read_identifier());
        LOGOS_TRY(auto* arena_name, HermesAccess::raw_string(doc_, name));
        return ParameterData::create(HermesAccess::arena(doc_), arena_name);
    }

    // --- Container element helpers ---

    logos::expected<void> push_element(ObjectArray* arr, void* elem) noexcept {
        auto* bytes = static_cast<const uint8_t*>(elem);
        TypeTag tag = TypeTag::read_before(bytes);
        uint64_t tc = tag.type_code();
        if (can_embed(tc)) {
            LOGOS_TRY_VOID(arr->push_back(make_embedded(elem, tc), HermesAccess::arena(doc_)));
        } else {
            LOGOS_TRY_VOID(arr->push_back(AnyVal{}, HermesAccess::arena(doc_)));
            arr->slot(arr->size() - 1, HermesAccess::base(doc_))->set_pointer(elem, HermesAccess::base(doc_));
        }
        return {};
    }

    logos::expected<void> put_map_entry(ObjectMap* map, std::string_view key, void* elem) noexcept {
        auto* bytes = static_cast<const uint8_t*>(elem);
        TypeTag tag = TypeTag::read_before(bytes);
        uint64_t tc = tag.type_code();
        if (can_embed(tc)) {
            LOGOS_TRY_VOID(map->put(key, make_embedded(elem, tc), HermesAccess::arena(doc_)));
        } else {
            LOGOS_TRY_VOID(map->put(key, AnyVal{}, HermesAccess::arena(doc_)));
            map->get_slot(key, HermesAccess::base(doc_))->set_pointer(elem, HermesAccess::base(doc_));
        }
        return {};
    }

    bool can_embed(uint64_t tc) noexcept {
        switch (tc) {
            case type_hash::TinyInt: case type_hash::UTinyInt: case type_hash::Boolean:
            case type_hash::SmallInt: case type_hash::USmallInt:
            case type_hash::Integer: case type_hash::UInteger:
            case type_hash::Real: case type_hash::Time:
                return true;
            default: return false;
        }
    }

    AnyVal make_embedded(const void* obj, uint64_t tc) noexcept {
        switch (tc) {
            case type_hash::TinyInt:  return AnyVal::from_value(*static_cast<const int8_t*>(obj), tc);
            case type_hash::UTinyInt: case type_hash::Boolean:
                return AnyVal::from_value(*static_cast<const uint8_t*>(obj), tc);
            case type_hash::SmallInt: return AnyVal::from_value(*static_cast<const int16_t*>(obj), tc);
            case type_hash::USmallInt:return AnyVal::from_value(*static_cast<const uint16_t*>(obj), tc);
            case type_hash::Integer:  return AnyVal::from_value(*static_cast<const int32_t*>(obj), tc);
            case type_hash::UInteger: return AnyVal::from_value(*static_cast<const uint32_t*>(obj), tc);
            case type_hash::Real:     return AnyVal::from_value(*static_cast<const float*>(obj), tc);
            default: return AnyVal{};
        }
    }

    logos::expected<void*> make_boolean(uint8_t val) noexcept {
        TypeTag tag(type_hash::Boolean, TagDescriptor::Data);
        LOGOS_TRY(void* mem, HermesAccess::arena(doc_).allocate(1, 2, tag));
        *static_cast<uint8_t*>(mem) = val;
        return mem;
    }

    void skip_balanced(char open, char close) noexcept {
        int depth = 0;
        if (peek() == open) { ++pos_; depth = 1; }
        while (depth > 0 && !at_end()) {
            char c = advance();
            if (c == open) ++depth;
            else if (c == close) --depth;
        }
    }
};

// ============================================================================
// Public API
// ============================================================================

logos::expected<Hermes> parse(std::string_view text) noexcept {
    LOGOS_TRY(auto doc, make_doc());
    Parser parser(text, doc);
    LOGOS_TRY(void* root, parser.parse_document());
    HermesAccess::set_root_offset(doc, HermesAccess::offset_of(doc, root));
    return doc;
}

} // namespace logos::hermes
