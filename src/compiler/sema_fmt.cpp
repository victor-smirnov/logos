// SPDX-License-Identifier: Apache-2.0
// sema_fmt.cpp — format-string parser. See sema_fmt.hpp for the
// surface and the slice 4.4 design context.

#include "sema_fmt.hpp"

#include <cctype>
#include <format>

namespace logos::compiler {

const char* format_trait_method(FormatTrait t) {
    switch (t) {
    case FormatTrait::Display:  return "fmt";
    case FormatTrait::Debug:    return "dbg";
    case FormatTrait::LowerHex: return "fmt_lower_hex";
    case FormatTrait::UpperHex: return "fmt_upper_hex";
    case FormatTrait::Octal:    return "fmt_octal";
    case FormatTrait::Binary:   return "fmt_binary";
    case FormatTrait::LowerExp: return "fmt_lower_exp";
    case FormatTrait::UpperExp: return "fmt_upper_exp";
    }
    return "fmt";
}

const char* format_trait_dispatcher(FormatTrait t) {
    // Free-fn wrapper that the macro lowering calls instead of an
    // x.method() dot-call. Each wrapper binds the trait at sema-time
    // through a generic-context bound (T: Display etc.), which sidesteps
    // lower_method_call's slice/str short-circuit.
    switch (t) {
    case FormatTrait::Display:  return "fmt_display";
    case FormatTrait::Debug:    return "fmt_debug";
    case FormatTrait::LowerHex: return "fmt_lower_hex";
    case FormatTrait::UpperHex: return "fmt_upper_hex";
    case FormatTrait::Octal:    return "fmt_octal";
    case FormatTrait::Binary:   return "fmt_binary";
    case FormatTrait::LowerExp: return "fmt_lower_exp";
    case FormatTrait::UpperExp: return "fmt_upper_exp";
    }
    return "fmt_display";
}

const char* format_trait_name(FormatTrait t) {
    switch (t) {
    case FormatTrait::Display:  return "Display";
    case FormatTrait::Debug:    return "Debug";
    case FormatTrait::LowerHex: return "LowerHex";
    case FormatTrait::UpperHex: return "UpperHex";
    case FormatTrait::Octal:    return "Octal";
    case FormatTrait::Binary:   return "Binary";
    case FormatTrait::LowerExp: return "LowerExp";
    case FormatTrait::UpperExp: return "UpperExp";
    }
    return "Display";
}

namespace {

// Match an unsigned-int prefix at `body[pos]`. Returns the parsed value
// and advances `pos`; returns -1 (no advance) if there is no digit.
int32_t parse_uint(std::string_view body, size_t& pos) {
    if (pos >= body.size() || !std::isdigit(static_cast<unsigned char>(body[pos])))
        return -1;
    int32_t v = 0;
    while (pos < body.size() &&
           std::isdigit(static_cast<unsigned char>(body[pos]))) {
        v = v * 10 + (body[pos] - '0');
        ++pos;
    }
    return v;
}

// Match an identifier-like name at `body[pos]`. Returns the slice and
// advances `pos`; empty if no identifier at that point.
std::string_view parse_name(std::string_view body, size_t& pos) {
    size_t start = pos;
    if (pos >= body.size()) return {};
    char c0 = body[pos];
    if (!(std::isalpha(static_cast<unsigned char>(c0)) || c0 == '_'))
        return {};
    while (pos < body.size()) {
        char c = body[pos];
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '_')
            ++pos;
        else
            break;
    }
    return body.substr(start, pos - start);
}

} // namespace

