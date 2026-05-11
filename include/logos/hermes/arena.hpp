// Logos project — https://github.com/victor-smirnov/logos

#pragma once

#include <atomic>
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

    Arena(Arena&& o) noexcept
        : mode_(o.mode_),
          sealed_(o.sealed_.load(std::memory_order_relaxed)),
          chunks_(std::move(o.chunks_)) {}

    Arena& operator=(Arena&& o) noexcept {
        mode_   = o.mode_;
        sealed_.store(o.sealed_.load(std::memory_order_relaxed),
                      std::memory_order_relaxed);
        chunks_ = std::move(o.chunks_);
        return *this;
    }

    // Allocate space for a tagged object. The TypeTag is written before the
    // returned address. Returns a pointer to the object (not the tag).
    // Returns an error on OOM.
    [[nodiscard]] logos::expected<void*>
    allocate(size_t size, size_t alignment, TypeTag tag) noexcept;

    // Allocate raw memory without a type tag (for untagged structures like DocumentHeader).
    // Returns an error on OOM.
    [[nodiscard]] logos::expected<void*>
    allocate_raw(size_t size, size_t alignment) noexcept;

    // --- Seal (immutable sharing) ---

    // Seal the arena: forbid further allocations.  After sealing, the arena
    // content is immutable and may be read concurrently from multiple threads
    // (reactors) via shared Own<View> references.
    //
    // Issues a release fence so that all prior writes are visible to any
    // thread that observes is_sealed() == true (acquire load).
    void seal() noexcept;

    // True after seal() has been called.  Acquire ordering ensures all arena
    // content written before seal() is visible to the caller.
    bool is_sealed() const noexcept {
        return sealed_.load(std::memory_order_acquire);
    }

    // --- Checkpoint / rollback (GrowableSingleChunk only) ---
    //
    // Save the current allocation watermark.  Passing the saved value to
    // rollback() reclaims all memory allocated since the checkpoint, zeroing
    // the freed region so future allocations receive zeroed memory.
    //
    // Used by generated PEG parsers to discard arena allocations made during
    // a failed parse alternative (backtracking).
    size_t checkpoint() const noexcept { return head().used; }

    // Restore the arena to a previously saved checkpoint.
    // Asserts GrowableSingleChunk mode and pos <= current used.
    void rollback(size_t pos) noexcept;

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
    ArenaMode              mode_ = ArenaMode::MultiChunk;
    std::atomic<bool>      sealed_{false};
    std::vector<Chunk>     chunks_;

    // Find space in the current chunk for the given allocation.
    // Returns the aligned address, or nullptr if the chunk is full.
    uint8_t* try_allocate_in_tail(size_t size, size_t alignment,
                                  size_t tag_bytes) noexcept;

    // Grow the arena to fit `needed` bytes.
    // Returns an error on OOM.
    logos::expected<void> grow(size_t needed) noexcept;
};

} // namespace logos::hermes
