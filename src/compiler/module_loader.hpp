// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos
//
// Module loader — resolves `use` declarations by finding and parsing
// dependent .logos files on disk.

#pragma once

#include <logos/hermes/document.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace logos::compiler {

// Parsed module: source path + Hermes AST.
struct ParsedModule {
    std::string    path;
    std::string    package;               // dotted package name (e.g. "std.io"); may be empty
    hermes::Hermes ast;
    bool           from_binary_module = false;  // loaded from a .hermes0 in a .a archive
};

// Load a .logos file and all its transitive dependencies.
// search_paths: directories to search for package files (e.g. {"stdlib"}).
// Returns all modules in dependency order (dependencies first, root last).
std::vector<ParsedModule> load_modules(
    const std::string& root_path,
    const std::vector<std::string>& search_paths) noexcept;

} // namespace logos::compiler
