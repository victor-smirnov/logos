// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos

#pragma once

#include <atomic>
#include <cstdint>
#include <logos/hermes/arena.hpp>
#include <logos/core/make_object.hpp>

namespace logos::hermes {

// MemHolder: reference-counted owner of an arena segment.
//
// Views store a raw MemHolder* (non-owning, cheap).
// Own<View> increments/decrements the refcount (owning, safe).
//
// When refcount drops to zero, the MemHolder and its arena are destroyed.
class MemHolder {
public:
    // Fallible constructor (InitTag protocol).
    // Creates the underlying Arena; signals failure via tag.fail() on OOM.
    MemHolder(logos::InitTag& tag, size_t arena_capacity,
              ArenaMode mode) noexcept
        : ref_count_(0)
    {
        auto arena_exp = Arena::make(mode, arena_capacity);
        if (!arena_exp) [[unlikely]] {
            tag.fail(std::move(arena_exp.error()));
            return;
        }
        arena_ = std::move(*arena_exp);
    }

    // --- Reference counting ---

    void ref() noexcept {
        ref_count_.fetch_add(1, std::memory_order_relaxed);
    }

    void unref() noexcept {
        if (ref_count_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            delete this;
        }
    }

    int32_t use_count() const noexcept {
        return ref_count_.load(std::memory_order_relaxed);
    }

    // --- Arena access ---

    Arena&       arena()       { return arena_; }
    const Arena& arena() const { return arena_; }

    uint8_t*       base()       { return arena_.head().data(); }
    const uint8_t* base() const { return arena_.head().data(); }

private:
    std::atomic<int32_t> ref_count_;
    Arena                arena_;

    ~MemHolder() = default; // Only destroyed via unref().
};

} // namespace logos::hermes
