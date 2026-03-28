// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos

#include <logos/hermes/arena.hpp>
#include <logos/verification/assert.hpp>

#include <algorithm>
#include <cstring>

namespace logos::hermes {

// --- Chunk ---

Chunk::Chunk(size_t cap)
    : memory(std::make_unique<uint8_t[]>(cap))
    , capacity(cap)
    , used(0)
{
    // Zero-initialize for predictable behavior (null RelativePtrs, etc.)
    std::memset(memory.get(), 0, cap);
}

// --- Arena ---

Arena::Arena(ArenaMode mode, size_t initial_capacity)
    : mode_(mode)
{
    chunks_.emplace_back(initial_capacity);
}

void* Arena::allocate(size_t size, size_t alignment, TypeTag tag) {
    LOGOS_ASSERT(alignment >= 2, "HERMES-ARENA-001",
        "Arena alignment must be >= 2 (got {}), required for TypeTag placement", alignment);
    LOGOS_ASSERT(size > 0, "HERMES-ARENA-001",
        "Arena allocation size must be > 0");

    size_t tag_bytes = tag.byte_length();

    uint8_t* addr = try_allocate_in_tail(size, alignment, tag_bytes);
    if (!addr) {
        grow(tag_bytes + size + alignment);
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

void* Arena::allocate_raw(size_t size, size_t alignment) {
    uint8_t* addr = try_allocate_in_tail(size, alignment, 0);
    if (!addr) {
        grow(size + alignment);
        addr = try_allocate_in_tail(size, alignment, 0);
        LOGOS_ASSERT(addr != nullptr, "HERMES-ARENA-001",
            "Arena raw allocation failed after grow for size={}, alignment={}", size, alignment);
    }

    Chunk& chunk = tail();
    size_t offset_in_chunk = static_cast<size_t>(addr - chunk.data());
    chunk.used = offset_in_chunk + size;

    return addr;
}

size_t Arena::total_used() const {
    size_t total = 0;
    for (const auto& c : chunks_) {
        total += c.used;
    }
    return total;
}

uint8_t* Arena::try_allocate_in_tail(size_t size, size_t alignment, size_t tag_bytes) {
    Chunk& chunk = tail();
    uint8_t* base = chunk.data();
    size_t pos = chunk.used;

    // Find the next aligned address that also leaves room for the tag.
    // The tag occupies the bytes immediately before the aligned address,
    // so we need: aligned_pos - tag_bytes >= pos (previous allocation end).
    //
    // Strategy: align (pos + tag_bytes), which guarantees the gap.
    size_t candidate = pos + tag_bytes;
    size_t aligned = (candidate + alignment - 1) & ~(alignment - 1);

    // Verify there's enough room for the tag between the previous allocation and aligned addr.
    // This is guaranteed by construction since aligned >= pos + tag_bytes.

    if (aligned + size > chunk.capacity) {
        return nullptr;
    }

    return base + aligned;
}

void Arena::grow(size_t needed) {
    if (mode_ == ArenaMode::GrowableSingleChunk) {
        // Double the single chunk until it fits.
        Chunk& chunk = chunks_.front();
        size_t new_cap = chunk.capacity;
        while (new_cap - chunk.used < needed) {
            new_cap *= 2;
        }

        auto new_mem = std::make_unique<uint8_t[]>(new_cap);
        std::memset(new_mem.get(), 0, new_cap);
        std::memcpy(new_mem.get(), chunk.data(), chunk.used);

        chunk.memory = std::move(new_mem);
        chunk.capacity = new_cap;
    } else {
        // MultiChunk: add a new chunk.
        size_t cap = std::max(needed, size_t{4096});
        chunks_.emplace_back(cap);
    }
}

} // namespace logos::hermes
