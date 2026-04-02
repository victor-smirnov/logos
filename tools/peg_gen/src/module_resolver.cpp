// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov

#include "module_resolver.hpp"
#include "grammar_parser.hpp"
#include "grammar_ast.hpp"

#include <print>
#include <filesystem>
#include <unordered_set>

namespace fs = std::filesystem;
namespace ast = logos::peg_gen::ast;

namespace logos::peg_gen {

std::optional<std::vector<ResolvedModule>>
resolve_modules(const std::string& root_path) {
    // TODO: implement topological import resolution
    // Algorithm:
    //   1. Parse root grammar
    //   2. For each %import, resolve path relative to importing file
    //   3. Recursively parse imported grammars
    //   4. Detect cycles (path already in in-progress set)
    //   5. Return in topological order (dependencies first, root last)
    std::println(stderr, "peg_gen: module_resolver not yet implemented ({})", root_path);
    return std::nullopt;
}

} // namespace logos::peg_gen
