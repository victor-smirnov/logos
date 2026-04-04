// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos
//
// HRPC Layer 0 exerciser — basic request-response RPC.
//
// Tests:
//   test_echo_rq         — single call, response matches request value
//   test_multi_rq        — 5 sequential calls on same session
//   test_bidirectional   — both sides register endpoints, call each other
//   test_unknown_endpoint — call to non-registered endpoint returns error

#include <logos/hrpc/hrpc.hpp>
#include <logos/reactor/reactor.hpp>
#include <logos/reactor/scheduler.hpp>
#include <logos/verification/assert.hpp>

#include <print>

using namespace logos::hrpc;
using namespace logos::reactor;

static constexpr uint16_t kBasePort = 57000;

// A well-known endpoint ID for the echo handler.
static const EndpointID kEchoEndpoint = [] {
    EndpointID id{};
    id[0] = 0xEC; id[1] = 0x40;  // "ECHO" marker
    return id;
}();

// A second endpoint ID for bidirectional test.
static const EndpointID kPingEndpoint = [] {
    EndpointID id{};
    id[0] = 0xB1; id[1] = 0xD1;  // "BIDI" marker
    return id;
}();

// ---------------------------------------------------------------------------
// test_echo_rq — single call, response value matches request value
// ---------------------------------------------------------------------------
static void test_echo_rq() {
    const uint16_t port = kBasePort;
    int32_t result_value = 0;

    Reactor reactor;

    reactor.spawn([&] {
        auto listener = TcpSocket::listen_on("127.0.0.1", port).get();
        auto conn     = listener.accept();

        Session session(std::move(conn), SessionSide::Server);
        session.endpoints().add(kEchoEndpoint, [](Context& ctx) -> Response {
            AnyVal param = ctx.request().get_param(keys::PARAMETERS);
            return Response::ok(param);
        });
        session.start();
        session.run();
    }, "server");

    reactor.spawn([&] {

        Session session(TcpSocket::connect_to("127.0.0.1", port).get(), SessionSide::Client);

        // Spawn the run fiber before start() so we can receive SESSION_START ack.
        Scheduler::current()->spawn([&session] { session.run(); }, "reader");

        session.start();

        Request rq = Request::make();
        rq.set_param(keys::PARAMETERS, AnyVal::from_value(int32_t(42)));

        Response rs = session.call(kEchoEndpoint, std::move(rq));

        LOGOS_ASSERT(rs.is_ok(), "HRPC-SESSION-T01a",
                     "Expected ok response, got error: {}", rs.error_description());
        result_value = rs.result().as_value<int32_t>();

        session.close();
    }, "client");

    reactor.run();

    LOGOS_ASSERT(result_value == 42, "HRPC-SESSION-T01b",
                 "Echo value mismatch: got {}, expected 42", result_value);
    std::println("  [ok] test_echo_rq");
}

// ---------------------------------------------------------------------------
// test_multi_rq — 5 sequential calls on same session
// ---------------------------------------------------------------------------
static void test_multi_rq() {
    const uint16_t port = kBasePort + 1;
    constexpr int N = 5;
    int results[N] = {};

    Reactor reactor;

    reactor.spawn([&] {
        auto listener = TcpSocket::listen_on("127.0.0.1", port).get();
        auto conn     = listener.accept();

        Session session(std::move(conn), SessionSide::Server);
        session.endpoints().add(kEchoEndpoint, [](Context& ctx) -> Response {
            return Response::ok(ctx.request().get_param(keys::PARAMETERS));
        });
        session.start();
        session.run();
    }, "server");

    reactor.spawn([&] {

        Session session(TcpSocket::connect_to("127.0.0.1", port).get(), SessionSide::Client);
        Scheduler::current()->spawn([&session] { session.run(); }, "reader");
        session.start();

        for (int i = 0; i < N; ++i) {
            Request rq = Request::make();
            rq.set_param(keys::PARAMETERS, AnyVal::from_value(int32_t(i * 10)));
            Response rs = session.call(kEchoEndpoint, std::move(rq));

            LOGOS_ASSERT(rs.is_ok(), "HRPC-SESSION-T02a",
                         "Call {} failed: {}", i, rs.error_description());
            results[i] = rs.result().as_value<int32_t>();
        }

        session.close();
    }, "client");

    reactor.run();

    for (int i = 0; i < N; ++i) {
        LOGOS_ASSERT(results[i] == i * 10, "HRPC-SESSION-T02b",
                     "Multi call {}: got {}, expected {}", i, results[i], i * 10);
    }
    std::println("  [ok] test_multi_rq ({} calls)", N);
}

