// Logos project — https://github.com/victor-smirnov/logos
//
// ImportTable builder (Writ) — see import_table.hpp.

#include <logos/writ/import_table.hpp>
#include <logos/writ/document.hpp>
#include <logos/writ/clone.hpp>          // compactify
#include <logos/writ/arena_string.hpp>
#include <logos/writ/object_array.hpp>
#include <logos/writ/tiny_object_map.hpp>
#include <logos/writ/type_codes.hpp>
#include <logos/core/expected.hpp>

namespace logos::writ {

namespace {
inline AnyVal ref_to(const void* p) noexcept { AnyVal a; a.set_ref(p); return a; }
}  // namespace

logos::expected<std::vector<uint8_t>>
build_import_table_blob(std::string_view                module_name,
                        const std::vector<ImportEntry>& imports) noexcept
{
    LOGOS_TRY(auto doc, WritCtr::make());
    Arena& a = doc.arena();

    LOGOS_TRY(auto* root, TinyObjectMap::create(a, 4));
    root->set_schema_type_code(import_table::SCHEMA_CODE);

    LOGOS_TRY(auto* name_str, ArenaString::create(a, module_name));

    // IMPORTS array indexed by arena_id; slot 0 = null sentinel.
    LOGOS_TRY(auto* imports_arr, ObjectArray::create(a, imports.size() + 1));
    LOGOS_TRY_VOID(imports_arr->push_back(AnyVal{}, a));
    for (auto& e : imports) {
        LOGOS_TRY(auto* em, TinyObjectMap::create(a, 2));
        LOGOS_TRY(auto* file_str, ArenaString::create(a, e.file_name));
        LOGOS_TRY(auto* doc_str,  ArenaString::create(a, e.doc_name));
        LOGOS_TRY_VOID(em->put(import_table::entry::FILE_NAME, ref_to(file_str), a));
        LOGOS_TRY_VOID(em->put(import_table::entry::DOC_NAME,  ref_to(doc_str),  a));
        LOGOS_TRY_VOID(imports_arr->push_back(ref_to(em), a));
    }

    LOGOS_TRY_VOID(root->put(import_table::SCHEMA_VERSION,
        AnyVal::pod(import_table::CURRENT_VERSION, tc::HT_U24), a));
    LOGOS_TRY_VOID(root->put(import_table::MODULE_NAME, ref_to(name_str),    a));
    LOGOS_TRY_VOID(root->put(import_table::IMPORTS,     ref_to(imports_arr), a));

    doc.set_root(ref_to(root));

    // Compact to a rigid single-segment blob, then dump its bytes.
    LOGOS_TRY(auto comp, compactify(doc));
    return std::vector<uint8_t>(comp.blob_data(), comp.blob_data() + comp.blob_size());
}

logos::expected<std::vector<ImportEntry>>
read_import_table_blob(const uint8_t* data, size_t size) noexcept
{
    LOGOS_TRY(auto doc, WritCtr::from_bytes(data, size));

    AnyVal root = doc.root();
    if (!root.is_ref())
        return std::unexpected(logos::err(ErrCode::parse_error));
    auto* tom = reinterpret_cast<const TinyObjectMap*>(root.resolve());
    if (tom->schema_type_code() != import_table::SCHEMA_CODE)
        return std::unexpected(logos::err(ErrCode::parse_error));

    AnyVal imports_av = tom->get(import_table::IMPORTS);
    if (!imports_av.is_ref())
        return std::unexpected(logos::err(ErrCode::parse_error));
    auto* arr = reinterpret_cast<const ObjectArray*>(imports_av.resolve());

    auto read_str = [](AnyVal av) -> std::string {
        if (!av.is_ref()) return {};
        return std::string(reinterpret_cast<const ArenaString*>(av.resolve())->view());
    };

    std::vector<ImportEntry> out;
    out.reserve(arr->size());
    for (uint64_t i = 0; i < arr->size(); ++i) {
        AnyVal e = arr->get(i);
        if (!e.is_ref()) { out.push_back(ImportEntry{}); continue; }   // slot 0 / sparse
        auto* em = reinterpret_cast<const TinyObjectMap*>(e.resolve());
        out.push_back(ImportEntry{
            read_str(em->get(import_table::entry::FILE_NAME)),
            read_str(em->get(import_table::entry::DOC_NAME))});
    }
    return out;
}

}  // namespace logos::writ
