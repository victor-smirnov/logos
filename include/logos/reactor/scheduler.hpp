// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
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
//
// Usage pattern:
//
//   Scheduler sched;
//   sched.spawn([] { ... });
//   sched.run();           // blocks until all fibers complete
//
// Inside a fiber:
//   Scheduler::current()->yield();
//   auto* f = Scheduler::current()->spawn([] { ... });
//   Scheduler::current()->join(f);
// ---------------------------------------------------------------------------
class Scheduler {
public:
    Scheduler();
    ~Scheduler();

    // Spawn a new fiber. The fiber is added to the run queue immediately.
    // Returns a raw pointer — the Scheduler owns the Fiber.
    Fiber* spawn(std::move_only_function<void()> fn,
                 std::string_view     name       = "",
                 size_t               stack_size = Fiber::kDefaultStackSize);

    // Run all fibers until the run queue is empty and no fibers are blocked.
    // Returns when all work is done.
    void run();

    // Run exactly one ready fiber from the queue.  Returns true if a fiber
    // was run, false if the queue was empty.  Used by Reactor::run().
    bool step();

    // Install/uninstall this scheduler as the current one for this thread.
    // Called by Reactor::run() before/after the event loop so that
    // Scheduler::current() works inside fibers during reactor operation.
    static void install(Scheduler* s) noexcept;
    static void uninstall() noexcept;

    // --- Fiber API (called from within a running fiber) ---

    // Yield execution back to the scheduler.  The calling fiber is put at the
    // back of the run queue and will resume on the next scheduler round.
    void yield();

    // Block the calling fiber until 'target' completes.  If 'target' is
    // already Done, returns immediately.
    void join(Fiber* target);

    // Block the calling fiber without re-queuing it.
    // The fiber stays Blocked until someone calls wake() on it.
    // Used by Mutex::lock(), Channel::recv()/send(), etc.
    void block();

    // Wake a blocked fiber (called from IO completion or channel).
    // Safe to call from the scheduler loop (not from another fiber).
    void wake(Fiber* fiber);

    // The Scheduler currently running on this OS thread (thread-local).
    static Scheduler* current() noexcept;

    // The fiber currently executing on this scheduler (null in scheduler loop).
    Fiber* running() const noexcept { return running_; }

    bool has_work() const noexcept { return !run_queue_.empty(); }

private:
    friend class Fiber;
    friend class Reactor;

    // Switch from 'running_' (or scheduler loop) to 'next'.
    void switch_to(Fiber* next);

    // Called by Fiber::finish() — marks fiber Done, wakes any joiner,
    // switches back to scheduler loop.
    void fiber_done(Fiber* fiber);

    // Scheduler's own "fiber" context (the OS thread's original stack).
    FiberRegs sched_regs_{};

    Fiber*             running_   = nullptr;
    std::deque<Fiber*> run_queue_;

    // All spawned fibers (owned).
    std::vector<std::unique_ptr<Fiber>> fibers_;
};

} // namespace logos::reactor
