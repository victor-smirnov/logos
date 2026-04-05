// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos
//
// Layer 4 exerciser: TCP networking via io_uring.

#include <logos/reactor/reactor.hpp>
#include <logos/reactor/tcp_socket.hpp>
#include <logos/verification/assert.hpp>
#include <logos/verification/trace.hpp>

#include <print>
#include <string>
#include <vector>

using namespace logos::reactor;

static constexpr uint16_t kBasePort = 54321;

static void test_echo_single() {
    LOGOS_TRACE("reactor.net.echo.single", "start", "");

    const uint16_t port = kBasePort;
    const std::string payload = "hello reactor";
    std::string received;
    Reactor reactor;

    reactor.spawn([&]() LOGOS_FIBER_FN {
        auto server = TcpSocket::listen_on("127.0.0.1", port).get();
        auto client = server.accept().get();
        char buf[256];
        int n = client.read(buf, sizeof(buf)).get();
        LOGOS_ASSERT(n > 0, "REACTOR-NET-T01a", "Server recv failed: {}", n);
        client.write_all(buf, static_cast<size_t>(n)).get();
    }, "server");

    reactor.spawn([&]() LOGOS_FIBER_FN {
        Scheduler::current()->yield();
        auto sock = TcpSocket::connect_to("127.0.0.1", port).get();
        sock.write_all(payload.data(), payload.size()).get();
        char buf[256];
        int n = sock.read(buf, sizeof(buf)).get();
        LOGOS_ASSERT(n > 0, "REACTOR-NET-T01b", "Client recv failed: {}", n);
        received.assign(buf, static_cast<size_t>(n));
    }, "client");

    reactor.run();

    LOGOS_ASSERT(received == payload, "REACTOR-NET-T01c",
                 "Echo mismatch: got '{}', expected '{}'", received, payload);
    LOGOS_TRACE("reactor.net.echo.single", "ok", "");
    std::println("  [ok] test_echo_single");
}

static void test_echo_multiple_clients() {
    LOGOS_TRACE("reactor.net.echo.multi", "start", "");

    constexpr int N = 5;
    const uint16_t port = kBasePort + 1;
    std::vector<std::string> results(N);
    Reactor reactor;

    reactor.spawn([&]() LOGOS_FIBER_FN {
        auto server = TcpSocket::listen_on("127.0.0.1", port).get();
        for (int i = 0; i < N; ++i) {
            auto client = server.accept().get();
            Scheduler::current()->spawn([c = std::move(client)]() mutable LOGOS_FIBER_FN {
                char buf[64];
                int n = c.read(buf, sizeof(buf)).get();
                if (n > 0) c.write_all(buf, static_cast<size_t>(n)).get();
            }, "handler");
        }
    }, "server");

    for (int i = 0; i < N; ++i) {
        reactor.spawn([&, i]() LOGOS_FIBER_FN {
            Scheduler::current()->yield();
            auto sock = TcpSocket::connect_to("127.0.0.1", port).get();
            std::string greeting = "client-" + std::to_string(i);
            sock.write_all(greeting.data(), greeting.size()).get();
            char buf[64];
            int n = sock.read(buf, sizeof(buf)).get();
            if (n > 0) results[i].assign(buf, static_cast<size_t>(n));
        }, "client");
    }

    reactor.run();

    for (int i = 0; i < N; ++i) {
        std::string expected = "client-" + std::to_string(i);
        LOGOS_ASSERT(results[i] == expected, "REACTOR-NET-T02",
                     "Client {} got '{}', expected '{}'", i, results[i], expected);
    }
    LOGOS_TRACE("reactor.net.echo.multi", "ok", "");
    std::println("  [ok] test_echo_multiple_clients ({} clients)", N);
}

static void test_streaming() {
    LOGOS_TRACE("reactor.net.streaming", "start", "");

    constexpr int N = 100;
    const uint16_t port = kBasePort + 2;
    int server_count = 0;
    Reactor reactor;

    reactor.spawn([&]() LOGOS_FIBER_FN {
        auto server = TcpSocket::listen_on("127.0.0.1", port).get();
        auto client = server.accept().get();
        char buf[4];
        while (true) {
            int n = client.read(buf, sizeof(buf)).get();
            if (n <= 0) break;
            if (n == 4) ++server_count;
        }
    }, "server");

    reactor.spawn([&]() LOGOS_FIBER_FN {
        Scheduler::current()->yield();
        auto sock = TcpSocket::connect_to("127.0.0.1", port).get();
        for (int i = 0; i < N; ++i) {
            uint32_t v = static_cast<uint32_t>(i);
            sock.write_all(&v, sizeof(v)).get();
        }
    }, "client");

    reactor.run();

    LOGOS_ASSERT(server_count == N, "REACTOR-NET-T03",
                 "Server received {} messages, expected {}", server_count, N);
    LOGOS_TRACE("reactor.net.streaming", "ok", "");
    std::println("  [ok] test_streaming ({} messages)", N);
}

int main() {
    std::println("=== reactor network exerciser (Layer 4 — TCP io_uring) ===");
    test_echo_single();
    test_echo_multiple_clients();
    test_streaming();
    std::println("=== all tests passed ===");
    return 0;
}
