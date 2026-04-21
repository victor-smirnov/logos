// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos

#include "module_loader.hpp"
#include "logos_parser.hpp"
#include <chrono>
#include <cstdlib>

#include <logos/compiler/ast.hpp>
#include <logos/hermes/document.hpp>
#include <logos/hermes/tiny_object_map.hpp>
#include <logos/hermes/object_array.hpp>
#include <logos/hermes/arena_string.hpp>
#include <logos/hermes/any_val.hpp>

#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <cstdio>
#include <cctype>

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
static std::vector<std::string> extract_uses(hermes::HermesView ast) {
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

        // Reconstruct dotted path from NAME + PATH_PARTS.
        // `use a.b.c;` → NAME="a", PATH_PARTS=[{NAME:"b"},{NAME:"c"}] → "a.b.c".
        std::string dotted;
        if (use_node.has_key(la::NAME)) {
            AnyVal name_av = use_node.get(la::NAME);
            if (!name_av.is_null() && name_av.is_pointer())
                dotted = std::string(StringView(name_av.to_offset(), holder).view());
        }
        if (use_node.has_key(la::mod::PATH_PARTS)) {
            AnyVal parts_av = use_node.get(la::mod::PATH_PARTS);
            if (!parts_av.is_null() && parts_av.is_pointer()) {
                auto parts = ArrayView(parts_av.to_offset(), holder);
                for (uint64_t pi = 0; pi < parts.size(); ++pi) {
                    AnyVal part_av = parts.get(pi);
                    if (part_av.is_null() || !part_av.is_pointer()) continue;
                    auto part = TinyMapView(part_av.to_offset(), holder);
                    if (!part.has_key(la::NAME)) continue;
                    AnyVal pn = part.get(la::NAME);
                    if (pn.is_null() || !pn.is_pointer()) continue;
                    if (!dotted.empty()) dotted += '.';
                    dotted += std::string(StringView(pn.to_offset(), holder).view());
                }
            }
        }
        if (!dotted.empty())
            result.push_back(dotted);
    }

    return result;
}

// Cheap text-level scan for a file's `package` declaration.
// Scans the first few non-comment, non-blank lines for `package <dotted>;`.
// Returns empty string if not found. Matches the grammar loosely — no
// attempt to validate, only to extract the name.
static std::string scan_package_decl(const std::string& path) {
    std::ifstream f(path);
    if (!f) return {};
    std::string line;
    int lines_seen = 0;
    bool in_block_comment = false;
    while (std::getline(f, line) && lines_seen < 200) {
        ++lines_seen;
        // Strip leading whitespace.
        size_t i = 0;
        while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i]))) ++i;
        // Handle block comments crudely.
        if (in_block_comment) {
            auto end = line.find("*/", i);
            if (end == std::string::npos) continue;
            i = end + 2;
            in_block_comment = false;
            while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i]))) ++i;
        }
        if (i + 1 < line.size() && line[i] == '/' && line[i + 1] == '*') {
            in_block_comment = true;
            continue;
        }
        if (i + 1 < line.size() && line[i] == '/' && line[i + 1] == '/') continue;
        if (i >= line.size()) continue;
        // Look for "package".
        const std::string_view kw = "package";
        if (line.compare(i, kw.size(), kw) != 0) return {};
        i += kw.size();
        if (i >= line.size() || !std::isspace(static_cast<unsigned char>(line[i]))) return {};
        while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i]))) ++i;
        // Consume dotted identifier.
        std::string name;
        while (i < line.size()) {
            char c = line[i];
            if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.') {
                name += c;
                ++i;
            } else break;
        }
        return name;
    }
    return {};
}

// Package → list of .logos file paths (canonical).
using PackageIndex = std::unordered_map<std::string, std::vector<std::string>>;

