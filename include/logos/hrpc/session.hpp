// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos
//
// HRPC Session — one TCP connection, full-duplex RPC + streaming.
//
// One Session wraps one TcpSocket. The session must be started (start()) and
// then run() must be called from a dedicated fiber to process incoming messages.

#pragma once

#include <logos/hrpc/common.hpp>
#include <logos/hrpc/schema.hpp>
#include <logos/hrpc/endpoint.hpp>
#include <logos/hrpc/context.hpp>
#include <logos/hrpc/call.hpp>
#include <logos/reactor/tcp_socket.hpp>
#include <logos/reactor/mutex.hpp>
#include <logos/reactor/condition_variable.hpp>

#include <memory>
#include <unordered_map>

namespace logos::hrpc {

// ---------------------------------------------------------------------------
// ActiveContext — server-side active call state (owned by Session).
//
// input_channels[i]:  client writes (CallChannelMessage) → server reads via
//                     Context::pop(). Session::handle_call_channel_msg() feeds
//                     these channels.
// No output_channels here: Context::push() sends directly over the wire.
// ---------------------------------------------------------------------------
struct ActiveContext {
    Context ctx;

    // raw_input_ptrs[i]: raw pointers into the channels owned by ctx.
    // Fed by Session::handle_call_channel_msg() when a CallChannelMessage arrives.
    // Context owns the underlying Channel objects (via unique_ptr).
    std::vector<logos::reactor::Channel<StreamMessage>*> raw_input_ptrs;
};

// ---------------------------------------------------------------------------
// Session
// ---------------------------------------------------------------------------
class Session {
public:
    // Construct a session from a connected (or accepted) socket.
    Session(logos::reactor::TcpSocket sock, SessionSide side);

    ~Session();

    Session(const Session&)            = delete;
    Session& operator=(const Session&) = delete;
    Session(Session&&)                 = delete;
    Session& operator=(Session&&)      = delete;

    // --- Configuration ---

    EndpointRegistry& endpoints() noexcept { return endpoints_; }

    // --- Session lifecycle ---

    // Start the session.  Client sends SESSION_START and waits for server ack.
    // Server waits for SESSION_START then sends its own.
    // Must be called from within a fiber.
    void start();

    // Run the inbound message loop.  Blocks the calling fiber until the session
    // is closed.  Should be called from a dedicated fiber.
    void run();

    // Close the session (sends SESSION_CLOSE, marks closed).
    void close();

    bool is_closed() const noexcept { return closed_; }

    // --- RPC: synchronous ---

    // Make a synchronous call.  Blocks the current fiber until the response
    // arrives.  Internally uses call_async() + wait().
    Response call(const EndpointID& endpoint, Request request,
                  uint16_t input_channels  = 0,
                  uint16_t output_channels = 0);

    // --- RPC: asynchronous ---

    // Submit a call and return immediately.  The caller must call call->wait()
    // to retrieve the response.
    std::shared_ptr<Call> call_async(const EndpointID& endpoint, Request request,
                                     uint16_t input_channels  = 0,
                                     uint16_t output_channels = 0);

    // --- Wire send helpers (used by Call::push and Context::push) ---

    // Send a channel message over the wire (acquires write_mutex_).
    void send_channel_message(MessageType type, CallID call_id,
                              ChannelCode code, StreamMessage msg);

private:
    // --- Internal message sending ---

    void send_raw(const std::vector<uint8_t>& buf);

    void send_message(MessageType type, CallID call_id,
                      const EndpointID* endpoint = nullptr,
                      ChannelCode ch_code        = 0,
                      const HermesCtr* payload   = nullptr);

    void send_return(CallID call_id, Response response);

    // --- Inbound message handling ---

    void handle_message(const MessageHeader& hdr,
                        const uint8_t* buf,
                        HermesCtr payload);

    void handle_session_start(const MessageHeader& hdr, HermesCtr payload);
    void handle_session_close();
    void handle_call(const MessageHeader& hdr, const uint8_t* buf, HermesCtr payload);
    void handle_return(const MessageHeader& hdr, HermesCtr payload);
    void handle_call_channel_msg(const MessageHeader& hdr, HermesCtr payload);
    void handle_ctx_channel_msg(const MessageHeader& hdr, HermesCtr payload);
    void handle_call_close_output(const MessageHeader& hdr);
    void handle_ctx_close_output(const MessageHeader& hdr);
    void handle_cancel(const MessageHeader& hdr);

    // --- State ---

    logos::reactor::TcpSocket sock_;
    SessionSide               side_;

    EndpointRegistry endpoints_;

    // Serializes all writes to sock_.
    logos::reactor::Mutex write_mutex_;

    // Pending client-side calls (keyed by call_id).
    std::unordered_map<CallID, std::shared_ptr<PendingCall>> pending_calls_;
    logos::reactor::Mutex pending_calls_mutex_;

    // Active server-side contexts (keyed by call_id).
    std::unordered_map<CallID, std::shared_ptr<ActiveContext>> contexts_;
    logos::reactor::Mutex contexts_mutex_;

    // SESSION_START negotiation.
    bool                              negotiated_ = false;
    logos::reactor::Mutex             ngt_mutex_;
    logos::reactor::ConditionVariable ngt_cv_;

    // Call ID counter. Client starts at 2 (even), server at 1 (odd).
    // Incremented by 2 per call to keep client/server IDs disjoint.
    CallID call_id_cnt_;

    bool closed_ = false;
};

} // namespace logos::hrpc
