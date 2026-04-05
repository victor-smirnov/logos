// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
//
// HRPC local transport exerciser — cross-reactor RPC without TCP.

#include <logos/hrpc/local_session.hpp>
#include <logos/reactor/reactor_engine.hpp>
#include <logos/verification/assert.hpp>

#include <chrono>
#include <print>
#include <thread>
#include <vector>

using namespace logos::hrpc;
using namespace logos::reactor;

static const EndpointID kEchoEndpoint    = endpoint_id_from_name("test.Local/echo");
static const EndpointID kUnknownEndpoint = endpoint_id_from_name("test.Local/unknown");

// ---------------------------------------------------------------------------
// test_local_echo — single call, response matches request value
// ---------------------------------------------------------------------------
static void test_local_echo() {
    ReactorEngine engine(2);
    int32_t result_value = 0;

    LocalServer server(engine.reactor(1));
    server.endpoints().add(kEchoEndpoint,
        [](Context& ctx) noexcept -> Response {
            AnyVal param = ctx.request().get_param(keys::PARAMETERS);
            return Response::ok(param).get();
        }).get();

    engine.reactor(0).spawn([&]() {
        Request rq = Request::make().get();
        rq.set_param(keys::PARAMETERS, AnyVal::from_value(int32_t(42))).get();
        auto rs = server.call(kEchoEndpoint, std::move(rq)).get();
        LOGOS_ASSERT(rs.is_ok(), "HRPC-LOCAL-T01a",
                     "Expected ok response, got error: {}", rs.error_description());
        result_value = rs.result().as_value<int32_t>();
    }, "client");

    engine.reactor(1).spawn([]() {
        Reactor::current()->sleep_for(std::chrono::milliseconds(500));
    }, "keepalive");

    std::jthread t1([&] { engine.reactor(1).run(); });
    engine.reactor(0).run();
    engine.reactor(1).stop();
    t1.join();

    LOGOS_ASSERT(result_value == 42, "HRPC-LOCAL-T01b",
                 "Echo value mismatch: got {}, expected 42", result_value);
    std::println("  [ok] test_local_echo");
}

// ---------------------------------------------------------------------------
// test_local_multi — N sequential calls on same LocalServer
// ---------------------------------------------------------------------------
static void test_local_multi() {
    ReactorEngine engine(2);
    constexpr int N = 10;
    int results[N] = {};

    LocalServer server(engine.reactor(1));
    server.endpoints().add(kEchoEndpoint,
        [](Context& ctx) noexcept -> Response {
            return Response::ok(ctx.request().get_param(keys::PARAMETERS)).get();
        }).get();

    engine.reactor(0).spawn([&]() {
        for (int i = 0; i < N; ++i) {
            Request rq = Request::make().get();
            rq.set_param(keys::PARAMETERS, AnyVal::from_value(int32_t(i * 10))).get();
            auto rs = server.call(kEchoEndpoint, std::move(rq)).get();
            LOGOS_ASSERT(rs.is_ok(), "HRPC-LOCAL-T02a",
                         "Call {} failed: {}", i, rs.error_description());
            results[i] = rs.result().as_value<int32_t>();
        }
    }, "client");

    engine.reactor(1).spawn([]() {
        Reactor::current()->sleep_for(std::chrono::seconds(2));
    }, "keepalive");

    std::jthread t1([&] { engine.reactor(1).run(); });
    engine.reactor(0).run();
    engine.reactor(1).stop();
    t1.join();

    for (int i = 0; i < N; ++i) {
        LOGOS_ASSERT(results[i] == i * 10, "HRPC-LOCAL-T02b",
                     "Call {} result mismatch: got {}, expected {}", i, results[i], i * 10);
    }
    std::println("  [ok] test_local_multi");
}

// ---------------------------------------------------------------------------
// test_local_fan_in — 3 client reactors call one server
// ---------------------------------------------------------------------------
static void test_local_fan_in() {
    constexpr size_t NUM_REACTORS = 4;
    ReactorEngine engine(NUM_REACTORS);
    std::atomic<int> total{0};

    LocalServer server(engine.reactor(0));
    server.endpoints().add(kEchoEndpoint,
        [](Context& ctx) noexcept -> Response {
            return Response::ok(ctx.request().get_param(keys::PARAMETERS)).get();
        }).get();

    engine.reactor(0).spawn([]() {
        Reactor::current()->sleep_for(std::chrono::seconds(2));
    }, "keepalive");

    for (size_t i = 1; i < NUM_REACTORS; ++i) {
        engine.reactor(i).spawn([&server, &total, i]() {
            Request rq = Request::make().get();
            rq.set_param(keys::PARAMETERS,
                         AnyVal::from_value(int32_t(i * 100))).get();
            auto rs = server.call(kEchoEndpoint, std::move(rq)).get();
            LOGOS_ASSERT(rs.is_ok(), "HRPC-LOCAL-T03a",
                         "Client {} failed: {}", i, rs.error_description());
            total.fetch_add(rs.result().as_value<int32_t>(), std::memory_order_relaxed);
        }, "client");
    }

    std::vector<std::jthread> threads;
    for (size_t i = 1; i < NUM_REACTORS; ++i)
        threads.emplace_back([&engine, i] { engine.reactor(i).run(); });

    engine.reactor(0).run();
    engine.reactor(0).stop();

    LOGOS_ASSERT(total.load() == 600, "HRPC-LOCAL-T03b",
                 "Fan-in total mismatch: got {}, expected 600", total.load());
    std::println("  [ok] test_local_fan_in");
}

// ---------------------------------------------------------------------------
// test_local_error — call to unregistered endpoint returns error
// ---------------------------------------------------------------------------
static void test_local_error() {
    ReactorEngine engine(2);
    bool got_error = false;

    LocalServer server(engine.reactor(1));

    engine.reactor(0).spawn([&]() {
        Request rq = Request::make().get();
        auto rs = server.call(kUnknownEndpoint, std::move(rq)).get();
        got_error = !rs.is_ok();
    }, "client");

    engine.reactor(1).spawn([]() {
        Reactor::current()->sleep_for(std::chrono::milliseconds(500));
    }, "keepalive");

    std::jthread t1([&] { engine.reactor(1).run(); });
    engine.reactor(0).run();
    engine.reactor(1).stop();
    t1.join();

    LOGOS_ASSERT(got_error, "HRPC-LOCAL-T04",
                 "Expected error for unknown endpoint, got success");
    std::println("  [ok] test_local_error");
}

int main() {
    std::println("=== HRPC local transport exerciser ===");
    test_local_echo();
    test_local_multi();
    test_local_fan_in();
    test_local_error();
    std::println("=== all tests passed ===");
    return 0;
}
