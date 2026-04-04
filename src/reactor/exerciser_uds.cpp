// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos
//
// Layer 5 exerciser: Unix Domain Sockets.

#include <logos/reactor/reactor.hpp>
#include <logos/reactor/unix_socket.hpp>
#include <logos/verification/assert.hpp>
#include <logos/verification/trace.hpp>

#include <filesystem>
#include <print>
#include <string>
#include <vector>

using namespace logos::reactor;
namespace fs = std::filesystem;

static constexpr const char* kSockPath  = "/tmp/logos_uds_test.sock";
static constexpr const char* kSockPath2 = "/tmp/logos_uds_test2.sock";
static constexpr const char* kSockPath3 = "/tmp/logos_uds_test3.sock";

// ---------------------------------------------------------------------------
// Test 1: echo server — single round-trip
// ---------------------------------------------------------------------------
static void test_uds_echo_single() {
    LOGOS_TRACE("reactor.uds.echo.single", "start", "");
    const std::string payload = "hello uds";
    std::string received;
    Reactor reactor;

    reactor.spawn([&] {
        auto server = UnixSocket::listen_on(kSockPath).get();
        auto client = server.accept().get();
        char buf[64];
        int n = client.read(buf, sizeof(buf)).get();
        LOGOS_ASSERT(n > 0, "REACTOR-UDS-T01a", "server recv failed: {}", n);
        client.write_all(buf, static_cast<size_t>(n)).get();
    }, "server");

    reactor.spawn([&] {
        Scheduler::current()->yield();
        auto sock = UnixSocket::connect_to(kSockPath).get();
        sock.write_all(payload.data(), payload.size()).get();
        char buf[64];
        int n = sock.read(buf, sizeof(buf)).get();
        LOGOS_ASSERT(n > 0, "REACTOR-UDS-T01b", "client recv failed: {}", n);
        received.assign(buf, static_cast<size_t>(n));
    }, "client");

    reactor.run();

    LOGOS_ASSERT(received == payload, "REACTOR-UDS-T01c",
                 "Echo mismatch: '{}' != '{}'", received, payload);
    fs::remove(kSockPath);
    LOGOS_TRACE("reactor.uds.echo.single", "ok", "");
    std::println("  [ok] test_uds_echo_single");
}

// ---------------------------------------------------------------------------
// Test 2: multiple clients
// ---------------------------------------------------------------------------
static void test_uds_multiple_clients() {
    LOGOS_TRACE("reactor.uds.echo.multi", "start", "");
    constexpr int N = 4;
    std::vector<std::string> results(N);
    Reactor reactor;

    reactor.spawn([&] {
        auto server = UnixSocket::listen_on(kSockPath2).get();
        for (int i = 0; i < N; ++i) {
            auto client = server.accept().get();
            Scheduler::current()->spawn([c = std::move(client)]() mutable {
                char buf[64];
                int n = c.read(buf, sizeof(buf)).get();
                if (n > 0) c.write_all(buf, static_cast<size_t>(n)).get();
            }, "handler");
        }
    }, "server");

    for (int i = 0; i < N; ++i) {
        reactor.spawn([&, i] {
            Scheduler::current()->yield();
            auto sock = UnixSocket::connect_to(kSockPath2).get();
            std::string greeting = "uds-client-" + std::to_string(i);
            sock.write_all(greeting.data(), greeting.size()).get();
            char buf[64];
            int n = sock.read(buf, sizeof(buf)).get();
            if (n > 0) results[i].assign(buf, static_cast<size_t>(n));
        }, "client");
    }

    reactor.run();

    for (int i = 0; i < N; ++i) {
        std::string expected = "uds-client-" + std::to_string(i);
        LOGOS_ASSERT(results[i] == expected, "REACTOR-UDS-T02",
                     "client {} got '{}', expected '{}'", i, results[i], expected);
    }
    fs::remove(kSockPath2);
    LOGOS_TRACE("reactor.uds.echo.multi", "ok", "");
    std::println("  [ok] test_uds_multiple_clients ({} clients)", N);
}

// ---------------------------------------------------------------------------
// Test 3: large transfer
// ---------------------------------------------------------------------------
static void test_uds_large_transfer() {
    LOGOS_TRACE("reactor.uds.large", "start", "");
    constexpr size_t TOTAL = 256 * 1024;  // 256 KB
    std::vector<uint8_t> sent(TOTAL), received;
    for (size_t i = 0; i < TOTAL; ++i) sent[i] = static_cast<uint8_t>(i & 0xFF);
    Reactor reactor;

    reactor.spawn([&] {
        auto server = UnixSocket::listen_on(kSockPath3).get();
        auto client = server.accept().get();
        uint8_t buf[4096];
        while (true) {
            int n = client.read(buf, sizeof(buf)).get();
            if (n <= 0) break;
            for (int i = 0; i < n; ++i) received.push_back(buf[i]);
        }
    }, "server");

    reactor.spawn([&] {
        Scheduler::current()->yield();
        auto sock = UnixSocket::connect_to(kSockPath3).get();
        sock.write_all(sent.data(), sent.size()).get();
        // Closing the socket signals EOF to the server.
    }, "client");

    reactor.run();

    LOGOS_ASSERT(received.size() == TOTAL, "REACTOR-UDS-T03a",
                 "Expected {} bytes, got {}", TOTAL, received.size());
    LOGOS_ASSERT(received == sent, "REACTOR-UDS-T03b", "Data corruption in large transfer");
    fs::remove(kSockPath3);
    LOGOS_TRACE("reactor.uds.large", "ok", "");
    std::println("  [ok] test_uds_large_transfer ({} KB)", TOTAL / 1024);
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main() {
    std::println("=== reactor UDS exerciser (Layer 5 — Unix domain sockets) ===");
    test_uds_echo_single();
    test_uds_multiple_clients();
    test_uds_large_transfer();
    std::println("=== all tests passed ===");
    return 0;
}