// Walk all search paths recursively, collecting every .logos file and
// bucketing by its declared `package` name. Files without a package
// declaration are skipped (not reachable via `use`).
static PackageIndex build_package_index(const std::vector<std::string>& search_paths) {
    PackageIndex idx;
    std::unordered_set<std::string> seen;
    for (const auto& dir : search_paths) {
        std::error_code ec;
        if (!fs::exists(dir, ec) || !fs::is_directory(dir, ec)) continue;
        for (auto it = fs::recursive_directory_iterator(
                 dir, fs::directory_options::skip_permission_denied, ec);
             !ec && it != fs::recursive_directory_iterator();
             it.increment(ec))
        {
            if (!it->is_regular_file(ec)) continue;
            if (it->path().extension() != ".logos") continue;
            auto canonical = fs::weakly_canonical(it->path(), ec).string();
            if (ec) continue;
            if (!seen.insert(canonical).second) continue;
            auto pkg = scan_package_decl(canonical);
            if (pkg.empty()) continue;
            idx[pkg].push_back(canonical);
        }
    }
    // Stable order for reproducible diagnostics and deterministic AST sequence.
    for (auto& [_, paths] : idx) std::sort(paths.begin(), paths.end());
    return idx;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

std::vector<ParsedModule> load_modules(
    const std::string& root_path,
    const std::vector<std::string>& search_paths) noexcept
{
    const bool trace = std::getenv("LOGOS_TRACE_PHASES") != nullptr;

    PackageIndex index = build_package_index(search_paths);
    if (trace) {
        size_t total_files = 0;
        for (auto& [_, v] : index) total_files += v.size();
        std::fprintf(stderr, "module_loader: indexed %zu package(s), %zu file(s)\n",
                     index.size(), total_files);
    }

    std::vector<ParsedModule> modules;
    std::unordered_set<std::string> visited_packages;
    std::unordered_set<std::string> visited_files;

    // Parse one .logos file. On success returns the AST and its use-list;
    // on failure logs to stderr and returns empty.
    auto parse_one = [&](const std::string& canonical)
        -> std::pair<hermes::Hermes, std::vector<std::string>>
    {
        auto source = read_file(canonical);
        if (source.empty()) {
            std::fprintf(stderr, "module_loader: cannot read '%s'\n", canonical.c_str());
            return {};
        }
        LogosParser parser(source);
        auto pt0 = std::chrono::steady_clock::now();
        auto ast = parser.parse_module();
        if (trace) {
            auto pt1 = std::chrono::steady_clock::now();
            auto us = std::chrono::duration_cast<std::chrono::microseconds>(pt1 - pt0).count();
            std::fprintf(stderr, "  parse %8lldus (%zu bytes) %s\n",
                         (long long)us, source.size(), canonical.c_str());
        }
        if (ast.is_null()) {
            std::fprintf(stderr, "module_loader: parse failed for '%s'\n", canonical.c_str());
            return {};
        }
        if (!parser.at_eof()) {
            auto fur_line = parser.furthest_line();
            auto nxt_line = parser.next_line();
            std::string_view err_text;
            uint32_t         err_line;
            if (fur_line > nxt_line && !parser.furthest_text().empty()) {
                err_text = parser.furthest_text();
                err_line = fur_line;
            } else {
                err_text = parser.next_text();
                err_line = nxt_line;
            }
            std::fprintf(stderr,
                "error [%s]: syntax error near '%.*s' at line %u\n",
                canonical.c_str(),
                static_cast<int>(err_text.size()), err_text.data(),
                err_line);
            return {};
        }
        auto uses = extract_uses(ast);
        return {std::move(ast), std::move(uses)};
    };

    std::function<void(const std::string&)> visit_package;
    std::function<void(const std::string&, const std::string&)> visit_file;

    // Visit a single file (used for the root entry point, which is addressed
    // by path rather than by package name).
    visit_file = [&](const std::string& canonical, const std::string& declared_pkg) {
        if (!visited_files.insert(canonical).second) return;
        auto [ast, uses] = parse_one(canonical);
        if (ast.is_null()) return;
        // If the root file declares a package, load all of its sibling files
        // through the package mechanism before recursing into use-deps.
        if (!declared_pkg.empty() && index.count(declared_pkg)) {
            // Mark this package as in-progress so sibling parse below doesn't
            // recurse back into us. But siblings (other than this file) still
            // need to be parsed.
            if (visited_packages.insert(declared_pkg).second) {
                for (const auto& sib : index.at(declared_pkg)) {
                    if (sib == canonical) continue;
                    if (!visited_files.insert(sib).second) continue;
                    auto [sast, suses] = parse_one(sib);
                    if (sast.is_null()) continue;
                    for (const auto& u : suses) visit_package(u);
                    modules.push_back({sib, std::move(sast)});
                }
            }
        }
        // Recurse into this file's own uses (post-order).
        for (const auto& u : uses) visit_package(u);
        modules.push_back({canonical, std::move(ast)});
    };

    // Visit every file belonging to a package, post-order on dependencies.
    visit_package = [&](const std::string& pkg) {
        if (!visited_packages.insert(pkg).second) return;
        auto it = index.find(pkg);
        if (it == index.end()) {
            std::fprintf(stderr, "module_loader: cannot find package '%s'\n", pkg.c_str());
            return;
        }
        // Parse all files of this package first so we can collect all their
        // uses before recursing (ensures even a single-file package behaves
        // identically to the legacy path).
        struct Pending { std::string path; hermes::Hermes ast; };
        std::vector<Pending> pending;
        std::vector<std::string> pkg_uses;
        for (const auto& file : it->second) {
            if (!visited_files.insert(file).second) continue;
            auto [ast, uses] = parse_one(file);
            if (ast.is_null()) continue;
            for (auto& u : uses) pkg_uses.push_back(std::move(u));
            pending.push_back({file, std::move(ast)});
        }
        // Recurse into dependencies (post-order).
        for (const auto& u : pkg_uses) visit_package(u);
        // Emit this package's files.
        for (auto& p : pending) modules.push_back({std::move(p.path), std::move(p.ast)});
    };

    // Kick off traversal at the root file. We need its package name to know
    // whether to pull in siblings — do a cheap scan first.
    auto root_canonical = fs::weakly_canonical(root_path).string();
    auto root_pkg = scan_package_decl(root_canonical);
    visit_file(root_canonical, root_pkg);

    return modules;
}

} // namespace logos::compiler
