// Logos project — https://github.com/victor-smirnov/logos

#include "module_loader.hpp"
#include "logos_parser.hpp"
#include <chrono>
#include <cstdlib>

#include <logos/compiler/ast.hpp>
#include <logos/writ/compat.hpp>
#include <logos/writ/compat.hpp>
#include <logos/writ/compat.hpp>
#include <logos/writ/compat.hpp>
#include <logos/writ/compat.hpp>
#include <logos/writ/compat.hpp>
#include <logos/writ/compat.hpp>
#include <logos/writ/compat.hpp>

#include <filesystem>
#include <fstream>
#include <functional>
#include <queue>
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

using writ::TinyMapView;
using writ::ArrayView;
using writ::StringView;
using writ::AnyVal;

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
// Three-layer split Phase 3.4: scan a parsed AST for the file-level inner
// attribute `#![no_implicit_prelude]`. Returns true if present. Walks
// root.ITEMS for any INNER_ANNOTATION node with NAME="no_implicit_prelude".
static bool file_opts_out_of_implicit_prelude(const writ::WritView& ast) {
    auto holder = ast.holder();
    auto root = ast.root_object().as_tiny_map();
    if (!root.has_key(la::ITEMS)) return false;
    AnyVal items_av = root.get(la::ITEMS);
    if (items_av.is_null() || !items_av.is_pointer()) return false;
    auto items = ArrayView(items_av, holder);
    for (uint64_t i = 0; i < items.size(); ++i) {
        AnyVal it = items.get(i);
        if (it.is_null() || !it.is_pointer()) continue;
        auto node = TinyMapView(it, holder);
        if (!node.has_key(la::CODE)) continue;
        AnyVal cav = node.get(la::CODE);
        if (cav.is_null() || cav.is_pointer()) continue;
        if (cav.as_value<int32_t>() != la::INNER_ANNOTATION.code) continue;
        if (!node.has_key(la::NAME)) continue;
        AnyVal nav = node.get(la::NAME);
        if (nav.is_null() || !nav.is_pointer()) continue;
        auto name = StringView(nav, holder).view();
        if (name == "no_implicit_prelude") return true;
    }
    return false;
}

// Three-layer split Phase 3.4: extract `use` deps from an AST, optionally
// appending an implicit prelude package (if `implicit_prelude` is non-empty
// AND the file does not carry `#![no_implicit_prelude]`).
// §B-coex: each entry is (package, from_module). `from_module` is the module
// named by `use pkg from <module>;` (empty = no `from` → default resolution).
using UseRef = std::pair<std::string, std::string>;
static std::vector<UseRef> extract_uses(const writ::WritView& ast,
                                        std::string_view implicit_prelude = {}) {
    std::vector<UseRef> result;
    auto holder = ast.holder();
    auto root = ast.root_object().as_tiny_map();

    // Helper: implicit-prelude tail used by every exit path. Idempotent
    // (deduped against already-collected explicit uses).
    auto finalize = [&]() -> std::vector<UseRef> {
        if (!implicit_prelude.empty() && !file_opts_out_of_implicit_prelude(ast)) {
            if (std::find_if(result.begin(), result.end(),
                    [&](const UseRef& u){ return u.first == implicit_prelude; })
                == result.end())
                result.emplace_back(std::string(implicit_prelude), std::string{});
        }
        return std::move(result);
    };

    if (!root.has_key(la::USES)) return finalize();
    AnyVal uses_av = root.get(la::USES);
    if (uses_av.is_null() || !uses_av.is_pointer()) return finalize();

    auto uses = ArrayView(uses_av, holder);
    for (uint64_t i = 0; i < uses.size(); ++i) {
        AnyVal use_av = uses.get(i);
        if (use_av.is_null() || !use_av.is_pointer()) continue;
        auto use_node = TinyMapView(use_av, holder);

        // §B-coex: `use pkg from <module>;` — the module name disambiguates which
        // archive provides pkg (two modules can share a package name). Bare IDENT
        // or quoted string (strip quotes); empty = no `from`.
        std::string from_module;
        if (use_node.has_key(la::mod::FROM_MODULE)) {
            AnyVal fmav = use_node.get(la::mod::FROM_MODULE);
            if (!fmav.is_null() && fmav.is_pointer()) {
                auto fm = TinyMapView(fmav, holder);
                if (fm.has_key(la::NAME)) {
                    AnyVal nv = fm.get(la::NAME);
                    if (!nv.is_null() && nv.is_pointer()) {
                        from_module = std::string(StringView(nv, holder).view());
                        if (from_module.size() >= 2 && from_module.front() == '"' &&
                            from_module.back() == '"')
                            from_module = from_module.substr(1, from_module.size() - 2);
                    }
                }
            }
        }

        // Reconstruct dotted path from NAME + PATH_PARTS.
        // `use a.b.c;` → NAME="a", PATH_PARTS=[{NAME:"b"},{NAME:"c"}] → "a.b.c".
        std::string dotted;
        if (use_node.has_key(la::NAME)) {
            AnyVal name_av = use_node.get(la::NAME);
            if (!name_av.is_null() && name_av.is_pointer())
                dotted = std::string(StringView(name_av, holder).view());
        }
        if (use_node.has_key(la::mod::PATH_PARTS)) {
            AnyVal parts_av = use_node.get(la::mod::PATH_PARTS);
            if (!parts_av.is_null() && parts_av.is_pointer()) {
                auto parts = ArrayView(parts_av, holder);
                for (uint64_t pi = 0; pi < parts.size(); ++pi) {
                    AnyVal part_av = parts.get(pi);
                    if (part_av.is_null() || !part_av.is_pointer()) continue;
                    auto part = TinyMapView(part_av, holder);
                    if (!part.has_key(la::NAME)) continue;
                    AnyVal pn = part.get(la::NAME);
                    if (pn.is_null() || !pn.is_pointer()) continue;
                    if (!dotted.empty()) dotted += '.';
                    dotted += std::string(StringView(pn, holder).view());
                }
            }
        }
        // GR-gp-02: USE_GROUP carries a VARIANTS list — each entry becomes
        // a separate `<dotted>.<name>` package to load.
        int32_t use_code = la::USE.code;
        if (use_node.has_key(la::CODE)) {
            AnyVal cav = use_node.get(la::CODE);
            if (!cav.is_null() && !cav.is_pointer())
                use_code = cav.as_value<int32_t>();
        }
        // GR-gp-02: `use pkg.{a, b};` — USE_VARIANTS with lowercase
        // TYPE_NAME desugars to N separate `use pkg.<TYPE_NAME>.<item>;`
        // imports. Distinguishes from real variants (uppercase-start
        // Type names) by first-character case — Rust naming convention
        // forces enums to be Capitalized.
        if (use_code == la::USE_VARIANTS.code && use_node.has_key(la::TYPE_NAME)) {
            AnyVal tn_av = use_node.get(la::TYPE_NAME);
            if (!tn_av.is_null() && tn_av.is_pointer()) {
                std::string tn(StringView(tn_av, holder).view());
                if (!tn.empty() && tn[0] >= 'a' && tn[0] <= 'z') {
                    // Lowercase: grouped sub-package import.
                    std::string prefix = dotted.empty()
                        ? tn : (dotted + "." + tn);
                    if (use_node.has_key(la::VARIANTS)) {
                        AnyVal vlist_av = use_node.get(la::VARIANTS);
                        if (!vlist_av.is_null() && vlist_av.is_pointer()) {
                            auto vlist = ArrayView(vlist_av, holder);
                            for (uint64_t vi = 0; vi < vlist.size(); ++vi) {
                                AnyVal vav = vlist.get(vi);
                                if (vav.is_null() || !vav.is_pointer()) continue;
                                auto v = TinyMapView(vav, holder);
                                if (!v.has_key(la::NAME)) continue;
                                AnyVal nv = v.get(la::NAME);
                                if (nv.is_null() || !nv.is_pointer()) continue;
                                std::string bare(StringView(nv, holder).view());
                                std::string full = prefix + "." + bare;
                                result.push_back({std::move(full), std::string{}});
                            }
                        }
                    }
                    continue;
                }
                // Uppercase: real enum-variant import. dotted is the
                // enum's pkg; load it as a wildcard so the type itself
                // is in scope.
                if (!dotted.empty()) result.push_back({dotted, std::string{}});
                continue;
            }
        }
        if (!dotted.empty())
            result.push_back({dotted, from_module});
    }

    return finalize();
}

