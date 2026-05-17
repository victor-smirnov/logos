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

    // Three-layer split (Phase 3) — `tier` and `prelude` directives.
    //
    // tier: declares the availability tier of this module.
    //   "lang" → no-alloc, no-OS. Loads only against logos.lang.*.
    //   "mem"  → heap, no-OS. Loads against lang + mem.
    //   "std"  → full (default). Loads against all three.
    // Empty string means "tier not declared" (legacy behaviour — module
    // sees whatever's on the search path, no enforcement). Phase 6.A wires
    // sema enforcement; Phase 3 stores the value only.
    std::string tier;

    // prelude: dotted package name to inject as implicit `use <pkg>;` at
    // the head of every file in this module that doesn't carry
    // `#![no_implicit_prelude]`. Empty means "no prelude" (legacy behaviour).
    // Typically:  logos.lang.prelude / logos.mem.prelude / logos.std.prelude.
    std::string prelude;
};

// Parse a logos.module manifest file.  Returns nullopt + message on error.
std::optional<ModuleManifest> parse_module_manifest(const std::string& path,
                                                    std::string& err_out);

} // namespace logos::compiler