void parse_format_string(
    std::string_view body,
    FormatParseResult& out,
    const std::function<void(std::string)>& emit_diag) {

    out.segments.clear();
    out.positional_count = 0;
    out.max_explicit_plus_one = 0;
    out.ok = true;

    size_t i = 0;
    size_t lit_start = 0;
    auto flush_literal = [&](size_t end) {
        if (end > lit_start) {
            FormatSegment seg{};
            seg.is_literal = true;
            seg.lit_text = body.substr(lit_start, end - lit_start);
            out.segments.push_back(std::move(seg));
        }
    };

    while (i < body.size()) {
        char c = body[i];

        // `{{` and `}}` escape one literal brace each. Keep the brace
        // in the literal segment — we emit a single `{` or `}` at
        // codegen time by slicing through this position once we strip
        // the duplicate.
        if (c == '{' && i + 1 < body.size() && body[i + 1] == '{') {
            // Close current literal up through this `{`, then advance
            // past the second `{` so the next literal starts fresh.
            flush_literal(i + 1);  // include the first `{`
            lit_start = i + 2;
            i += 2;
            continue;
        }
        if (c == '}' && i + 1 < body.size() && body[i + 1] == '}') {
            flush_literal(i + 1);  // include the first `}`
            lit_start = i + 2;
            i += 2;
            continue;
        }
        if (c == '}') {
            flush_literal(i);
            emit_diag(std::format(
                "unmatched `}}` at format-string offset {} (use `}}}}` to escape)",
                i));
            out.ok = false;
            return;
        }
        if (c != '{') {
            ++i;
            continue;
        }

        // Placeholder. `{` already at i; parse to matching `}`.
        flush_literal(i);
        size_t ph_start = i;
        ++i;  // past `{`

        FormatSegment seg{};
        seg.is_literal = false;

        // arg_id: integer (explicit position) or identifier (named).
        if (i < body.size()) {
            if (std::isdigit(static_cast<unsigned char>(body[i]))) {
                int32_t v = parse_uint(body, i);
                seg.arg_idx = v;
                if (v + 1 > out.max_explicit_plus_one)
                    out.max_explicit_plus_one = v + 1;
            } else if (std::isalpha(static_cast<unsigned char>(body[i])) || body[i] == '_') {
                auto nm = parse_name(body, i);
                seg.named.assign(nm.data(), nm.size());
            } else {
                seg.arg_idx = out.positional_count;
                ++out.positional_count;
            }
        }

        // Optional spec after `:`.
        if (i < body.size() && body[i] == ':') {
            ++i;  // past ':'

            // fill+align: lookahead to detect a 2-char fill+align prefix.
            // `(any char) (< | > | ^)` — but ONLY if the second char is
            // one of those alignment markers. Otherwise the first char
            // belongs to a later field (sign, '#', '0', width, type).
            if (i + 1 < body.size() &&
                (body[i + 1] == '<' || body[i + 1] == '>' || body[i + 1] == '^')) {
                seg.spec.fill = body[i];
                seg.spec.align =
                    body[i + 1] == '<' ? FormatAlign::Left :
                    body[i + 1] == '>' ? FormatAlign::Right
                                       : FormatAlign::Center;
                i += 2;
            } else if (i < body.size() &&
                       (body[i] == '<' || body[i] == '>' || body[i] == '^')) {
                // Bare alignment, default fill.
                seg.spec.align =
                    body[i] == '<' ? FormatAlign::Left :
                    body[i] == '>' ? FormatAlign::Right
                                   : FormatAlign::Center;
                ++i;
            }

            // sign
            if (i < body.size() && body[i] == '+') {
                seg.spec.sign = FormatSign::Plus;
                ++i;
            } else if (i < body.size() && body[i] == '-') {
                seg.spec.sign = FormatSign::Minus;
                ++i;
            }

            // alt `#`
            if (i < body.size() && body[i] == '#') {
                seg.spec.alt = true;
                ++i;
            }

            // zero-pad `0`
            if (i < body.size() && body[i] == '0') {
                seg.spec.zero = true;
                ++i;
            }

            // width
            int32_t w = parse_uint(body, i);
            if (w >= 0) seg.spec.width = w;

            // `.` precision
            if (i < body.size() && body[i] == '.') {
                ++i;
                int32_t p = parse_uint(body, i);
                if (p < 0) {
                    emit_diag(std::format(
                        "format spec at offset {}: `.` must be followed by a precision number",
                        ph_start));
                    out.ok = false;
                    return;
                }
                seg.spec.precision = p;
            }

            // type
            if (i < body.size() && body[i] != '}') {
                char t = body[i];
                switch (t) {
                case '?': seg.spec.trait_kind = FormatTrait::Debug;    break;
                case 'x': seg.spec.trait_kind = FormatTrait::LowerHex; break;
                case 'X': seg.spec.trait_kind = FormatTrait::UpperHex; break;
                case 'o': seg.spec.trait_kind = FormatTrait::Octal;    break;
                case 'b': seg.spec.trait_kind = FormatTrait::Binary;   break;
                case 'e': seg.spec.trait_kind = FormatTrait::LowerExp; break;
                case 'E': seg.spec.trait_kind = FormatTrait::UpperExp; break;
                default:
                    emit_diag(std::format(
                        "format spec at offset {}: unknown type char '{}'",
                        ph_start, t));
                    out.ok = false;
                    return;
                }
                ++i;
            }
        }

        // Expect `}`.
        if (i >= body.size() || body[i] != '}') {
            emit_diag(std::format(
                "unmatched `{{` at format-string offset {}", ph_start));
            out.ok = false;
            return;
        }
        ++i;  // past `}`

        out.segments.push_back(std::move(seg));
        lit_start = i;
    }

    flush_literal(body.size());
}

} // namespace logos::compiler
