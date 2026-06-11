// Logos project — https://github.com/victor-smirnov/logos
//
// ExternalRef resolution (Hermes). See external_ref.hpp. Walks the pool dispatch
// arena_id → MemHolder → LirArenaRoot → DIRECTORY[obj_id] → target object. Every
// step degenerates to ok()=false (no UB) on a missing/out-of-range reference.

#include <logos/hermes/external_ref.hpp>
#include <logos/hermes/document.hpp>        // doc_header
#include <logos/hermes/lir_arena_root.hpp>  // SCHEMA_CODE, DIRECTORY
#include <logos/hermes/mem_holder.hpp>
#include <logos/hermes/object_array.hpp>
#include <logos/hermes/tiny_object_map.hpp>

namespace logos::hermes {

arena_offset_t ExternalRefResolved::offset() const noexcept {
    return mem ? arena_offset_t(static_cast<uint32_t>(obj - mem->arena().head().data()))
               : NULL_OFFSET;
}

ExternalRefResolved resolve_external_ref(const ExternalRef& ref, ArenaPool& pool) noexcept {
    ExternalRefResolved fail{};

    if (!ref.aid.is_valid()) return fail;
    if (ref.oid == 0)        return fail;   // obj_id 0 is the INVALID sentinel

    auto* mem = pool.get(ref.aid);
    if (!mem) return fail;

    AnyVal root = doc_header(mem)->root;
    if (!root.is_ref()) return fail;
    auto* tom = reinterpret_cast<const TinyObjectMap*>(root.resolve());
    if (tom->schema_type_code() != lir_arena_root::SCHEMA_CODE) return fail;

    AnyVal dir_av = tom->get(lir_arena_root::DIRECTORY);
    if (!dir_av.is_ref()) return fail;
    auto* dir = reinterpret_cast<const ObjectArray*>(dir_av.resolve());
    if (ref.oid >= dir->size()) return fail;

    AnyVal target = dir->get(ref.oid);
    if (!target.is_ref()) return fail;

    return ExternalRefResolved{mem, target.resolve()};
}

ExternalRefResolved resolve_external_ref_local(arena_id_t         source_arena,
                                               const ExternalRef& ref,
                                               ArenaPool&         pool) noexcept {
    arena_id_t global = pool.resolve_local_arena_id(source_arena, ref.aid);
    if (!global.is_valid()) return ExternalRefResolved{};
    return resolve_external_ref(ExternalRef{global, ref.oid}, pool);
}

} // namespace logos::hermes
