// Logos project — https://github.com/victor-smirnov/logos
//
// ModuleResolver: resolves %import directives and builds the dependency graph.
//
// Given a root grammar, recursively parses all imported grammars and returns
// them in topological order (dependencies first). This ensures that when
// generating code, imported parsers are declared before they are referenced.
//
// Import resolution:
//   1. Path is relative to the importing grammar file's directory.
//   2. Circular imports are detected and reported as errors.
//   3. Diamond imports (A imports B and C, both import D) are fine — D is
//      included once, in topological order.
//
// The resolved module set is a flat list of (alias → grammar document) pairs
// in dependency order. The root grammar is last.

#pragma once

#include <string>
#include <vector>
#include <optional>
#include <logos/hermes2/view.hpp>
#include <logos/hermes2/compat.hpp>

namespace logos::peg_gen {

struct ResolvedModule {
    std::string  path;           // absolute path to the .peg file
    std::string  alias;          // import alias (empty for the root module)
    logos::hermes2::Hermes grammar;  // parsed grammar document
};

// Resolve all imports starting from root_path.
// Returns modules in topological order (root grammar last).
// Returns nullopt if any import cannot be resolved or a cycle is detected.
std::optional<std::vector<ResolvedModule>>
resolve_modules(const std::string& root_path);

} // namespace logos::peg_gen
