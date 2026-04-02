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

// Port range to use for tests.  Using high numbered ports to avoid conflicts.
static constexpr uint16_t kBasePort = 54321;

// ---------------------------------------------------------------------------
// Test 1: echo server — single client, single round-trip
// ---------------------------------------------------------------------------
static void test_echo_single() {
    LOGOS_TRACE("reactor.net.echo.single", "start", "");

    const uint16_t port = kBasePort;
    const std::string payload = "hello reactor";
    std::string received;

    Reactor reactor;

    // Server fiber: accept one client, echo once, close.
    reactor.spawn([&] {
        auto server = TcpSocket::listen_on("127.0.0.1", port);
        auto client = server.accept();

        char buf[256];
        int n = client.read(buf, sizeof(buf));
        LOGOS_ASSERT(n > 0, "REACTOR-NET-T01a",
                     "Server recv failed: {}", n);
        client.write_all(buf, static_cast<size_t>(n));
    }, "server");

    // Client fiber: connect, send, receive.
    reactor.spawn([&] {
        // Yield once to let server fiber bind and listen first.
        Scheduler::current()->yield();
        auto sock = TcpSocket::connect_to("127.0.0.1", port);
        sock.write_all(payload.data(), payload.size());

        char buf[256];
        int n = sock.read(buf, sizeof(buf));
        LOGOS_ASSERT(n > 0, "REACTOR-NET-T01b",
                     "Client recv failed: {}", n);
        received.assign(buf, static_cast<size_t>(n));
    }, "client");

    reactor.run();

    LOGOS_ASSERT(received == payload, "REACTOR-NET-T01c",
                 "Echo mismatch: got '{}', expected '{}'", received, payload);
    LOGOS_TRACE("reactor.net.echo.single", "ok", "");
    std::println("  [ok] test_echo_single");
}

// ---------------------------------------------------------------------------
// Test 2: echo server — N clients, each gets one round-trip
// ---------------------------------------------------------------------------
static void test_echo_multiple_clients() {
    LOGOS_TRACE("reactor.net.echo.multi", "start", "");

    constexpr int N = 5;
    const uint16_t port = kBasePort + 1;
    std::vector<std::string> results(N);

    Reactor reactor;

    // Server: accept N clients, spawn a handler fiber per client.
    reactor.spawn([&] {
        auto server = TcpSocket::listen_on("127.0.0.1", port);
        for (int i = 0; i < N; ++i) {
            auto client = server.accept();
            Scheduler::current()->spawn([c = std::move(client)]() mutable {
                char buf[64];
                int n = c.read(buf, sizeof(buf));
                if (n > 0) c.write_all(buf, static_cast<size_t>(n));
            }, "handler");
        }
    }, "server");

    // N client fibers.
    for (int i = 0; i < N; ++i) {
        reactor.spawn([&, i] {
            Scheduler::current()->yield();
            auto sock = TcpSocket::connect_to("127.0.0.1", port);
            std::string greeting = "client-" + std::to_string(i);
            sock.write_all(greeting.data(), greeting.size());
            char buf[64];
            int n = sock.read(buf, sizeof(buf));
            if (n > 0) results[i].assign(buf, static_cast<size_t>(n));
        }, "client");
    }

    reactor.run();

    for (int i = 0; i < N; ++i) {
        std::string expected = "client-" + std::to_string(i);
        LOGOS_ASSERT(results[i] == expected, "REACTOR-NET-T02",
                     "Client {} got '{}', expected '{}'",
                     i, results[i], expected);
    }
    LOGOS_TRACE("reactor.net.echo.multi", "ok", "");
    std::println("  [ok] test_echo_multiple_clients ({} clients)", N);
}

// ---------------------------------------------------------------------------
// Test 3: streaming — client sends 100 messages, server counts them
// ---------------------------------------------------------------------------
static void test_streaming() {
    LOGOS_TRACE("reactor.net.streaming", "start", "");

    constexpr int N = 100;
    const uint16_t port = kBasePort + 2;
    int server_count = 0;

    Reactor reactor;

    // Server: read until connection closes, count 4-byte messages.
    reactor.spawn([&] {
        auto server = TcpSocket::listen_on("127.0.0.1", port);
        auto client = server.accept();
        char buf[4];
        while (true) {
            int n = client.read(buf, sizeof(buf));
            if (n <= 0) break;
            if (n == 4) ++server_count;
        }
    }, "server");

    // Client: send N 4-byte messages, then close.
    reactor.spawn([&] {
        Scheduler::current()->yield();
        auto sock = TcpSocket::connect_to("127.0.0.1", port);
        for (int i = 0; i < N; ++i) {
            uint32_t v = static_cast<uint32_t>(i);
            sock.write_all(&v, sizeof(v));
        }
        // Closing sock drops the fd; server gets EOF.
    }, "client");

    reactor.run();

    LOGOS_ASSERT(server_count == N, "REACTOR-NET-T03",
                 "Server received {} messages, expected {}", server_count, N);
    LOGOS_TRACE("reactor.net.streaming", "ok", "");
    std::println("  [ok] test_streaming ({} messages)", N);
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main() {
    std::println("=== reactor network exerciser (Layer 4 — TCP io_uring) ===");
    test_echo_single();
    test_echo_multiple_clients();
    test_streaming();
    std::println("=== all tests passed ===");
    return 0;
}
