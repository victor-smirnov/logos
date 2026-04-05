// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos
//
// HRPC Layer 1 exerciser — streaming RPC.

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
    id[0] = 0x50; id[1] = 0x55; id[2] = 0x4D;
    return id;
}();

static const EndpointID kCountEndpoint = [] {
    EndpointID id{};
    id[0] = 0x43; id[1] = 0x4E; id[2] = 0x54;
    return id;
}();

// ---------------------------------------------------------------------------
// test_input_stream — client sends 100 int32 messages, server sums them.
// ---------------------------------------------------------------------------
static void test_input_stream() {
    const uint16_t port = kBasePort;
    constexpr int N = 100;
    int32_t client_sum = 0;
    int32_t server_computed_sum = 0;

    for (int i = 0; i < N; ++i) client_sum += i;

    Reactor reactor;

    reactor.spawn([&]() LOGOS_FIBER_FN {
        auto listener = TcpSocket::listen_on("127.0.0.1", port).get();
        auto conn     = listener.accept().get();

        Session session(std::move(conn), SessionSide::Server);

        // Handler calls ctx.pop() — green, must run in green context.
        session.endpoints().add(kSumEndpoint,
            [&server_computed_sum](Context& ctx) noexcept LOGOS_GREEN -> Response {
                int32_t sum = 0;
                StreamMessage msg;
                while (ctx.pop(msg, 0)) {
                    sum += msg.data().as_value<int32_t>();
                }
                server_computed_sum = sum;
                return Response::ok(AnyVal::from_value(sum)).get();
            }).get();

        session.start().get();
        session.run();
    }, "server");

    reactor.spawn([&]() LOGOS_FIBER_FN {
        Session session(TcpSocket::connect_to("127.0.0.1", port).get(), SessionSide::Client);
        Scheduler::current()->spawn([&session]() LOGOS_FIBER_FN { session.run(); }, "reader");
        session.start().get();

        Request rq = Request::make().get();
        auto call = session.call_async(kSumEndpoint, std::move(rq),
                                       /*input_channels=*/1,
                                       /*output_channels=*/0).get();

        for (int i = 0; i < N; ++i) {
            call->push(StreamMessage::make(AnyVal::from_value(int32_t(i))).get(), 0).get();
        }
        call->push(StreamMessage{}, 0).get();

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
// test_output_stream — server sends 50 messages via output channel.
// ---------------------------------------------------------------------------
static void test_output_stream() {
    const uint16_t port = kBasePort + 1;
    constexpr int N = 50;
    int32_t client_total = 0;
    int32_t expected_total = 0;
    for (int i = 0; i < N; ++i) expected_total += i * 2;

    Reactor reactor;

    reactor.spawn([&]() LOGOS_FIBER_FN {
        auto listener = TcpSocket::listen_on("127.0.0.1", port).get();
        auto conn     = listener.accept().get();

        Session session(std::move(conn), SessionSide::Server);

        // Handler calls ctx.push() — green, must run in green context.
        session.endpoints().add(kCountEndpoint,
            [](Context& ctx) noexcept LOGOS_GREEN -> Response {
                constexpr int count = 50;
                for (int i = 0; i < count; ++i) {
                    ctx.push(StreamMessage::make(AnyVal::from_value(int32_t(i * 2))).get(), 0).get();
                }
                ctx.push(StreamMessage{}, 0).get();
                return Response::ok(AnyVal::from_value(int32_t(count))).get();
            }).get();

        session.start().get();
        session.run();
    }, "server");

    reactor.spawn([&]() LOGOS_FIBER_FN {
        Session session(TcpSocket::connect_to("127.0.0.1", port).get(), SessionSide::Client);
        Scheduler::current()->spawn([&session]() LOGOS_FIBER_FN { session.run(); }, "reader");
        session.start().get();

        Request rq = Request::make().get();
        auto call = session.call_async(kCountEndpoint, std::move(rq),
                                       /*input_channels=*/0,
                                       /*output_channels=*/1).get();

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

int main() {
    std::println("=== HRPC exerciser (Layer 1 — streaming) ===");
    test_input_stream();
    test_output_stream();
    std::println("=== all tests passed ===");
    return 0;
}
