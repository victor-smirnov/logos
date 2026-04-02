// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos
//
// Layer 2 exerciser: fiber-aware Mutex, ConditionVariable, Channel.

#include <logos/reactor/channel.hpp>
#include <logos/reactor/condition_variable.hpp>
#include <logos/reactor/mutex.hpp>
#include <logos/reactor/scheduler.hpp>
#include <logos/verification/assert.hpp>
#include <logos/verification/trace.hpp>

#include <numeric>
#include <print>
#include <vector>

using namespace logos::reactor;

// ---------------------------------------------------------------------------
// Mutex tests
// ---------------------------------------------------------------------------

static void test_mutex_basic() {
    LOGOS_TRACE("reactor.sync.mutex.basic", "start", "");
    int counter = 0;
    Mutex mu;
    Scheduler sched;

    // 10 fibers each increment counter 100 times under the mutex.
    for (int i = 0; i < 10; ++i) {
        sched.spawn([&] {
            for (int j = 0; j < 100; ++j) {
                mu.lock();
                ++counter;
                mu.unlock();
            }
        });
    }
    sched.run();

    LOGOS_ASSERT(counter == 1000, "REACTOR-SYNC-M01",
                 "Expected counter=1000, got {}", counter);
    LOGOS_TRACE("reactor.sync.mutex.basic", "ok", "");
    std::println("  [ok] test_mutex_basic");
}

static void test_mutex_contention() {
    LOGOS_TRACE("reactor.sync.mutex.contention", "start", "");
    std::vector<int> log;
    Mutex mu;
    Scheduler sched;

    // Two fibers serialize writes: A writes 0..4, B writes 10..14.
    // Interleaved without mutex they'd mix; with mutex each run of 5 is atomic.
    sched.spawn([&] {
        mu.lock();
        for (int i = 0; i < 5; ++i) {
            log.push_back(i);
            Scheduler::current()->yield();
        }
        mu.unlock();
    }, "A");

    sched.spawn([&] {
        mu.lock();
        for (int i = 10; i < 15; ++i) {
            log.push_back(i);
            Scheduler::current()->yield();
        }
        mu.unlock();
    }, "B");

    sched.run();

    LOGOS_ASSERT(log.size() == 10, "REACTOR-SYNC-M02a",
                 "Expected 10 log entries, got {}", log.size());
    // First 5 must be either all A's (0..4) or all B's (10..14).
    bool a_first = (log[0] == 0);
    for (int i = 0; i < 5; ++i) {
        int expected = a_first ? i : (10 + i);
        LOGOS_ASSERT(log[i] == expected, "REACTOR-SYNC-M02b",
                     "Mutex contention: log[{}]={} expected {}", i, log[i], expected);
    }
    LOGOS_TRACE("reactor.sync.mutex.contention", "ok", "");
    std::println("  [ok] test_mutex_contention");
}

static void test_try_lock() {
    LOGOS_TRACE("reactor.sync.mutex.try_lock", "start", "");
    Mutex mu;
    Scheduler sched;
    bool try_succeeded = false;
    bool try_failed    = false;

    sched.spawn([&] {
        // Lock is free — try_lock should succeed.
        try_succeeded = mu.try_lock();
        // Lock is held by us — second try_lock should fail.
        try_failed = !mu.try_lock();
        mu.unlock();
    });
    sched.run();

    LOGOS_ASSERT(try_succeeded, "REACTOR-SYNC-M03a",
                 "try_lock should succeed on free mutex");
    LOGOS_ASSERT(try_failed, "REACTOR-SYNC-M03b",
                 "try_lock should fail on held mutex");
    LOGOS_TRACE("reactor.sync.mutex.try_lock", "ok", "");
    std::println("  [ok] test_try_lock");
}

// ---------------------------------------------------------------------------
// ConditionVariable tests
// ---------------------------------------------------------------------------

