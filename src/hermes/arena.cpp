// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos

#include <logos/hermes/arena.hpp>
#include <logos/hermes/config.hpp>
#include <logos/verification/assert.hpp>

#include <algorithm>
#include <cstring>

namespace logos::hermes {

// --- Chunk ---

logos::expected<Chunk> Chunk::make(size_t cap) noexcept {
    auto* mem = new (std::nothrow) uint8_t[cap];
    if (!mem) [[unlikely]]
        return std::unexpected(logos::err(ErrCode::out_of_memory));

    std::memset(mem, 0, cap);

    Chunk c;
    c.memory.reset(mem);
    c.capacity = cap;
    c.used     = 0;
    return c;
}

// --- Arena ---

Arena::Arena(logos::InitTag& tag, ArenaMode mode, size_t initial_capacity) noexcept
    : mode_(mode)
{
    auto chunk_exp = Chunk::make(initial_capacity);
    if (!chunk_exp) [[unlikely]] {
        tag.fail(std::move(chunk_exp.error()));
        return;
    }

    chunks_.push_back(std::move(*chunk_exp));
}

logos::expected<Arena> Arena::make(ArenaMode mode, size_t initial_capacity) noexcept {
    logos::InitTag tag;
    Arena arena(tag, mode, initial_capacity);
    if (!tag.ok())
        return std::unexpected(std::move(tag.err));
    return arena;
}

void Arena::seal() noexcept {
    sealed_.store(true, std::memory_order_release);
}

logos::expected<void*>
Arena::allocate(size_t size, size_t alignment, TypeTag tag) noexcept {
    LOGOS_ASSERT(!is_sealed(), "HERMES-ARENA-002",
        "Arena::allocate() called on a sealed arena");
    LOGOS_ASSERT(alignment >= 2, "HERMES-ARENA-001",
        "Arena alignment must be >= 2 (got {}), required for TypeTag placement", alignment);
    LOGOS_ASSERT(size > 0, "HERMES-ARENA-001",
        "Arena allocation size must be > 0");

    size_t tag_bytes = tag.byte_length();

    uint8_t* addr = try_allocate_in_tail(size, alignment, tag_bytes);
    if (!addr) {
        auto res = grow(tag_bytes + size + alignment);
        if (!res) [[unlikely]]
            return std::unexpected(std::move(res.error()));
        addr = try_allocate_in_tail(size, alignment, tag_bytes);
        LOGOS_ASSERT(addr != nullptr, "HERMES-ARENA-001",
            "Arena allocation failed after grow for size={}, alignment={}", size, alignment);
    }

    // Write the type tag in the bytes before the object.
    tag.write_before(addr);

    // Advance the used pointer past the object.
    Chunk& chunk = tail();
    size_t offset_in_chunk = static_cast<size_t>(addr - chunk.data());
    chunk.used = offset_in_chunk + size;

    return addr;
}

logos::expected<void*>
Arena::allocate_raw(size_t size, size_t alignment) noexcept {
    LOGOS_ASSERT(!is_sealed(), "HERMES-ARENA-002",
        "Arena::allocate_raw() called on a sealed arena");
    uint8_t* addr = try_allocate_in_tail(size, alignment, 0);
    if (!addr) {
        auto res = grow(size + alignment);
        if (!res) [[unlikely]]
            return std::unexpected(std::move(res.error()));
        addr = try_allocate_in_tail(size, alignment, 0);
        LOGOS_ASSERT(addr != nullptr, "HERMES-ARENA-001",
            "Arena raw allocation failed after grow for size={}, alignment={}", size, alignment);
    }

    Chunk& chunk = tail();
    size_t offset_in_chunk = static_cast<size_t>(addr - chunk.data());
    chunk.used = offset_in_chunk + size;

    return addr;
}

size_t Arena::total_used() const noexcept {
    size_t total = 0;
    for (const auto& c : chunks_) {
        total += c.used;
    }
    return total;
}

uint8_t* Arena::try_allocate_in_tail(size_t size, size_t alignment,
                                     size_t tag_bytes) noexcept {
    Chunk& chunk = tail();
    uint8_t* base = chunk.data();
    size_t pos = chunk.used;

    // Find the next aligned address that also leaves room for the tag.
    // The tag occupies the bytes immediately before the aligned address,
    // so we need: aligned_pos - tag_bytes >= pos (previous allocation end).
    //
    // Strategy: align (pos + tag_bytes), which guarantees the gap.
    size_t candidate = pos + tag_bytes;
    size_t aligned   = (candidate + alignment - 1) & ~(alignment - 1);

    if (aligned + size > chunk.capacity) {
        return nullptr;
    }

    return base + aligned;
}

logos::expected<void> Arena::grow(size_t needed) noexcept {
    if (mode_ == ArenaMode::GrowableSingleChunk) {
        // Double the single chunk until it fits.
        Chunk& chunk = chunks_.front();
        size_t new_cap = chunk.capacity;
        while (new_cap - chunk.used < needed) {
            new_cap *= 2;
        }

        auto* new_mem = new (std::nothrow) uint8_t[new_cap];
        if (!new_mem) [[unlikely]]
            return std::unexpected(logos::err(ErrCode::out_of_memory));

        std::memset(new_mem, 0, new_cap);
        std::memcpy(new_mem, chunk.data(), chunk.used);

        chunk.memory.reset(new_mem);
        chunk.capacity = new_cap;
    } else {
        // MultiChunk: add a new chunk.
        size_t cap = std::max(needed, size_t{4096});

        auto* new_mem = new (std::nothrow) uint8_t[cap];
        if (!new_mem) [[unlikely]]
            return std::unexpected(logos::err(ErrCode::out_of_memory));

        std::memset(new_mem, 0, cap);

        Chunk chunk;
        chunk.memory.reset(new_mem);
        chunk.capacity = cap;
        chunk.used     = 0;

        chunks_.push_back(std::move(chunk));
    }

    return {};
}

} // namespace logos::hermes
