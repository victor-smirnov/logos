// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos
//
// Layer 0 exerciser: fiber context switch, spawn, yield, join.

#include <logos/reactor/scheduler.hpp>
#include <logos/verification/assert.hpp>
#include <logos/verification/trace.hpp>

#include <print>
#include <vector>

using namespace logos::reactor;

static void test_single_fiber() {
    LOGOS_TRACE("reactor.fibers.single", "start", "");
    bool ran = false;
    Scheduler sched;
    sched.spawn([&ran]() { ran = true; });
    sched.run();
    LOGOS_ASSERT(ran, "REACTOR-FIBER-T01", "Single fiber did not run");
    LOGOS_TRACE("reactor.fibers.single", "ok", "");
    std::println("  [ok] test_single_fiber");
}

static void test_fifo_order() {
    LOGOS_TRACE("reactor.fibers.fifo", "start", "");
    std::vector<int> order;
    Scheduler sched;
    for (int i = 0; i < 5; ++i)
        sched.spawn([&order, i]() { order.push_back(i); });
    sched.run();
    LOGOS_ASSERT(order.size() == 5, "REACTOR-FIBER-T02a",
                 "Expected 5 fibers, got {}", order.size());
    for (int i = 0; i < 5; ++i)
        LOGOS_ASSERT(order[i] == i, "REACTOR-FIBER-T02b",
                     "FIFO order violated at index {}: got {}", i, order[i]);
    LOGOS_TRACE("reactor.fibers.fifo", "ok", "");
    std::println("  [ok] test_fifo_order");
}

static void test_yield_interleave() {
    LOGOS_TRACE("reactor.fibers.yield", "start", "");
    std::vector<int> log;
    Scheduler sched;

    sched.spawn([&log]() {
        log.push_back(0);
        Scheduler::current()->yield();
        log.push_back(2);
    }, "A");

    sched.spawn([&log]() {
        log.push_back(1);
        Scheduler::current()->yield();
        log.push_back(3);
    }, "B");

    sched.run();

    LOGOS_ASSERT(log.size() == 4, "REACTOR-FIBER-T03a",
                 "Expected 4 log entries, got {}", log.size());
    LOGOS_ASSERT(log[0] == 0 && log[1] == 1 && log[2] == 2 && log[3] == 3,
                 "REACTOR-FIBER-T03b",
                 "Yield interleave wrong: {},{},{},{}",
                 log[0], log[1], log[2], log[3]);
    LOGOS_TRACE("reactor.fibers.yield", "ok", "");
    std::println("  [ok] test_yield_interleave");
}

static void test_join() {
    LOGOS_TRACE("reactor.fibers.join", "start", "");
    std::vector<int> log;
    Scheduler sched;
    Fiber* child = nullptr;

    sched.spawn([&log, &child, &sched]() {
        child = sched.spawn([&log]() {
            log.push_back(1);
        }, "child");

        Scheduler::current()->yield();
        Scheduler::current()->join(child);
        log.push_back(2);
    }, "parent");

    sched.run();

    LOGOS_ASSERT(log.size() == 2, "REACTOR-FIBER-T04a",
                 "Expected 2 log entries, got {}", log.size());
    LOGOS_ASSERT(log[0] == 1, "REACTOR-FIBER-T04b",
                 "Child must run before parent resumes from join");
    LOGOS_ASSERT(log[1] == 2, "REACTOR-FIBER-T04c",
                 "Parent must log 2 after join");
    LOGOS_TRACE("reactor.fibers.join", "ok", "");
    std::println("  [ok] test_join");
}

static void test_nested_spawn() {
    LOGOS_TRACE("reactor.fibers.nested", "start", "");
    int count = 0;
    Scheduler sched;

    sched.spawn([&count, &sched]() {
        for (int i = 0; i < 10; ++i)
            sched.spawn([&count]() { ++count; });
        Scheduler::current()->yield();
    }, "root");

    sched.run();

    LOGOS_ASSERT(count == 10, "REACTOR-FIBER-T05",
                 "Expected 10 nested fibers to run, got {}", count);
    LOGOS_TRACE("reactor.fibers.nested", "ok", "");
    std::println("  [ok] test_nested_spawn");
}

static void test_many_fibers() {
    LOGOS_TRACE("reactor.fibers.many", "start", "");
    constexpr int N = 1000;
    int count = 0;
    Scheduler sched;
    for (int i = 0; i < N; ++i)
        sched.spawn([&count]() { ++count; });
    sched.run();
    LOGOS_ASSERT(count == N, "REACTOR-FIBER-T06",
                 "Expected {} fibers, got {}", N, count);
    LOGOS_TRACE("reactor.fibers.many", "ok", "");
    std::println("  [ok] test_many_fibers ({} fibers)", N);
}

static void recursive_fiber(Scheduler& sched, int depth, int& counter) {
    ++counter;
    if (depth > 0) {
        Fiber* child = sched.spawn([&sched, depth, &counter]() {
            recursive_fiber(sched, depth - 1, counter);
        });
        Scheduler::current()->join(child);
    }
}

static void test_deep_recursion() {
    LOGOS_TRACE("reactor.fibers.deep", "start", "");
    int counter = 0;
    Scheduler sched;
    sched.spawn([&sched, &counter]() {
        recursive_fiber(sched, 50, counter);
    }, "root");
    sched.run();
    LOGOS_ASSERT(counter == 51, "REACTOR-FIBER-T07",
                 "Expected 51 recursive calls, got {}", counter);
    LOGOS_TRACE("reactor.fibers.deep", "ok", "");
    std::println("  [ok] test_deep_recursion (depth 50)");
}

int main() {
    std::println("=== reactor fiber exerciser (Layer 0) ===");
    test_single_fiber();
    test_fifo_order();
    test_yield_interleave();
    test_join();
    test_nested_spawn();
    test_many_fibers();
    test_deep_recursion();
    std::println("=== all tests passed ===");
    return 0;
}
