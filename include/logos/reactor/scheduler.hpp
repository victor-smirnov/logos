// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos

#pragma once

#include <logos/reactor/fiber.hpp>

#include <deque>
#include <memory>
#include <vector>

// ---------------------------------------------------------------------------
// Scheduler — single-core cooperative fiber scheduler.
//
// One Scheduler lives per OS thread (reactor core). It owns all fibers
// spawned on that core and runs them cooperatively.
//
// Function coloring (green mode):
//   The whole namespace is [[clang::green]] so that fiber primitives
//   (yield, block, join, fiber_done) are green by default.
//   Functions that run in the scheduler event loop (switch_to, run, step,
//   wake, install, uninstall, current) must be [[clang::red]] because they
//   execute on the system thread stack, not on a fiber stack.
// ---------------------------------------------------------------------------
LOGOS_NS_BEGIN

class Scheduler {
public:
    // stack_size: classic mode only — passed to StackPool.
    // Ignored in green mode (stacks grow dynamically).
    LOGOS_RED explicit Scheduler(size_t stack_size = Fiber::kDefaultStackSize) noexcept;
    LOGOS_RED ~Scheduler();

    // Spawn a new fiber.  Added to the run queue immediately.
    // In green mode, fn is a GreenFn (or any callable implicitly convertible to it).
    // In classic mode, fn is a move_only_function<void()>.
#if LOGOS_HAS_GREEN_STACKS
    LOGOS_RED Fiber* spawn(GreenFn fn,
                           std::string_view name       = "",
                           size_t           stack_size = Fiber::kDefaultStackSize) noexcept;
#else
    Fiber* spawn(std::move_only_function<void()> fn,
                 std::string_view                name       = "",
                 size_t                          stack_size = Fiber::kDefaultStackSize) noexcept;
#endif

    // Run all fibers until the run queue is empty.
    LOGOS_RED void run() noexcept;

    // Run exactly one ready fiber.  Returns true if a fiber ran.
    LOGOS_RED bool step();

    // Install/uninstall this scheduler as the current one for this thread.
    LOGOS_RED static void install(Scheduler* s) noexcept;
    LOGOS_RED static void uninstall() noexcept;

    // --- Fiber API (called from within a running fiber) ---
    //
    // These are green (inherited from namespace): they call fiber_switch from
    // fiber context and must run on the fiber's green stack so that
    // fiber_switch saves the correct (green) RSP.

    void yield() noexcept;
    void join(Fiber* target) noexcept;
    void block() noexcept;

    // wake() is called from the scheduler loop (red context).
    LOGOS_RED void wake(Fiber* fiber) noexcept;

    LOGOS_RED static Scheduler* current() noexcept;

    LOGOS_RED Fiber* running() const noexcept { return running_; }
    LOGOS_RED bool   has_work() const noexcept { return !run_queue_.empty(); }

private:
    friend class Fiber;
    friend class Reactor;

    // switch_to: called from scheduler loop (red), NOT green.
    LOGOS_RED void switch_to(Fiber* next) noexcept;

    // fiber_done: called from Fiber::finish (green context).
    void fiber_done(Fiber* fiber) noexcept;

    FiberRegs sched_regs_{};

    Fiber*             running_   = nullptr;
    std::deque<Fiber*> run_queue_;
    std::vector<std::unique_ptr<Fiber>> fibers_;

#if !LOGOS_HAS_GREEN_STACKS
    StackPool pool_;
#endif
};

LOGOS_NS_END
