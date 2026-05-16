// Logos project — https://github.com/victor-smirnov/logos
//
// Arena publish helpers — implementation.
// See include/logos/hermes/arena_publish.hpp and
// docs/internals/multi-arena-ir.md §6.1.

#include <logos/hermes/arena_publish.hpp>
#include <logos/hermes/access.hpp>
#include <logos/hermes/any_val.hpp>
#include <logos/hermes/arena_string.hpp>
#include <logos/hermes/external_ref.hpp>
#include <logos/hermes/lir_arena_root.hpp>
#include <logos/hermes/mem_holder.hpp>
#include <logos/hermes/object_array.hpp>
#include <logos/hermes/tiny_object_map.hpp>
#include <logos/hermes/type_registry.hpp>
#include <logos/verification/assert.hpp>

namespace logos::hermes {

logos::expected<ArenaPublishBuilder>
lir_arena_root_begin(Hermes&                       doc,
                     std::string_view              module_name,
                     const std::vector<std::string>& dep_names) noexcept
{
    ArenaPublishBuilder b;
    // Borrow ref on the document (copy increments the MemHolder ref).
    b.doc = doc;

    // Allocate the root TinyObjectMap with schema_type_code = LirArenaRoot.
    LOGOS_TRY(auto root_map, b.doc.make_tiny_map(4));
    root_map.ptr()->set_schema_type_code(type_hash::LirArenaRoot);
    b.root_map = std::move(root_map);

    // Module name string.
    LOGOS_TRY(auto name_str, b.doc.make_string(module_name));

    // Deps array (each entry an AnyVal pointer → ArenaString).
    LOGOS_TRY(auto deps_arr, b.doc.make_array(dep_names.size()));
    for (auto& dn : dep_names) {
        LOGOS_TRY(auto dep_str, b.doc.make_string(dn));
        LOGOS_TRY_VOID(deps_arr.push_back(dep_str.to_anyval()));
    }

    // Directory array — sized for typical stdlib (~10K), grows as needed.
    LOGOS_TRY(auto dir_arr, b.doc.make_array(64));
    // Slot 0 = null sentinel (invariant #13: obj_id 0 is INVALID).
    LOGOS_TRY_VOID(dir_arr.push_back(AnyVal{}));
    b.directory = std::move(dir_arr);

    // Populate the root map's fields. Note: we must re-fetch via the
    // ptr() each time because allocation may have grown the arena.
    LOGOS_TRY_VOID(b.root_map.put(lir_arena_root::SCHEMA_VERSION,
        AnyVal::from_value<uint32_t>(
            lir_arena_root::CURRENT_VERSION,
            static_cast<uint8_t>(type_hash::U24))));
    LOGOS_TRY_VOID(b.root_map.put(lir_arena_root::MODULE_NAME, name_str.to_anyval()));
    LOGOS_TRY_VOID(b.root_map.put(lir_arena_root::DEPS,        deps_arr.to_anyval()));
    LOGOS_TRY_VOID(b.root_map.put(lir_arena_root::DIRECTORY,   b.directory.to_anyval()));

    return b;
}

logos::expected<uint32_t>
arena_publish(ArenaPublishBuilder& b, AnyVal target) noexcept
{
    LOGOS_ASSERT(!b.finalized, "ARENA-PUBLISH-001",
        "arena_publish: builder already finalized; arena is sealed");

    // obj_id is the index in the directory.
    uint32_t oid = static_cast<uint32_t>(b.directory.size());
    LOGOS_TRY_VOID(b.directory.push_back(target));
    return oid;
}

logos::expected<uint32_t>
arena_publish_reserved(ArenaPublishBuilder& b) noexcept
{
    return arena_publish(b, AnyVal{});  // null entry
}

logos::expected<arena_offset_t>
lir_arena_root_finalize(ArenaPublishBuilder& b) noexcept
{
    LOGOS_ASSERT(!b.finalized, "ARENA-FINALIZE-001",
        "lir_arena_root_finalize: builder already finalized");

    // Set DocumentHeader.root_offset → LirArenaRoot.
    b.doc.set_root(b.root_map);

    // Seal arena: no further allocations. After this, the arena bytes
    // can be safely dumped to .hermes0 and shared cross-thread.
    b.doc.seal();

    b.finalized = true;
    return b.root_map.offset();
}

logos::expected<ModuleHandle>
register_lir_arena(Hermes& doc, ArenaPool& pool) noexcept
{
    // Walk root: DocumentHeader → root_offset → object expected to be a
    // TinyObjectMap with schema_type_code = LirArenaRoot.
    LOGOS_ASSERT(doc.has_root(), "ARENA-REG-001",
        "register_lir_arena: doc has no root");

    auto root_obj = doc.root_object();
    LOGOS_ASSERT(root_obj.tagged().is_pointer(), "ARENA-REG-002",
        "register_lir_arena: root must be a pointer-mode AnyVal");

    // The root is a TinyMapView — wrap as such.
    auto root_av = root_obj.tagged();
    TinyMapView root_map(root_av.to_offset(), doc.holder());
    auto* tom = root_map.ptr();
    LOGOS_ASSERT(tom->schema_type_code() == type_hash::LirArenaRoot,
        "ARENA-REG-003",
        "register_lir_arena: root schema_type_code must be LirArenaRoot "
        "({}); got {}", type_hash::LirArenaRoot, tom->schema_type_code());

    LirArenaRootView lar(root_map);

    // Extract MODULE_NAME.
    auto name_view = lar.module_name();
    LOGOS_ASSERT(!name_view.is_null(), "ARENA-REG-004",
        "register_lir_arena: MODULE_NAME field is missing or null");
    std::string name(name_view.view());

    // Extract DEPS (vector of dep names).
    std::vector<std::string> deps;
    auto deps_arr = lar.deps();
    if (!deps_arr.is_null()) {
        deps.reserve(deps_arr.size());
        for (uint64_t i = 0; i < deps_arr.size(); ++i) {
            auto dep_av = deps_arr.get(i);
            LOGOS_ASSERT(dep_av.is_pointer(), "ARENA-REG-005",
                "register_lir_arena: DEPS[{}] must be a string pointer", i);
            StringView dep_str(dep_av.to_offset(), doc.holder());
            deps.emplace_back(dep_str.view());
        }
    }

    return pool.register_module(doc.holder(), std::move(name), std::move(deps));
}

// ── Phase 2.A: ExternalRef resolution via ArenaPool dispatch ────────────
//
// Walks: arena_id → MemHolder → LirArenaRoot → directory → obj_id slot.
// 3 indirections per docs/internals/multi-arena-ir.md §3.1. All steps
// degenerate to "arena not registered" / "obj_id out of bounds" failures
// that return ok()=false (no UB).

ExternalRefResolved resolve_external_ref(
    const ExternalRef& ref,
    ArenaPool&         pool) noexcept
{
    ExternalRefResolved fail{nullptr, arena_offset_t{}};

    auto aid = ref.arena_id();
    if (!aid.is_valid()) return fail;
    uint32_t oid = ref.obj_id();
    if (oid == 0) return fail;  // invariant #13: obj_id 0 is INVALID

    auto* mem = pool.get(aid);
    if (!mem) return fail;

    // Walk LirArenaRoot → DIRECTORY.
    auto* hdr = reinterpret_cast<const DocumentHeader*>(mem->base());
    if (hdr->root_offset == NULL_OFFSET) return fail;

    auto* root_tom = reinterpret_cast<const TinyObjectMap*>(
        mem->base() + hdr->root_offset.value());
    if (root_tom->schema_type_code() != type_hash::LirArenaRoot) return fail;

    auto dir_av = root_tom->get(lir_arena_root::DIRECTORY.code, mem->base());
    if (dir_av.is_null() || !dir_av.is_pointer()) return fail;

    auto* dir_arr = reinterpret_cast<const ObjectArray*>(
        mem->base() + dir_av.to_offset().value());
    if (oid >= dir_arr->size()) return fail;

    auto target_av = const_cast<ObjectArray*>(dir_arr)->get(
        oid, const_cast<uint8_t*>(mem->base()));
    if (target_av.is_null() || !target_av.is_pointer()) return fail;

    return ExternalRefResolved{mem, target_av.to_offset()};
}

} // namespace logos::hermes
