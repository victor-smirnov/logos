// sema_fmt.hpp — format-string parser for the `format!()` family of
// macros (slice 4.4 of fn-macros). Sema parses a format string into a
// structured sequence of literal text + typed placeholders so that
//   (a) slice 4.2 arity / brace-balance validation runs against the
//       same parsed form, and
//   (b) slice 4.4b lowers each placeholder to an explicit trait call
//       (Display::fmt, Debug::dbg, LowerHex::fmt, …) instead of
//       routing every arg through the variadic runtime.

#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <vector>
#include <cstdint>

namespace logos::compiler {

enum class FormatTrait : uint8_t {
    Display,    // `{}`
    Debug,      // `{:?}`
    LowerHex,   // `{:x}`
    UpperHex,   // `{:X}`
    Octal,      // `{:o}`
    Binary,     // `{:b}`
    LowerExp,   // `{:e}`  (future)
    UpperExp,   // `{:E}`  (future)
};

enum class FormatAlign : uint8_t {
    None,    // unspecified — caller chooses default per trait
    Left,    // `<`
    Right,   // `>`
    Center,  // `^`
};

enum class FormatSign : uint8_t {
    None,
    Plus,    // `+`
    Minus,   // `-` (rare; Rust reserves but doesn't render)
};

struct FormatSpec {
    char         fill      = ' ';
    FormatAlign  align     = FormatAlign::None;
    FormatSign   sign      = FormatSign::None;
    bool         alt       = false;   // `#` prefix (0x/0o/0b)
    bool         zero      = false;   // `0` pad with zeros (overridden by fill)
    int32_t      width     = -1;      // -1 = unset
    int32_t      precision = -1;      // -1 = unset; >=0 = exact precision
    FormatTrait  trait_kind = FormatTrait::Display;
};

struct FormatSegment {
    bool is_literal;
    // For literal segments: slice into the format-string body
    // (no quote-stripping needed by the caller).
    std::string_view lit_text;
    // For placeholder segments:
    //   arg_idx = positional auto-counter (0,1,2,…) or explicit index;
    //   -1 means "use named lookup" once that lands.
    int32_t arg_idx = -1;
    std::string named;        // empty unless explicit `{name}`
    FormatSpec spec;
};

struct FormatParseResult {
    std::vector<FormatSegment> segments;
    // Number of POSITIONAL args (consecutive `{}`/`{:spec}` without an
    // explicit index). Drives arity validation when none of the
    // placeholders use named or explicit-index forms.
    int32_t positional_count = 0;
    // Highest explicit `{N}` index seen + 1, or 0 if none.
    int32_t max_explicit_plus_one = 0;
    // Set on the first hard parse error; subsequent errors append to
    // the diag stream but parsing halts at the broken placeholder.
    bool ok = true;
};

// Parse `body` (format-string content WITHOUT surrounding quotes) into
// segments. Emits diagnostics through `emit_diag` — caller wraps them
// with file:line context. Always returns; check `out.ok` for hard
// failures. Soft issues (e.g. unsupported spec char) emit a diag but
// continue with a best-effort segment.
void parse_format_string(
    std::string_view body,
    FormatParseResult& out,
    const std::function<void(std::string)>& emit_diag);

// Convert trait kind to its method-name + (eventual) stdlib trait name.
const char* format_trait_method(FormatTrait t);   // "fmt", "dbg", …
const char* format_trait_name  (FormatTrait t);   // "Display", "Debug", …
// Free-fn dispatcher in std.fmt that binds the trait via a generic
// bound — sema-resident lowering calls this instead of `x.method()`
// to bypass the slice/str method short-circuit.
const char* format_trait_dispatcher(FormatTrait t);

} // namespace logos::compiler
