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

// M3: stdlib exports payload carried in the .hermes0 v3 trailer.
// Populated by emit_module from the post-sema LProgram before mono runs;
// future mono-side hookup will use it to skip iterating in_.structs/enums/
// functions for stdlib content and instead seed templates_/struct_templates_/
// enum_templates_ directly. For now it's a name catalog only — entries here
// are precisely the items whose type_params is non-empty in sema's output.
//
// Trailer format (inside the u64-prefixed exports section of the .hermes0):
//   u16 trailer_version    // 1 = "templates only (names)"
//   u16 reserved (0)
//   u32 num_struct_templates
//   for each: u32 pkg_len, pkg bytes, u32 name_len, name bytes
//   u32 num_enum_templates
//   for each: u32 pkg_len, pkg bytes, u32 name_len, name bytes
//   u32 num_fn_templates
//   for each: u32 name_len, name bytes (already pkg-mangled)
//
// Forward-compat: v3 readers that don't know about a future trailer_version
// must skip the section (the outer u64-length prefix lets them do so).
struct StdlibExports {
    // (pkg, name) — pkg may be empty for items without a package decl
    std::vector<std::pair<std::string, std::string>> struct_templates;
    std::vector<std::pair<std::string, std::string>> enum_templates;
    // Mangled name (already pkg-qualified per the unconditional-mangling epic).
    std::vector<std::string> fn_templates;
};

// Decode the exports trailer from a .hermes0 blob. Returns empty exports on
// v2 archives or when the trailer is absent/zero-length. Returns nullopt on
// a malformed trailer (caller should treat that as a fatal load error).
struct StdlibExportsOpt {
    bool present = false;
    StdlibExports value;
};
StdlibExportsOpt extract_hermes0_exports(const std::vector<uint8_t>& data,
                                          const std::string& archive_path);

// Load a .logos file and all its transitive dependencies.
// search_paths: directories to search for package files (e.g. {"stdlib"}).
// extra_archive_files: explicit `.a` paths from -l / --lib (additional
//   binary modules outside any search dir).
// Returns all modules in dependency order (dependencies first, root last).
// If `out_had_error` is non-null, it is set to true when at least one
// `use <pkg>;` could not be resolved (B-mv-03/04). The diagnostic is still
// printed to stderr; the flag lets the caller treat it as fatal.
std::vector<ParsedModule> load_modules(
    const std::string& root_path,
    const std::vector<std::string>& search_paths,
    bool* out_had_error = nullptr,
    const std::vector<std::string>& extra_archive_files = {}) noexcept;

} // namespace logos::compiler
