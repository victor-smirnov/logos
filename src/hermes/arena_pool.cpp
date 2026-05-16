// Logos project — https://github.com/victor-smirnov/logos
//
// InMemoryArenaPool implementation. See arena_pool.hpp + the design doc
// (docs/internals/multi-arena-ir.md §3.4) for invariants.

#include <logos/hermes/arena_pool.hpp>
#include <logos/hermes/mem_holder.hpp>
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

ArenaPool& global_arena_pool() {
    static InMemoryArenaPool pool;
    return pool;
}

} // namespace logos::hermes
