// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos

#include "module_loader.hpp"
#include "logos_parser.hpp"
#include <chrono>
#include <cstdlib>

#include <logos/compiler/ast.hpp>
#include <logos/hermes/binary_codec.hpp>
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
#include <cstring>

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

// ---------------------------------------------------------------------------
// Binary module loader (M5)
// ---------------------------------------------------------------------------
//
// AR format: "!<arch>\n" + 60-byte member headers + data.
// .hermes0 format: "HERMAST0" magic + uint32 version + uint32 num_files +
//   per-file: uint32 path_len + path + uint64 ast_len + ast bytes.
// ---------------------------------------------------------------------------

static uint32_t read_le_u32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0])
         | (static_cast<uint32_t>(p[1]) << 8)
         | (static_cast<uint32_t>(p[2]) << 16)
         | (static_cast<uint32_t>(p[3]) << 24);
}

static uint64_t read_le_u64(const uint8_t* p) {
    uint64_t lo = read_le_u32(p);
    uint64_t hi = read_le_u32(p + 4);
    return lo | (hi << 32);
}

// Read member data from an AR archive by member name suffix.
// Returns the raw bytes of the first matching member, or empty on failure.
static std::vector<uint8_t> ar_read_member(const std::string& archive_path,
                                            const std::string& member_suffix) {
    // Read entire archive into memory to avoid seekg issues with large files.
    std::ifstream f(archive_path, std::ios::binary | std::ios::ate);
    if (!f) return {};
    auto file_size = static_cast<size_t>(f.tellg());
    f.seekg(0);
    std::vector<uint8_t> raw(file_size);
    if (!f.read(reinterpret_cast<char*>(raw.data()), file_size)) return {};

    const uint8_t* p = raw.data();
    const uint8_t* end = p + file_size;

    // Check AR magic.
    if (file_size < 8 || std::memcmp(p, "!<arch>\n", 8) != 0) return {};
    p += 8;

    while (p + 60 <= end) {
        // 60-byte AR member header.
        const uint8_t* hdr = p;

        // Name field is 16 bytes, right-padded with spaces.
        std::string name(reinterpret_cast<const char*>(hdr), 16);
        while (!name.empty() && (name.back() == ' ' || name.back() == '/'))
            name.pop_back();

        // Size field is 10 bytes (decimal ASCII).
        char size_str[11] = {};
        std::memcpy(size_str, hdr + 48, 10);
        long long member_size = std::atoll(size_str);
        if (member_size < 0 || p + 60 + member_size > end) break;

        // Check end-of-header magic "`\n".
        if (hdr[58] != '`' || hdr[59] != '\n') break;

        const uint8_t* data_start = hdr + 60;

        // Does this member's name end with the requested suffix?
        bool match = name.size() >= member_suffix.size() &&
                     name.compare(name.size() - member_suffix.size(),
                                  member_suffix.size(), member_suffix) == 0;

        if (match) {
            return std::vector<uint8_t>(data_start, data_start + member_size);
        }

        // Advance to next member (size padded to even boundary).
        p = data_start + member_size + (member_size & 1);
    }
    return {};
}

