// Logos project — module manifest parser for `logosc --emit-module`.

#pragma once
#include <string>
#include <vector>
#include <optional>

namespace logos::compiler {

// Parsed logos.module manifest.
struct ModuleManifest {
    std::string name;      // e.g. "stdlogos"
    std::string version;   // e.g. "0.1"
    std::string root;      // directory containing .logos files (relative or absolute)
    std::vector<std::string> depends;  // other module names (for future use)
    std::vector<std::string> excludes; // path-prefixes to drop from the archive entirely (no .o, no .hermes0)
    std::vector<std::string> ast_only; // path-prefixes included as .hermes0 only — codegen skipped (host-extern bodies invalid for user link)

    // Multi-arena IR Phase 6 — hybrid lazy mode.
    //
    // eager (default): emit_module runs full sema+mono+codegen, writes
    //   NAME.o + .hermes0 (with parsed AST + LIR blob). Consumer-side
    //   compile uses pre-built .o for linking and the LIR blob for
    //   cross-arena generic instantiation (Phase 5.B).
    //
    // lazy: emit_module writes ONLY .hermes0 with parsed AST. No .o, no
    //   LIR blob, no exports trailer. Consumer-side sema loads the AST
    //   and lowers any referenced items into the consumer's own arena
    //   on demand (item bodies become user-code-equivalent in the
    //   consumer's emit). Smaller archive; per-consumer compile cost
    //   trade-off for libraries with many items but sparse consumer
    //   reference patterns.
    //
    // Manifest directive: `lowering eager` / `lowering lazy`.
    bool lazy = false;
};

// Parse a logos.module manifest file.  Returns nullopt + message on error.
std::optional<ModuleManifest> parse_module_manifest(const std::string& path,
                                                    std::string& err_out);

} // namespace logos::compiler
