// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos
//
// HRPC EndpointRegistry — maps EndpointIDs to handler functions.

#pragma once

#include <logos/hrpc/common.hpp>
#include <logos/hrpc/schema.hpp>
#include <logos/hrpc/green_handler_fn.hpp>

#include <unordered_map>

namespace logos::hrpc {

// Handler function type: receives a Context reference, returns a Response.
// GreenHandlerFn (not std::function) so that handler bodies may call green
// (fiber-blocking) reactor primitives such as ctx.pop(), ctx.push(),
// session.call(), etc.
using HandlerFn = GreenHandlerFn;

// ---------------------------------------------------------------------------
// EndpointRegistry — thread-local (single-core fiber) registry mapping
// 256-bit EndpointIDs to handler functions.
// ---------------------------------------------------------------------------
class EndpointRegistry {
public:
    EndpointRegistry()  = default;
    ~EndpointRegistry() = default;

    // EndpointRegistry is not copyable (handlers are move-only-capable).
    EndpointRegistry(const EndpointRegistry&)            = delete;
    EndpointRegistry& operator=(const EndpointRegistry&) = delete;

    // Register a handler for the given endpoint ID.
    // Overwrites any previously registered handler for the same ID.
    [[nodiscard]] logos::expected<void> add(const EndpointID& id, HandlerFn handler) noexcept {
        handlers_[id] = std::move(handler);
        return {};
    }

    // Unregister the handler for the given endpoint ID (no-op if not found).
    void remove(const EndpointID& id) noexcept {
        handlers_.erase(id);
    }

    // Look up a handler. Returns nullptr if no handler is registered.
    HandlerFn* get(const EndpointID& id) noexcept {
        auto it = handlers_.find(id);
        if (it == handlers_.end()) return nullptr;
        return &it->second;
    }

private:
    std::unordered_map<EndpointID, HandlerFn, EndpointIDHash> handlers_;
};

} // namespace logos::hrpc
