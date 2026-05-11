//
// Layer 8 exerciser: cross-reactor submit_to with P2P SPSC queues.

#include <logos/reactor/reactor_engine.hpp>
#include <logos/reactor/submit_to.hpp>
#include <logos/verification/assert.hpp>
#include <logos/verification/trace.hpp>

#include <chrono>
#include <print>
#include <thread>
#include <vector>

using namespace logos::reactor;
using namespace std::chrono_literals;

static void test_submit_to_basic() {
    LOGOS_TRACE("cross.basic", "start", "");

    ReactorEngine engine(2);
    std::atomic<bool> ran_on_target{false};
    int result_value = 0;

    engine.reactor(0).spawn([&]() {
        auto result = submit_to(engine.reactor(1),
            [&ran_on_target]() noexcept -> logos::expected<int> {
                ran_on_target.store(true, std::memory_order_relaxed);
                return 42;
            });
        result_value = result.get();
    }, "caller");

    engine.reactor(1).spawn([]() {
        Reactor::current()->sleep_for(500ms);
    }, "keepalive");

    std::jthread t1([&] { engine.reactor(1).run(); });
    engine.reactor(0).run();
    engine.reactor(1).stop();
    t1.join();

    LOGOS_ASSERT(ran_on_target.load(), "CROSS-T01a",
                 "Callable did not run on target reactor");
    LOGOS_ASSERT(result_value == 42, "CROSS-T01b",
                 "Expected 42, got {}", result_value);
    LOGOS_TRACE("cross.basic", "ok", "");
    std::println("  [ok] test_submit_to_basic");
}

static void test_bidirectional() {
    LOGOS_TRACE("cross.bidi", "start", "");

    ReactorEngine engine(2);
    std::atomic<int> sum{0};

    engine.reactor(0).spawn([&]() {
        auto v = submit_to(engine.reactor(1),
            []() noexcept -> logos::expected<int> { return 10; });
        sum.fetch_add(v.get(), std::memory_order_relaxed);
    }, "r0-caller");

    engine.reactor(1).spawn([&]() {
        auto v = submit_to(engine.reactor(0),
            []() noexcept -> logos::expected<int> { return 20; });
        sum.fetch_add(v.get(), std::memory_order_relaxed);
    }, "r1-caller");

    engine.reactor(0).spawn([]() { Reactor::current()->sleep_for(500ms); }, "ka0");
    engine.reactor(1).spawn([]() { Reactor::current()->sleep_for(500ms); }, "ka1");

    std::jthread t1([&] { engine.reactor(1).run(); });
    engine.reactor(0).run();
    engine.reactor(1).stop();
    t1.join();

    LOGOS_ASSERT(sum.load() == 30, "CROSS-T02",
                 "Expected sum 30, got {}", sum.load());
    LOGOS_TRACE("cross.bidi", "ok", "");
    std::println("  [ok] test_bidirectional");
}

static void test_fan_out() {
    LOGOS_TRACE("cross.fanout", "start", "");

    constexpr size_t N = 4;
    ReactorEngine engine(N);
    std::atomic<int> total{0};

    engine.reactor(0).spawn([&]() {
        for (size_t i = 1; i < N; ++i) {
            auto v = submit_to(engine.reactor(i),
                [i]() noexcept -> logos::expected<int> {
                    return static_cast<int>(i * 100);
                });
            total.fetch_add(v.get(), std::memory_order_relaxed);
        }
    }, "fan-out");

    for (size_t i = 1; i < N; ++i)
        engine.reactor(i).spawn([]() { Reactor::current()->sleep_for(500ms); }, "ka");

    std::vector<std::jthread> threads;
    for (size_t i = 1; i < N; ++i)
        threads.emplace_back([&engine, i] { engine.reactor(i).run(); });

    engine.reactor(0).run();
    for (size_t i = 1; i < N; ++i)
        engine.reactor(i).stop();

    LOGOS_ASSERT(total.load() == 600, "CROSS-T03",
                 "Expected 600, got {}", total.load());
    LOGOS_TRACE("cross.fanout", "ok", "");
    std::println("  [ok] test_fan_out");
}

static void test_burst() {
    LOGOS_TRACE("cross.burst", "start", "");

    ReactorEngine engine(2);
    constexpr int COUNT = 100;
    std::atomic<int> sum{0};

    engine.reactor(0).spawn([&]() {
        for (int i = 0; i < COUNT; ++i) {
            auto v = submit_to(engine.reactor(1),
                [i]() noexcept -> logos::expected<int> { return i; });
            sum.fetch_add(v.get(), std::memory_order_relaxed);
        }
    }, "burst");

    engine.reactor(1).spawn([]() { Reactor::current()->sleep_for(2s); }, "ka");

    std::jthread t1([&] { engine.reactor(1).run(); });
    engine.reactor(0).run();
    engine.reactor(1).stop();
    t1.join();

    int expected = COUNT * (COUNT - 1) / 2;
    LOGOS_ASSERT(sum.load() == expected, "CROSS-T04",
                 "Expected {}, got {}", expected, sum.load());
    LOGOS_TRACE("cross.burst", "ok", "");
    std::println("  [ok] test_burst");
}

int main() {
    std::println("=== cross-reactor exerciser (Layer 8 — P2P SPSC + submit_to) ===");
    test_submit_to_basic();
    test_bidirectional();
    test_fan_out();
    test_burst();
    std::println("=== all tests passed ===");
    return 0;
}
