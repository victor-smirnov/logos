// Logos project — https://github.com/victor-smirnov/logos
//
// ImportTable builder — see include/logos/hermes/import_table.hpp.

#include <logos/hermes/import_table.hpp>

#include <logos/hermes/any_val.hpp>
#include <logos/hermes/arena_string.hpp>
#include <logos/hermes/document.hpp>
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

logos::expected<std::vector<ImportEntry>>
read_import_table_blob(const uint8_t* data, size_t size) noexcept
{
    LOGOS_TRY(auto doc, from_bytes_copy(data, size));
    auto* holder = doc.holder();
    if (!holder) return std::unexpected(logos::Err::from_code(1));
    uint8_t* base = const_cast<uint8_t*>(holder->base());

    auto* hdr = reinterpret_cast<const DocumentHeader*>(base);
    if (hdr->root_offset == NULL_OFFSET)
        return std::unexpected(logos::Err::from_code(1));
    auto* root = reinterpret_cast<const TinyObjectMap*>(
        base + hdr->root_offset.value());
    if (root->schema_type_code() != type_hash::ImportTable)
        return std::unexpected(logos::Err::from_code(1));

    AnyVal imports_av = root->get(import_table::IMPORTS.code, base);
    if (imports_av.is_null() || !imports_av.is_pointer())
        return std::unexpected(logos::Err::from_code(1));
    auto* arr = reinterpret_cast<const ObjectArray*>(
        base + imports_av.to_offset().value());

    auto read_str = [&](AnyVal av) -> std::string {
        if (av.is_null() || !av.is_pointer()) return {};
        return std::string(StringView(av.to_offset(), holder).view());
    };

    std::vector<ImportEntry> out;
    out.reserve(arr->size());
    for (uint64_t i = 0; i < arr->size(); ++i) {
        AnyVal e_av = const_cast<ObjectArray*>(arr)->get(i, base);
        if (e_av.is_null() || !e_av.is_pointer()) {
            out.push_back(ImportEntry{});  // slot 0 sentinel (or sparse)
            continue;
        }
        auto* em = reinterpret_cast<const TinyObjectMap*>(
            base + e_av.to_offset().value());
        out.push_back(ImportEntry{
            read_str(em->get(import_table::entry::FILE_NAME.code, base)),
            read_str(em->get(import_table::entry::DOC_NAME.code, base))});
    }
    return out;
}

}  // namespace logos::hermes
