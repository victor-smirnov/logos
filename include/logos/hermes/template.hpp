// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos

#pragma once

#include <string>
#include <string_view>
#include <logos/hermes/document.hpp>

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
HermesCtr parse_template(std::string_view tpl);

// Render a parsed template against data. Returns the rendered string.
std::string render_template(const HermesCtr& tpl, const HermesCtr& data);

// Convenience: parse + render in one step.
std::string render(std::string_view tpl, const HermesCtr& data);

} // namespace logos::hermes
