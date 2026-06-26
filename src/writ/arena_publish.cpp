// Logos project — https://github.com/victor-smirnov/logos
//
// Arena publish helpers (Writ) — implementation. See arena_publish.hpp.

#include <logos/writ/arena_publish.hpp>
#include <logos/writ/lir_arena_root.hpp>
#include <logos/writ/view.hpp>
#include <logos/writ/type_codes.hpp>
#include <logos/core/expected.hpp>
#include <logos/verification/assert.hpp>

namespace logos::writ {

namespace {
// A value-form Ref AnyVal pointing at an in-arena object.
inline AnyVal ref_to(const void* p) noexcept { AnyVal a; a.set_ref(p); return a; }
}  // namespace

logos::expected<ArenaPublishBuilder>
lir_arena_root_begin(WritCtr&                       doc,
                     std::string_view                module_name,
                     const std::vector<std::string>& dep_names) noexcept
{
    ArenaPublishBuilder b;
    b.doc = &doc;
    Arena& a = doc.arena();

    // Root TinyObjectMap, schema-tagged as LirArenaRoot.
    LOGOS_TRY(auto* root, TinyObjectMap::create(a, 8));
    root->set_schema_type_code(lir_arena_root::SCHEMA_CODE);
    b.root_map = root;

    // Module-name string.
    LOGOS_TRY(auto* name_str, ArenaString::create(a, module_name));

    // Deps array (each entry a Ref → ArenaString).
    LOGOS_TRY(auto* deps_arr, ObjectArray::create(a, dep_names.empty() ? 1 : dep_names.size()));
    for (auto& dn : dep_names) {
        LOGOS_TRY(auto* dep_str, ArenaString::create(a, dn));
        LOGOS_TRY_VOID(deps_arr->push_back(ref_to(dep_str), a));
    }

    // Directory — slot 0 = null sentinel (obj_id 0 is INVALID).
    LOGOS_TRY(auto* dir, ObjectArray::create(a, 64));
    LOGOS_TRY_VOID(dir->push_back(AnyVal{}, a));
    b.directory = dir;

    // Exports map (name → obj_id).
    LOGOS_TRY(auto* ex, ObjectMap::create(a, 8));
    b.exports = ex;

    // Wire the root's fields. (No re-fetch dance: the never-move arena keeps `root`
    // stable across these allocations.)
    LOGOS_TRY_VOID(root->put(lir_arena_root::SCHEMA_VERSION,
        AnyVal::pod(lir_arena_root::CURRENT_VERSION, tc::HT_U24), a));
    LOGOS_TRY_VOID(root->put(lir_arena_root::MODULE_NAME, ref_to(name_str), a));
    LOGOS_TRY_VOID(root->put(lir_arena_root::DEPS,        ref_to(deps_arr), a));
    LOGOS_TRY_VOID(root->put(lir_arena_root::DIRECTORY,   ref_to(dir),      a));
    LOGOS_TRY_VOID(root->put(lir_arena_root::EXPORTS,     ref_to(ex),       a));

    return b;
}

logos::expected<uint32_t>
arena_publish(ArenaPublishBuilder& b, AnyVal target) noexcept
{
    LOGOS_ASSERT(!b.finalized, "ARENA-PUBLISH-001",
        "arena_publish: builder already finalized; arena is sealed");
    uint32_t oid = static_cast<uint32_t>(b.directory->size());
    LOGOS_TRY_VOID(b.directory->push_back(target, b.arena()));
    return oid;
}

logos::expected<uint32_t>
arena_publish_reserved(ArenaPublishBuilder& b) noexcept
{
    return arena_publish(b, AnyVal{});  // null directory entry
}

logos::expected<uint32_t>
arena_publish_named(ArenaPublishBuilder& b,
                    std::string_view     name,
                    AnyVal               target) noexcept
{
    LOGOS_ASSERT(!b.finalized, "ARENA-PUBLISH-NAMED-001",
        "arena_publish_named: builder already finalized; arena is sealed");
    LOGOS_TRY(uint32_t oid, arena_publish(b, target));
    LOGOS_TRY_VOID(b.exports->put(name, AnyVal::pod(oid, tc::HT_U24), b.arena()));
    return oid;
}

logos::expected<AnyVal>
lir_arena_root_finalize(ArenaPublishBuilder& b) noexcept
{
    LOGOS_ASSERT(!b.finalized, "ARENA-FINALIZE-001",
        "lir_arena_root_finalize: builder already finalized");
    AnyVal root = ref_to(b.root_map);
    b.doc->set_root(root);
    b.arena().seal();
    b.finalized = true;
    return root;
}

logos::expected<ModuleHandle>
register_lir_arena(WritCtr& doc, ArenaPool& pool) noexcept
{
    AnyVal root = doc.root();
    if (!root.is_ref())
        return std::unexpected(logos::err(ErrCode::parse_error));
    auto* tom = reinterpret_cast<TinyObjectMap*>(const_cast<uint8_t*>(root.resolve()));
    if (tom->schema_type_code() != lir_arena_root::SCHEMA_CODE)
        return std::unexpected(logos::err(ErrCode::parse_error));

    LirArenaRootView lar(TinyMapView(tom, doc.holder()));

    auto name_v = lar.module_name();
    if (name_v.is_null())
        return std::unexpected(logos::err(ErrCode::parse_error));
    std::string name(name_v.view());

    // Idempotency: re-registering the same module name returns the existing handle.
    if (auto existing = pool.find_by_name(name); existing.has_value())
        return *existing;

    std::vector<std::string> deps;
    auto da = lar.deps();
    if (!da.is_null()) {
        deps.reserve(da.size());
        for (uint64_t i = 0; i < da.size(); ++i) {
            auto sv = as_string(da.get(i), doc.holder());
            if (!sv.is_null()) deps.emplace_back(sv.view());
        }
    }

    return pool.register_module(doc.holder(), std::move(name), std::move(deps));
}

} // namespace logos::writ
