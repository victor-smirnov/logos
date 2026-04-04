// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos
//
// HRPC Layer 1 exerciser — streaming RPC.
//
// Tests:
//   test_input_stream  — client sends 100 messages via input channel,
//                        server returns their sum
//   test_output_stream — server sends 50 messages via output channel,
//                        client collects them

#include <logos/hrpc/hrpc.hpp>
#include <logos/reactor/reactor.hpp>
#include <logos/reactor/scheduler.hpp>
#include <logos/verification/assert.hpp>

#include <print>

using namespace logos::hrpc;
using namespace logos::reactor;

static constexpr uint16_t kBasePort = 57010;

static const EndpointID kSumEndpoint = [] {
    EndpointID id{};
    id[0] = 0x50; id[1] = 0x55; id[2] = 0x4D;  // "SUM"
    return id;
}();

static const EndpointID kCountEndpoint = [] {
    EndpointID id{};
    id[0] = 0x43; id[1] = 0x4E; id[2] = 0x54;  // "CNT"
    return id;
}();

// ---------------------------------------------------------------------------
// test_input_stream — client sends 100 int32 messages via input channel[0],
//                     server reads them and returns their sum.
//
// End-of-stream protocol: client pushes a null-doc StreamMessage sentinel,
// which call->push() converts to a CallCloseOutput wire message.
// Server's Context::pop() returns false on the sentinel.
// ---------------------------------------------------------------------------
static void test_input_stream() {
    const uint16_t port = kBasePort;
    constexpr int N = 100;
    int32_t client_sum = 0;
    int32_t server_computed_sum = 0;

    for (int i = 0; i < N; ++i) client_sum += i;

    Reactor reactor;

    reactor.spawn([&] {
        auto listener = TcpSocket::listen_on("127.0.0.1", port).get();
        auto conn     = listener.accept().get();

        Session session(std::move(conn), SessionSide::Server);

        session.endpoints().add(kSumEndpoint, [&server_computed_sum](Context& ctx) -> Response {
            int32_t sum = 0;
            StreamMessage msg;
            while (ctx.pop(msg, 0)) {
                sum += msg.data().as_value<int32_t>();
            }
            server_computed_sum = sum;
            return Response::ok(AnyVal::from_value(sum)).get();
        }).get();

        session.start();
        session.run();
    }, "server");

    reactor.spawn([&] {

        Session session(TcpSocket::connect_to("127.0.0.1", port).get(), SessionSide::Client);
        Scheduler::current()->spawn([&session] { session.run(); }, "reader");
        session.start();

        // Make async call with 1 input channel (client → server).
        Request rq = Request::make().get();
        auto call = session.call_async(kSumEndpoint, std::move(rq),
                                       /*input_channels=*/1,
                                       /*output_channels=*/0);

        // Push N integer messages to the server via input_channel[0].
        for (int i = 0; i < N; ++i) {
            call->push(StreamMessage::make(AnyVal::from_value(int32_t(i))).get(), 0);
        }

        // Push a null-doc sentinel to signal end-of-stream.
        // call->push() detects the null doc and sends CallCloseOutput on the wire
        // instead of a CallChannelMessage. Context::pop() returns false on this.
        call->push(StreamMessage{}, 0);

        Response rs = call->wait();
        LOGOS_ASSERT(rs.is_ok(), "HRPC-STREAM-T01a",
                     "Sum call failed: {}", rs.error_description());
        int32_t returned_sum = rs.result().as_value<int32_t>();
        LOGOS_ASSERT(returned_sum == client_sum, "HRPC-STREAM-T01b",
                     "Sum mismatch: got {}, expected {}", returned_sum, client_sum);

        session.close();
    }, "client");

    reactor.run();

    LOGOS_ASSERT(server_computed_sum == client_sum, "HRPC-STREAM-T01c",
                 "Server sum {} != expected {}", server_computed_sum, client_sum);
    std::println("  [ok] test_input_stream ({} messages, sum={})", N, client_sum);
}

// ---------------------------------------------------------------------------
// test_output_stream — server sends 50 int32 messages via output channel[0],
//                      client collects them and verifies.
//
// End-of-stream protocol: server handler pushes a null-doc StreamMessage
// sentinel, which Context::push() converts to ContextCloseOutput on the wire.
// Call::pop() returns false on the sentinel.
// ---------------------------------------------------------------------------
static void test_output_stream() {
    const uint16_t port = kBasePort + 1;
    constexpr int N = 50;
    int32_t client_total = 0;
    int32_t expected_total = 0;
    for (int i = 0; i < N; ++i) expected_total += i * 2;

    Reactor reactor;

    reactor.spawn([&] {
        auto listener = TcpSocket::listen_on("127.0.0.1", port).get();
        auto conn     = listener.accept().get();

        Session session(std::move(conn), SessionSide::Server);

        session.endpoints().add(kCountEndpoint, [](Context& ctx) -> Response {
            constexpr int count = 50;
            for (int i = 0; i < count; ++i) {
                ctx.push(StreamMessage::make(AnyVal::from_value(int32_t(i * 2))).get(), 0);
            }
            // Null-doc sentinel signals end-of-stream to call->pop().
            ctx.push(StreamMessage{}, 0);
            return Response::ok(AnyVal::from_value(int32_t(count))).get();
        }).get();

        session.start();
        session.run();
    }, "server");

    reactor.spawn([&] {

        Session session(TcpSocket::connect_to("127.0.0.1", port).get(), SessionSide::Client);
        Scheduler::current()->spawn([&session] { session.run(); }, "reader");
        session.start();

        Request rq = Request::make().get();
        auto call = session.call_async(kCountEndpoint, std::move(rq),
                                       /*input_channels=*/0,
                                       /*output_channels=*/1);

        // Collect messages from output_channel[0] until end-of-stream.
        StreamMessage msg;
        int32_t total = 0;
        while (call->pop(msg, 0)) {
            total += msg.data().as_value<int32_t>();
        }
        client_total = total;

        Response rs = call->wait();
        LOGOS_ASSERT(rs.is_ok(), "HRPC-STREAM-T02a",
                     "Count call failed: {}", rs.error_description());

        session.close();
    }, "client");

    reactor.run();

    LOGOS_ASSERT(client_total == expected_total, "HRPC-STREAM-T02b",
                 "Output stream total {} != expected {}", client_total, expected_total);
    std::println("  [ok] test_output_stream ({} messages, total={})", N, client_total);
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main() {
    std::println("=== HRPC exerciser (Layer 1 — streaming) ===");
    test_input_stream();
    test_output_stream();
    std::println("=== all tests passed ===");
    return 0;
}
