
#include "module_manifest.hpp"
#include <fstream>
#include <sstream>
#include <cctype>

namespace logos::compiler {

static std::string trim(std::string_view s) {
    size_t i = 0, j = s.size();
    while (i < j && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
    while (j > i && std::isspace(static_cast<unsigned char>(s[j-1]))) --j;
    return std::string(s.substr(i, j - i));
}

std::optional<ModuleManifest> parse_module_manifest(const std::string& path,
                                                    std::string& err_out) {
    std::ifstream f(path);
    if (!f) { err_out = "cannot open manifest: " + path; return {}; }

    ModuleManifest m;
    std::string line;
    while (std::getline(f, line)) {
        auto t = trim(line);
        if (t.empty() || t[0] == '#') continue;

        // Split into key + value on first whitespace.
        size_t sp = 0;
        while (sp < t.size() && !std::isspace(static_cast<unsigned char>(t[sp]))) ++sp;
        std::string key = t.substr(0, sp);
        std::string val = sp < t.size() ? trim(t.substr(sp)) : "";

        if      (key == "module")  m.name    = val;
        else if (key == "version") m.version = val;
        else if (key == "root")    m.root    = val;
        else if (key == "depends")  { if (!val.empty()) m.depends.push_back(val); }
        else if (key == "exclude")  { if (!val.empty()) m.excludes.push_back(val); }
        else if (key == "ast_only") { if (!val.empty()) m.ast_only.push_back(val); }
        else if (key == "lowering") {
            // Phase 6 (multi-arena IR): hybrid lazy mode. Default is eager
            // (existing behaviour — ship .o + LIR blob). `lazy` writes only
            // the parsed AST .hermes0; consumer lowers locally on use.
            if      (val == "lazy")  m.lazy = true;
            else if (val == "eager") m.lazy = false;
            else { err_out = "manifest: 'lowering' must be 'lazy' or 'eager', got '" + val + "'"; return {}; }
        }
        else if (key == "tier") {
            // Phase 3 of three-layer split. Validate against the closed set;
            // empty/unknown values rejected so typos surface early.
            if (val != "lang" && val != "mem" && val != "std") {
                err_out = "manifest: 'tier' must be 'lang', 'mem', or 'std', got '" + val + "'";
                return {};
            }
            m.tier = val;
        }
        else if (key == "prelude") {
            if (val.empty()) {
                err_out = "manifest: 'prelude' requires a package name";
                return {};
            }
            m.prelude = val;
        }
        // ignore unknown keys for forward compat
    }

    if (m.name.empty())  { err_out = "manifest missing 'module' directive"; return {}; }
    if (m.root.empty())  { err_out = "manifest missing 'root' directive";   return {}; }
    if (m.version.empty()) m.version = "0.0";

    return m;
}

} // namespace logos::compiler
