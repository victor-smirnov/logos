// Logos project — https://github.com/victor-smirnov/logos

#pragma once

#include <string>
#include <string_view>
#include <logos/hermes/document.hpp>
#include <logos/core/expected.hpp>

namespace logos::hermes {

// Parse a Jinja-like template string into an AST (stored as Hermes ObjectArray).
// Template syntax:
//   {{ expression }}       — output expression result
//   {% for x in expr %}    — loop
//   {% if expr %}          — conditional
//   {% elif expr %}        — else-if
//   {% else %}             — else
//   {% endif %}            — end if
//   {% endfor %}           — end for
//   {% set x = expr %}     — set variable
//   {%- / -%} / {%+ / +%} — whitespace control
[[nodiscard]] logos::expected<Hermes>    parse_template(std::string_view tpl) noexcept;

// Render a parsed template against data. Returns the rendered string.
[[nodiscard]] logos::expected<std::string>  render_template(const Hermes& tpl,
                                                             const Hermes& data) noexcept;

// Convenience: parse + render in one step.
[[nodiscard]] logos::expected<std::string>  render(std::string_view tpl,
                                                    const Hermes& data) noexcept;

} // namespace logos::hermes
