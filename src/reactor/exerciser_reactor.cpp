// Logos project — https://github.com/victor-smirnov/logos
//
// Layer 1 exerciser: Reactor + io_uring sleep_for.

#include <logos/reactor/reactor.hpp>
#include <logos/verification/assert.hpp>
#include <logos/verification/trace.hpp>

#include <chrono>
#include <print>
#include <vector>

using namespace logos::reactor;
using namespace std::chrono_literals;

static void test_sleep_basic() {
    LOGOS_TRACE("reactor.sleep.basic", "start", "");
    bool after_sleep = false;
    Reactor reactor;
    reactor.spawn([&after_sleep]() {
        Reactor::current()->sleep_for(10ms);
        after_sleep = true;
    }, "sleeper");
    reactor.run();
    LOGOS_ASSERT(after_sleep, "REACTOR-SLEEP-T01",
                 "Fiber did not resume after sleep_for");
    LOGOS_TRACE("reactor.sleep.basic", "ok", "");
    std::println("  [ok] test_sleep_basic");
}

static void test_sleep_ordering() {
    LOGOS_TRACE("reactor.sleep.ordering", "start", "");
    std::vector<int> order;
    Reactor reactor;

    reactor.spawn([&order]() {
        Reactor::current()->sleep_for(50ms);
        order.push_back(2);
    }, "slow");

    reactor.spawn([&order]() {
        Reactor::current()->sleep_for(10ms);
        order.push_back(1);
    }, "fast");

    reactor.run();

    LOGOS_ASSERT(order.size() == 2, "REACTOR-SLEEP-T02a",
                 "Expected 2 completions, got {}", order.size());
    LOGOS_ASSERT(order[0] == 1 && order[1] == 2, "REACTOR-SLEEP-T02b",
                 "Fast fiber should complete before slow: got {}, {}",
                 order[0], order[1]);
    LOGOS_TRACE("reactor.sleep.ordering", "ok", "");
    std::println("  [ok] test_sleep_ordering");
}

static void test_mixed_compute_and_sleep() {
    LOGOS_TRACE("reactor.sleep.mixed", "start", "");
    int compute_count = 0;
    bool sleep_done   = false;
    Reactor reactor;

    for (int i = 0; i < 5; ++i) {
        reactor.spawn([&compute_count]() {
            for (int j = 0; j < 3; ++j) {
                ++compute_count;
                Scheduler::current()->yield();
            }
        }, "compute");
    }

    reactor.spawn([&sleep_done]() {
        Reactor::current()->sleep_for(20ms);
        sleep_done = true;
    }, "sleeper");

    reactor.run();

    LOGOS_ASSERT(compute_count == 15, "REACTOR-SLEEP-T03a",
                 "Expected 15 compute increments, got {}", compute_count);
    LOGOS_ASSERT(sleep_done, "REACTOR-SLEEP-T03b",
                 "Sleep fiber did not complete");
    LOGOS_TRACE("reactor.sleep.mixed", "ok", "");
    std::println("  [ok] test_mixed_compute_and_sleep");
}

static void test_sequential_sleeps() {
    LOGOS_TRACE("reactor.sleep.sequential", "start", "");
    int steps = 0;
    Reactor reactor;
    reactor.spawn([&steps]() {
        for (int i = 0; i < 3; ++i) {
            Reactor::current()->sleep_for(10ms);
            ++steps;
        }
    }, "multi-sleep");
    reactor.run();
    LOGOS_ASSERT(steps == 3, "REACTOR-SLEEP-T04",
                 "Expected 3 sleep steps, got {}", steps);
    LOGOS_TRACE("reactor.sleep.sequential", "ok", "");
    std::println("  [ok] test_sequential_sleeps");
}

int main() {
    std::println("=== reactor exerciser (Layer 1 — io_uring sleep) ===");
    test_sleep_basic();
    test_sleep_ordering();
    test_mixed_compute_and_sleep();
    test_sequential_sleeps();
    std::println("=== all tests passed ===");
    return 0;
}
