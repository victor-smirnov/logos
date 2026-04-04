// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos
//
// Layer 7 exerciser: SignalWatcher + graceful shutdown.

#include <logos/core/make_object.hpp>
#include <logos/reactor/reactor.hpp>
#include <logos/reactor/signal_watcher.hpp>
#include <logos/verification/assert.hpp>
#include <logos/verification/trace.hpp>

#include <csignal>
#include <print>

using namespace logos::reactor;

// ---------------------------------------------------------------------------
// Test 1: catch SIGUSR1 sent from within the reactor
// ---------------------------------------------------------------------------
static void test_signal_catch() {
    LOGOS_TRACE("reactor.signal.catch", "start", "");
    int caught = 0;
    Reactor reactor;

    auto watcher = logos::make_object<SignalWatcher>(std::initializer_list<int>{SIGUSR1}).get();

    // Fiber 1: waits for SIGUSR1.
    reactor.spawn([&] {
        caught = watcher.wait().get();
    }, "signal-waiter");

    // Fiber 2: raises SIGUSR1 after yielding to let waiter block.
    reactor.spawn([&] {
        Scheduler::current()->yield();
        ::raise(SIGUSR1);
    }, "signal-sender");

    reactor.run();

    LOGOS_ASSERT(caught == SIGUSR1, "REACTOR-SIG-T01",
                 "Expected SIGUSR1 ({}), got {}", SIGUSR1, caught);
    LOGOS_TRACE("reactor.signal.catch", "ok", "");
    std::println("  [ok] test_signal_catch (caught SIGUSR1)");
}

// ---------------------------------------------------------------------------
// Test 2: graceful shutdown — SIGUSR2 stops the reactor while work is running
// ---------------------------------------------------------------------------
static void test_graceful_shutdown() {
    LOGOS_TRACE("reactor.signal.shutdown", "start", "");
    bool work_started = false;
    bool shutdown_seen = false;
    Reactor reactor;

    auto watcher = logos::make_object<SignalWatcher>(std::initializer_list<int>{SIGUSR2}).get();

    // Signal handler fiber: waits, then calls reactor.stop().
    reactor.spawn([&] {
        watcher.wait().get();
        shutdown_seen = true;
        reactor.stop();
    }, "shutdown-handler");

    // Work fiber: starts, yields a few times.  After stop(), the reactor
    // exits when the run queue drains — this fiber may or may not complete.
    reactor.spawn([&] {
        work_started = true;
        for (int i = 0; i < 5; ++i)
            Scheduler::current()->yield();
    }, "worker");

    // Trigger fiber: sends SIGUSR2 after work starts.
    reactor.spawn([&] {
        Scheduler::current()->yield();  // let work_fiber start
        ::raise(SIGUSR2);
    }, "trigger");

    reactor.run();

    LOGOS_ASSERT(work_started, "REACTOR-SIG-T02a", "Worker never started");
    LOGOS_ASSERT(shutdown_seen, "REACTOR-SIG-T02b", "Shutdown handler never ran");
    LOGOS_TRACE("reactor.signal.shutdown", "ok", "");
    std::println("  [ok] test_graceful_shutdown");
}

// ---------------------------------------------------------------------------
// Test 3: multiple signals — catch SIGUSR1 twice
// ---------------------------------------------------------------------------
static void test_multiple_signals() {
    LOGOS_TRACE("reactor.signal.multiple", "start", "");
    int count = 0;
    Reactor reactor;

    auto watcher = logos::make_object<SignalWatcher>(std::initializer_list<int>{SIGUSR1}).get();

    reactor.spawn([&] {
        for (int i = 0; i < 2; ++i) {
            int sig = watcher.wait().get();
            LOGOS_ASSERT(sig == SIGUSR1, "REACTOR-SIG-T03a",
                         "Expected SIGUSR1, got {}", sig);
            ++count;
        }
    }, "waiter");

    reactor.spawn([&] {
        Scheduler::current()->yield();
        ::raise(SIGUSR1);
        Scheduler::current()->yield();
        ::raise(SIGUSR1);
    }, "sender");

    reactor.run();

    LOGOS_ASSERT(count == 2, "REACTOR-SIG-T03b",
                 "Expected 2 signals caught, got {}", count);
    LOGOS_TRACE("reactor.signal.multiple", "ok", "");
    std::println("  [ok] test_multiple_signals");
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main() {
    std::println("=== reactor signal exerciser (Layer 7 — signalfd + io_uring poll) ===");
    test_signal_catch();
    test_graceful_shutdown();
    test_multiple_signals();
    std::println("=== all tests passed ===");
    return 0;
}
