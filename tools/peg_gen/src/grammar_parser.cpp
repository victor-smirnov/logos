// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov

#include "grammar_parser.hpp"
#include <print>

namespace logos::peg_gen {

std::optional<logos::hermes::HermesCtr>
parse_grammar(const std::string& path) {
    // TODO: implement .peg file parser
    std::println(stderr, "peg_gen: grammar_parser not yet implemented ({})", path);
    return std::nullopt;
}

std::optional<logos::hermes::HermesCtr>
parse_grammar_string(std::string_view source, std::string_view source_name) {
    // TODO: implement
    std::println(stderr, "peg_gen: parse_grammar_string not yet implemented ({})", source_name);
    return std::nullopt;
}

} // namespace logos::peg_gen