// Parse a .hermes0 blob and return decoded ParsedModules.
static std::vector<ParsedModule> parse_hermes0(const std::vector<uint8_t>& data,
                                                const std::string& archive_path) {
    const uint8_t* p = data.data();
    const uint8_t* end = p + data.size();

    if (data.size() < 16) {
        std::fprintf(stderr, "module_loader: %s: .hermes0 too small\n", archive_path.c_str());
        return {};
    }
    if (std::memcmp(p, "HERMAST0", 8) != 0) {
        std::fprintf(stderr, "module_loader: %s: bad .hermes0 magic\n", archive_path.c_str());
        return {};
    }
    uint32_t version   = read_le_u32(p + 8);
    uint32_t num_files = read_le_u32(p + 12);
    if (version != 2) {
        std::fprintf(stderr, "module_loader: %s: unsupported .hermes0 version %u (want 2)\n",
                     archive_path.c_str(), version);
        return {};
    }
    p += 16;

    std::vector<ParsedModule> result;
    for (uint32_t i = 0; i < num_files; ++i) {
        if (p + 4 > end) { std::fprintf(stderr, "module_loader: .hermes0 truncated\n"); return {}; }
        uint32_t path_len = read_le_u32(p); p += 4;
        if (p + path_len > end) { std::fprintf(stderr, "module_loader: .hermes0 truncated path\n"); return {}; }
        std::string path(reinterpret_cast<const char*>(p), path_len); p += path_len;

        if (p + 4 > end) { std::fprintf(stderr, "module_loader: .hermes0 truncated pkg\n"); return {}; }
        uint32_t pkg_len = read_le_u32(p); p += 4;
        if (p + pkg_len > end) { std::fprintf(stderr, "module_loader: .hermes0 truncated pkg data\n"); return {}; }
        std::string pkg(reinterpret_cast<const char*>(p), pkg_len); p += pkg_len;

        if (p + 8 > end) { std::fprintf(stderr, "module_loader: .hermes0 truncated size\n"); return {}; }
        uint64_t ast_len = read_le_u64(p); p += 8;
        if (p + ast_len > end) { std::fprintf(stderr, "module_loader: .hermes0 truncated ast\n"); return {}; }

        auto decoded = hermes::binary_decode(p, static_cast<size_t>(ast_len));
        p += ast_len;

        if (!decoded) {
            std::fprintf(stderr, "module_loader: binary_decode failed for %s in %s\n",
                         path.c_str(), archive_path.c_str());
            return {};
        }
        result.push_back({path, pkg, std::move(*decoded)});
    }
    return result;
}

// ---------------------------------------------------------------------------
// Binary package index: package_name → archive_path
// ---------------------------------------------------------------------------
//
// We scan each lib*.a for a .hermes0 member, peek at the stored file paths,
// extract their `package` declarations (via cheap text scan of the stored paths),
// and build a map from every package provided by the archive to its path.
//
// This is done once at startup; the cost is O(#archives × #files_per_archive).
// For a typical stdlib with 50 files it's negligible.
// ---------------------------------------------------------------------------

// Scan a v2 .hermes0 blob to extract the list of package names it contains.
// Version 2 stores pkg_len+pkg explicitly — no AST decode needed.
static std::vector<std::string>
hermes0_packages(const std::vector<uint8_t>& data) {
    const uint8_t* p = data.data();
    const uint8_t* end = p + data.size();
    if (data.size() < 16 || std::memcmp(p, "HERMAST0", 8) != 0) return {};
    uint32_t version   = read_le_u32(p + 8);
    uint32_t num_files = read_le_u32(p + 12);
    if (version != 2) return {};  // only v2 has explicit pkg names
    p += 16;

    std::vector<std::string> pkgs;
    std::unordered_set<std::string> seen;
    for (uint32_t i = 0; i < num_files; ++i) {
        if (p + 4 > end) break;
        uint32_t path_len = read_le_u32(p); p += 4;
        if (p + path_len > end) break;
        p += path_len;  // skip path

        if (p + 4 > end) break;
        uint32_t pkg_len = read_le_u32(p); p += 4;
        if (p + pkg_len > end) break;
        std::string pkg(reinterpret_cast<const char*>(p), pkg_len); p += pkg_len;

        if (p + 8 > end) break;
        uint64_t ast_len = read_le_u64(p); p += 8;
        // Guard against corrupted ast_len that would overflow pointer arithmetic.
        if (ast_len > static_cast<uint64_t>(end - p)) break;
        p += ast_len;  // skip AST bytes

        if (!pkg.empty() && seen.insert(pkg).second)
            pkgs.push_back(pkg);
    }
    return pkgs;
}

