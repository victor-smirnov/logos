// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
//
// HRPC Session implementation.
//
// Wire framing:
//   1. Read 4 bytes → message_size (total wire bytes for this message).
//   2. Allocate message_size bytes; copy first 4 bytes (message_size).
//   3. Read remaining (message_size - 4) bytes into the buffer.
//   4. Parse MessageHeader from buffer start.
//   5. If payload_size > 0, binary_decode the payload bytes.
//
// Streaming design:
//   - Call::push()    → sends CallChannelMessage over the wire immediately.
//   - Context::push() → sends ContextChannelMessage over the wire immediately.
//   - Context::pop()  → blocks on a local Channel fed by handle_call_channel_msg().
//   - Call::pop()     → blocks on a local Channel fed by handle_ctx_channel_msg().
//
// This avoids needing an extra "forward from local channel to wire" fiber.

#include <logos/hrpc/session.hpp>
#include <logos/hermes/binary_codec.hpp>
#include <logos/reactor/scheduler.hpp>
#include <logos/verification/assert.hpp>

#include <cstring>

namespace logos::hrpc {

using logos::hermes::binary_encode;
using logos::hermes::binary_decode;
using logos::reactor::Scheduler;

// ---------------------------------------------------------------------------
// Local helper: read exactly 'size' bytes from sock.
// Returns false on connection close or error.
// ---------------------------------------------------------------------------
LOGOS_GREEN static bool read_exact(logos::reactor::TcpSocket& sock, void* buf, size_t size) noexcept {
    uint8_t* ptr = static_cast<uint8_t*>(buf);
    size_t remaining = size;
    while (remaining > 0) {
        auto res = sock.read(ptr, remaining);
        if (!res || *res == 0) return false;
        ptr       += static_cast<size_t>(*res);
        remaining -= static_cast<size_t>(*res);
    }
    return true;
}

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------

Session::Session(logos::reactor::TcpSocket sock, SessionSide side)
    : sock_(std::move(sock))
    , side_(side)
    , call_id_cnt_(side == SessionSide::Client ? 2u : 1u)
{}

Session::~Session() {
    closed_ = true;
}

// ---------------------------------------------------------------------------
// start() — session negotiation
// ---------------------------------------------------------------------------

LOGOS_GREEN logos::expected<void> Session::start() noexcept {
    if (side_ == SessionSide::Client) {
        // Send SESSION_START to server, then block until run() receives the ack.
        // The run() fiber must already be spawned before calling start().
        LOGOS_TRY(auto meta, ConnectionMetadata::make(1024 * 1024));
        LOGOS_TRY_VOID(send_message(MessageType::SessionStart, 0, nullptr, 0, &meta.doc));

        ngt_mutex_.lock();
        ngt_cv_.wait(ngt_mutex_, [this] { return negotiated_; });
        ngt_mutex_.unlock();
    }
    // Server side: no-op. Negotiation is handled by run() when it receives
    // the client's SESSION_START and sends back the ack automatically.
    // Endpoints should be registered before calling run().
    return {};
}

// ---------------------------------------------------------------------------
// run() — inbound message loop
// ---------------------------------------------------------------------------

LOGOS_GREEN void Session::run() noexcept {
    while (!closed_) {
        // Step 1: read first 4 bytes (message_size field).
        uint32_t msg_size = 0;
        if (!read_exact(sock_, &msg_size, 4)) {
            break;
        }

        if (msg_size < MessageHeader::kBaseSize) {
            break;
        }

        // Step 2: allocate full message buffer and copy message_size into it.
        std::vector<uint8_t> buf(msg_size);
        std::memcpy(buf.data(), &msg_size, 4);

        // Step 3: read remaining bytes.
        size_t remaining = static_cast<size_t>(msg_size) - 4u;
        if (remaining > 0) {
            if (!read_exact(sock_, buf.data() + 4, remaining)) {
                break;
            }
        }

        // Step 4: parse header.
        const MessageHeader* hdr =
            reinterpret_cast<const MessageHeader*>(buf.data());

        // Step 5: decode payload if present.
        HermesCtr payload;
        size_t hdr_size     = hdr->header_size();
        size_t payload_size = hdr->payload_size();
        if (payload_size > 0 && hdr_size + payload_size <= static_cast<size_t>(msg_size)) {
            auto dec = binary_decode(buf.data() + hdr_size, payload_size);
            if (!dec) break;
            payload = std::move(*dec);
        }

        if (!handle_message(*hdr, buf.data(), std::move(payload))) break;
    }

    closed_ = true;

    // Wake any fibers waiting for negotiation (prevent hang on error).
    {
        logos::reactor::LockGuard lock(ngt_mutex_);
        negotiated_ = true;
        ngt_cv_.notify_all();
    }

    // Wake any fibers waiting for responses (deliver error responses).
    {
        std::lock_guard lock(pending_calls_mutex_);
        for (auto& kv : pending_calls_) {
            auto& pc = kv.second;
            logos::reactor::LockGuard pc_lock(pc->mutex);
            if (!pc->response.has_value()) {
                auto err_resp = Response::error("Session closed");
                pc->response = err_resp ? std::move(*err_resp) : Response{};
                pc->cv.notify_one();
            }
        }
    }
}

// ---------------------------------------------------------------------------
// close()
// ---------------------------------------------------------------------------

LOGOS_GREEN void Session::close() noexcept {
    if (closed_) return;
    closed_ = true;
    send_message(MessageType::SessionClose, 0);  // best-effort, ignore error
    sock_.close();
}

// ---------------------------------------------------------------------------
// call() and call_async()
// ---------------------------------------------------------------------------

logos::expected<Response> Session::call(const EndpointID& endpoint, Request request,
                                         uint16_t input_channels,
                                         uint16_t output_channels) noexcept {
    LOGOS_TRY(auto pending, call_async(endpoint, std::move(request),
                                       input_channels, output_channels));
    return pending->wait();
}

logos::expected<std::shared_ptr<Call>> Session::call_async(
    const EndpointID& endpoint,
    Request           request,
    uint16_t          input_channels,
    uint16_t          output_channels) noexcept {

    // Allocate call ID.
    CallID call_id = call_id_cnt_;
    call_id_cnt_ += 2;

    // Set channel counts in the request.
    if (input_channels > 0) {
        LOGOS_TRY_VOID(request.set_input_channels(input_channels));
    }
    if (output_channels > 0) {
        LOGOS_TRY_VOID(request.set_output_channels(output_channels));
    }

    // Build PendingCall state.
    auto pending = std::make_shared<PendingCall>();
    pending->call_id = call_id;

    // output_channels: Context (server) sends → Call (client) receives.
    // These channels are fed by handle_ctx_channel_msg().
    pending->output_channels.reserve(output_channels);
    for (uint16_t i = 0; i < output_channels; ++i) {
        pending->output_channels.push_back(
            std::make_unique<logos::reactor::Channel<StreamMessage>>(0));
    }

    // Register in pending map before sending (avoid race with fast response).
    {
        std::lock_guard lock(pending_calls_mutex_);
        pending_calls_[call_id] = pending;
    }

    // Send the CALL message.
    LOGOS_TRY_VOID(send_message(MessageType::Call, call_id, &endpoint, 0, &request.doc));

    return std::make_shared<Call>(std::move(pending), this);
}

// ---------------------------------------------------------------------------
// send_raw() — write a fully formed buffer under write_mutex_
// ---------------------------------------------------------------------------

LOGOS_GREEN logos::expected<void> Session::send_raw(const std::vector<uint8_t>& buf) noexcept {
    std::lock_guard lock(write_mutex_);
    return sock_.write_all(buf.data(), buf.size());
}

// ---------------------------------------------------------------------------
// send_message() — build a wire message and send it
// ---------------------------------------------------------------------------

LOGOS_GREEN logos::expected<void> Session::send_message(MessageType type, CallID call_id,
                                             const EndpointID* endpoint,
                                             ChannelCode ch_code,
                                             const HermesCtr* payload) noexcept {
    // Encode payload bytes.
    std::vector<uint8_t> payload_bytes;
    if (payload && !payload->is_null()) {
        LOGOS_TRY(payload_bytes, binary_encode(*payload));
    }

    // Compute sizes.
    size_t hdr_size = MessageHeader::kBaseSize + (endpoint ? 32u : 0u);
    size_t total    = hdr_size + payload_bytes.size();

    std::vector<uint8_t> msg_buf(total, 0);
    MessageHeader* hdr = reinterpret_cast<MessageHeader*>(msg_buf.data());
    hdr->message_size  = static_cast<uint32_t>(total);
    hdr->bits          = 0;
    hdr->call_id       = call_id;

    hdr->set_type(type);
    hdr->set_channel_code(ch_code);

    if (endpoint) {
        hdr->set_optionals(MessageHeader::kOptEndpointId);
        hdr->set_endpoint_id(msg_buf.data() + MessageHeader::kBaseSize, *endpoint);
    }

    if (!payload_bytes.empty()) {
        std::memcpy(msg_buf.data() + hdr_size,
                    payload_bytes.data(), payload_bytes.size());
    }

    return send_raw(msg_buf);
}

// ---------------------------------------------------------------------------
// send_return() — send a RETURN message with a Response payload
// ---------------------------------------------------------------------------

LOGOS_GREEN logos::expected<void> Session::send_return(CallID call_id, Response response) noexcept {
    return send_message(MessageType::Return, call_id, nullptr, 0, &response.doc);
}

// ---------------------------------------------------------------------------
// send_channel_message() — public, used by Call::push() and Context::push()
// ---------------------------------------------------------------------------

LOGOS_GREEN logos::expected<void> Session::send_channel_message(MessageType type, CallID call_id,
                                                     ChannelCode code,
                                                     StreamMessage msg) noexcept {
    if (msg.doc.is_null()) {
        // Null doc = end-of-stream sentinel: send close-output message instead.
        MessageType close_type = (type == MessageType::CallChannelMessage)
            ? MessageType::CallCloseOutput
            : MessageType::ContextCloseOutput;
        return send_message(close_type, call_id, nullptr, code, nullptr);
    } else {
        return send_message(type, call_id, nullptr, code, &msg.doc);
    }
}

// ---------------------------------------------------------------------------
// handle_message() — dispatch inbound messages
// ---------------------------------------------------------------------------

LOGOS_GREEN logos::expected<void> Session::handle_message(const MessageHeader& hdr,
                                               const uint8_t* buf,
                                               HermesCtr payload) noexcept {
    switch (hdr.type()) {
        case MessageType::SessionStart:
            return handle_session_start(hdr, std::move(payload));
        case MessageType::SessionClose:
            handle_session_close();
            break;
        case MessageType::Call:
            return handle_call(hdr, buf, std::move(payload));
        case MessageType::Return:
            handle_return(hdr, std::move(payload));
            break;
        case MessageType::CallChannelMessage:
            handle_call_channel_msg(hdr, std::move(payload));
            break;
        case MessageType::ContextChannelMessage:
            handle_ctx_channel_msg(hdr, std::move(payload));
            break;
        case MessageType::CallCloseOutput:
            handle_call_close_output(hdr);
            break;
        case MessageType::ContextCloseOutput:
            handle_ctx_close_output(hdr);
            break;
        case MessageType::CancelCall:
            handle_cancel(hdr);
            break;
        default:
            break;
    }
    return {};
}

// ---------------------------------------------------------------------------
// handle_session_start()
// ---------------------------------------------------------------------------

LOGOS_GREEN logos::expected<void> Session::handle_session_start(const MessageHeader& /*hdr*/,
                                                     HermesCtr /*payload*/) noexcept {
    if (side_ == SessionSide::Server) {
        // Acknowledge: send our own SESSION_START.
        LOGOS_TRY(auto meta, ConnectionMetadata::make(1024 * 1024));
        LOGOS_TRY_VOID(send_message(MessageType::SessionStart, 0, nullptr, 0, &meta.doc));

        logos::reactor::LockGuard lock(ngt_mutex_);
        negotiated_ = true;
        ngt_cv_.notify_all();
    } else {
        // Client received server's acknowledgment.
        logos::reactor::LockGuard lock(ngt_mutex_);
        negotiated_ = true;
        ngt_cv_.notify_all();
    }
    return {};
}

// ---------------------------------------------------------------------------
// handle_session_close()
// ---------------------------------------------------------------------------

void Session::handle_session_close() noexcept {
    closed_ = true;
    sock_.close();
}

// ---------------------------------------------------------------------------
// handle_call() — server side: dispatch inbound call to a handler fiber
// ---------------------------------------------------------------------------

LOGOS_GREEN logos::expected<void> Session::handle_call(const MessageHeader& hdr,
                                            const uint8_t* buf,
                                            HermesCtr payload) noexcept {
    // Extract endpoint ID from the optional field.
    EndpointID endpoint_id{};
    if (hdr.has_endpoint_id()) {
        endpoint_id = hdr.endpoint_id(buf + MessageHeader::kBaseSize);
    }

    HandlerFn* handler_fn_ptr = endpoints_.get(endpoint_id);
    if (!handler_fn_ptr) {
        LOGOS_TRY(auto err_resp, Response::error("Unknown endpoint"));
        return send_return(hdr.call_id, std::move(err_resp));
    }

    // Copy the handler (std::function is copyable).
    HandlerFn handler_fn = *handler_fn_ptr;

    Request request = Request::from_doc(std::move(payload));

    uint16_t in_count = request.input_channels();

    // Build ActiveContext.
    auto actx = std::make_shared<ActiveContext>();

    // Create input channels and collect raw pointers before moving ownership
    // into Context. Context owns the Channel objects (unique_ptr); ActiveContext
    // keeps raw pointers so Session can feed them when CallChannelMessage arrives.
    std::vector<std::unique_ptr<logos::reactor::Channel<StreamMessage>>> owned_channels;
    owned_channels.reserve(in_count);
    actx->raw_input_ptrs.reserve(in_count);
    for (uint16_t i = 0; i < in_count; ++i) {
        auto ch = std::make_unique<logos::reactor::Channel<StreamMessage>>(0);
        actx->raw_input_ptrs.push_back(ch.get());
        owned_channels.push_back(std::move(ch));
    }

    // Set up Context.
    actx->ctx.set_session(this);
    actx->ctx.set_request(std::move(request));
    actx->ctx.set_endpoint_id(endpoint_id);
    actx->ctx.set_call_id(hdr.call_id);

    // Transfer channel ownership into Context.
    actx->ctx.set_input_channels(std::move(owned_channels));

    // Register context.
    {
        std::lock_guard lock(contexts_mutex_);
        contexts_[hdr.call_id] = actx;
    }

    CallID call_id = hdr.call_id;

    // Spawn handler fiber.
    Scheduler::current()->spawn(
        [this, handler_fn = std::move(handler_fn), actx, call_id]() mutable LOGOS_FIBER_FN {
            Response rs = handler_fn(actx->ctx);
            send_return(call_id, std::move(rs));  // best-effort, connection dies if this fails

            std::lock_guard lock(contexts_mutex_);
            contexts_.erase(call_id);
        },
        "hrpc-handler"
    );

    return {};
}

// ---------------------------------------------------------------------------
// handle_return() — client side: deliver response to waiting Call
// ---------------------------------------------------------------------------

LOGOS_GREEN void Session::handle_return(const MessageHeader& hdr, HermesCtr payload) noexcept {
    std::shared_ptr<PendingCall> pc;
    {
        std::lock_guard lock(pending_calls_mutex_);
        auto it = pending_calls_.find(hdr.call_id);
        if (it == pending_calls_.end()) return;
        pc = it->second;
        pending_calls_.erase(it);
    }

    Response rs = Response::from_doc(std::move(payload));

    logos::reactor::LockGuard pc_lock(pc->mutex);
    pc->response = std::move(rs);
    pc->cv.notify_one();
}

// ---------------------------------------------------------------------------
// handle_call_channel_msg() — client pushed a message → feed context channel
// ---------------------------------------------------------------------------

LOGOS_GREEN void Session::handle_call_channel_msg(const MessageHeader& hdr, HermesCtr payload) noexcept {
    std::shared_ptr<ActiveContext> actx;
    {
        std::lock_guard lock(contexts_mutex_);
        auto it = contexts_.find(hdr.call_id);
        if (it == contexts_.end()) return;
        actx = it->second;
    }

    ChannelCode code = hdr.channel_code();
    if (code < actx->raw_input_ptrs.size()) {
        StreamMessage msg = StreamMessage::from_doc(std::move(payload));
        actx->raw_input_ptrs[code]->send(std::move(msg));
    }
}

// ---------------------------------------------------------------------------
// handle_ctx_channel_msg() — context pushed a message → feed call channel
// ---------------------------------------------------------------------------

LOGOS_GREEN void Session::handle_ctx_channel_msg(const MessageHeader& hdr, HermesCtr payload) noexcept {
    std::shared_ptr<PendingCall> pc;
    {
        std::lock_guard lock(pending_calls_mutex_);
        auto it = pending_calls_.find(hdr.call_id);
        if (it == pending_calls_.end()) return;
        pc = it->second;
    }

    ChannelCode code = hdr.channel_code();
    if (code < pc->output_channels.size()) {
        StreamMessage msg = StreamMessage::from_doc(std::move(payload));
        pc->output_channels[code]->send(std::move(msg));
    }
}

// ---------------------------------------------------------------------------
// handle_call_close_output() — call side closed its output → sentinel to ctx
// ---------------------------------------------------------------------------

LOGOS_GREEN void Session::handle_call_close_output(const MessageHeader& hdr) noexcept {
    std::shared_ptr<ActiveContext> actx;
    {
        std::lock_guard lock(contexts_mutex_);
        auto it = contexts_.find(hdr.call_id);
        if (it == contexts_.end()) return;
        actx = it->second;
    }

    ChannelCode code = hdr.channel_code();
    if (code < actx->raw_input_ptrs.size()) {
        // Null-doc sentinel signals end-of-stream to Context::pop().
        actx->raw_input_ptrs[code]->send(StreamMessage{});
    }
}

// ---------------------------------------------------------------------------
// handle_ctx_close_output() — context closed its output → sentinel to call
// ---------------------------------------------------------------------------

LOGOS_GREEN void Session::handle_ctx_close_output(const MessageHeader& hdr) noexcept {
    std::shared_ptr<PendingCall> pc;
    {
        std::lock_guard lock(pending_calls_mutex_);
        auto it = pending_calls_.find(hdr.call_id);
        if (it == pending_calls_.end()) return;
        pc = it->second;
    }

    ChannelCode code = hdr.channel_code();
    if (code < pc->output_channels.size()) {
        pc->output_channels[code]->send(StreamMessage{});
    }
}

// ---------------------------------------------------------------------------
// handle_cancel()
// ---------------------------------------------------------------------------

void Session::handle_cancel(const MessageHeader& hdr) noexcept {
    std::lock_guard lock(contexts_mutex_);
    auto it = contexts_.find(hdr.call_id);
    if (it != contexts_.end()) {
        it->second->ctx.set_cancelled(true);
    }
}

} // namespace logos::hrpc

// ---------------------------------------------------------------------------
// Context::push() — defined here to avoid circular include with session.hpp.
// Sends a ContextChannelMessage from server to client via the wire.
// ---------------------------------------------------------------------------

#include <logos/hrpc/context.hpp>

namespace logos::hrpc {

LOGOS_GREEN logos::expected<void> Context::push(StreamMessage msg, ChannelCode code) noexcept {
    if (session_) {
        return session_->send_channel_message(
            MessageType::ContextChannelMessage, call_id_, code, std::move(msg));
    }
    return {};
}

} // namespace logos::hrpc

// ---------------------------------------------------------------------------
// Call::push() — sends a CallChannelMessage from client to server via wire.
// ---------------------------------------------------------------------------

#include <logos/hrpc/call.hpp>

namespace logos::hrpc {

LOGOS_GREEN logos::expected<void> Call::push(StreamMessage msg, ChannelCode code) noexcept {
    if (session_) {
        return session_->send_channel_message(
            MessageType::CallChannelMessage,
            state_->call_id,
            code,
            std::move(msg));
    }
    return {};
}

} // namespace logos::hrpc
