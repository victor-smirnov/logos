// Logos project — https://github.com/victor-smirnov/logos
//
// InMemoryArenaPool (Hermes2) — see arena_pool.hpp. Ported from the Hermes1
// implementation onto the hermes2 MemHolder + self-relative root AnyVal.

#include <logos/hermes2/arena_pool.hpp>
#include <logos/hermes2/document.hpp>        // DocumentHeader, doc_header
#include <logos/hermes2/lir_arena_root.hpp>  // SCHEMA_CODE, EXPORTS
#include <logos/hermes2/mem_holder.hpp>
#include <logos/hermes2/object_map.hpp>
#include <logos/hermes2/tiny_object_map.hpp>
#include <logos/verification/assert.hpp>

namespace logos::hermes2 {

InMemoryArenaPool::InMemoryArenaPool() {
    // Slot 0 = the INVALID_ARENA_ID sentinel. Real registrations start at slot 1.
    slots_.emplace_back();
}

InMemoryArenaPool::~InMemoryArenaPool() {
    for (auto& slot : slots_) {
        if (slot.mem) slot.mem->unref();
    }
}

ModuleHandle
InMemoryArenaPool::register_module(MemHolder*               mem,
                                    std::string              name,
                                    std::vector<std::string> dep_names)
{
    LOGOS_ASSERT(mem != nullptr, "ARENA-POOL-REG-001",
        "register_module: MemHolder must be non-null");
    LOGOS_ASSERT(by_name_.find(name) == by_name_.end(), "ARENA-POOL-REG-002",
        "register_module: module name '{}' already registered", name);
    LOGOS_ASSERT(slots_.size() <= MAX_ARENA_ID_VALUE, "ARENA-POOL-REG-003",
        "register_module: arena_id space exhausted ({} max)", MAX_ARENA_ID_VALUE);

    std::vector<arena_id_t> dep_ids;
    dep_ids.reserve(dep_names.size());
    for (auto& dn : dep_names) {
        auto it = by_name_.find(dn);
        LOGOS_ASSERT(it != by_name_.end(), "ARENA-POOL-REG-004",
            "register_module: dependency '{}' is not registered (required by '{}')", dn, name);
        dep_ids.push_back(it->second);
    }

    arena_id_t aid{static_cast<uint32_t>(slots_.size())};   // append-only
    mem->ref();                                             // pool's own +1

    slots_.push_back(Entry{mem, name, dep_ids, {}, {}, {}});
    by_name_.emplace(name, aid);

    return ModuleHandle{aid, std::move(name), std::move(dep_ids)};
}

void InMemoryArenaPool::unregister(arena_id_t aid) {
    LOGOS_ASSERT(aid.is_valid(), "ARENA-POOL-UNREG-001", "unregister: aid must be valid");
    LOGOS_ASSERT(aid.value < slots_.size(), "ARENA-POOL-UNREG-002",
        "unregister: arena_id {} out of range (max {})", aid.value, slots_.size() - 1);

    auto& slot = slots_[aid.value];
    LOGOS_ASSERT(slot.mem != nullptr, "ARENA-POOL-UNREG-003",
        "unregister: arena_id {} already unregistered", aid.value);

    by_name_.erase(slot.name);
    slot.mem->unref();
    slot.mem = nullptr;
    slot.name.clear();
    slot.deps.clear();
    // Slot kept; arena_id never reassigned (invariant #2).
}

MemHolder* InMemoryArenaPool::get(arena_id_t aid) noexcept {
    if (aid.value == 0 || aid.value >= slots_.size()) return nullptr;
    return slots_[aid.value].mem;
}

std::optional<ModuleHandle>
InMemoryArenaPool::find_by_name(std::string_view name) {
    auto it = by_name_.find(std::string(name));
    if (it == by_name_.end()) return std::nullopt;
    auto& slot = slots_[it->second.value];
    return ModuleHandle{it->second, slot.name, slot.deps};
}

ArenaPool::ExportLookup
InMemoryArenaPool::lookup_export(std::string_view name) noexcept {
    // Walk every live slot (skip slot 0 + unregistered). Each holder's root is the
    // LirArenaRoot TinyObjectMap (schema_type_code == SCHEMA_CODE) → EXPORTS map →
    // name → u24 obj_id Pod. First hit wins (mangled names are globally unique).
    for (size_t i = 1; i < slots_.size(); ++i) {
        auto& slot = slots_[i];
        if (!slot.mem) continue;

        AnyVal root = doc_header(slot.mem)->root;
        if (!root.is_ref()) continue;
        auto* tom = reinterpret_cast<const TinyObjectMap*>(root.resolve());
        if (tom->schema_type_code() != lir_arena_root::SCHEMA_CODE) continue;

        AnyVal exports_av = tom->get(lir_arena_root::EXPORTS);
        if (!exports_av.is_ref()) continue;
        auto* map = reinterpret_cast<const ObjectMap*>(exports_av.resolve());

        AnyVal hit = map->get(name);
        if (!hit.is_pod()) continue;
        return ExportLookup{arena_id_t{static_cast<uint32_t>(i)},
                            static_cast<uint32_t>(hit.as_i56())};
    }
    return ExportLookup{};  // INVALID, no hit
}

void InMemoryArenaPool::set_module_imports(arena_id_t               aid,
                                            std::string              file_name,
                                            std::vector<ImportEntry> imports)
{
    LOGOS_ASSERT(aid.value < slots_.size(), "ARENA-POOL-IMP-001",
        "set_module_imports: arena_id {} out of range", aid.value);
    auto& slot = slots_[aid.value];
    if (!file_name.empty()) by_file_.insert_or_assign(file_name, aid);
    slot.file_name = std::move(file_name);
    slot.imports   = std::move(imports);
    slot.import_map.assign(slot.imports.size(), INVALID_ARENA_ID);  // reset lazy cache
}

arena_id_t InMemoryArenaPool::find_arena_by_file(std::string_view file_name) noexcept {
    auto it = by_file_.find(std::string(file_name));
    return it == by_file_.end() ? INVALID_ARENA_ID : it->second;
}

arena_id_t InMemoryArenaPool::resolve_local_arena_id(arena_id_t source_aid,
                                                     arena_id_t local_aid) noexcept {
    if (source_aid.value == 0 || source_aid.value >= slots_.size())
        return INVALID_ARENA_ID;
    auto& slot = slots_[source_aid.value];
    if (local_aid.value == 0 || local_aid.value >= slot.imports.size())
        return INVALID_ARENA_ID;
    if (local_aid.value < slot.import_map.size()) {
        arena_id_t cached = slot.import_map[local_aid.value];
        if (cached.is_valid()) return cached;
    }
    const auto& entry = slot.imports[local_aid.value];
    if (entry.file_name.empty()) return INVALID_ARENA_ID;
    arena_id_t resolved = find_arena_by_file(entry.file_name);
    if (resolved.is_valid() && local_aid.value < slot.import_map.size())
        slot.import_map[local_aid.value] = resolved;
    return resolved;
}

ArenaPool& global_arena_pool() {
    static InMemoryArenaPool pool;
    return pool;
}

} // namespace logos::hermes2
