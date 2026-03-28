// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos

#pragma once

#include <cstdint>
#include <cstddef>
#include <memory>
#include <vector>

#include <logos/hermes/type_tag.hpp>

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
    size_t capacity;
    size_t used;

    Chunk(size_t cap);

    uint8_t* data() { return memory.get(); }
    const uint8_t* data() const { return memory.get(); }
    size_t available() const { return capacity - used; }
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
    explicit Arena(ArenaMode mode = ArenaMode::MultiChunk, size_t initial_capacity = 4096);

    // Non-copyable, movable.
    Arena(const Arena&) = delete;
    Arena& operator=(const Arena&) = delete;
    Arena(Arena&&) = default;
    Arena& operator=(Arena&&) = default;

    // Allocate space for a tagged object. The TypeTag is written before the
    // returned address. Returns a pointer to the object (not the tag).
    void* allocate(size_t size, size_t alignment, TypeTag tag);

    // Allocate raw memory without a type tag (for untagged structures like DocumentHeader).
    void* allocate_raw(size_t size, size_t alignment);

    // --- Accessors ---

    ArenaMode mode() const { return mode_; }

    // The first chunk (contains document header at offset 0).
    Chunk& head() { return chunks_.front(); }
    const Chunk& head() const { return chunks_.front(); }

    // The current allocation target (last chunk).
    Chunk& tail() { return chunks_.back(); }
    const Chunk& tail() const { return chunks_.back(); }

    size_t chunk_count() const { return chunks_.size(); }
    size_t total_used() const;

private:
    ArenaMode mode_;
    std::vector<Chunk> chunks_;

    // Find or create space in the current chunk for the given allocation.
    // Returns pointer to the aligned position, or nullptr if growth is needed.
    uint8_t* try_allocate_in_tail(size_t size, size_t alignment, size_t tag_bytes);

    // Grow the arena to fit the allocation.
    void grow(size_t needed);
};

} // namespace logos::hermes
