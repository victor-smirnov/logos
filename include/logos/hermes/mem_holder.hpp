// Logos project — https://github.com/victor-smirnov/logos

#pragma once

#include <atomic>
#include <cstdint>
#include <new>
#include <logos/hermes/arena.hpp>
#include <logos/hermes/config.hpp>

namespace logos::hermes {

// MemHolder — reference-counted owner of a Hermes container's segment set (the
// `Rc<dyn Resident>` residency root of the design: while a holder lives, its
// version's immutable data is alive and its resolved views are valid). When the
// refcount hits zero the holder and its arena (all segments) are freed.
//
// Unlike Hermes1, Hermes VIEWS ARE OWNING (they carry a holder ref — see view.hpp),
// because without a borrow checker C++ cannot otherwise prove the holder outlives a
// view. So there is no separate non-owning view / Own<View> split: a view holds +1.
class MemHolder {
public:
    // Create a holder owning a fresh arena. Returns it with refcount 1 (the caller
    // owns that initial ref and must release it via unref()). Defaults to the
    // never-move MultiChunk arena.
    [[nodiscard]] static logos::expected<MemHolder*>
    make(size_t arena_capacity = 4096, ArenaMode mode = ArenaMode::MultiChunk) noexcept {
        LOGOS_TRY(auto arena, Arena::make(mode, arena_capacity));
        auto* h = new (std::nothrow) MemHolder(std::move(arena));
        if (!h) [[unlikely]] return std::unexpected(logos::err(ErrCode::out_of_memory));
        h->ref_count_.store(1, std::memory_order_relaxed);
        return h;
    }

    // Wrap a rigid single-segment blob (a compactify() dump) as a fresh holder
    // (refcount 1). Self-relative pointers in the blob resolve in place.
    [[nodiscard]] static logos::expected<MemHolder*>
    from_bytes(const void* data, size_t size) noexcept {
        LOGOS_TRY(auto arena, Arena::from_bytes(data, size));
        auto* h = new (std::nothrow) MemHolder(std::move(arena));
        if (!h) [[unlikely]] return std::unexpected(logos::err(ErrCode::out_of_memory));
        h->ref_count_.store(1, std::memory_order_relaxed);
        return h;
    }

    void ref() noexcept { ref_count_.fetch_add(1, std::memory_order_relaxed); }
    void unref() noexcept {
        if (ref_count_.fetch_sub(1, std::memory_order_acq_rel) == 1) delete this;
    }
    int32_t use_count() const noexcept { return ref_count_.load(std::memory_order_relaxed); }

    Arena&       arena()       noexcept { return arena_; }
    const Arena& arena() const noexcept { return arena_; }

    // The single-segment base (head chunk start) — Hermes1 spelling kept for the
    // mirror/TypePool handles, which address their GrowableSingleChunk by offset.
    uint8_t* base() const noexcept {
        return const_cast<uint8_t*>(arena_.head().data());
    }

private:
    std::atomic<int32_t> ref_count_{0};
    Arena                arena_;

    explicit MemHolder(Arena&& arena) noexcept : arena_(std::move(arena)) {}
    ~MemHolder() = default;   // only via unref()
};

} // namespace logos::hermes
