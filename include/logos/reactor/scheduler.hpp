// Logos project — https://github.com/victor-smirnov/logos

#pragma once

#include <logos/reactor/fiber.hpp>

#include <deque>
#include <memory>
#include <vector>

namespace logos::reactor {

// ---------------------------------------------------------------------------
// Scheduler — single-core cooperative fiber scheduler.
//
// One Scheduler lives per OS thread (reactor core). It owns all fibers
// spawned on that core and runs them cooperatively.
// ---------------------------------------------------------------------------
class Scheduler {
public:
    explicit Scheduler(size_t stack_size = Fiber::kDefaultStackSize) noexcept;
    ~Scheduler();

    Fiber* spawn(std::move_only_function<void()> fn,
                 std::string_view                name       = "",
                 size_t                          stack_size = Fiber::kDefaultStackSize) noexcept;

    void run() noexcept;
    bool step();

    static void install(Scheduler* s) noexcept;
    static void uninstall() noexcept;

    // --- Fiber API (called from within a running fiber) ---
    void yield() noexcept;
    void join(Fiber* target) noexcept;
    void block() noexcept;

    void wake(Fiber* fiber) noexcept;

    static Scheduler* current() noexcept;

    Fiber* running() const noexcept { return running_; }
    bool   has_work() const noexcept { return !run_queue_.empty(); }

private:
    friend class Fiber;
    friend class Reactor;

    void switch_to(Fiber* next) noexcept;
    void fiber_done(Fiber* fiber) noexcept;

    FiberRegs sched_regs_{};

    Fiber*             running_   = nullptr;
    std::deque<Fiber*> run_queue_;
    std::vector<std::unique_ptr<Fiber>> fibers_;

    StackPool pool_;
};

} // namespace logos::reactor
