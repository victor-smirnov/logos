// Logos project — https://github.com/victor-smirnov/logos

#include <logos/hermes2/arena.hpp>
#include <logos/hermes2/config.hpp>
#include <logos/verification/assert.hpp>

#include <algorithm>
#include <cstring>

namespace logos::hermes2 {

// --- Chunk ---

logos::expected<Chunk> Chunk::make(size_t cap) noexcept {
    auto* mem = new (std::nothrow) uint8_t[cap];
    if (!mem) [[unlikely]]
        return std::unexpected(logos::err(ErrCode::out_of_memory));

    // NOT zeroed here — allocate()/allocate_raw() zero each [old_used, new_used)
    // region on demand. This keeps a large pre-sized chunk LAZY (only touched pages
    // commit), which lets the mirror/TypePool reserve a big single segment up front
    // so it never reallocs (a realloc would dangle the live container `this` ptrs).

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

logos::expected<Arena> Arena::from_bytes(const void* data, size_t size) noexcept {
    auto chunk_exp = Chunk::make(size ? size : 1);
    if (!chunk_exp) [[unlikely]]
        return std::unexpected(std::move(chunk_exp.error()));
    std::memcpy(chunk_exp->data(), data, size);
    chunk_exp->used = size;

    Arena arena;
    arena.mode_ = ArenaMode::GrowableSingleChunk;
    arena.chunks_.push_back(std::move(*chunk_exp));
    return arena;
}

void Arena::rollback(size_t pos) noexcept {
    LOGOS_ASSERT(mode_ == ArenaMode::GrowableSingleChunk, "HERMES-ARENA-004",
        "Arena::rollback requires GrowableSingleChunk mode");
    LOGOS_ASSERT(pos <= head().used, "HERMES-ARENA-004",
        "Arena::rollback: pos {} > used {}", pos, head().used);
    std::memset(head().data() + pos, 0, head().used - pos);
    head().used = pos;
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

    // Advance the used pointer past the object.
    Chunk& chunk = tail();
    size_t offset_in_chunk = static_cast<size_t>(addr - chunk.data());
    size_t end = offset_in_chunk + size;
    // Zero the freshly-claimed region (alignment padding + tag gap + object), then
    // write the tag (after the memset, which would clobber it). Chunk memory is no
    // longer pre-zeroed — this keeps a large pre-sized chunk lazy.
    std::memset(chunk.data() + chunk.used, 0, end - chunk.used);
    tag.write_before(addr);
    chunk.used = end;

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
    size_t end = offset_in_chunk + size;
    std::memset(chunk.data() + chunk.used, 0, end - chunk.used);   // lazy zero (see allocate)
    chunk.used = end;

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

        // Not zeroed — per-allocation memset handles [old_used, new_used). Only the
        // copied live prefix matters; the tail commits lazily as it's allocated into.
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

} // namespace logos::hermes2
