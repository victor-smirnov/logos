// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos
//
// Layer 6 exerciser: UDP sockets via io_uring sendmsg/recvmsg.

#include <logos/reactor/reactor.hpp>
#include <logos/reactor/udp_socket.hpp>
#include <logos/verification/assert.hpp>
#include <logos/verification/trace.hpp>

#include <print>
#include <string>
#include <vector>

using namespace logos::reactor;

static constexpr uint16_t kUdpBase = 55400;

// ---------------------------------------------------------------------------
// Test 1: echo server — single round-trip (unconnected)
// ---------------------------------------------------------------------------
static void test_udp_echo_single() {
    LOGOS_TRACE("reactor.udp.echo.single", "start", "");
    const std::string payload = "hello udp";
    std::string received;
    Reactor reactor;

    reactor.spawn([&]() LOGOS_FIBER_FN {
        auto server = UdpSocket::bind_to("127.0.0.1", kUdpBase).get();
        char buf[256];
        UdpEndpoint from;
        int n = server.recv_from(buf, sizeof(buf), from).get();
        LOGOS_ASSERT(n > 0, "REACTOR-UDP-T01a", "server recv_from failed: {}", n);
        server.send_to(buf, static_cast<size_t>(n), from).get();
    }, "server");

    reactor.spawn([&]() LOGOS_FIBER_FN {
        Scheduler::current()->yield();
        auto sock = UdpSocket::bind_to("127.0.0.1", kUdpBase + 1).get();
        auto server_ep = UdpEndpoint::make("127.0.0.1", kUdpBase).get();
        sock.send_to(payload.data(), payload.size(), server_ep).get();
        char buf[256];
        UdpEndpoint from;
        int n = sock.recv_from(buf, sizeof(buf), from).get();
        LOGOS_ASSERT(n > 0, "REACTOR-UDP-T01b", "client recv_from failed: {}", n);
        received.assign(buf, static_cast<size_t>(n));
    }, "client");

    reactor.run();

    LOGOS_ASSERT(received == payload, "REACTOR-UDP-T01c",
                 "Echo mismatch: '{}' != '{}'", received, payload);
    LOGOS_TRACE("reactor.udp.echo.single", "ok", "");
    std::println("  [ok] test_udp_echo_single");
}

// ---------------------------------------------------------------------------
// Test 2: connected mode echo
// ---------------------------------------------------------------------------
static void test_udp_connected() {
    LOGOS_TRACE("reactor.udp.connected", "start", "");
    const std::string payload = "connected udp test";
    std::string received;
    Reactor reactor;

    reactor.spawn([&]() LOGOS_FIBER_FN {
        auto server = UdpSocket::bind_to("127.0.0.1", kUdpBase + 2).get();
        char buf[256];
        UdpEndpoint from;
        int n = server.recv_from(buf, sizeof(buf), from).get();
        LOGOS_ASSERT(n > 0, "REACTOR-UDP-T02a", "server recv failed: {}", n);
        server.send_to(buf, static_cast<size_t>(n), from).get();
    }, "server");

    reactor.spawn([&]() LOGOS_FIBER_FN {
        Scheduler::current()->yield();
        auto sock = UdpSocket::connect_to("127.0.0.1", kUdpBase + 2).get();
        sock.send(payload.data(), payload.size()).get();
        char buf[256];
        int n = sock.recv(buf, sizeof(buf)).get();
        LOGOS_ASSERT(n > 0, "REACTOR-UDP-T02b", "client recv failed: {}", n);
        received.assign(buf, static_cast<size_t>(n));
    }, "client");

    reactor.run();

    LOGOS_ASSERT(received == payload, "REACTOR-UDP-T02c",
                 "Connected echo mismatch: '{}' != '{}'", received, payload);
    LOGOS_TRACE("reactor.udp.connected", "ok", "");
    std::println("  [ok] test_udp_connected");
}

// ---------------------------------------------------------------------------
// Test 3: multiple datagrams — server echoes N packets
// ---------------------------------------------------------------------------
static void test_udp_multi_datagram() {
    LOGOS_TRACE("reactor.udp.multi", "start", "");
    constexpr int N = 20;
    std::vector<int> echoed;
    Reactor reactor;

    reactor.spawn([&]() LOGOS_FIBER_FN {
        auto server = UdpSocket::bind_to("127.0.0.1", kUdpBase + 3).get();
        for (int i = 0; i < N; ++i) {
            char buf[8];
            UdpEndpoint from;
            int n = server.recv_from(buf, sizeof(buf), from).get();
            LOGOS_ASSERT(n == 4, "REACTOR-UDP-T03a", "server recv wrong size: {}", n);
            server.send_to(buf, static_cast<size_t>(n), from).get();
        }
    }, "server");

    reactor.spawn([&]() LOGOS_FIBER_FN {
        Scheduler::current()->yield();
        auto sock = UdpSocket::bind_to("0.0.0.0", kUdpBase + 4).get();
        auto server_ep = UdpEndpoint::make("127.0.0.1", kUdpBase + 3).get();
        for (int i = 0; i < N; ++i) {
            uint32_t val = static_cast<uint32_t>(i);
            sock.send_to(&val, sizeof(val), server_ep).get();
            uint32_t reply;
            UdpEndpoint from;
            int n = sock.recv_from(&reply, sizeof(reply), from).get();
            LOGOS_ASSERT(n == 4, "REACTOR-UDP-T03b", "client recv wrong size: {}", n);
            echoed.push_back(static_cast<int>(reply));
        }
    }, "client");

    reactor.run();

    LOGOS_ASSERT((int)echoed.size() == N, "REACTOR-UDP-T03c",
                 "Expected {} datagrams, got {}", N, echoed.size());
    for (int i = 0; i < N; ++i)
        LOGOS_ASSERT(echoed[i] == i, "REACTOR-UDP-T03d",
                     "Datagram {}: got {}, expected {}", i, echoed[i], i);
    LOGOS_TRACE("reactor.udp.multi", "ok", "");
    std::println("  [ok] test_udp_multi_datagram ({} datagrams)", N);
}

int main() {
    std::println("=== reactor UDP exerciser (Layer 6 — UDP io_uring sendmsg/recvmsg) ===");
    test_udp_echo_single();
    test_udp_connected();
    test_udp_multi_datagram();
    std::println("=== all tests passed ===");
    return 0;
}