// ---------------------------------------------------------------------------
// test_bidirectional — both sides register endpoints and call each other.
//
// Pattern: client calls server (echo), server's handler calls client back
// (ping) as part of processing, then returns.  Both calls complete before
// session.close() is issued by the client.
// ---------------------------------------------------------------------------
static void test_bidirectional() {
    const uint16_t port = kBasePort + 2;
    int32_t client_got  = 0;
    int32_t server_called_client_with = 0;

    Reactor reactor;

    reactor.spawn([&] {
        auto listener = TcpSocket::listen_on("127.0.0.1", port).get();
        auto conn     = listener.accept();

        Session session(std::move(conn), SessionSide::Server);

        // Echo handler: receives client's value, calls client's ping endpoint,
        // then returns the ping result to the client.
        session.endpoints().add(kEchoEndpoint,
            [&server_called_client_with, &session](Context& ctx) -> Response {
                int32_t client_val =
                    ctx.request().get_param(keys::PARAMETERS).as_value<int32_t>();

                // Call the client's kPingEndpoint from within the handler fiber.
                Request ping_rq = Request::make();
                ping_rq.set_param(keys::PARAMETERS,
                                  AnyVal::from_value(int32_t(client_val + 1)));
                Response ping_rs = session.call(kPingEndpoint, std::move(ping_rq));

                server_called_client_with = client_val + 1;

                if (!ping_rs.is_ok()) return Response::error("ping failed");
                return Response::ok(AnyVal::from_value(int32_t(200)));
            });

        // Server runs its read loop — no separate "server makes first call" step.
        session.start();
        session.run();
    }, "server");

    reactor.spawn([&] {

        Session session(TcpSocket::connect_to("127.0.0.1", port).get(), SessionSide::Client);

        // Client registers kPingEndpoint so server can call it back.
        session.endpoints().add(kPingEndpoint, [](Context& ctx) -> Response {
            return Response::ok(ctx.request().get_param(keys::PARAMETERS));
        });

        Scheduler::current()->spawn([&session] { session.run(); }, "reader");
        session.start();

        // Client calls the server's echo endpoint.
        Request rq = Request::make();
        rq.set_param(keys::PARAMETERS, AnyVal::from_value(int32_t(77)));
        Response rs = session.call(kEchoEndpoint, std::move(rq));

        LOGOS_ASSERT(rs.is_ok(), "HRPC-SESSION-T03b",
                     "Client->server call failed: {}", rs.error_description());
        client_got = rs.result().as_value<int32_t>();

        session.close();
    }, "client");

    reactor.run();

    LOGOS_ASSERT(server_called_client_with == 78, "HRPC-SESSION-T03c",
                 "server_called_client_with={}, expected 78", server_called_client_with);
    LOGOS_ASSERT(client_got == 200, "HRPC-SESSION-T03d",
                 "client_got={}, expected 200", client_got);
    std::println("  [ok] test_bidirectional");
}

// ---------------------------------------------------------------------------
// test_unknown_endpoint — call to non-registered endpoint returns error
// ---------------------------------------------------------------------------
static void test_unknown_endpoint() {
    const uint16_t port = kBasePort + 3;
    bool got_error = false;

    Reactor reactor;

    reactor.spawn([&] {
        auto listener = TcpSocket::listen_on("127.0.0.1", port).get();
        auto conn     = listener.accept();

        Session session(std::move(conn), SessionSide::Server);
        // Register nothing — all calls should return error.
        session.start();
        session.run();
    }, "server");

    reactor.spawn([&] {

        Session session(TcpSocket::connect_to("127.0.0.1", port).get(), SessionSide::Client);
        Scheduler::current()->spawn([&session] { session.run(); }, "reader");
        session.start();

        EndpointID unknown = make_random_endpoint_id();
        Request rq = Request::make();
        Response rs = session.call(unknown, std::move(rq));

        got_error = !rs.is_ok();
        LOGOS_ASSERT(got_error, "HRPC-SESSION-T04a",
                     "Expected error for unknown endpoint, got ok");

        session.close();
    }, "client");

    reactor.run();

    LOGOS_ASSERT(got_error, "HRPC-SESSION-T04b",
                 "test_unknown_endpoint: error flag not set");
    std::println("  [ok] test_unknown_endpoint");
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main() {
    std::println("=== HRPC exerciser (Layer 0 — basic request-response) ===");
    test_echo_rq();
    test_multi_rq();
    test_bidirectional();
    test_unknown_endpoint();
    std::println("=== all tests passed ===");
    return 0;
}
