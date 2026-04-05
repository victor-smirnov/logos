// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos

#include "module_loader.hpp"
#include "logos_parser.hpp"

#include <logos/compiler/ast.hpp>
#include <logos/hermes/document.hpp>
#include <logos/hermes/tiny_object_map.hpp>
#include <logos/hermes/object_array.hpp>
#include <logos/hermes/arena_string.hpp>
#include <logos/hermes/any_val.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_set>
#include <cstdio>

namespace logos::compiler {

namespace la = logos::compiler::ast;
namespace fs = std::filesystem;
using hermes::TinyMapView;
using hermes::ArrayView;
using hermes::StringView;
using hermes::AnyVal;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::string read_file(const std::string& path) {
    std::ifstream f(path);
    if (!f) return {};
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// Extract use declarations from a parsed module AST.
// Returns dotted package paths (e.g. "std.io").
static std::vector<std::string> extract_uses(hermes::HermesCtrView ast) {
    std::vector<std::string> result;
    auto holder = ast.holder();
    auto root = ast.root_object().as_tiny_map();

    if (!root.has_key(la::USES)) return result;
    AnyVal uses_av = root.get(la::USES);
    if (uses_av.is_null() || !uses_av.is_pointer()) return result;

    auto uses = ArrayView(uses_av.to_offset(), holder);
    for (uint64_t i = 0; i < uses.size(); ++i) {
        AnyVal use_av = uses.get(i);
        if (use_av.is_null() || !use_av.is_pointer()) continue;
        auto use_node = TinyMapView(use_av.to_offset(), holder);

        // Reconstruct dotted path from NAME + PATH fields.
        // use std.io; → NAME="std", PATH="io" → "std.io"
        std::string dotted;
        if (use_node.has_key(la::NAME)) {
            AnyVal name_av = use_node.get(la::NAME);
            if (!name_av.is_null() && name_av.is_pointer())
                dotted = std::string(StringView(name_av.to_offset(), holder).view());
        }
        if (use_node.has_key(la::PATH)) {
            AnyVal path_av = use_node.get(la::PATH);
            if (!path_av.is_null() && path_av.is_pointer()) {
                if (!dotted.empty()) dotted += '.';
                dotted += std::string(StringView(path_av.to_offset(), holder).view());
            }
        }
        if (!dotted.empty())
            result.push_back(dotted);
    }

    return result;
}

// Convert a dotted package path to a filesystem path.
// "std.io" + search_path "stdlib" → "stdlib/std/io.logos"
static std::string resolve_package_path(
    std::string_view dotted_name,
    const std::vector<std::string>& search_paths)
{
    // Convert dots to slashes.
    std::string rel_path;
    for (char c : dotted_name) {
        rel_path += (c == '.') ? '/' : c;
    }
    rel_path += ".logos";

    for (const auto& dir : search_paths) {
        auto full = fs::path(dir) / rel_path;
        if (fs::exists(full)) {
            return full.string();
        }
    }
    return {};
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

std::vector<ParsedModule> load_modules(
    const std::string& root_path,
    const std::vector<std::string>& search_paths) noexcept
{
    std::vector<ParsedModule> modules;
    std::unordered_set<std::string> loaded;  // paths already loaded

    // BFS queue of files to load.
    std::vector<std::string> queue;
    queue.push_back(root_path);

    while (!queue.empty()) {
        auto path = queue.back();
        queue.pop_back();

        // Normalize path.
        auto canonical = fs::weakly_canonical(path).string();
        if (loaded.count(canonical)) continue;
        loaded.insert(canonical);

        // Read and parse.
        auto source = read_file(canonical);
        if (source.empty()) {
            std::fprintf(stderr, "module_loader: cannot read '%s'\n", canonical.c_str());
            continue;
        }

        LogosParser parser(source);
        auto ast = parser.parse_module();
        if (ast.is_null()) {
            std::fprintf(stderr, "module_loader: parse failed for '%s'\n", canonical.c_str());
            continue;
        }

        // Extract use declarations and queue dependencies.
        auto uses = extract_uses(ast);
        for (const auto& pkg : uses) {
            auto dep_path = resolve_package_path(pkg, search_paths);
            if (dep_path.empty()) {
                std::fprintf(stderr, "module_loader: cannot find package '%s' (used by '%s')\n",
                             pkg.c_str(), canonical.c_str());
            } else if (!loaded.count(fs::weakly_canonical(dep_path).string())) {
                queue.push_back(dep_path);
            }
        }

        modules.push_back({canonical, std::move(ast)});
    }

    // Reverse: dependencies first, root last.
    // (BFS loaded root first, deps after — reverse gives deps-first order.)
    std::reverse(modules.begin(), modules.end());

    return modules;
}

} // namespace logos::compiler
