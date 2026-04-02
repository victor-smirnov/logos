// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos
//
// HRPC EndpointRegistry — maps EndpointIDs to handler functions.

#pragma once

#include <logos/hrpc/common.hpp>
#include <logos/hrpc/schema.hpp>

#include <functional>
#include <unordered_map>

namespace logos::hrpc {

// Forward declaration.
class Context;

// Handler function type: receives a Context reference, returns a Response.
// std::function (not move_only_function) so it can be copied when spawning
// handler fibers.
using HandlerFn = std::function<Response(Context&)>;

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
    void add(const EndpointID& id, HandlerFn handler) {
        handlers_[id] = std::move(handler);
    }

    // Unregister the handler for the given endpoint ID (no-op if not found).
    void remove(const EndpointID& id) {
        handlers_.erase(id);
    }

    // Look up a handler. Returns nullptr if no handler is registered.
    HandlerFn* get(const EndpointID& id) {
        auto it = handlers_.find(id);
        if (it == handlers_.end()) return nullptr;
        return &it->second;
    }

private:
    std::unordered_map<EndpointID, HandlerFn, EndpointIDHash> handlers_;
};

} // namespace logos::hrpc
