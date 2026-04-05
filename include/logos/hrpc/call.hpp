// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos
//
// HRPC Call — client-side handle for a pending RPC invocation.
//
// Obtained from Session::call_async(). Caller blocks on wait() to get the
// response, or uses push()/pop() for streaming communication.

#pragma once

#include <logos/hrpc/common.hpp>
#include <logos/hrpc/schema.hpp>
#include <logos/reactor/channel.hpp>
#include <logos/reactor/mutex.hpp>
#include <logos/reactor/condition_variable.hpp>

#include <optional>
#include <vector>
#include <memory>

namespace logos::hrpc {

// Forward declaration — Call needs to call back into Session for push().
class Session;

// ---------------------------------------------------------------------------
// PendingCall — internal state shared between Session and Call.
//
// Lives behind a shared_ptr so Session and Call can both reference it safely
// even if one outlives the other.
// ---------------------------------------------------------------------------
struct PendingCall {
    // Set by Session when the Return message arrives.
    std::optional<Response> response;

    logos::reactor::Mutex             mutex;
    logos::reactor::ConditionVariable cv;

    // output_channels[i]: Context (server) sends → Call (client) receives.
    // These are filled by Session::handle_ctx_channel_msg().
    // Using unique_ptr because Channel is not movable/copyable.
    std::vector<std::unique_ptr<logos::reactor::Channel<StreamMessage>>> output_channels;

    // The call_id for this pending call (needed for push()).
    CallID call_id = 0;
};

// ---------------------------------------------------------------------------
// Call — client-side view of a pending RPC call.
//
// Not copyable. Share via shared_ptr<Call>.
// ---------------------------------------------------------------------------
class Call {
public:
    // session is a raw pointer — Call must not outlive Session.
    Call(std::shared_ptr<PendingCall> state, Session* session) noexcept
        : state_(std::move(state))
        , session_(session)
    {}

    Call(const Call&)            = delete;
    Call& operator=(const Call&) = delete;

    // Block the current fiber until the server sends a Return message.
    LOGOS_GREEN Response wait() {
        auto& s = *state_;
        s.mutex.lock();
        s.cv.wait(s.mutex, [&] { return s.response.has_value(); });
        Response rs = std::move(*s.response);
        s.mutex.unlock();
        return rs;
    }

    // Non-blocking check — true if a response has arrived.
    bool is_done() const noexcept {
        return state_->response.has_value();
    }

    // Send a message to the context (server) side on input_channel[code].
    // Immediately sends a CallChannelMessage over the wire.
    LOGOS_GREEN logos::expected<void> push(StreamMessage msg, ChannelCode code = 0) noexcept;

    // Block until a message arrives from the context (server) on output_channel[code].
    // Returns false when the channel is closed (sentinel empty doc received).
    LOGOS_GREEN bool pop(StreamMessage& msg, ChannelCode code = 0) {
        if (code >= state_->output_channels.size()) return false;
        msg = state_->output_channels[code]->recv();
        return !msg.doc.is_null();
    }

    std::shared_ptr<PendingCall> state() const noexcept { return state_; }
    Session* session() const noexcept { return session_; }

private:
    std::shared_ptr<PendingCall> state_;
    Session* session_;  // raw pointer, must not outlive Session
};

} // namespace logos::hrpc
