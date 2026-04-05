// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos

#include <logos/reactor/scheduler.hpp>
#include <logos/verification/assert.hpp>

namespace logos::reactor {

// ---------------------------------------------------------------------------
// Thread-local state
// ---------------------------------------------------------------------------
static thread_local Scheduler* tl_current_scheduler = nullptr;

#if LOGOS_HAS_GREEN_STACKS
// Used by green_bridge.cpp for __morestack → StackChain lookup.
thread_local Fiber* tls_current_fiber = nullptr;

// Jenny's TLS slot for the system (red) stack pointer — defined in green_bridge.cpp.
// Scheduler::switch_to() writes the current RSP here before fiber_switch so that
// [[clang::green]] → [[clang::red]] calls inside the fiber switch to this stack.
extern "C" __thread void* __green_fiber_system_stack;
#endif

Scheduler* Scheduler::current() noexcept { return tl_current_scheduler; }

void Scheduler::install(Scheduler* s) noexcept   { tl_current_scheduler = s; }
void Scheduler::uninstall() noexcept             { tl_current_scheduler = nullptr; }

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------
Scheduler::Scheduler(size_t stack_size) noexcept
#if !LOGOS_HAS_GREEN_STACKS
    : pool_(stack_size)
#endif
{
    (void)stack_size;  // suppress unused-parameter warning in green mode
}

Scheduler::~Scheduler() = default;

// ---------------------------------------------------------------------------
// spawn
// ---------------------------------------------------------------------------
#if LOGOS_HAS_GREEN_STACKS
Fiber* Scheduler::spawn(GreenFn fn,
                        std::string_view name,
                        size_t           /*stack_size*/) noexcept
{
    auto fiber = std::make_unique<Fiber>(std::move(fn), name);
    Fiber* raw = fiber.get();
    raw->scheduler_ = this;
    raw->state_     = FiberState::Ready;
    run_queue_.push_back(raw);
    fibers_.push_back(std::move(fiber));
    return raw;
}
#else
Fiber* Scheduler::spawn(std::move_only_function<void()> fn,
                        std::string_view name,
                        size_t           /*stack_size*/) noexcept
{
    auto fiber = std::make_unique<Fiber>(std::move(fn), name, &pool_);
    Fiber* raw = fiber.get();
    raw->scheduler_ = this;
    raw->state_     = FiberState::Ready;
    run_queue_.push_back(raw);
    fibers_.push_back(std::move(fiber));
    return raw;
}
#endif

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
// run
// ---------------------------------------------------------------------------
void Scheduler::run() noexcept {
    LOGOS_ASSERT(tl_current_scheduler == nullptr, "REACTOR-SCHED-001",
                 "Scheduler::run() called on a thread that already has a scheduler");
    install(this);
    while (step()) {}
    uninstall();
}

// ---------------------------------------------------------------------------
// yield
// ---------------------------------------------------------------------------
void Scheduler::yield() noexcept {
    Fiber* self = running_;
    LOGOS_ASSERT(self != nullptr, "REACTOR-SCHED-010",
                 "Scheduler::yield() called outside a fiber");
    LOGOS_ASSERT(self->state_ == FiberState::Running, "REACTOR-SCHED-011",
                 "Fiber '{}' yielding but state is not Running", self->name_);

    self->state_ = FiberState::Ready;
    run_queue_.push_back(self);
    running_ = nullptr;

    fiber_switch(&self->regs_, &sched_regs_);
    self->state_ = FiberState::Running;
}

// ---------------------------------------------------------------------------
// join
// ---------------------------------------------------------------------------
void Scheduler::join(Fiber* target) noexcept {
    LOGOS_ASSERT(target != nullptr, "REACTOR-SCHED-020",
                 "Scheduler::join() called with null fiber");
    if (target->state_ == FiberState::Done) return;

    Fiber* self = running_;
    LOGOS_ASSERT(self != nullptr, "REACTOR-SCHED-021",
                 "Scheduler::join() called outside a fiber");
    LOGOS_ASSERT(target->join_waiter_ == nullptr, "REACTOR-SCHED-022",
                 "Fiber '{}' already has a joiner", target->name_);

    target->join_waiter_ = self;
    self->state_ = FiberState::Blocked;
    running_ = nullptr;

    fiber_switch(&self->regs_, &sched_regs_);
    self->state_ = FiberState::Running;
}

// ---------------------------------------------------------------------------
// block
// ---------------------------------------------------------------------------
void Scheduler::block() noexcept {
    Fiber* self = running_;
    LOGOS_ASSERT(self != nullptr, "REACTOR-SCHED-060",
                 "Scheduler::block() called outside a fiber");
    LOGOS_ASSERT(self->state_ == FiberState::Running, "REACTOR-SCHED-061",
                 "Fiber '{}' blocking but state is not Running", self->name_);

    self->state_ = FiberState::Blocked;
    running_ = nullptr;

    fiber_switch(&self->regs_, &sched_regs_);
    self->state_ = FiberState::Running;
}

// ---------------------------------------------------------------------------
// wake
// ---------------------------------------------------------------------------
void Scheduler::wake(Fiber* fiber) noexcept {
    LOGOS_ASSERT(fiber != nullptr, "REACTOR-SCHED-030",
                 "Scheduler::wake() called with null fiber");
    LOGOS_ASSERT(fiber->state_ == FiberState::Blocked, "REACTOR-SCHED-031",
                 "Scheduler::wake(): fiber '{}' is not Blocked (state={})",
                 fiber->name_, static_cast<int>(fiber->state_));

    fiber->state_ = FiberState::Ready;
    run_queue_.push_back(fiber);
}

// ---------------------------------------------------------------------------
// switch_to — context-switch from the scheduler loop into a fiber.
// Sets tls_current_fiber (green mode) so that __morestack can locate the
// fiber's StackChain via green_bridge.cpp.
// ---------------------------------------------------------------------------
void Scheduler::switch_to(Fiber* next) noexcept {
    LOGOS_ASSERT(next != nullptr, "REACTOR-SCHED-040",
                 "switch_to() called with null fiber");
    LOGOS_ASSERT(next->state_ == FiberState::Ready, "REACTOR-SCHED-041",
                 "switch_to(): fiber '{}' is not Ready (state={})",
                 next->name_, static_cast<int>(next->state_));

    next->state_ = FiberState::Running;
    running_ = next;

#if LOGOS_HAS_GREEN_STACKS
    tls_current_fiber = next;
    // Point __green_fiber_system_stack well below the current frame.
    // Jenny 19 switches RSP to this value for green→red calls inside the fiber.
    // Red calls then grow downward.  Without enough headroom they overwrite
    // the caller's stack (switch_to / step / run / sched_regs_).
    void* rsp;
    asm volatile("movq %%rsp, %0" : "=r"(rsp));
    __green_fiber_system_stack = static_cast<char*>(rsp) - 65536;
#endif

    fiber_switch_red(&sched_regs_, &next->regs_);

    // Returns here after the fiber yields or finishes.
#if LOGOS_HAS_GREEN_STACKS
    tls_current_fiber = nullptr;
#endif
}

// ---------------------------------------------------------------------------
// fiber_done
// ---------------------------------------------------------------------------
void Scheduler::fiber_done(Fiber* fiber) noexcept {
    LOGOS_ASSERT(fiber != nullptr, "REACTOR-SCHED-050",
                 "fiber_done() called with null fiber");

    fiber->state_ = FiberState::Done;
    running_ = nullptr;

    if (fiber->join_waiter_) {
        wake(fiber->join_waiter_);
        fiber->join_waiter_ = nullptr;
    }

    fiber_switch(&fiber->regs_, &sched_regs_);
    // __builtin_unreachable() not usable in [[clang::green]] context (Jenny 19).
    for (;;) {}  // fiber_done's fiber_switch never returns here
}

} // namespace logos::reactor
