// Rule files embedded into logosc at build time (cmake/DlEmbed.cmake, ADR 0028).
#pragma once

#include <optional>
#include <string_view>

namespace logos::dl {

// Text of src/compiler/dl/rules/<name>, or nullopt for an unknown name.
std::optional<std::string_view> embedded_rule_file(std::string_view name);

} // namespace logos::dl
