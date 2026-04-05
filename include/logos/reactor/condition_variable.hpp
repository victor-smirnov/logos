// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos
//
// Fiber-aware condition variable.
//
// Cooperative single-core only (no OS synchronization).
//
// Usage:
//   Mutex mu;
//   ConditionVariable cv;
//   // Producer fiber:
//   mu.lock();
//   queue.push(item);
//   cv.notify_one();
//   mu.unlock();
//   // Consumer fiber:
//   mu.lock();
//   cv.wait(mu, [&]{ return !queue.empty(); });
//   auto item = queue.front(); queue.pop_front();
//   mu.unlock();

#pragma once

#include <logos/reactor/mutex.hpp>

#include <deque>

namespace logos::reactor {

class ConditionVariable {
public:
    ConditionVariable()  = default;
    ~ConditionVariable() = default;

    ConditionVariable(const ConditionVariable&)            = delete;
    ConditionVariable& operator=(const ConditionVariable&) = delete;

    // Unlock 'mutex', suspend this fiber, then re-lock 'mutex' on wake.
    // Must be called with 'mutex' already locked.
    void wait(Mutex& mutex) noexcept {
        Scheduler* s = Scheduler::current();
        LOGOS_ASSERT(s && s->running(), "REACTOR-CV-001",
                     "ConditionVariable::wait() called outside a fiber");
        LOGOS_ASSERT(mutex.is_locked(), "REACTOR-CV-002",
                     "ConditionVariable::wait() called with unlocked mutex");

        waiters_.push_back(s->running());
        mutex.unlock();
        s->block();
        mutex.lock();
    }

    // Wait until predicate() returns true (spurious wake resilient).
    template<typename Predicate>
    void wait(Mutex& mutex, Predicate predicate) noexcept {
        while (!predicate())
            wait(mutex);
    }

    // Wake one waiting fiber.
    void notify_one() noexcept {
        if (waiters_.empty()) return;
        Fiber* f = waiters_.front();
        waiters_.pop_front();
        Scheduler::current()->wake(f);
    }

    // Wake all waiting fibers.
    void notify_all() noexcept {
        Scheduler* s = Scheduler::current();
        while (!waiters_.empty()) {
            Fiber* f = waiters_.front();
            waiters_.pop_front();
            s->wake(f);
        }
    }

private:
    std::deque<Fiber*> waiters_;
};

} // namespace logos::reactor
