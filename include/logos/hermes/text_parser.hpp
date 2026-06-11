// Logos project — https://github.com/victor-smirnov/logos

#pragma once

#include <string_view>

#include <logos/hermes/document.hpp>
#include <logos/core/expected.hpp>

namespace logos::hermes {

// Parse JSON-compatible text into a fresh Hermes2 document. The JSON core round-trips
// exactly with stringify(): null → null; true/false → Bool Pod; an integer that fits
// 56 bits → an inline i56 Pod (else a boxed i64); a float → a boxed f64; "string" →
// HString; [..] → HArray<HVal>; {"k":v} → HMap<HString,HVal>. Returns parse_error on
// malformed input.
[[nodiscard]] logos::expected<HermesCtr> text_parse(std::string_view text) noexcept;

} // namespace logos::hermes
