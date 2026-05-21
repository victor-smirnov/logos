// Logos project — https://github.com/victor-smirnov/logos
//
// ImportTable: a module's import-table document (multi-arena IR).
//
// Shipped as a SEPARATE Hermes document (its own `.imp` archive member, wrapped
// in a `.limports` ELF section) so a tool can read just that one small member
// to see which libraries a module imports — fast dependency inspection without
// touching the (large) LIR blob.
//
// Root is a TinyObjectMap with schema_type_code = type_hash::ImportTable.
// It maps the module-local `arena_id` (the index into IMPORTS) to a
// (file_name, doc_name) pair. A cross-arena ExternalRef's arena_id (3 bytes)
// is exactly this index: arena_id → IMPORTS[arena_id] → (file_name, doc_name)
// → the concrete loaded Hermes document → directory[element_id] → element.
//
// Schema (byte-keyed fields per TinyObjectMap convention):
//   SCHEMA_VERSION (key 0) : uint32_t embedded AnyVal
//   MODULE_NAME    (key 1) : AnyVal pointer → ArenaString (this module's name)
//   IMPORTS        (key 2) : AnyVal pointer → ObjectArray<AnyVal>, indexed by
//                            arena_id. Slot 0 = null sentinel (arena_id 0 is
//                            INVALID, matching ExternalRef). Slot i (i>=1) =
//                            AnyVal pointer → an import-entry TinyObjectMap.
//
// Import-entry TinyObjectMap (import_table::entry):
//   FILE_NAME (key 0) : AnyVal pointer → ArenaString (e.g. "liblogos-lang.a")
//   DOC_NAME  (key 1) : AnyVal pointer → ArenaString (document name within the
//                       file; "" = the file's single/default document — we ship
//                       one doc per .hermes0 today, multi-doc is reserved).

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <logos/core/expected.hpp>
#include <logos/hermes/named_code.hpp>
#include <logos/hermes/type_registry.hpp>

namespace logos::hermes::import_table {

inline constexpr ::logos::hermes::NamedCode<uint8_t> SCHEMA_VERSION{"schema_version", 0};
inline constexpr ::logos::hermes::NamedCode<uint8_t> MODULE_NAME   {"module_name",    1};
inline constexpr ::logos::hermes::NamedCode<uint8_t> IMPORTS       {"imports",        2};

inline constexpr uint32_t CURRENT_VERSION = 1;

namespace entry {
inline constexpr ::logos::hermes::NamedCode<uint8_t> FILE_NAME{"file_name", 0};
inline constexpr ::logos::hermes::NamedCode<uint8_t> DOC_NAME {"doc_name",  1};
}  // namespace entry

}  // namespace logos::hermes::import_table

namespace logos::hermes {

// One import-table entry: (file_name, doc_name). doc_name "" = default doc.
struct ImportEntry {
    std::string file_name;
    std::string doc_name;
};

// Build a standalone import-table Hermes document from `imports` (ordered;
// arena_id = index+1, since slot 0 is the null sentinel) and return its raw
// arena bytes — ready to write to a file + wrap into a `.limports` member.
[[nodiscard]] logos::expected<std::vector<uint8_t>>
build_import_table_blob(std::string_view                module_name,
                        const std::vector<ImportEntry>& imports) noexcept;

// Parse an import-table blob (the bytes produced by build_import_table_blob)
// back into entries INDEXED BY arena_id: result[0] is the null sentinel
// (empty file_name) and result[arena_id] is the (file_name, doc_name) for
// that module-local arena_id. So `result[ref.arena_id()]` is the direct
// lookup. Returns an error if the bytes are not an ImportTable document.
[[nodiscard]] logos::expected<std::vector<ImportEntry>>
read_import_table_blob(const uint8_t* data, size_t size) noexcept;

}  // namespace logos::hermes
