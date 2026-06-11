// Logos project — https://github.com/victor-smirnov/logos
//
// ImportTable (Hermes2 port) — a module's import-table document.
//
// Shipped as a SEPARATE Hermes2 document (its own `.imp` archive member) so a tool
// can read just that small member to inspect which libraries a module imports.
//
// Root is a TinyObjectMap whose schema_type_code == import_table::SCHEMA_CODE. It
// maps the module-local arena_id (index into IMPORTS) to a (file_name, doc_name)
// pair: a cross-arena ExternalRef's arena_id IS that index.
//
// Schema (byte-keyed fields):
//   SCHEMA_VERSION (key 0) : u24-embedded AnyVal Pod
//   MODULE_NAME    (key 1) : Ref → ArenaString
//   IMPORTS        (key 2) : Ref → ObjectArray, indexed by arena_id (slot 0 = null
//                            sentinel; slot i ≥ 1 = Ref → import-entry TinyObjectMap)
//
// Import-entry TinyObjectMap (import_table::entry):
//   FILE_NAME (key 0) : Ref → ArenaString  (e.g. "liblogos-lang.a")
//   DOC_NAME  (key 1) : Ref → ArenaString  ("" = the file's single/default document)

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <logos/core/expected.hpp>

namespace logos::hermes::import_table {

inline constexpr uint8_t SCHEMA_VERSION = 0;
inline constexpr uint8_t MODULE_NAME    = 1;
inline constexpr uint8_t IMPORTS        = 2;

inline constexpr uint64_t SCHEMA_CODE     = 5003;   // TinyObjectMap.schema_type_code tag
inline constexpr uint32_t CURRENT_VERSION = 1;

namespace entry {
inline constexpr uint8_t FILE_NAME = 0;
inline constexpr uint8_t DOC_NAME  = 1;
}  // namespace entry

}  // namespace logos::hermes::import_table

namespace logos::hermes {

// One import-table entry: (file_name, doc_name). doc_name "" = default doc.
struct ImportEntry {
    std::string file_name;
    std::string doc_name;
};

// Build a standalone import-table Hermes2 blob (compacted, single-segment) from
// `imports` (ordered; arena_id = index + 1, slot 0 = null sentinel). Ready to write
// to a file / wrap into a `.imp` member.
[[nodiscard]] logos::expected<std::vector<uint8_t>>
build_import_table_blob(std::string_view                module_name,
                        const std::vector<ImportEntry>& imports) noexcept;

// Parse an import-table blob back into entries INDEXED BY arena_id: result[0] is
// the null sentinel (empty file_name); result[arena_id] is its (file_name,
// doc_name). So `result[ref.arena_id()]` is the direct lookup.
[[nodiscard]] logos::expected<std::vector<ImportEntry>>
read_import_table_blob(const uint8_t* data, size_t size) noexcept;

}  // namespace logos::hermes
