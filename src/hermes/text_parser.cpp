// Logos project — https://github.com/victor-smirnov/logos

#include <logos/hermes/text_parser.hpp>
#include <logos/hermes/type_codes.hpp>
#include <logos/hermes/arena_string.hpp>
#include <logos/hermes/object_array.hpp>
#include <logos/hermes/object_map.hpp>
#include <logos/hermes/any_val.hpp>

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <string>

namespace logos::hermes {
namespace {

struct Parser {
    const char* p;
    const char* end;
    Arena&      a;
    bool        ok = true;

    void skip_ws() { while (p < end && std::isspace(static_cast<unsigned char>(*p))) ++p; }
    bool more() { skip_ws(); return p < end; }
    char peek() { return p < end ? *p : '\0'; }

    bool literal(const char* s) {
        size_t n = std::strlen(s);
        if (static_cast<size_t>(end - p) >= n && std::memcmp(p, s, n) == 0) { p += n; return true; }
        return false;
    }

    AnyVal fail() { ok = false; return AnyVal::null(); }

    // Box a wide scalar (8-byte tagged object) → value-form Ref AnyVal.
    AnyVal box(uint64_t code, const void* src8) {
        auto r = a.allocate(8, 8, TypeTag{code});
        if (!r) return fail();
        std::memcpy(*r, src8, 8);
        AnyVal av; av.set_ref(*r); return av;
    }

    AnyVal value() {
        skip_ws();
        if (p >= end) return fail();
        char c = *p;
        if (c == '{') return object();
        if (c == '[') return array();
        if (c == '"') return string();
        if (c == 't') return literal("true")  ? AnyVal::pod_bool(true,  tc::HA_BOOL) : fail();
        if (c == 'f') return literal("false") ? AnyVal::pod_bool(false, tc::HA_BOOL) : fail();
        if (c == 'n') return literal("null")  ? AnyVal::null() : fail();
        if (c == '-' || (c >= '0' && c <= '9')) return number();
        return fail();
    }

    AnyVal number() {
        const char* start = p;
        bool is_float = false;
        if (peek() == '-') ++p;
        while (p < end && std::isdigit(static_cast<unsigned char>(*p))) ++p;
        if (p < end && *p == '.') { is_float = true; ++p; while (p < end && std::isdigit(static_cast<unsigned char>(*p))) ++p; }
        if (p < end && (*p == 'e' || *p == 'E')) {
            is_float = true; ++p;
            if (p < end && (*p == '+' || *p == '-')) ++p;
            while (p < end && std::isdigit(static_cast<unsigned char>(*p))) ++p;
        }
        std::string tok(start, p);
        if (is_float) {
            double d = std::strtod(tok.c_str(), nullptr);
            return box(tc::F64, &d);
        }
        long long v = std::strtoll(tok.c_str(), nullptr, 10);
        // fits a 56-bit signed inline Pod?
        if (v >= -(1LL << 55) && v < (1LL << 55)) return AnyVal::pod(v, tc::HA_I56);
        int64_t w = v; return box(tc::I64, &w);   // wider → boxed i64
    }

    // Parse a JSON string body into `out`; assumes the opening quote is at *p.
    bool string_bytes(std::string& out) {
        if (peek() != '"') return false;
        ++p;
        while (p < end && *p != '"') {
            char c = *p++;
            if (c == '\\') {
                if (p >= end) return false;
                char e = *p++;
                switch (e) {
                case '"':  out += '"';  break;
                case '\\': out += '\\'; break;
                case '/':  out += '/';  break;
                case 'n':  out += '\n'; break;
                case 't':  out += '\t'; break;
                case 'r':  out += '\r'; break;
                case 'b':  out += '\b'; break;
                case 'f':  out += '\f'; break;
                case 'u': {
                    if (end - p < 4) return false;
                    unsigned cp = 0;
                    for (int i = 0; i < 4; ++i) {
                        char h = *p++; cp <<= 4;
                        if (h >= '0' && h <= '9') cp |= h - '0';
                        else if (h >= 'a' && h <= 'f') cp |= h - 'a' + 10;
                        else if (h >= 'A' && h <= 'F') cp |= h - 'A' + 10;
                        else return false;
                    }
                    // minimal UTF-8 encode (BMP only)
                    if (cp < 0x80) out += static_cast<char>(cp);
                    else if (cp < 0x800) { out += static_cast<char>(0xC0 | (cp >> 6)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
                    else { out += static_cast<char>(0xE0 | (cp >> 12)); out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
                    break;
                }
                default: return false;
                }
            } else out += c;
        }
        if (p >= end) return false;
        ++p; // closing quote
        return true;
    }

    AnyVal string() {
        std::string s;
        if (!string_bytes(s)) return fail();
        auto r = ArenaString::create(a, s);
        if (!r) return fail();
        AnyVal av; av.set_ref(*r); return av;
    }

    AnyVal array() {
        ++p; // '['
        auto r = ObjectArray::create(a, 4);
        if (!r) return fail();
        ObjectArray* arr = *r;
        skip_ws();
        if (peek() == ']') { ++p; AnyVal av; av.set_ref(arr); return av; }
        for (;;) {
            AnyVal el = value(); if (!ok) return AnyVal::null();
            (void)arr->push_back(el, a);
            skip_ws();
            char c = peek();
            if (c == ',') { ++p; continue; }
            if (c == ']') { ++p; break; }
            return fail();
        }
        AnyVal av; av.set_ref(arr); return av;
    }

    AnyVal object() {
        ++p; // '{'
        auto r = ObjectMap::create(a, 8);
        if (!r) return fail();
        ObjectMap* map = *r;
        skip_ws();
        if (peek() == '}') { ++p; AnyVal av; av.set_ref(map); return av; }
        for (;;) {
            skip_ws();
            std::string key;
            if (!string_bytes(key)) return fail();
            skip_ws();
            if (peek() != ':') return fail();
            ++p;
            AnyVal val = value(); if (!ok) return AnyVal::null();
            (void)map->put(key, val, a);
            skip_ws();
            char c = peek();
            if (c == ',') { ++p; continue; }
            if (c == '}') { ++p; break; }
            return fail();
        }
        AnyVal av; av.set_ref(map); return av;
    }
};

} // namespace

logos::expected<HermesCtr> text_parse(std::string_view text) noexcept {
    LOGOS_TRY(auto ctr, HermesCtr::make());
    Parser ps{text.data(), text.data() + text.size(), ctr.arena()};
    AnyVal root = ps.value();
    ps.skip_ws();
    if (!ps.ok || ps.p != ps.end)
        return std::unexpected(logos::err(ErrCode::parse_error));
    ctr.set_root(root);
    return ctr;
}

} // namespace logos::hermes
