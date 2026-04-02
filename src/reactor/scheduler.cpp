// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos

#include <logos/reactor/scheduler.hpp>
#include <logos/verification/assert.hpp>

namespace logos::reactor {

// ---------------------------------------------------------------------------
// Thread-local current scheduler
// ---------------------------------------------------------------------------
static thread_local Scheduler* tl_current_scheduler = nullptr;

Scheduler* Scheduler::current() noexcept {
    return tl_current_scheduler;
}

void Scheduler::install(Scheduler* s) noexcept {
    tl_current_scheduler = s;
}

void Scheduler::uninstall() noexcept {
    tl_current_scheduler = nullptr;
}

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------
Scheduler::Scheduler() = default;
Scheduler::~Scheduler() = default;

// ---------------------------------------------------------------------------
// spawn — create a fiber and put it in the run queue
// ---------------------------------------------------------------------------
Fiber* Scheduler::spawn(std::move_only_function<void()> fn,
                        std::string_view     name,
                        size_t               stack_size)
{
    auto fiber = std::make_unique<Fiber>(std::move(fn), name, stack_size);
    Fiber* raw = fiber.get();
    raw->scheduler_ = this;
    raw->state_     = FiberState::Ready;
    run_queue_.push_back(raw);
    fibers_.push_back(std::move(fiber));
    return raw;
}

// ---------------------------------------------------------------------------
// step — run one fiber from the run queue; returns false if queue empty
// ---------------------------------------------------------------------------
bool Scheduler::step() {
    if (run_queue_.empty()) return false;
    Fiber* next = run_queue_.front();
    run_queue_.pop_front();
    switch_to(next);
    return true;
}

// ---------------------------------------------------------------------------
// run — execute fibers until the run queue is empty
// (Reactor calls step() in its own event loop instead)
// ---------------------------------------------------------------------------
void Scheduler::run() {
    LOGOS_ASSERT(tl_current_scheduler == nullptr, "REACTOR-SCHED-001",
                 "Scheduler::run() called on a thread that already has a scheduler");

    install(this);
    while (step()) {}
    uninstall();
}

// ---------------------------------------------------------------------------
// yield — called from within a fiber
// ---------------------------------------------------------------------------
void Scheduler::yield() {
    Fiber* self = running_;
    LOGOS_ASSERT(self != nullptr, "REACTOR-SCHED-010",
                 "Scheduler::yield() called outside a fiber");
    LOGOS_ASSERT(self->state_ == FiberState::Running, "REACTOR-SCHED-011",
                 "Fiber '{}' yielding but state is not Running", self->name_);

    self->state_ = FiberState::Ready;
    run_queue_.push_back(self);
    running_ = nullptr;

    // Switch back to the scheduler loop.
    fiber_switch(&self->regs_, &sched_regs_);
    // We resume here when the scheduler picks us again.
    self->state_ = FiberState::Running;
}

// ---------------------------------------------------------------------------
// join — block the calling fiber until 'target' finishes
// ---------------------------------------------------------------------------
void Scheduler::join(Fiber* target) {
    LOGOS_ASSERT(target != nullptr, "REACTOR-SCHED-020",
                 "Scheduler::join() called with null fiber");

    if (target->state_ == FiberState::Done)
        return;

    Fiber* self = running_;
    LOGOS_ASSERT(self != nullptr, "REACTOR-SCHED-021",
                 "Scheduler::join() called outside a fiber");
    LOGOS_ASSERT(target->join_waiter_ == nullptr, "REACTOR-SCHED-022",
                 "Fiber '{}' already has a joiner", target->name_);

    target->join_waiter_ = self;
    self->state_ = FiberState::Blocked;
    running_ = nullptr;

    fiber_switch(&self->regs_, &sched_regs_);
    // Resumed by fiber_done() when target completes.
    self->state_ = FiberState::Running;
}

// ---------------------------------------------------------------------------
// block — suspend the calling fiber without re-queuing it.
// It will remain Blocked until someone explicitly calls wake(fiber).
// ---------------------------------------------------------------------------
void Scheduler::block() {
    Fiber* self = running_;
    LOGOS_ASSERT(self != nullptr, "REACTOR-SCHED-060",
                 "Scheduler::block() called outside a fiber");
    LOGOS_ASSERT(self->state_ == FiberState::Running, "REACTOR-SCHED-061",
                 "Fiber '{}' blocking but state is not Running", self->name_);

    self->state_ = FiberState::Blocked;
    running_ = nullptr;

    fiber_switch(&self->regs_, &sched_regs_);
    // Resumed here by wake() → scheduler picks this fiber from the run queue.
    self->state_ = FiberState::Running;
}

// ---------------------------------------------------------------------------
// wake — move a Blocked fiber back to the run queue
// ---------------------------------------------------------------------------
void Scheduler::wake(Fiber* fiber) {
    LOGOS_ASSERT(fiber != nullptr, "REACTOR-SCHED-030",
                 "Scheduler::wake() called with null fiber");
    LOGOS_ASSERT(fiber->state_ == FiberState::Blocked, "REACTOR-SCHED-031",
                 "Scheduler::wake(): fiber '{}' is not Blocked (state={})",
                 fiber->name_, static_cast<int>(fiber->state_));

    fiber->state_ = FiberState::Ready;
    run_queue_.push_back(fiber);
}

// ---------------------------------------------------------------------------
// switch_to — context-switch from the scheduler loop into a fiber
// ---------------------------------------------------------------------------
void Scheduler::switch_to(Fiber* next) {
    LOGOS_ASSERT(next != nullptr, "REACTOR-SCHED-040",
                 "switch_to() called with null fiber");
    LOGOS_ASSERT(next->state_ == FiberState::Ready, "REACTOR-SCHED-041",
                 "switch_to(): fiber '{}' is not Ready (state={})",
                 next->name_, static_cast<int>(next->state_));

    next->state_ = FiberState::Running;
    running_ = next;

    fiber_switch(&sched_regs_, &next->regs_);
    // Returns here after the fiber yields or finishes.
}

// ---------------------------------------------------------------------------
// fiber_done — called by Fiber::finish() when a fiber's fn() returns
// ---------------------------------------------------------------------------
void Scheduler::fiber_done(Fiber* fiber) {
    LOGOS_ASSERT(fiber != nullptr, "REACTOR-SCHED-050",
                 "fiber_done() called with null fiber");

    fiber->state_ = FiberState::Done;
    running_ = nullptr;

    // Wake any fiber waiting to join this one.
    if (fiber->join_waiter_) {
        wake(fiber->join_waiter_);
        fiber->join_waiter_ = nullptr;
    }

    // Switch back to the scheduler loop.
    fiber_switch(&fiber->regs_, &sched_regs_);
    // Unreachable — the scheduler never switches back to a Done fiber.
    __builtin_unreachable();
}

} // namespace logos::reactor
