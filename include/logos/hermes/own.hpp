// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos

#pragma once

#include <logos/hermes/config.hpp>
#include <logos/hermes/mem_holder.hpp>

namespace logos::hermes {

// Own<ViewT>: owning wrapper around a non-owning View.
//
// Inherits from ViewT (so it IS-A view), but manages the MemHolder refcount:
//   - Constructor: holder->ref()
//   - Destructor:  holder->unref()
//   - Copy:        new ref
//   - Move:        transfer, no ref change
//
// Usage:
//   TinyMapView view(offset, holder);  // non-owning, cheap
//   Own<TinyMapView> owned(view);      // owning, safe to store
//   owned.put(0, value);               // use like a view
//
// The View base class must provide:
//   - MemHolder* holder() const
//   - A constructor from (offset, MemHolder*)
template <typename ViewT>
class Own : public ViewT {
public:
    // Default: null.
    Own() noexcept : ViewT() {}

    // Take ownership of a non-owning view.
    explicit Own(const ViewT& view) noexcept : ViewT(view) {
        if (auto* h = this->holder()) h->ref();
    }

    // Construct from offset + holder (owning).
    Own(arena_offset_t offset, MemHolder* holder) noexcept
        : ViewT(offset, holder)
    {
        if (holder) holder->ref();
    }

    // Copy: new reference.
    Own(const Own& other) noexcept : ViewT(static_cast<const ViewT&>(other)) {
        if (auto* h = this->holder()) h->ref();
    }

    Own& operator=(const Own& other) noexcept {
        if (this != &other) {
            if (auto* h = this->holder()) h->unref();
            ViewT::operator=(static_cast<const ViewT&>(other));
            if (auto* h = this->holder()) h->ref();
        }
        return *this;
    }

    // Move: transfer ownership, no refcount change.
    Own(Own&& other) noexcept : ViewT(static_cast<const ViewT&>(other)) {
        other.reset();
    }

    Own& operator=(Own&& other) noexcept {
        if (this != &other) {
            if (auto* h = this->holder()) h->unref();
            ViewT::operator=(static_cast<const ViewT&>(other));
            other.reset();
        }
        return *this;
    }

    // Destructor: release reference.
    ~Own() noexcept {
        if (auto* h = this->holder()) h->unref();
    }

    // Check if this owns a valid view.
    explicit operator bool() const noexcept { return !this->is_null(); }
};

} // namespace logos::hermes