static void test_cv_producer_consumer() {
    LOGOS_TRACE("reactor.sync.cv.prodcon", "start", "");
    std::vector<int> results;
    Mutex mu;
    ConditionVariable cv;
    std::vector<int> queue;

    Scheduler sched;

    // Producer: sends 1..5 with a delay between each.
    sched.spawn([&] {
        for (int i = 1; i <= 5; ++i) {
            mu.lock();
            queue.push_back(i);
            cv.notify_one();
            mu.unlock();
            Scheduler::current()->yield();
        }
        // Sentinel: push -1 to signal done.
        mu.lock();
        queue.push_back(-1);
        cv.notify_one();
        mu.unlock();
    }, "producer");

    // Consumer: reads until sentinel.
    sched.spawn([&] {
        while (true) {
            mu.lock();
            cv.wait(mu, [&]{ return !queue.empty(); });
            int v = queue.front();
            queue.erase(queue.begin());
            mu.unlock();
            if (v == -1) break;
            results.push_back(v);
        }
    }, "consumer");

    sched.run();

    LOGOS_ASSERT(results.size() == 5, "REACTOR-SYNC-CV01a",
                 "Expected 5 results, got {}", results.size());
    for (int i = 0; i < 5; ++i)
        LOGOS_ASSERT(results[i] == i + 1, "REACTOR-SYNC-CV01b",
                     "results[{}]={} expected {}", i, results[i], i + 1);
    LOGOS_TRACE("reactor.sync.cv.prodcon", "ok", "");
    std::println("  [ok] test_cv_producer_consumer");
}

static void test_cv_notify_all() {
    LOGOS_TRACE("reactor.sync.cv.notify_all", "start", "");
    Mutex mu;
    ConditionVariable cv;
    bool ready = false;
    int woken = 0;
    Scheduler sched;

    // 5 waiter fibers.
    for (int i = 0; i < 5; ++i) {
        sched.spawn([&] {
            mu.lock();
            cv.wait(mu, [&]{ return ready; });
            ++woken;
            mu.unlock();
        }, "waiter");
    }

    // Notifier fiber: sets ready and wakes all.
    sched.spawn([&] {
        Scheduler::current()->yield();  // let waiters block first
        mu.lock();
        ready = true;
        cv.notify_all();
        mu.unlock();
    }, "notifier");

    sched.run();

    LOGOS_ASSERT(woken == 5, "REACTOR-SYNC-CV02",
                 "Expected 5 fibers woken by notify_all, got {}", woken);
    LOGOS_TRACE("reactor.sync.cv.notify_all", "ok", "");
    std::println("  [ok] test_cv_notify_all");
}

// ---------------------------------------------------------------------------
// Channel tests
// ---------------------------------------------------------------------------

static void test_channel_unbounded() {
    LOGOS_TRACE("reactor.sync.channel.unbounded", "start", "");
    Channel<int> ch;  // unbounded
    Scheduler sched;
    std::vector<int> received;

    sched.spawn([&] {
        for (int i = 0; i < 10; ++i)
            ch.send(i);
    }, "producer");

    sched.spawn([&] {
        for (int i = 0; i < 10; ++i)
            received.push_back(ch.recv());
    }, "consumer");

    sched.run();

    LOGOS_ASSERT(received.size() == 10, "REACTOR-SYNC-CH01a",
                 "Expected 10, got {}", received.size());
    for (int i = 0; i < 10; ++i)
        LOGOS_ASSERT(received[i] == i, "REACTOR-SYNC-CH01b",
                     "received[{}]={} expected {}", i, received[i], i);
    LOGOS_TRACE("reactor.sync.channel.unbounded", "ok", "");
    std::println("  [ok] test_channel_unbounded");
}

