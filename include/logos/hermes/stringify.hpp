// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos

#pragma once

#include <string>
#include <logos/hermes/document.hpp>

namespace logos::hermes {

// Convert a Hermes document to its text representation.
std::string stringify(const HermesCtr& doc, bool pretty = false);

} // namespace logos::hermes
