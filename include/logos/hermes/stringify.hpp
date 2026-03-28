#pragma once

#include <string>
#include <logos/hermes/document.hpp>

namespace logos::hermes {

// Convert a Hermes document to its text representation.
std::string stringify(const HermesCtr& doc, bool pretty = false);

} // namespace logos::hermes