static void test_channel_bounded() {
    LOGOS_TRACE("reactor.sync.channel.bounded", "start", "");
    Channel<int> ch(2);  // only 2 slots
    Scheduler sched;
    std::vector<int> order;

    // Producer tries to send 5 values into a 2-slot channel.
    // It will block after the 2nd send until consumer drains.
    sched.spawn([&] {
        for (int i = 0; i < 5; ++i) {
            order.push_back(-(i + 1));  // negative = "sent i"
            ch.send(i);
        }
    }, "producer");

    sched.spawn([&] {
        for (int i = 0; i < 5; ++i) {
            int v = ch.recv();
            order.push_back(v + 1);  // positive = "received i"
        }
    }, "consumer");

    sched.run();

    LOGOS_ASSERT(order.size() == 10, "REACTOR-SYNC-CH02a",
                 "Expected 10 order events, got {}", order.size());
    // Verify all values were sent and received (sum check).
    int sent_sum     = 0;
    int received_sum = 0;
    for (int x : order) {
        if (x < 0) sent_sum     += (-x - 1);   // encoded as -(i+1)
        else        received_sum += (x - 1);    // encoded as i+1
    }
    LOGOS_ASSERT(sent_sum == 10, "REACTOR-SYNC-CH02b",
                 "Sent sum wrong: {}", sent_sum);
    LOGOS_ASSERT(received_sum == 10, "REACTOR-SYNC-CH02c",
                 "Received sum wrong: {}", received_sum);
    LOGOS_TRACE("reactor.sync.channel.bounded", "ok", "");
    std::println("  [ok] test_channel_bounded");
}

static void test_channel_fan_out() {
    LOGOS_TRACE("reactor.sync.channel.fanout", "start", "");
    Channel<int> ch(0);  // unbounded
    Scheduler sched;
    std::vector<int> received;

    // 4 consumer fibers, 1 producer sends 20 items.
    constexpr int N = 4, M = 20;
    for (int i = 0; i < N; ++i) {
        sched.spawn([&] {
            for (int j = 0; j < M / N; ++j)
                received.push_back(ch.recv());
        }, "consumer");
    }

    sched.spawn([&] {
        for (int i = 0; i < M; ++i)
            ch.send(i);
    }, "producer");

    sched.run();

    LOGOS_ASSERT(received.size() == (size_t)M, "REACTOR-SYNC-CH03a",
                 "Expected {} received, got {}", M, received.size());
    int sum = std::accumulate(received.begin(), received.end(), 0);
    LOGOS_ASSERT(sum == M * (M - 1) / 2, "REACTOR-SYNC-CH03b",
                 "Sum mismatch: got {}, expected {}", sum, M * (M - 1) / 2);
    LOGOS_TRACE("reactor.sync.channel.fanout", "ok", "");
    std::println("  [ok] test_channel_fan_out");
}

static void test_try_send_recv() {
    LOGOS_TRACE("reactor.sync.channel.trysend", "start", "");
    Channel<int> ch(2);
    Scheduler sched;

    sched.spawn([&] {
        LOGOS_ASSERT(ch.try_send(1), "REACTOR-SYNC-CH04a", "try_send slot 1 failed");
        LOGOS_ASSERT(ch.try_send(2), "REACTOR-SYNC-CH04b", "try_send slot 2 failed");
        LOGOS_ASSERT(!ch.try_send(3), "REACTOR-SYNC-CH04c", "try_send on full should fail");

        auto v1 = ch.try_recv();
        LOGOS_ASSERT(v1.has_value() && *v1 == 1, "REACTOR-SYNC-CH04d",
                     "try_recv expected 1, got {}", v1.value_or(-999));
        auto v2 = ch.try_recv();
        LOGOS_ASSERT(v2.has_value() && *v2 == 2, "REACTOR-SYNC-CH04e",
                     "try_recv expected 2, got {}", v2.value_or(-999));
        auto v3 = ch.try_recv();
        LOGOS_ASSERT(!v3.has_value(), "REACTOR-SYNC-CH04f", "try_recv on empty should return nullopt");
    });
    sched.run();

    LOGOS_TRACE("reactor.sync.channel.trysend", "ok", "");
    std::println("  [ok] test_try_send_recv");
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main() {
    std::println("=== reactor sync exerciser (Layer 2 — mutex / cv / channel) ===");

    std::println("--- Mutex ---");
    test_mutex_basic();
    test_mutex_contention();
    test_try_lock();

    std::println("--- ConditionVariable ---");
    test_cv_producer_consumer();
    test_cv_notify_all();

    std::println("--- Channel ---");
    test_channel_unbounded();
    test_channel_bounded();
    test_channel_fan_out();
    test_try_send_recv();

    std::println("=== all tests passed ===");
    return 0;
}