// Topologically sort modules by package-level `use` deps.
//
// Rationale (post-mortem of bug (D)): the loader's DFS produces dep-first
// order for the text-only path, but binary-archive loading bypasses that
// invariant — packages from a .a may end up in `modules` at indices that
// don't reflect actual dependency order (e.g. text `logos.lang.writ.check`
// before binary `std.writ.map` even though check has `use logos.lang.writ.map;`).
// Lower-pass lookups that walk `prog.struct_specializations` etc. break
// when a dependent module is processed before its dep has populated those
// data structures.
//
// This sort makes the dep-order invariant explicit. It runs on the final
// `modules` vector after load and is independent of how each module got
// loaded (text vs binary).
//
// Cycles are legitimate in Logos at the package level (e.g. option ↔ result:
// Option methods return Result and vice versa). We handle them via
// Tarjan SCC + condensation: each SCC collapses to a single node, and
// the resulting (provably acyclic) condensation graph is topo-sorted.
// Within an SCC, packages keep original (first-seen) load order; this is
// safe because by construction nothing outside the SCC orders them.
//
// Stable: when no dep edge forces a reorder, original (load-order) position
// is preserved.
//
// Sort granularity is package-level: all files of a package move together.
// Within a package, original (load-order) order is preserved.
//
// `implicit_prelude`: when non-empty, adds an edge from every text module
// (those not from a binary archive AND not opted out via
// `#![no_implicit_prelude]`) to the prelude package. Binary modules already
// baked in their producer's prelude at original-build time.
static std::vector<ParsedModule>
topo_sort_modules(std::vector<ParsedModule> in,
                  std::string_view implicit_prelude)
{
    const size_t N_in = in.size();
    // Group modules by package, capturing first-seen order.
    std::unordered_map<std::string, std::vector<size_t>> pkg_to_indices;
    std::vector<std::string> pkg_order;  // stable, first-seen
    std::unordered_map<std::string, size_t> rank;
    for (size_t i = 0; i < in.size(); ++i) {
        const auto& pkg = in[i].package;
        if (!pkg_to_indices.count(pkg)) {
            rank[pkg] = pkg_order.size();
            pkg_order.push_back(pkg);
        }
        pkg_to_indices[pkg].push_back(i);
    }

    const size_t N = pkg_order.size();
    // adj[u] = list of package ids that u depends on (i.e. u → dep edge).
    std::vector<std::vector<size_t>> adj(N);
    {
        std::unordered_set<size_t> seen_edges;  // (u<<32)|v dedup
        for (size_t i = 0; i < in.size(); ++i) {
            const auto& pkg = in[i].package;
            std::string_view ip = in[i].from_binary_module
                ? std::string_view{} : implicit_prelude;
            auto uses = extract_uses(in[i].ast, ip);
            size_t u = rank[pkg];
            for (auto& used_ref : uses) {
                const std::string& used = used_ref.first;  // §B-coex: (pkg, from)
                if (used == pkg) continue;
                auto it = rank.find(used);
                if (it == rank.end()) continue;
                size_t v = it->second;
                size_t key = (u << 32) | v;
                if (!seen_edges.insert(key).second) continue;
                adj[u].push_back(v);
            }
        }
    }

    // Tarjan's SCC algorithm. Iterative to avoid stack blow-up on large
    // dep graphs. Each SCC gets an id in scc_id[v]; SCCs are output in
    // reverse topological order of the condensation (which is exactly the
    // order Tarjan produces them — deepest SCCs first).
    std::vector<int> index_of(N, -1);
    std::vector<int> lowlink(N, 0);
    std::vector<char> on_stack(N, 0);
    std::vector<size_t> stack;
    std::vector<int> scc_id(N, -1);
    int next_index = 0;
    int next_scc = 0;
    // Reverse-topo-order list of SCC ids as Tarjan emits them.
    std::vector<int> scc_rev_topo;

    // Iterative DFS state.
    struct Frame { size_t v; size_t next_child; };
    std::vector<Frame> dfs;

    for (size_t s = 0; s < N; ++s) {
        if (index_of[s] != -1) continue;
        dfs.push_back({s, 0});
        index_of[s] = next_index;
        lowlink[s] = next_index;
        ++next_index;
        stack.push_back(s);
        on_stack[s] = 1;

        while (!dfs.empty()) {
            auto& fr = dfs.back();
            size_t v = fr.v;
            if (fr.next_child < adj[v].size()) {
                size_t w = adj[v][fr.next_child++];
                if (index_of[w] == -1) {
                    index_of[w] = next_index;
                    lowlink[w] = next_index;
                    ++next_index;
                    stack.push_back(w);
                    on_stack[w] = 1;
                    dfs.push_back({w, 0});
                } else if (on_stack[w]) {
                    if (index_of[w] < lowlink[v]) lowlink[v] = index_of[w];
                }
            } else {
                // Done with v. If root of an SCC, pop it.
                if (lowlink[v] == index_of[v]) {
                    int id = next_scc++;
                    while (true) {
                        size_t w = stack.back();
                        stack.pop_back();
                        on_stack[w] = 0;
                        scc_id[w] = id;
                        if (w == v) break;
                    }
                    scc_rev_topo.push_back(id);
                }
                dfs.pop_back();
                if (!dfs.empty()) {
                    size_t parent = dfs.back().v;
                    if (lowlink[v] < lowlink[parent]) lowlink[parent] = lowlink[v];
                }
            }
        }
    }

    // Group packages by SCC, preserving each SCC's internal first-seen order.
    std::vector<std::vector<size_t>> scc_members(next_scc);
    for (size_t v = 0; v < N; ++v) {
        scc_members[scc_id[v]].push_back(v);
    }
    // scc_members[i] is already in first-seen order of vertices (we
    // iterated v from 0 to N-1 above, where v is the rank/first-seen id).

    // Diagnostic: report any non-trivial SCC (cycle of ≥2 packages).
    if (std::getenv("LOGOS_TRACE_PHASES") != nullptr) {
        for (int id = 0; id < next_scc; ++id) {
            if (scc_members[id].size() <= 1) continue;
            std::fprintf(stderr, "module_loader: package SCC (%zu members):\n",
                         scc_members[id].size());
            for (size_t v : scc_members[id]) {
                std::fprintf(stderr, "  - %s\n",
                             pkg_order[v].empty() ? "<root>" : pkg_order[v].c_str());
            }
        }
    }

    // Tarjan emits SCCs in "reverse topological order of the condensation"
    // where edges u→v mean "u before v". Our edges are inverse — u→v means
    // "u depends on v", so v must be processed first. Tarjan emits deepest-
    // first, which in our semantics is dependencies-first. No reversal.
    std::vector<int>& scc_topo = scc_rev_topo;

    // Re-emit modules: walk SCCs in topo order; within an SCC, walk member
    // packages in first-seen order; within a package, preserve module order.
    std::vector<ParsedModule> out;
    out.reserve(in.size());
    for (int id : scc_topo) {
        for (size_t v : scc_members[id]) {
            for (size_t idx : pkg_to_indices[pkg_order[v]]) {
                out.push_back(std::move(in[idx]));
            }
        }
    }
    // Output must be a permutation of input — the algorithm partitions
    // vertices across SCCs (each in exactly one SCC) and walks every SCC
    // member's indices exactly once. A count mismatch indicates an
    // implementation bug.
    if (out.size() != N_in) {
        std::fprintf(stderr,
            "module_loader: topo_sort_modules INVARIANT VIOLATED — "
            "in=%zu out=%zu (returning input unchanged)\n",
            N_in, out.size());
        return in;  // refuse to corrupt the build; fall back to original
    }
    return out;
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
        // Three-layer split Phase 3.4: skip inner attribute lines
        // (`#![...]`) that may legitimately precede `package`. Coarse
        // single-line skip — true multi-line attrs not supported by
        // this scanner (none currently exist in practice).
        if (i + 1 < line.size() && line[i] == '#' && line[i + 1] == '!') continue;
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
// .writ0 format: "WRITAST0" magic + uint32 version + uint32 num_files +
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

// If `data` is an ELF64 relocatable object containing a section named
// `section_name`, return that section's bytes; otherwise return `data`
// unchanged (backwards-compat with legacy raw-in-archive members + a
// pass-through path when callers receive non-ELF input). Tolerates
// corrupt/truncated ELF by falling through to the raw path — the caller
// validates the payload (.writ0 magic / .pkgi line shape / etc.).
static std::vector<uint8_t>
unwrap_elf_section(const std::vector<uint8_t>& data, const char* section_name) {
    constexpr uint8_t kElfMagic[4] = {0x7f, 'E', 'L', 'F'};
    if (data.size() < 64 || std::memcmp(data.data(), kElfMagic, 4) != 0)
        return data;
    if (data[4] != 2 /*ELFCLASS64*/) return data;
    auto rd64 = [&](size_t off) -> uint64_t {
        uint64_t v; std::memcpy(&v, data.data() + off, 8); return v;
    };
    auto rd16 = [&](size_t off) -> uint16_t {
        uint16_t v; std::memcpy(&v, data.data() + off, 2); return v;
    };
    uint64_t e_shoff     = rd64(0x28);
    uint16_t e_shentsize = rd16(0x3a);
    uint16_t e_shnum     = rd16(0x3c);
    uint16_t e_shstrndx  = rd16(0x3e);
    if (e_shentsize != 64 || e_shnum == 0) return data;
    if (e_shoff + uint64_t(e_shnum) * e_shentsize > data.size()) return data;
    if (e_shstrndx >= e_shnum) return data;
    // Section header layout (Elf64_Shdr): name(0,4) type(4,4) flags(8,8)
    // addr(0x10,8) offset(0x18,8) size(0x20,8) ...
    auto sh = [&](uint16_t i) -> const uint8_t* {
        return data.data() + e_shoff + uint64_t(i) * e_shentsize;
    };
    auto sh_name   = [&](uint16_t i) { uint32_t v; std::memcpy(&v, sh(i), 4); return v; };
    auto sh_offset = [&](uint16_t i) { uint64_t v; std::memcpy(&v, sh(i) + 0x18, 8); return v; };
    auto sh_size   = [&](uint16_t i) { uint64_t v; std::memcpy(&v, sh(i) + 0x20, 8); return v; };
    uint64_t shstr_off = sh_offset(e_shstrndx);
    uint64_t shstr_sz  = sh_size(e_shstrndx);
    if (shstr_off + shstr_sz > data.size()) return data;
    const char* shstr = reinterpret_cast<const char*>(data.data() + shstr_off);
    for (uint16_t i = 1; i < e_shnum; ++i) {
        uint32_t name_off = sh_name(i);
        if (name_off >= shstr_sz) continue;
        const char* nm = shstr + name_off;
        size_t nm_max = shstr_sz - name_off;
        if (strnlen(nm, nm_max) >= nm_max) continue;
        if (std::strcmp(nm, section_name) != 0) continue;
        uint64_t off = sh_offset(i);
        uint64_t sz  = sh_size(i);
        if (off + sz > data.size()) return data;
        return std::vector<uint8_t>(data.data() + off, data.data() + off + sz);
    }
    return data;
}

// Legacy alias — .wr0 members are ELF-wrapped with section ".lwrit".
static std::vector<uint8_t>
unwrap_lwrit(const std::vector<uint8_t>& data) {
    return unwrap_elf_section(data, ".lwrit");
}

// Read member data from an AR archive by member name suffix.
// Returns the raw bytes of the first matching member, or empty on failure.
// Returns the bytes of EVERY archive member whose name ends with
// `member_suffix`. Per-file emit (B1.7) writes one .writ0 per source
// file into the eventual archive, so callers that previously read a
// single .writ0 now need to merge several. Existing monolithic
// archives still yield exactly one entry — same behaviour as before
// for that path.
static std::vector<std::vector<uint8_t>>
ar_read_members_raw(const std::string& archive_path,
                    const std::string& member_suffix) {
    std::vector<std::vector<uint8_t>> result;
    std::ifstream f(archive_path, std::ios::binary | std::ios::ate);
    if (!f) return result;
    auto file_size = static_cast<size_t>(f.tellg());
    f.seekg(0);
    std::vector<uint8_t> raw(file_size);
    if (!f.read(reinterpret_cast<char*>(raw.data()), file_size)) return result;

    const uint8_t* p = raw.data();
    const uint8_t* end = p + file_size;
    if (file_size < 8 || std::memcmp(p, "!<arch>\n", 8) != 0) return result;
    p += 8;
    // GNU ar long-name table: a member named "//" whose payload holds long
    // names separated by "/\n". A member's 16-byte name field then holds
    // "/<decimal-offset>" pointing into this table.
    const uint8_t* longnames     = nullptr;
    size_t         longnames_sz  = 0;
    while (p + 60 <= end) {
        const uint8_t* hdr = p;
        std::string raw_name(reinterpret_cast<const char*>(hdr), 16);
        while (!raw_name.empty() && raw_name.back() == ' ')
            raw_name.pop_back();
        char size_str[11] = {};
        std::memcpy(size_str, hdr + 48, 10);
        long long member_size = std::atoll(size_str);
        if (member_size < 0 || p + 60 + member_size > end) break;
        if (hdr[58] != '`' || hdr[59] != '\n') break;
        const uint8_t* data_start = hdr + 60;

        std::string name;
        if (raw_name == "//") {
            // Long-name table — record and skip.
            longnames    = data_start;
            longnames_sz = (size_t)member_size;
        } else if (!raw_name.empty() && raw_name[0] == '/' &&
                   raw_name.size() > 1 && std::isdigit((unsigned char)raw_name[1])) {
            size_t off = (size_t)std::atoll(raw_name.c_str() + 1);
            if (longnames && off < longnames_sz) {
                const uint8_t* s = longnames + off;
                const uint8_t* e = longnames + longnames_sz;
                const uint8_t* q = s;
                // Long names terminate with '/' then '\n'.
                while (q < e && *q != '/') ++q;
                name.assign(reinterpret_cast<const char*>(s), q - s);
            }
        } else {
            name = raw_name;
            if (!name.empty() && name.back() == '/') name.pop_back();
        }

        bool match = !name.empty() && name.size() >= member_suffix.size() &&
                     name.compare(name.size() - member_suffix.size(),
                                  member_suffix.size(), member_suffix) == 0;
        if (match)
            result.emplace_back(data_start, data_start + member_size);
        p = data_start + member_size + (member_size & 1);
    }
    return result;
}

// Wrapper: read archive members and unwrap each ELF-wrapped `.lwrit`
// section, falling through to raw bytes for legacy archives.
static std::vector<std::vector<uint8_t>>
ar_read_members(const std::string& archive_path,
                const std::string& member_suffix) {
    auto raw = ar_read_members_raw(archive_path, member_suffix);
    for (auto& m : raw) m = unwrap_lwrit(m);
    return raw;
}


// Parse a .writ0 blob and return decoded ParsedModules.
static std::vector<ParsedModule> parse_writ0(const std::vector<uint8_t>& data,
                                                const std::string& archive_path) {
    const uint8_t* p = data.data();
    const uint8_t* end = p + data.size();

    if (data.size() < 16) {
        std::fprintf(stderr, "module_loader: %s: .writ0 too small\n", archive_path.c_str());
        return {};
    }
    if (std::memcmp(p, "WRITAST0", 8) != 0) {
        std::fprintf(stderr, "module_loader: %s: bad .writ0 magic\n", archive_path.c_str());
        return {};
    }
    uint32_t version   = read_le_u32(p + 8);
    uint32_t num_files = read_le_u32(p + 12);
    if (version != 2 && version != 3) {
        std::fprintf(stderr, "module_loader: %s: unsupported .writ0 version %u (want 2 or 3)\n",
                     archive_path.c_str(), version);
        return {};
    }
    p += 16;

    std::vector<ParsedModule> result;
    for (uint32_t i = 0; i < num_files; ++i) {
        if (p + 4 > end) { std::fprintf(stderr, "module_loader: .writ0 truncated\n"); return {}; }
        uint32_t path_len = read_le_u32(p); p += 4;
        if (p + path_len > end) { std::fprintf(stderr, "module_loader: .writ0 truncated path\n"); return {}; }
        std::string path(reinterpret_cast<const char*>(p), path_len); p += path_len;

        if (p + 4 > end) { std::fprintf(stderr, "module_loader: .writ0 truncated pkg\n"); return {}; }
        uint32_t pkg_len = read_le_u32(p); p += 4;
        if (p + pkg_len > end) { std::fprintf(stderr, "module_loader: .writ0 truncated pkg data\n"); return {}; }
        std::string pkg(reinterpret_cast<const char*>(p), pkg_len); p += pkg_len;

        if (p + 8 > end) { std::fprintf(stderr, "module_loader: .writ0 truncated size\n"); return {}; }
        uint64_t ast_len = read_le_u64(p); p += 8;
        if (p + ast_len > end) { std::fprintf(stderr, "module_loader: .writ0 truncated ast\n"); return {}; }

        auto decoded = writ::binary_decode(p, static_cast<size_t>(ast_len));
        p += ast_len;

        if (!decoded) {
            std::fprintf(stderr, "module_loader: binary_decode failed for %s in %s\n",
                         path.c_str(), archive_path.c_str());
            return {};
        }
        result.push_back({path, pkg, std::move(*decoded), false, {}, {}});
    }
    // M3: v3 has a trailing exports section (u64 length + bytes). Skip it
    // here — consumers needing the exports pull them via
    // extract_writ0_exports() on the same blob. v2 has no trailer.
    bool is_lazy = false;
    if (version == 3 && p + 8 <= end) {
        uint64_t exports_len = read_le_u64(p);
        p += 8;
        if (p + exports_len > end) {
            std::fprintf(stderr, "module_loader: %s: .writ0 truncated exports\n",
                         archive_path.c_str());
            return {};
        }
        p += exports_len;  // skip; consumers use extract_writ0_exports()
        // M4 step 1: optional LIR blob section right after the exports
        // trailer (u64 length + bytes). M3-era v3 archives stop after the
        // exports trailer — no bytes here. Skip in either shape.
        if (p + 8 <= end) {
            uint64_t blob_len = read_le_u64(p);
            p += 8;
            if (p + blob_len > end) {
                std::fprintf(stderr, "module_loader: %s: .writ0 truncated lir_blob\n",
                             archive_path.c_str());
                return {};
            }
            p += blob_len;  // skip; consumers use extract_writ0_lir_blob()
            // Phase 6: optional module_flags u64. Pre-Phase-6 archives stop
            // here (EOF after lir_blob) — they're eager by default.
            if (p + 8 <= end) {
                uint64_t mflags = read_le_u64(p);
                p += 8;
                constexpr uint64_t LAZY_BIT = 1ULL << 0;
                if (mflags & LAZY_BIT) is_lazy = true;
            }
        }
    }
    if (is_lazy) {
        for (auto& m : result) m.is_lazy = true;
    }
    return result;
}

// M3 step 2 reader: decode the v3 exports trailer into a StdlibExports value.
// Returns `{present=false}` for v2 archives or when the trailer is zero-
// length. Returns `{present=true, value=…}` on success. Returns nullopt on
// a malformed trailer; the caller should treat that as fatal. Reads the
// outer file table just to advance past it — no AST decode needed.
StdlibExportsOpt extract_writ0_exports(const std::vector<uint8_t>& data,
                                         const std::string& archive_path) {
    StdlibExportsOpt r;
    const uint8_t* p = data.data();
    const uint8_t* end = p + data.size();
    if (data.size() < 16 || std::memcmp(p, "WRITAST0", 8) != 0) return r;
    uint32_t version   = read_le_u32(p + 8);
    uint32_t num_files = read_le_u32(p + 12);
    if (version == 2) return r;  // no trailer in v2
    if (version != 3) return r;
    p += 16;
    // Skip file table — entries store path + pkg + ast_len-prefixed bytes.
    for (uint32_t i = 0; i < num_files; ++i) {
        if (p + 4 > end) return r;
        uint32_t path_len = read_le_u32(p); p += 4;
        if (p + path_len > end) return r;
        p += path_len;
        if (p + 4 > end) return r;
        uint32_t pkg_len = read_le_u32(p); p += 4;
        if (p + pkg_len > end) return r;
        p += pkg_len;
        if (p + 8 > end) return r;
        uint64_t ast_len = read_le_u64(p); p += 8;
        if (ast_len > static_cast<uint64_t>(end - p)) return r;
        p += ast_len;
    }
    if (p + 8 > end) return r;
    uint64_t exports_len = read_le_u64(p); p += 8;
    if (exports_len == 0) return r;  // present but empty → treat as absent
    if (exports_len > static_cast<uint64_t>(end - p)) {
        std::fprintf(stderr, "module_loader: %s: exports trailer truncated\n",
                     archive_path.c_str());
        return r;
    }
    const uint8_t* tp = p;
    const uint8_t* tend = p + exports_len;
    auto need = [&](size_t n) { return tp + n <= tend; };
    auto rd_u16 = [&]() -> uint16_t {
        uint16_t v = static_cast<uint16_t>(tp[0]) | (static_cast<uint16_t>(tp[1]) << 8);
        tp += 2;
        return v;
    };
    auto rd_u32 = [&]() -> uint32_t { uint32_t v = read_le_u32(tp); tp += 4; return v; };
    auto rd_str = [&](std::string& out) -> bool {
        if (!need(4)) return false;
        uint32_t n = rd_u32();
        if (!need(n)) return false;
        out.assign(reinterpret_cast<const char*>(tp), n);
        tp += n;
        return true;
    };
    if (!need(4)) {
        std::fprintf(stderr, "module_loader: %s: exports trailer header truncated\n",
                     archive_path.c_str());
        return r;
    }
    uint16_t trailer_version = rd_u16();
    (void) rd_u16();  // reserved
    if (trailer_version < 1) {
        // Unknown / pre-v1 — treat as absent.
        return r;
    }
    auto rd_vec_pp = [&](std::vector<std::pair<std::string, std::string>>& v) -> bool {
        if (!need(4)) return false;
        uint32_t n = rd_u32();
        v.reserve(n);
        for (uint32_t i = 0; i < n; ++i) {
            std::string pkg, name;
            if (!rd_str(pkg)) return false;
            if (!rd_str(name)) return false;
            v.emplace_back(std::move(pkg), std::move(name));
        }
        return true;
    };
    auto rd_vec_s = [&](std::vector<std::string>& v) -> bool {
        if (!need(4)) return false;
        uint32_t n = rd_u32();
        v.reserve(n);
        for (uint32_t i = 0; i < n; ++i) {
            std::string s;
            if (!rd_str(s)) return false;
            v.push_back(std::move(s));
        }
        return true;
    };
    // v1 fields (always present from trailer_version >= 1)
    if (!rd_vec_pp(r.value.struct_templates) ||
        !rd_vec_pp(r.value.enum_templates)   ||
        !rd_vec_s(r.value.fn_templates))
    {
        std::fprintf(stderr, "module_loader: %s: exports trailer v1 payload malformed\n",
                     archive_path.c_str());
        r.value = {};
        return r;
    }
    // v2 additions: blanket + concrete impl catalog
    if (trailer_version >= 2) {
        if (!need(4)) {
            std::fprintf(stderr, "module_loader: %s: exports trailer v2 truncated at blanket_impls\n",
                         archive_path.c_str());
            r.value = {};
            return r;
        }
        uint32_t nb = rd_u32();
        r.value.blanket_impls.reserve(nb);
        for (uint32_t i = 0; i < nb; ++i) {
            StdlibExports::BlanketImpl bi;
            if (!rd_str(bi.trait_name) || !rd_str(bi.bound_trait) || !need(4)) {
                std::fprintf(stderr, "module_loader: %s: blanket_impls[%u] truncated\n",
                             archive_path.c_str(), i);
                r.value = {};
                return r;
            }
            uint32_t ne = rd_u32();
            bi.extra_bounds.reserve(ne);
            for (uint32_t j = 0; j < ne; ++j) {
                std::string b;
                if (!rd_str(b)) {
                    std::fprintf(stderr, "module_loader: %s: blanket_impls[%u].extra[%u] truncated\n",
                                 archive_path.c_str(), i, j);
                    r.value = {};
                    return r;
                }
                bi.extra_bounds.push_back(std::move(b));
            }
            r.value.blanket_impls.push_back(std::move(bi));
        }
        if (!need(4)) {
            std::fprintf(stderr, "module_loader: %s: exports trailer v2 truncated at concrete_impls\n",
                         archive_path.c_str());
            r.value = {};
            return r;
        }
        uint32_t nc = rd_u32();
        r.value.concrete_impls.reserve(nc);
        for (uint32_t i = 0; i < nc; ++i) {
            StdlibExports::ConcreteImpl ci;
            if (!rd_str(ci.trait_name) || !rd_str(ci.target_type)) {
                std::fprintf(stderr, "module_loader: %s: concrete_impls[%u] truncated\n",
                             archive_path.c_str(), i);
                r.value = {};
                return r;
            }
            r.value.concrete_impls.push_back(std::move(ci));
        }
    }
    // v3 additions (G156-1): all-nominal-decls for the ambiguity universe.
    if (trailer_version >= 3) {
        if (!rd_vec_pp(r.value.all_struct_decls) ||
            !rd_vec_pp(r.value.all_enum_decls))
        {
            std::fprintf(stderr, "module_loader: %s: exports trailer v3 all-decls malformed\n",
                         archive_path.c_str());
            r.value = {};
            return r;
        }
    }
    // trailer_version > 3: future fields ignored (outer length prefix bounds
    // the scan, so unknown bytes after our last-known field are harmless).
    r.present = true;
    return r;
}

// M4 step 1 reader: decode the optional LIR blob section that follows the
// exports trailer in a .writ0 v3 archive. Walks the file table + exports
// trailer just to advance past them — no AST decode needed.
LirBlobOpt extract_writ0_lir_blob(const std::vector<uint8_t>& data,
                                     const std::string& archive_path) {
    LirBlobOpt r;
    const uint8_t* p = data.data();
    const uint8_t* end = p + data.size();
    if (data.size() < 16 || std::memcmp(p, "WRITAST0", 8) != 0) return r;
    uint32_t version   = read_le_u32(p + 8);
    uint32_t num_files = read_le_u32(p + 12);
    if (version != 3) return r;
    p += 16;
    // Skip file table.
    for (uint32_t i = 0; i < num_files; ++i) {
        if (p + 4 > end) return r;
        uint32_t path_len = read_le_u32(p); p += 4;
        if (p + path_len > end) return r;
        p += path_len;
        if (p + 4 > end) return r;
        uint32_t pkg_len = read_le_u32(p); p += 4;
        if (p + pkg_len > end) return r;
        p += pkg_len;
        if (p + 8 > end) return r;
        uint64_t ast_len = read_le_u64(p); p += 8;
        if (ast_len > static_cast<uint64_t>(end - p)) return r;
        p += ast_len;
    }
    // Skip exports trailer.
    if (p + 8 > end) return r;
    uint64_t exports_len = read_le_u64(p); p += 8;
    if (exports_len > static_cast<uint64_t>(end - p)) return r;
    p += exports_len;
    // Optional LIR blob section. M3-era archives stop here — no bytes left.
    if (p + 8 > end) return r;
    uint64_t blob_len = read_le_u64(p); p += 8;
    if (blob_len == 0) return r;  // present but empty → treat as absent
    if (blob_len > static_cast<uint64_t>(end - p)) {
        std::fprintf(stderr, "module_loader: %s: lir_blob section truncated\n",
                     archive_path.c_str());
        return r;
    }
    r.bytes.assign(p, p + blob_len);
    r.present = true;
    return r;
}

// M3 step 3: union exports trailers from a set of archives. Same archive
// scan path as load_modules uses (ar_read_members + unwrap_lwrit), but
// only the trailer is decoded — no AST work.
StdlibExports load_archive_exports(const std::vector<std::string>& archive_paths) {
    StdlibExports merged;
    for (const auto& archive_path : archive_paths) {
        auto members = ar_read_members(archive_path, ".wr0");
        for (auto& m : members) {
            auto opt = extract_writ0_exports(m, archive_path);
            if (!opt.present) continue;
            // Append; dedup is deferred — mono can build a set if needed.
            // Duplicates are rare in practice (a name in two archives
            // would already be a multiply-defined linker symbol).
            merged.struct_templates.insert(merged.struct_templates.end(),
                std::make_move_iterator(opt.value.struct_templates.begin()),
                std::make_move_iterator(opt.value.struct_templates.end()));
            merged.enum_templates.insert(merged.enum_templates.end(),
                std::make_move_iterator(opt.value.enum_templates.begin()),
                std::make_move_iterator(opt.value.enum_templates.end()));
            merged.fn_templates.insert(merged.fn_templates.end(),
                std::make_move_iterator(opt.value.fn_templates.begin()),
                std::make_move_iterator(opt.value.fn_templates.end()));
            merged.blanket_impls.insert(merged.blanket_impls.end(),
                std::make_move_iterator(opt.value.blanket_impls.begin()),
                std::make_move_iterator(opt.value.blanket_impls.end()));
            merged.concrete_impls.insert(merged.concrete_impls.end(),
                std::make_move_iterator(opt.value.concrete_impls.begin()),
                std::make_move_iterator(opt.value.concrete_impls.end()));
            // G156-1 (v3): all-nominal-decls for the ambiguity universe.
            merged.all_struct_decls.insert(merged.all_struct_decls.end(),
                std::make_move_iterator(opt.value.all_struct_decls.begin()),
                std::make_move_iterator(opt.value.all_struct_decls.end()));
            merged.all_enum_decls.insert(merged.all_enum_decls.end(),
                std::make_move_iterator(opt.value.all_enum_decls.begin()),
                std::make_move_iterator(opt.value.all_enum_decls.end()));
        }
    }
    return merged;
}

// ---------------------------------------------------------------------------
// Binary package index: package_name → archive_path
// ---------------------------------------------------------------------------
//
// We scan each lib*.a for a .writ0 member, peek at the stored file paths,
// extract their `package` declarations (via cheap text scan of the stored paths),
// and build a map from every package provided by the archive to its path.
//
// This is done once at startup; the cost is O(#archives × #files_per_archive).
// For a typical stdlib with 50 files it's negligible.
// ---------------------------------------------------------------------------

// Scan a v2 or v3 .writ0 blob to extract the list of package names it
// contains. Both versions store pkg_len+pkg explicitly — no AST decode
// needed. The file table has the same layout in both; v3 only adds a
// trailing exports section that this scan ignores.
static std::vector<std::string>
writ0_packages(const std::vector<uint8_t>& data) {
    const uint8_t* p = data.data();
    const uint8_t* end = p + data.size();
    if (data.size() < 16 || std::memcmp(p, "WRITAST0", 8) != 0) return {};
    uint32_t version   = read_le_u32(p + 8);
    uint32_t num_files = read_le_u32(p + 12);
    if (version != 2 && version != 3) return {};
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

// Streaming AR-member reader: walks member headers sequentially via seeks,
// only loads the bytes of members matching `suffix`. Critical for fast
// build_binary_index — without it, getting at the small `.pkgi` member
// would still cost the full 30-40MB memcpy of ar_read_members_raw.
// Returns empty when no match (or on parse error). Caller pays only the
// header walk (60 bytes per member, plus optional GNU long-name table)
// plus the matching member's actual bytes.
//
// Mirrors ar_read_members_raw's GNU-ar long-name handling.
static std::vector<std::vector<uint8_t>>
ar_read_members_streaming(const std::string& archive_path,
                          const std::string& member_suffix) {
    std::vector<std::vector<uint8_t>> result;
    std::ifstream f(archive_path, std::ios::binary);
    if (!f) return result;
    char magic[8];
    if (!f.read(magic, 8) || std::memcmp(magic, "!<arch>\n", 8) != 0) return result;
    std::vector<uint8_t> longnames;
    char hdr[60];
    while (f.read(hdr, 60)) {
        std::string raw_name(hdr, 16);
        while (!raw_name.empty() && raw_name.back() == ' ')
            raw_name.pop_back();
        char size_str[11] = {};
        std::memcpy(size_str, hdr + 48, 10);
        long long member_size = std::atoll(size_str);
        if (member_size < 0) break;
        if (hdr[58] != '`' || hdr[59] != '\n') break;
        std::streampos data_start = f.tellg();
        std::string name;
        if (raw_name == "//") {
            longnames.resize(member_size);
            if (!f.read(reinterpret_cast<char*>(longnames.data()), member_size)) break;
        } else if (!raw_name.empty() && raw_name[0] == '/' &&
                   raw_name.size() > 1 && std::isdigit((unsigned char)raw_name[1])) {
            size_t off = (size_t)std::atoll(raw_name.c_str() + 1);
            if (off < longnames.size()) {
                const uint8_t* s = longnames.data() + off;
                const uint8_t* e = longnames.data() + longnames.size();
                const uint8_t* q = s;
                while (q < e && *q != '/') ++q;
                name.assign(reinterpret_cast<const char*>(s), q - s);
            }
        } else {
            name = raw_name;
            if (!name.empty() && name.back() == '/') name.pop_back();
        }
        bool match = !name.empty() && name.size() >= member_suffix.size() &&
                     name.compare(name.size() - member_suffix.size(),
                                  member_suffix.size(), member_suffix) == 0;
        if (match) {
            // Read this member's bytes; if we just consumed longnames
            // above, the read pointer was advanced — seek back to data
            // start before reading.
            f.seekg(data_start);
            std::vector<uint8_t> buf(member_size);
            if (!f.read(reinterpret_cast<char*>(buf.data()), member_size)) break;
            result.push_back(std::move(buf));
        }
        // Advance to next header (member_size + pad).
        auto next = std::streamoff(data_start) + member_size + (member_size & 1);
        f.seekg(next);
    }
    return result;
}

// Parse a `.pkgi` text member: one package per line, comments (# …) skipped.
// A leading `@module <name> <id>` header line (emit_module, one module per
// archive) is parsed into out_module_name/out_module_id and kept OUT of the
// package list. Any other `@`-line is an unknown directive, skipped for
// forward-compat.
static std::vector<std::string>
parse_pkgi_member(const std::vector<uint8_t>& data,
                  std::string* out_module_name = nullptr,
                  std::string* out_module_id   = nullptr,
                  std::string* out_abi_version = nullptr) {
    std::vector<std::string> out;
    std::string line;
    auto handle = [&](std::string& ln) {
        if (ln.empty() || ln[0] == '#') return;
        if (ln.rfind("@module ", 0) == 0) {
            std::istringstream iss(ln.substr(8));
            std::string nm, id;
            iss >> nm >> id;
            if (out_module_name) *out_module_name = nm;
            if (out_module_id)   *out_module_id   = id;
            return;
        }
        if (ln.rfind("@abi ", 0) == 0) {   // builder's version stamp
            if (out_abi_version) *out_abi_version = ln.substr(5);
            return;
        }
        if (ln[0] == '@') return;   // unknown directive — forward-compat skip
        out.push_back(std::move(ln));
    };
    for (uint8_t b : data) {
        if (b == '\n') { handle(line); line.clear(); }
        else line += (char)b;
    }
    handle(line);
    return out;
}

// Runtime ABI reuse check: may THIS compiler use a binary library built by
// `lib_ver`? One-directional compat — a newer minor reads libraries built by
// older minors of the SAME major (a major bump is a new language). Pre-release
// (`-pre`) / snapshot (`+meta`) builds carry NO ABI guarantee → require an EXACT
// version match, else just warn. Returns 0 OK / 1 warned / 2 incompatible
// (caller should not use the archive). Disable with LOGOS_NO_ABI_CHECK=1.
static int check_abi_reuse(std::string_view lib_ver, const std::string& archive) {
#ifndef LOGOS_VERSION_FULL
    (void)lib_ver; (void)archive; return 0;
#else
    if (std::getenv("LOGOS_NO_ABI_CHECK")) return 0;
    if (lib_ver.empty()) return 0;              // legacy archive (no stamp) — don't enforce
    const std::string self = LOGOS_VERSION_FULL;
    if (lib_ver == self) return 0;              // identical build — always fine
    auto parse = [](std::string_view v, int& maj, int& min, bool& stable) {
        stable = v.find('-') == std::string_view::npos && v.find('+') == std::string_view::npos;
        size_t i = 0;
        auto num = [&](int& o){ o = 0; while (i < v.size() && v[i] >= '0' && v[i] <= '9') o = o*10 + (v[i++]-'0'); };
        num(maj); min = 0; if (i < v.size() && v[i] == '.') { ++i; num(min); }
    };
    int lmaj, lmin, cmaj, cmin; bool lstable, cstable;
    parse(lib_ver, lmaj, lmin, lstable);
    parse(self, cmaj, cmin, cstable);
    const std::string lv(lib_ver);
    if (lmaj != cmaj) {
        std::fprintf(stderr, "logosc: error: %s was built with logos %s — incompatible "
            "language major (this compiler is %s); they cannot be mixed\n",
            archive.c_str(), lv.c_str(), self.c_str());
        return 2;
    }
    if (lstable && cstable) {
        if (lmin > cmin) {
            std::fprintf(stderr, "logosc: error: %s was built with logos %s (newer ABI); "
                "this compiler is %s — upgrade the compiler or rebuild the library\n",
                archive.c_str(), lv.c_str(), self.c_str());
            return 2;
        }
        return 0;                                // older-or-equal minor → readable
    }
    std::fprintf(stderr, "logosc: warning: %s was built with logos %s but this compiler is "
        "%s — pre-release/snapshot builds offer no ABI guarantee; result may be unstable\n",
        archive.c_str(), lv.c_str(), self.c_str());
    return 1;
#endif
}

// Scan search paths for lib*.a files. Returns map: package_name → archive_path.
// Each archive may provide multiple packages (e.g. libstdlib.a provides std.*, writ.*, etc.)
//
// Fast path: each emit_module-built archive embeds a `.pkgi` member with
// the package list as ASCII text. ar_read_members_streaming pulls it out
// with only a header walk + small member read — no 30MB memcpy of the
// .wr0 blob. Legacy archives without `.pkgi` fall through to scanning
// the .wr0 directly (slow but correct).
static std::unordered_map<std::string, std::string>
build_binary_index(const std::vector<std::string>& search_paths,
                   std::unordered_map<std::string, std::string>* module_archives = nullptr) {
    auto t0 = std::chrono::steady_clock::now();
    std::unordered_map<std::string, std::string> idx;
    size_t bytes_read = 0;
    size_t archives_seen = 0;
    size_t pkgi_hits = 0;
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
            ++archives_seen;
            // Fast path: streaming-read .pkgi members from the archive.
            auto pkgi_members = ar_read_members_streaming(archive, ".pkgi");
            if (!pkgi_members.empty()) {
                ++pkgi_hits;
                for (auto& m : pkgi_members) {
                    bytes_read += m.size();
                    auto unwrapped = unwrap_elf_section(m, ".lpkgindex");
                    std::string mod_name, abi_ver;  // §B-coex name + ABI stamp
                    auto pkgs = parse_pkgi_member(unwrapped, &mod_name, nullptr, &abi_ver);
                    // Runtime reuse check: skip indexing an ABI-incompatible
                    // archive so its packages aren't used (error already printed).
                    if (check_abi_reuse(abi_ver, archive) == 2) continue;
                    for (auto& pkg : pkgs)
                        if (!idx.count(pkg)) idx[pkg] = archive;
                    if (module_archives && !mod_name.empty())
                        module_archives->emplace(mod_name, archive);
                }
                continue;
            }
            // Fallback: legacy archive without .pkgi — scan .wr0 the slow way.
            auto members = ar_read_members(archive, ".wr0");
            for (auto& member : members) {
                bytes_read += member.size();
                for (auto& pkg : writ0_packages(member))
                    if (!idx.count(pkg)) idx[pkg] = archive;
            }
        }
    }
    if (std::getenv("LOGOS_TIME_INDEX") != nullptr) {
        auto us = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - t0).count();
        std::fprintf(stderr,
            "build_binary_index: %zu archives (%zu pkgi-hits), %zu pkgs, %zu bytes, %lldus\n",
            archives_seen, pkgi_hits, idx.size(), bytes_read, (long long)us);
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
//
// abs_excludes: absolute path prefixes that filter the walk — any
// discovered file whose canonical path starts with one of these
// prefixes is dropped from the index, so a `use foo.bar;` resolving
// to such a file falls back to the binary index (and ultimately to
// the "package not found" diagnostic if nothing supplies it). This
// is the loader-side mirror of the manifest `exclude` directive
// (see emit_module.cpp): when the monolith excludes `lang/` etc.,
// dependants must satisfy those imports via binary archives, not
// by re-absorbing the excluded sub-tree.
static PackageIndex build_package_index(
    const std::vector<std::string>& search_paths,
    const std::vector<std::string>& abs_excludes = {})
{
    PackageIndex idx;
    std::unordered_set<std::string> seen;
    auto excluded = [&](const std::string& path) {
        for (auto& p : abs_excludes)
            if (path.compare(0, p.size(), p) == 0) return true;
        return false;
    };
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
            if (excluded(canonical)) continue;
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
    const std::vector<std::string>& extra_archive_files,
    std::string_view implicit_prelude,
    const std::vector<std::string>& abs_excludes) noexcept
{
    const bool trace = std::getenv("LOGOS_TRACE_PHASES") != nullptr;
    bool had_error = false;

    PackageIndex index = build_package_index(search_paths, abs_excludes);
    // §B-coex: module canonical-NAME → archive, for `use pkg from <module>;`.
    std::unordered_map<std::string, std::string> module_archives;
    auto binary_index  = build_binary_index(search_paths, &module_archives);

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
        auto members = ar_read_members(archive, ".wr0");
        for (auto& member : members) {
            auto pkgs = writ0_packages(member);
            for (auto& pkg : pkgs) {
                if (!binary_index.count(pkg)) binary_index[pkg] = archive;
            }
        }
        // §B-coex: capture the explicit archive's @module name for `from`.
        for (auto& pm : ar_read_members_streaming(archive, ".pkgi")) {
            std::string mod_name;
            parse_pkgi_member(unwrap_elf_section(pm, ".lpkgindex"), &mod_name);
            if (!mod_name.empty()) { module_archives.emplace(mod_name, archive); break; }
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
        -> std::pair<writ::Writ, std::vector<UseRef>>
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
            // B-lx-03: identifiers are ASCII-only. If the failing line carries
            // a high-bit byte, the bare "syntax error near 'fn'" is misleading
            // (the lexer slurped 'привет' as bytes the parser then couldn't
            // classify). Surface that explicitly.
            const char* hint = "";
            uint32_t line_no = 1, line_start = 0;
            for (size_t i = 0; i < source.size(); ++i) {
                if (line_no == err_line) break;
                if (source[i] == '\n') { ++line_no; line_start = static_cast<uint32_t>(i + 1); }
            }
            for (size_t i = line_start; i < source.size() && source[i] != '\n'; ++i) {
                if (static_cast<unsigned char>(source[i]) >= 0x80) {
                    hint = " (note: identifiers must be ASCII; non-ASCII bytes "
                           "found on this line)";
                    break;
                }
            }
            std::fprintf(stderr,
                "error [%s]: syntax error near '%.*s' at line %u col %u%s\n",
                canonical.c_str(),
                static_cast<int>(err_text.size()), err_text.data(),
                err_line, err_col, hint);
            return {};
        }
        auto uses = extract_uses(ast, implicit_prelude);
        return {std::move(ast), std::move(uses)};
    };

    std::function<void(const std::string&, const std::string&)> visit_package;
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
        // Three-layer split Phase 4 transition: also recognize the new
        // logos.lang.*/mem.*/writ.* prefixes as foundational packages
        // so stdlib-internal cross-cutting traits (Default, Ord, Send,
        // AnyVal, ...) keep auto-loading after their package moves.
        // Phase 7 cleanup replaces this prefix-based hack with the
        // manifest-driven tier system.
        // R1: the cross-cutting foundation lives entirely in `logos.lang.*`
        // (marker/clone/cmp/ops/convert/default/hash/iter/option/result traits
        // + the lang.writ genos substrate: AnyVal, Map, view, ...). The
        // `logos.mem.*` layer (collections, encoding, mem.writ builders/
        // parser/clone/stringify) is NOT cross-cutting — modules that need
        // those `use` them explicitly. Auto-dragging the whole mem layer on
        // every compile that touches Vec/String was a perf regression AND
        // force-lowered mem generic templates in the consumer (exposing the
        // ObjectMap::init / Array__equal force-lower artifacts). Drop it.
        //
        // R2 (implicit-prelude 2x regression, 2026-05-24): exclude the
        // `logos.lang.writ.*` genos read-substrate (anyval/view/string/
        // scalar/array/map/objectmap/typed_value/decimal/fabric — ~40 of the
        // ~57 lang modules). It is the HEAVY part of the lang tier, and unlike
        // the cross-cutting TRAIT packages (marker/ops/cmp/clone/... which
        // binary modules reference bare without a `use` edge) the substrate is
        // ALWAYS reached via explicit `use logos.lang.writ.*` edges (verified
        // across stdlib). So the eager sibling auto-load of the substrate was
        // pure waste on every compile — requesting any one lang trait dragged
        // in the entire substrate. Let explicit-`use` recursion pull exactly
        // the substrate modules a compile references.
        return (starts("std.lang") || starts("std.writ")
                || starts("logos.lang"))
            && !starts("logos.lang.writ");
    };
    auto visit_binary_module = [&](const std::string& cache_key,
                                   const std::string& archive_path,
                                   const std::string& requested_pkg) {
        auto cit = binary_cache.find(cache_key);
        if (cit == binary_cache.end()) {
            if (trace)
                std::fprintf(stderr, "module_loader: loading binary module from %s\n",
                             archive_path.c_str());
            auto members = ar_read_members(archive_path, ".wr0");
            if (members.empty()) {
                std::fprintf(stderr, "module_loader: no .writ0 in %s\n", archive_path.c_str());
                binary_cache[cache_key].clear();
                return;
            }
            std::vector<ParsedModule> decoded;
            for (auto& member : members) {
                auto part = parse_writ0(member, archive_path);
                for (auto& pm : part) decoded.push_back(std::move(pm));
                if (trace) {
                    auto eopt = extract_writ0_exports(member, archive_path);
                    if (eopt.present) {
                        std::fprintf(stderr,
                            "module_loader: %s — exports: %zu struct, %zu enum, %zu fn templates, %zu blanket, %zu concrete impls\n",
                            archive_path.c_str(),
                            eopt.value.struct_templates.size(),
                            eopt.value.enum_templates.size(),
                            eopt.value.fn_templates.size(),
                            eopt.value.blanket_impls.size(),
                            eopt.value.concrete_impls.size());
                    }
                    auto bopt = extract_writ0_lir_blob(member, archive_path);
                    if (bopt.present) {
                        std::fprintf(stderr,
                            "module_loader: %s — lir_blob: %zu bytes\n",
                            archive_path.c_str(), bopt.bytes.size());
                    }
                }
                // Multi-arena IR Phase 3: if the lir_blob carries a
                // LirArenaRoot (emitted by Phase 3+ emit_module), load it
                // into a Writ doc and register with the global ArenaPool.
                // The ArenaPool keeps the holder alive via its own ref; the
                // local Writ doc handle drops at end of scope but the
                // arena stays live until pool unregisters (process exit).
                //
                // Failure modes (legacy archives without LirArenaRoot,
                // unknown module names, malformed root) are non-fatal —
                // we log under trace and continue without registration.
                {
                    auto bopt = extract_writ0_lir_blob(member, archive_path);
                    if (bopt.present && !bopt.bytes.empty()) {
                        auto doc_exp = writ::from_bytes_copy(
                            bopt.bytes.data(), bopt.bytes.size());
                        if (doc_exp) {
                            auto reg = writ::register_lir_arena(*doc_exp);
                            if (reg) {
                                if (trace) {
                                    std::fprintf(stderr,
                                        "module_loader: %s — registered LIR arena '%s' as arena_id=%u (%zu deps)\n",
                                        archive_path.c_str(),
                                        reg->name.c_str(),
                                        reg->arena_id.value,
                                        reg->depends_on.size());
                                }
                                // Loader-side import resolution: read this
                                // archive's `.imp` member (the module's import
                                // table) and attach it to the registered arena.
                                // A module-local arena_id in an ExternalRef then
                                // resolves via pool.resolve_local_arena_id ->
                                // (file_name, doc_name) -> the loaded document.
                                std::string base = archive_path;
                                if (auto s = base.find_last_of('/');
                                    s != std::string::npos)
                                    base = base.substr(s + 1);
                                for (auto& im : ar_read_members(archive_path, ".imp")) {
                                    auto blob = unwrap_elf_section(im, ".limports");
                                    auto entries = writ::read_import_table_blob(
                                        blob.data(), blob.size());
                                    if (entries) {
                                        size_t n = entries->size();
                                        writ::global_arena_pool().set_module_imports(
                                            reg->arena_id, base, std::move(*entries));
                                        if (trace) {
                                            std::fprintf(stderr,
                                                "module_loader: %s — import table: %zu slot(s)\n",
                                                archive_path.c_str(), n);
                                        }
                                        break;
                                    }
                                }
                            } else {
                                if (trace) {
                                    std::fprintf(stderr,
                                        "module_loader: %s — LIR blob present but register_lir_arena failed (legacy or no LirArenaRoot)\n",
                                        archive_path.c_str());
                                }
                            }
                        }
                    }
                }
            }
            // Stamp the owning module identity (canonical name + mangle id)
            // onto every file decoded from this archive, read from its single
            // `@module` .pkgi header. Downstream sema uses module_id to qualify
            // these items' symbols (one module per archive).
            {
                std::string mod_name, mod_id;
                for (auto& pm : ar_read_members_streaming(archive_path, ".pkgi")) {
                    auto unwrapped = unwrap_elf_section(pm, ".lpkgindex");
                    parse_pkgi_member(unwrapped, &mod_name, &mod_id);
                    if (!mod_id.empty() || !mod_name.empty()) break;
                }
                for (auto& pm : decoded) {
                    pm.module_id   = mod_id;
                    pm.module_name = mod_name;
                }
            }
            if (trace)
                std::fprintf(stderr, "module_loader: decoded %zu file(s) from %zu .writ0 member(s) in %s\n",
                             decoded.size(), members.size(), archive_path.c_str());
            cit = binary_cache.emplace(cache_key, std::move(decoded)).first;
        }

        // wanted: load the requested package unconditionally. For prelude
        // siblings (cross-cutting traits / genos types), skip if the
        // consumer's TEXT walk already provides the package — otherwise
        // we'd load the same package from both sources, registering its
        // impls twice ("conflicting implementations of trait X for
        // type Y" / "duplicate datatype Z" at sema). The requested_pkg
        // never gets text-index'd at this point (visit_package's text-
        // first check would have caught it), so it doesn't need the
        // skip; conversely prelude siblings DO need it when monolith's
        // recursive root absorbs lang/mem sub-trees that layer archives
        // also text-walk independently.
        auto wanted = [&](const std::string& pkg) {
            if (pkg == requested_pkg) return true;
            if (!pkg_in_prelude(pkg)) return false;
            if (index.count(pkg)) return false;  // text-walk already has it
            return true;
        };

        std::vector<UseRef> pkg_uses;
        for (auto& pm : cit->second) {
            if (!wanted(pm.package)) continue;
            auto uses = extract_uses(pm.ast);
            for (auto& u : uses) pkg_uses.push_back(std::move(u));
        }
        for (const auto& u : pkg_uses) visit_package(u.first, u.second);

        for (auto& pm : cit->second) {
            if (!wanted(pm.package)) continue;
            if (!visited_files.insert(pm.path).second) continue;
            if (!pm.package.empty()) visited_packages.insert(pm.package);
            modules.push_back({pm.path, pm.package, pm.ast,
                               /*from_binary_module=*/true,
                               /*module_id=*/pm.module_id,
                               /*module_name=*/pm.module_name,
                               /*is_lazy=*/pm.is_lazy});
        }
    };

    // Visit every file belonging to a package, post-order on dependencies.
    visit_package = [&](const std::string& pkg, const std::string& from_module) {
        // §B-coex: a no-`from` load keeps the bare pkg key (unchanged behavior); a
        // `from <M>` load keys by (M, pkg) so `use pkg from A;` and `from B;` BOTH
        // load (distinct modules sharing a package name).
        std::string vkey = from_module.empty() ? pkg : (from_module + "\x01" + pkg);
        if (!visited_packages.insert(vkey).second) return;

        // §B-coex: an explicit `from <module>` pins the source archive — load pkg
        // from THAT module (lets two modules' same-named package coexist).
        if (!from_module.empty()) {
            auto mit = module_archives.find(from_module);
            if (mit != module_archives.end()) {
                visit_binary_module(mit->second, mit->second, pkg);
                return;
            }
            // Unknown module name: fall through to default resolution; sema's
            // `use … from` check emits the precise "no loaded module" error.
        }

        // Check text index first (source build takes priority).
        auto it = index.find(pkg);
        if (it != index.end()) {
            struct Pending { std::string path; writ::Writ ast; };
            std::vector<Pending> pending;
            std::vector<UseRef> pkg_uses;
            for (const auto& file : it->second) {
                if (!visited_files.insert(file).second) continue;
                auto [ast, uses] = parse_one(file);
                if (ast.is_null()) continue;
                for (auto& u : uses) pkg_uses.push_back(std::move(u));
                pending.push_back({file, std::move(ast)});
            }
            for (const auto& u : pkg_uses) visit_package(u.first, u.second);
            for (auto& p : pending) modules.push_back({std::move(p.path), pkg, std::move(p.ast), false, {}, {}});
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
                    for (const auto& u : suses) visit_package(u.first, u.second);
                    modules.push_back({sib, declared_pkg, std::move(sast), false, {}, {}});
                }
            }
        }
        // Recurse into this file's own uses (post-order).
        for (const auto& u : uses) visit_package(u.first, u.second);
        modules.push_back({canonical, declared_pkg, std::move(ast), false, {}, {}});
    };

    // Kick off traversal at the root file.
    auto root_canonical = fs::weakly_canonical(root_path).string();
    auto root_pkg = scan_package_decl(root_canonical);
    visit_file(root_canonical, root_pkg);

    // Restore the dep-order invariant: text-only loads naturally produce
    // dep-first order via post-order DFS, but binary-archive loads bypass
    // that — packages from a .a land in `modules` at indices that don't
    // reflect actual deps. The topo-sort makes the invariant uniform
    // across both code paths. See topo_sort_modules() for the post-mortem
    // of bug (D) that motivated this.
    modules = topo_sort_modules(std::move(modules), implicit_prelude);

    if (out_had_error) *out_had_error = had_error;
    return modules;
}

} // namespace logos::compiler
