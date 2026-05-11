// Logos project — https://github.com/victor-smirnov/logos

#include "module_resolver.hpp"
#include "grammar_parser.hpp"
#include "grammar_ast.hpp"

#include <filesystem>
#include <print>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace fs  = std::filesystem;
namespace ast = logos::peg_gen::ast;

using logos::hermes::AnyVal;
using logos::hermes::ArrayView;
using logos::hermes::TinyMapView;
using logos::hermes::StringView;
using logos::hermes::MemHolder;

namespace logos::peg_gen {

// ── Grammar document navigation ───────────────────────────────────────────
//
// A parsed grammar document has this root structure (ObjectMap, string-keyed):
//   "imports" → Array of TinyObjectMap {CODE:IMPORT, PATH:str, ALIAS:str}
//
// We read it by navigating AnyVal offsets with the document's MemHolder.

struct ImportEntry {
    std::string path;
    std::string alias;
};

static std::string_view read_str(AnyVal val, MemHolder* h) {
    return StringView(val.to_offset(), h).view();
}

static std::vector<ImportEntry>
collect_imports(const logos::hermes::HermesView& grammar) {
    std::vector<ImportEntry> result;
    if (!grammar.has_root()) return result;

    MemHolder* h = grammar.holder();

    // Root is an ObjectMap (set by PegParser::parse()).
    auto root_obj = grammar.root_object();
    if (root_obj.is_null()) return result;

    auto root_map = root_obj.as_map();
    AnyVal imports_val = root_map.get("imports");
    if (imports_val.is_null()) return result;

    ArrayView imports(imports_val.to_offset(), h);
    for (uint64_t i = 0; i < imports.size(); ++i) {
        AnyVal elem = imports.get(i);
        if (elem.is_null() || !elem.is_pointer()) continue;

        TinyMapView node(elem.to_offset(), h);

        // Use unchecked get(uint8_t) — avoid hard assert on possibly partial nodes.
        AnyVal path_val  = node.get(uint8_t(ast::PATH));
        AnyVal alias_val = node.get(uint8_t(ast::ALIAS));
        if (path_val.is_null() || alias_val.is_null()) continue;

        result.push_back({
            std::string(read_str(path_val,  h)),
            std::string(read_str(alias_val, h)),
        });
    }
    return result;
}

// ── DFS topological resolver ──────────────────────────────────────────────

class Resolver {
public:
    explicit Resolver(std::vector<ResolvedModule>& out) : output_(out) {}

    bool visit(const fs::path& path, const std::string& alias) {
        std::string canonical;
        try {
            canonical = fs::weakly_canonical(path).string();
        } catch (const fs::filesystem_error& e) {
            std::println(stderr, "peg_gen: cannot resolve path '{}': {}",
                path.string(), e.what());
            return false;
        }

        // Diamond: already fully processed — skip.
        if (processed_.count(canonical)) return true;

        // Cycle: currently on the DFS stack.
        if (in_progress_.count(canonical)) {
            std::println(stderr, "peg_gen: import cycle detected at '{}'", canonical);
            return false;
        }

        if (!fs::exists(path)) {
            std::println(stderr, "peg_gen: imported grammar not found: '{}'",
                path.string());
            return false;
        }

        auto grammar = parse_grammar(path.string());
        if (!grammar) return false;  // parse error already printed

        in_progress_.insert(canonical);

        // Recurse: process imports before this module (dependencies first).
        auto imports = collect_imports(*grammar);
        fs::path parent = path.parent_path();
        for (const auto& imp : imports) {
            fs::path imp_path = parent / imp.path;
            if (!visit(imp_path, imp.alias)) return false;
        }

        in_progress_.erase(canonical);
        processed_.insert(canonical);

        output_.push_back(ResolvedModule{
            .path    = canonical,
            .alias   = alias,
            .grammar = std::move(*grammar),
        });
        return true;
    }

private:
    std::vector<ResolvedModule>&  output_;
    std::unordered_set<std::string> in_progress_;
    std::unordered_set<std::string> processed_;
};

// ── Public API ────────────────────────────────────────────────────────────

std::optional<std::vector<ResolvedModule>>
resolve_modules(const std::string& root_path) {
    std::vector<ResolvedModule> result;
    Resolver resolver(result);

    if (!resolver.visit(fs::path(root_path), /*alias=*/""))
        return std::nullopt;

    std::println("peg_gen: resolved {} module(s) for '{}'",
        result.size(), root_path);
    for (const auto& m : result) {
        if (m.alias.empty())
            std::println("  [root]  {}", m.path);
        else
            std::println("  [{}]  {}", m.alias, m.path);
    }

    return result;
}

} // namespace logos::peg_gen