// Scan search paths for lib*.a files. Returns map: package_name → archive_path.
// Each archive may provide multiple packages (e.g. libstdlib.a provides std.*, hermes.*, etc.)
static std::unordered_map<std::string, std::string>
build_binary_index(const std::vector<std::string>& search_paths) {
    std::unordered_map<std::string, std::string> idx;
    for (const auto& dir : search_paths) {
        std::error_code ec;
        if (!fs::exists(dir, ec) || !fs::is_directory(dir, ec)) continue;
        for (auto it = fs::directory_iterator(dir, ec); !ec && it != fs::end(it); it.increment(ec)) {
            if (!it->is_regular_file(ec)) continue;
            auto p = it->path();
            if (p.extension() != ".a") continue;
            auto stem = p.stem().string();
            if (stem.size() < 4 || stem.substr(0, 3) != "lib") continue;
            auto archive = fs::weakly_canonical(p, ec).string();
            // Read .hermes0 to discover which packages this archive provides.
            auto member = ar_read_member(archive, ".hermes0");
            if (member.empty()) continue;
            auto pkgs = hermes0_packages(member);
            for (auto& pkg : pkgs) {
                if (!idx.count(pkg)) idx[pkg] = archive;
            }
        }
    }
    return idx;
}

// ---------------------------------------------------------------------------
// Package → list of .logos file paths (canonical).
// ---------------------------------------------------------------------------
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
    const std::vector<std::string>& search_paths,
    bool* out_had_error,
    const std::vector<std::string>& extra_archive_files) noexcept
{
    const bool trace = std::getenv("LOGOS_TRACE_PHASES") != nullptr;
    bool had_error = false;

    PackageIndex index = build_package_index(search_paths);
    auto binary_index  = build_binary_index(search_paths);

    // Fold explicit `-l FILE` archives into the binary index. Each file
    // contributes its packages exactly like a *.a found in a search dir.
    for (const auto& f : extra_archive_files) {
        std::error_code ec;
        if (!fs::exists(f, ec) || !fs::is_regular_file(f, ec)) {
            std::fprintf(stderr, "module_loader: -l '%s' is not a file\n", f.c_str());
            had_error = true;
            continue;
        }
        auto archive = fs::weakly_canonical(f, ec).string();
        auto member  = ar_read_member(archive, ".hermes0");
        if (member.empty()) continue;
        auto pkgs = hermes0_packages(member);
        for (auto& pkg : pkgs) {
            if (!binary_index.count(pkg)) binary_index[pkg] = archive;
        }
    }

    if (trace) {
        size_t total_files = 0;
        for (auto& [_, v] : index) total_files += v.size();
        std::fprintf(stderr, "module_loader: indexed %zu text pkg(s), %zu file(s); %zu binary module(s)\n",
                     index.size(), total_files, binary_index.size());
    }

    std::vector<ParsedModule> modules;
    std::unordered_set<std::string> visited_packages;
    std::unordered_set<std::string> visited_files;

    // Cache: binary module name → decoded ParsedModules (loaded lazily once per archive).
    std::unordered_map<std::string, std::vector<ParsedModule>> binary_cache;

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
            uint32_t         err_col;
            if (fur_line > nxt_line && !parser.furthest_text().empty()) {
                err_text = parser.furthest_text();
                err_line = fur_line;
                err_col  = parser.furthest_column();
            } else {
                err_text = parser.next_text();
                err_line = nxt_line;
                err_col  = parser.next_column();
            }
            std::fprintf(stderr,
                "error [%s]: syntax error near '%.*s' at line %u col %u\n",
                canonical.c_str(),
                static_cast<int>(err_text.size()), err_text.data(),
                err_line, err_col);
            return {};
        }
        auto uses = extract_uses(ast);
        return {std::move(ast), std::move(uses)};
    };

    std::function<void(const std::string&)> visit_package;
    std::function<void(const std::string&, const std::string&)> visit_file;

    // Load and emit files belonging to `requested_pkg` plus the implicit
    // prelude. The prelude covers stdlib packages that define cross-cutting
    // traits and genos types (Send, Default, Ord, Map<K,V>, AnyVal, ...)
    // referenced by other stdlib modules without explicit `use` statements.
    // Other packages in the same archive load only when visited.
    auto pkg_in_prelude = [](std::string_view pkg) {
        auto starts = [&](std::string_view p) {
            return pkg.size() >= p.size() &&
                   pkg.compare(0, p.size(), p) == 0;
        };
        return starts("std.lang") || starts("std.hermes") || starts("std.mem");
    };
    auto visit_binary_module = [&](const std::string& cache_key,
                                   const std::string& archive_path,
                                   const std::string& requested_pkg) {
        auto cit = binary_cache.find(cache_key);
        if (cit == binary_cache.end()) {
            if (trace)
                std::fprintf(stderr, "module_loader: loading binary module from %s\n",
                             archive_path.c_str());
            auto member = ar_read_member(archive_path, ".hermes0");
            if (member.empty()) {
                std::fprintf(stderr, "module_loader: no .hermes0 in %s\n", archive_path.c_str());
                binary_cache[cache_key] = {};
                return;
            }
            auto decoded = parse_hermes0(member, archive_path);
            if (trace)
                std::fprintf(stderr, "module_loader: decoded %zu file(s) from %s\n",
                             decoded.size(), archive_path.c_str());
            cit = binary_cache.emplace(cache_key, std::move(decoded)).first;
        }

        auto wanted = [&](const std::string& pkg) {
            return pkg == requested_pkg || pkg_in_prelude(pkg);
        };

        std::vector<std::string> pkg_uses;
        for (auto& pm : cit->second) {
            if (!wanted(pm.package)) continue;
            auto uses = extract_uses(pm.ast);
            for (auto& u : uses) pkg_uses.push_back(std::move(u));
        }
        for (const auto& u : pkg_uses) visit_package(u);

        for (auto& pm : cit->second) {
            if (!wanted(pm.package)) continue;
            if (!visited_files.insert(pm.path).second) continue;
            if (!pm.package.empty()) visited_packages.insert(pm.package);
            modules.push_back({pm.path, pm.package, pm.ast, /*from_binary_module=*/true});
        }
    };

    // Visit every file belonging to a package, post-order on dependencies.
    visit_package = [&](const std::string& pkg) {
        if (!visited_packages.insert(pkg).second) return;

        // Check text index first (source build takes priority).
        auto it = index.find(pkg);
        if (it != index.end()) {
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
            for (const auto& u : pkg_uses) visit_package(u);
            for (auto& p : pending) modules.push_back({std::move(p.path), pkg, std::move(p.ast)});
            return;
        }

        // Check binary index: does any archive provide this package?
        auto bit = binary_index.find(pkg);
        if (bit != binary_index.end()) {
            // Use the archive path as a stable key for the binary cache.
            visit_binary_module(bit->second, bit->second, pkg);
            return;
        }

        std::fprintf(stderr, "module_loader: cannot find package '%s'\n", pkg.c_str());
        had_error = true;
    };

    // Visit a single file (used for the root entry point, which is addressed
    // by path rather than by package name).
    visit_file = [&](const std::string& canonical, const std::string& declared_pkg) {
        if (!visited_files.insert(canonical).second) return;
        auto [ast, uses] = parse_one(canonical);
        if (ast.is_null()) return;
        // If the root file declares a package, load all of its sibling files
        // through the package mechanism before recursing into use-deps.
        if (!declared_pkg.empty() && index.count(declared_pkg)) {
            if (visited_packages.insert(declared_pkg).second) {
                for (const auto& sib : index.at(declared_pkg)) {
                    if (sib == canonical) continue;
                    if (!visited_files.insert(sib).second) continue;
                    auto [sast, suses] = parse_one(sib);
                    if (sast.is_null()) continue;
                    for (const auto& u : suses) visit_package(u);
                    modules.push_back({sib, declared_pkg, std::move(sast)});
                }
            }
        }
        // Recurse into this file's own uses (post-order).
        for (const auto& u : uses) visit_package(u);
        modules.push_back({canonical, declared_pkg, std::move(ast)});
    };

    // Kick off traversal at the root file.
    auto root_canonical = fs::weakly_canonical(root_path).string();
    auto root_pkg = scan_package_decl(root_canonical);
    visit_file(root_canonical, root_pkg);

    if (out_had_error) *out_had_error = had_error;
    return modules;
}

} // namespace logos::compiler
