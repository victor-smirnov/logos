// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos
//
// HRPC Context — server-side handler context.
//
// Passed by reference to handler functions registered via EndpointRegistry.
// Provides access to the inbound request, streaming channels (push/pop),
// and call metadata.

#pragma once

#include <logos/hrpc/common.hpp>
#include <logos/hrpc/schema.hpp>
#include <logos/reactor/channel.hpp>

#include <memory>
#include <vector>

namespace logos::hrpc {

// Forward declaration — Context needs to call back into Session for push().
class Session;

// ---------------------------------------------------------------------------
// Context — server-side handle for one active RPC invocation.
//
// Non-copyable. Created and owned by Session internally; passed by reference
// to handler functions. The handler may block fibers via push()/pop().
// ---------------------------------------------------------------------------
class Context {
public:
    Context() = default;

    // Not copyable.
    Context(const Context&)            = delete;
    Context& operator=(const Context&) = delete;

    // Movable (needed for storage in ActiveContext before handler starts).
    Context(Context&&)            = default;
    Context& operator=(Context&&) = default;

    // --- Accessors ---

    const Request& request() const noexcept { return request_; }
    const EndpointID& endpoint_id() const noexcept { return endpoint_id_; }
    CallID call_id() const noexcept { return call_id_; }
    bool is_cancelled() const noexcept { return cancelled_; }

    // --- Streaming: input channels (server reads, client writes via wire) ---
    //
    // Block the current fiber until a StreamMessage arrives on input channel
    // 'code'. Returns false when a sentinel (null doc) is received, signalling
    // end-of-stream.
    bool pop(StreamMessage& msg, ChannelCode code = 0) {
        if (code >= input_channels_.size()) return false;
        msg = input_channels_[code]->recv();
        return !msg.doc.is_null();
    }

    // --- Streaming: output channels (server writes, client reads via wire) ---
    //
    // Send a message to the client. Immediately transmits a
    // ContextChannelMessage over the wire.
    logos::expected<void> push(StreamMessage msg, ChannelCode code = 0) noexcept;

private:
    friend class Session;
    friend class LocalServer;

    void set_request(Request rq) noexcept       { request_     = std::move(rq); }
    void set_endpoint_id(const EndpointID& id) noexcept { endpoint_id_ = id; }
    void set_call_id(CallID id) noexcept        { call_id_     = id; }
    void set_cancelled(bool c) noexcept         { cancelled_   = c; }
    void set_session(Session* s) noexcept       { session_     = s; }

    // input_channels[i]: server pops messages from these.
    // Filled by Session::handle_call_channel_msg().
    void set_input_channels(
        std::vector<std::unique_ptr<logos::reactor::Channel<StreamMessage>>> ch) noexcept
    {
        input_channels_ = std::move(ch);
    }

    logos::reactor::Channel<StreamMessage>* input_channel(ChannelCode code) noexcept {
        if (code >= input_channels_.size()) return nullptr;
        return input_channels_[code].get();
    }

    Request      request_;
    EndpointID   endpoint_id_{};
    CallID       call_id_     = 0;
    bool         cancelled_   = false;
    Session*     session_     = nullptr;

    // input_channels[i]: server receives from these (Session feeds them).
    std::vector<std::unique_ptr<logos::reactor::Channel<StreamMessage>>> input_channels_;
};

} // namespace logos::hrpc
