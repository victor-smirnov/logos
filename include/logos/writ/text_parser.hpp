// Logos project — https://github.com/victor-smirnov/logos

#pragma once

#include <string_view>

#include <logos/writ/document.hpp>
#include <logos/core/expected.hpp>

namespace logos::writ {

// Parse JSON-compatible text into a fresh Hermes document. The JSON core round-trips
// exactly with stringify(): null → null; true/false → Bool Pod; an integer that fits
// 56 bits → an inline i56 Pod (else a boxed i64); a float → a boxed f64; "string" →
// HString; [..] → HArray<HAny>; {"k":v} → HMap<HString,HAny>. Returns parse_error on
// malformed input.
[[nodiscard]] logos::expected<HermesCtr> text_parse(std::string_view text) noexcept;

} // namespace logos::writ
