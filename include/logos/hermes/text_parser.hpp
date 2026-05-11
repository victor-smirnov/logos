// Logos project — https://github.com/victor-smirnov/logos

#pragma once

#include <string_view>
#include <logos/hermes/document.hpp>
#include <logos/core/expected.hpp>

namespace logos::hermes {

// Parse a Hermes text document into a Hermes.
// Returns an error (ErrCode::parse_error) on parse failure with line:column info.
[[nodiscard]] logos::expected<Hermes> parse(std::string_view text) noexcept;

} // namespace logos::hermes
