// Logos project — https://github.com/victor-smirnov/logos
//
// ImportTable builder — see include/logos/hermes/import_table.hpp.

#include <logos/hermes/import_table.hpp>

#include <logos/hermes/any_val.hpp>
#include <logos/hermes/arena_string.hpp>
#include <logos/hermes/object_array.hpp>
#include <logos/hermes/tiny_object_map.hpp>
#include <logos/hermes/type_registry.hpp>
#include <logos/hermes/view.hpp>

namespace logos::hermes {

logos::expected<std::vector<uint8_t>>
build_import_table_blob(std::string_view                module_name,
                        const std::vector<ImportEntry>& imports) noexcept
{
    LOGOS_TRY(auto doc, make_doc());

    // Root TinyObjectMap, schema = ImportTable.
    LOGOS_TRY(auto root, doc.make_tiny_map(4));
    root.ptr()->set_schema_type_code(type_hash::ImportTable);

    LOGOS_TRY(auto name_str, doc.make_string(module_name));

    // IMPORTS array indexed by arena_id; slot 0 = null sentinel (arena_id 0
    // is INVALID, matching ExternalRef).
    LOGOS_TRY(auto imports_arr, doc.make_array(imports.size() + 1));
    LOGOS_TRY_VOID(imports_arr.push_back(AnyVal{}));
    for (auto& e : imports) {
        LOGOS_TRY(auto entry_map, doc.make_tiny_map(2));
        LOGOS_TRY(auto file_str, doc.make_string(e.file_name));
        LOGOS_TRY(auto doc_str,  doc.make_string(e.doc_name));
        LOGOS_TRY_VOID(entry_map.put(import_table::entry::FILE_NAME, file_str.to_anyval()));
        LOGOS_TRY_VOID(entry_map.put(import_table::entry::DOC_NAME,  doc_str.to_anyval()));
        LOGOS_TRY_VOID(imports_arr.push_back(entry_map.to_anyval()));
    }

    LOGOS_TRY_VOID(root.put(import_table::SCHEMA_VERSION,
        AnyVal::from_value<uint32_t>(import_table::CURRENT_VERSION,
                                     static_cast<uint8_t>(type_hash::U24))));
    LOGOS_TRY_VOID(root.put(import_table::MODULE_NAME, name_str.to_anyval()));
    LOGOS_TRY_VOID(root.put(import_table::IMPORTS,     imports_arr.to_anyval()));

    doc.set_root(root);
    doc.seal();

    const auto& chunk = doc.holder()->arena().head();
    return std::vector<uint8_t>(chunk.data(), chunk.data() + chunk.used);
}

}  // namespace logos::hermes
