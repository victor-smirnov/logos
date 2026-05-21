// Logos project — https://github.com/victor-smirnov/logos
//
// InMemoryArenaPool implementation. See arena_pool.hpp + the design doc
// (docs/internals/multi-arena-ir.md §3.4) for invariants.

#include <logos/hermes/arena_pool.hpp>
#include <logos/hermes/any_val.hpp>
#include <logos/hermes/document.hpp>     // DocumentHeader
#include <logos/hermes/lir_arena_root.hpp>
#include <logos/hermes/mem_holder.hpp>
#include <logos/hermes/object_map.hpp>
#include <logos/hermes/tiny_object_map.hpp>
#include <logos/hermes/type_registry.hpp>
#include <logos/verification/assert.hpp>

namespace logos::hermes {

InMemoryArenaPool::InMemoryArenaPool() {
    // Reserve slot 0 as the INVALID_ARENA_ID sentinel. Real registrations
    // start at slot 1, matching arena_id_t::is_valid() (value != 0).
    slots_.emplace_back();
}

InMemoryArenaPool::~InMemoryArenaPool() {
    // Drop pool-held refs on every live arena. Caller-held refs (if any)
    // keep MemHolders alive past pool destruction.
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

    // Resolve dep names → arena_ids. Every dep must already be registered
    // (per invariant #9 — DAG enforced at registration order).
    std::vector<arena_id_t> dep_ids;
    dep_ids.reserve(dep_names.size());
    for (auto& dn : dep_names) {
        auto it = by_name_.find(dn);
        LOGOS_ASSERT(it != by_name_.end(), "ARENA-POOL-REG-004",
            "register_module: dependency '{}' is not registered "
            "(required by '{}')", dn, name);
        dep_ids.push_back(it->second);
    }

    // Assign new arena_id (append-only per invariant #2).
    arena_id_t aid{static_cast<uint32_t>(slots_.size())};

    // Pool takes its own ref on mem; caller is free to drop theirs.
    mem->ref();

    slots_.push_back(Entry{mem, name, dep_ids});
    by_name_.emplace(name, aid);

    return ModuleHandle{aid, std::move(name), std::move(dep_ids)};
}

void InMemoryArenaPool::unregister(arena_id_t aid) {
    LOGOS_ASSERT(aid.is_valid(), "ARENA-POOL-UNREG-001",
        "unregister: aid must be valid");
    LOGOS_ASSERT(aid.value < slots_.size(), "ARENA-POOL-UNREG-002",
        "unregister: arena_id {} out of range (max {})",
        aid.value, slots_.size() - 1);

    auto& slot = slots_[aid.value];
    LOGOS_ASSERT(slot.mem != nullptr, "ARENA-POOL-UNREG-003",
        "unregister: arena_id {} already unregistered", aid.value);

    by_name_.erase(slot.name);
    slot.mem->unref();    // pool's ref dropped; caller's ref (if any) keeps it alive
    slot.mem = nullptr;
    slot.name.clear();
    slot.deps.clear();
    // Slot left in vector — arena_id stays invalid for future lookups but
    // never reassigned (per invariant #2).
}

MemHolder* InMemoryArenaPool::get(arena_id_t aid) noexcept {
    if (aid.value == 0 || aid.value >= slots_.size()) return nullptr;
    return slots_[aid.value].mem;
}

std::optional<ModuleHandle>
InMemoryArenaPool::find_by_name(std::string_view name) {
    // std::unordered_map::find without heterogeneous lookup needs a string.
    // Cold path; allocation acceptable.
    auto it = by_name_.find(std::string(name));
    if (it == by_name_.end()) return std::nullopt;
    auto& slot = slots_[it->second.value];
    return ModuleHandle{it->second, slot.name, slot.deps};
}

ArenaPool::ExportLookup
InMemoryArenaPool::lookup_export(std::string_view name) noexcept {
    // Walk every live slot (skip slot 0 = sentinel and unregistered slots).
    // Each slot's MemHolder's DocumentHeader.root_offset → LirArenaRoot →
    // EXPORTS map (string-keyed). First hit wins. Names are mangled fn
    // names — globally unique within a build by construction (package
    // prefix), so first-hit is also unique-hit in practice.
    for (size_t i = 1; i < slots_.size(); ++i) {
        auto& slot = slots_[i];
        if (!slot.mem) continue;
        auto* base = slot.mem->base();
        auto* hdr = reinterpret_cast<const DocumentHeader*>(base);
        if (hdr->root_offset == NULL_OFFSET) continue;

        auto* root_tom = reinterpret_cast<const TinyObjectMap*>(
            base + hdr->root_offset.value());
        if (root_tom->schema_type_code() != type_hash::LirArenaRoot) continue;

        AnyVal exports_av = root_tom->get(lir_arena_root::EXPORTS.code, base);
        if (exports_av.is_null() || !exports_av.is_pointer()) continue;

        auto* map = reinterpret_cast<const ObjectMap*>(
            base + exports_av.to_offset().value());
        AnyVal hit = map->get(name, const_cast<uint8_t*>(base));
        if (hit.is_null() || !hit.is_value()) continue;

        uint32_t oid = hit.as_value<uint32_t>();
        return ExportLookup{arena_id_t{static_cast<uint32_t>(i)}, oid};
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
}

arena_id_t InMemoryArenaPool::find_arena_by_file(std::string_view file_name) noexcept {
    auto it = by_file_.find(std::string(file_name));
    return it == by_file_.end() ? INVALID_ARENA_ID : it->second;
}

arena_id_t InMemoryArenaPool::resolve_local_arena_id(arena_id_t source_aid,
                                                     arena_id_t local_aid) noexcept {
    if (source_aid.value == 0 || source_aid.value >= slots_.size())
        return INVALID_ARENA_ID;
    const auto& slot = slots_[source_aid.value];
    if (local_aid.value == 0 || local_aid.value >= slot.imports.size())
        return INVALID_ARENA_ID;
    const auto& entry = slot.imports[local_aid.value];
    if (entry.file_name.empty()) return INVALID_ARENA_ID;
    return find_arena_by_file(entry.file_name);
}

ArenaPool& global_arena_pool() {
    static InMemoryArenaPool pool;
    return pool;
}

} // namespace logos::hermes
