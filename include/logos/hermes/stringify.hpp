// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos

#pragma once

#include <string>
#include <logos/hermes/document.hpp>
#include <logos/core/expected.hpp>

namespace logos::hermes {

// Convert a Hermes document to its text representation.
[[nodiscard]] logos::expected<std::string> stringify(const Hermes& doc, bool pretty = false) noexcept;

} // namespace logos::hermes
