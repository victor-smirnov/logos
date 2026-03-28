// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos

#pragma once

#include <string_view>
#include <logos/hermes/document.hpp>

namespace logos::hermes {

// Parse a Hermes text document into a HermesCtr.
// Throws std::runtime_error on parse failure with line:column info.
HermesCtr parse(std::string_view text);

} // namespace logos::hermes
