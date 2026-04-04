// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos

#pragma once

#include <cstdint>
#include <cstddef>
#include <memory>
#include <vector>

#include <logos/hermes/type_tag.hpp>
#include <logos/core/expected.hpp>   // includes err.hpp (InitTag, Err, logos::err)

namespace logos::hermes {

// How the arena manages its backing memory.
enum class ArenaMode {
    // Single contiguous buffer, doubled on overflow.
    // Used for immutable/compacted documents and zero-copy serialization.
    GrowableSingleChunk,

    // Multiple independently allocated chunks.
    // Used for mutable documents under construction.
    MultiChunk,
};

// A contiguous block of memory owned by an Arena.
struct Chunk {
    std::unique_ptr<uint8_t[]> memory;
    size_t capacity = 0;
    size_t used     = 0;

    Chunk() noexcept = default;
    Chunk(Chunk&&) noexcept = default;
    Chunk& operator=(Chunk&&) noexcept = default;

    // Allocate a zeroed chunk of the given capacity.
    // Returns an error on allocation failure (never throws).
    static logos::expected<Chunk> make(size_t cap) noexcept;

    uint8_t*       data()      noexcept { return memory.get(); }
    const uint8_t* data() const noexcept { return memory.get(); }
    size_t         available() const noexcept { return capacity - used; }
};

// Arena: bump-pointer allocator for Hermes objects.
//
// Objects are allocated sequentially within chunks. Each tagged object has its
// TypeTag written in the bytes immediately before the object's address. The
// allocator ensures proper alignment and leaves enough gap for the tag.
//
// Arena memory is fully relocatable: all internal references use RelativePtr,
// so segments can be mmap'd, serialized, or shared across processes.
class Arena {
public:
    // Default-constructed arena: empty/invalid — no chunks.
    // Safe to move into; must not attempt any allocation.
    Arena() noexcept = default;

    // Fallible constructor (InitTag protocol).
    // Allocates the initial chunk; signals failure via tag.fail() on OOM.
    Arena(logos::InitTag& tag, ArenaMode mode, size_t initial_capacity) noexcept;

    // Factory: returns an Arena or an OOM error.
    static logos::expected<Arena> make(ArenaMode mode, size_t initial_capacity) noexcept;

    // Non-copyable, movable.
    Arena(const Arena&)            = delete;
    Arena& operator=(const Arena&) = delete;
    Arena(Arena&&) noexcept        = default;
    Arena& operator=(Arena&&) noexcept = default;

    // Allocate space for a tagged object. The TypeTag is written before the
    // returned address. Returns a pointer to the object (not the tag).
    // Returns an error on OOM.
    [[nodiscard]] logos::expected<void*>
    allocate(size_t size, size_t alignment, TypeTag tag) noexcept;

    // Allocate raw memory without a type tag (for untagged structures like DocumentHeader).
    // Returns an error on OOM.
    [[nodiscard]] logos::expected<void*>
    allocate_raw(size_t size, size_t alignment) noexcept;

    // --- Accessors ---

    ArenaMode mode() const noexcept { return mode_; }

    // The first chunk (contains document header at offset 0).
    Chunk&       head()       noexcept { return chunks_.front(); }
    const Chunk& head() const noexcept { return chunks_.front(); }

    // The current allocation target (last chunk).
    Chunk&       tail()       noexcept { return chunks_.back(); }
    const Chunk& tail() const noexcept { return chunks_.back(); }

    size_t chunk_count() const noexcept { return chunks_.size(); }
    size_t total_used()  const noexcept;

private:
    ArenaMode           mode_ = ArenaMode::MultiChunk;
    std::vector<Chunk>  chunks_;

    // Find space in the current chunk for the given allocation.
    // Returns the aligned address, or nullptr if the chunk is full.
    uint8_t* try_allocate_in_tail(size_t size, size_t alignment,
                                  size_t tag_bytes) noexcept;

    // Grow the arena to fit `needed` bytes.
    // Returns an error on OOM.
    logos::expected<void> grow(size_t needed) noexcept;
};

} // namespace logos::hermes
