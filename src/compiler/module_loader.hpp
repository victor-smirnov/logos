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
//   u16 trailer_version    // 1 = templates only; 2 = +blanket/concrete impls
//   u16 reserved (0)
//   u32 num_struct_templates
//   for each: u32 pkg_len, pkg bytes, u32 name_len, name bytes
//   u32 num_enum_templates
//   for each: u32 pkg_len, pkg bytes, u32 name_len, name bytes
//   u32 num_fn_templates
//   for each: u32 name_len, name bytes (already pkg-mangled)
//   // v2 additions (absent in v1):
//   u32 num_blanket_impls
//   for each:
//     u32 trait_len, trait bytes
//     u32 bound_len, bound bytes
//     u32 num_extra_bounds
//     for each: u32 b_len, b bytes
//   u32 num_concrete_impls
//   for each:
//     u32 trait_len, trait bytes
//     u32 target_len, target bytes
//
// Forward-compat: v3 readers that don't know about a future trailer_version
// must skip the section (the outer u64-length prefix lets them do so). The
// in-trailer fields are read top-down: a v2 reader against a v3 trailer
// reads through the v2 fields and stops (outer length prefix bounds the
// scan).
struct StdlibExports {
    // (pkg, name) — pkg may be empty for items without a package decl
    std::vector<std::pair<std::string, std::string>> struct_templates;
    std::vector<std::pair<std::string, std::string>> enum_templates;
    // Mangled name (already pkg-qualified per the unconditional-mangling epic).
    std::vector<std::string> fn_templates;
    // v2: blanket impls — `impl<T: Bound + Extra...> Trait for T`.
    // target_type is always the typevar by definition.
    struct BlanketImpl {
        std::string trait_name;
        std::string bound_trait;            // primary bound; "" for unbounded
        std::vector<std::string> extra_bounds;
    };
    std::vector<BlanketImpl> blanket_impls;
    // v2: concrete impls — `impl Trait for Target`. Negative impls + DST
    // target patterns are dropped (catalog is for fast-path lookups only).
    struct ConcreteImpl {
        std::string trait_name;
        std::string target_type;
    };
    std::vector<ConcreteImpl> concrete_impls;
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

// M3 step 3: extract + merge StdlibExports across a set of archive paths.
// For each archive, reads its .hermes0 member(s) and unions any present
// exports trailer into the result. Archives without a v3 trailer (e.g.
// non-stdlib libraries that haven't been re-emitted) contribute nothing.
// Order is preserved as given; later archives win on duplicate fn_templates
// (rare — only happens if a project redefines a stdlib mangled symbol).
StdlibExports load_archive_exports(const std::vector<std::string>& archive_paths);

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
