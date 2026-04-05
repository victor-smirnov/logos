// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos
//
// Fiber-aware mutex.
//
// This is NOT a thread-safe mutex. It is designed for single-core cooperative
// schedulers where fibers run one at a time. No OS primitives are used; the
// cost of lock()/unlock() is a scheduler block/wake when contended.
//
// Usage:
//   logos::reactor::Mutex mu;
//   // inside a fiber:
//   mu.lock();
//   // ... critical section ...
//   mu.unlock();
//
// Or with RAII:
//   std::lock_guard lg(mu);  // calls mu.lock() / mu.unlock()

#pragma once

#include <logos/reactor/scheduler.hpp>
#include <logos/verification/assert.hpp>

#include <deque>

namespace logos::reactor {

class Mutex {
public:
    Mutex()  = default;
    ~Mutex() = default;

    Mutex(const Mutex&)            = delete;
    Mutex& operator=(const Mutex&) = delete;

    // Acquire the mutex. Blocks the calling fiber if already held.
    void lock() noexcept {
        if (!locked_) {
            locked_ = true;
            return;
        }
        Scheduler* s = Scheduler::current();
        LOGOS_ASSERT(s && s->running(), "REACTOR-MUTEX-001",
                     "Mutex::lock() called outside a fiber");
        waiters_.push_back(s->running());
        s->block();
        // When we return here, unlock() transferred ownership to us —
        // locked_ is still true and we are the new owner.
    }

    // Try to acquire without blocking. Returns true on success.
    bool try_lock() noexcept {
        if (!locked_) {
            locked_ = true;
            return true;
        }
        return false;
    }

    // Release the mutex. If there are waiters, transfers ownership directly
    // to the first one (locked_ stays true) and wakes it.
    void unlock() noexcept {
        LOGOS_ASSERT(locked_, "REACTOR-MUTEX-010", "Mutex::unlock() called on unlocked mutex");
        if (!waiters_.empty()) {
            Fiber* next = waiters_.front();
            waiters_.pop_front();
            // Transfer ownership: locked_ remains true — the woken fiber
            // is now the owner and will return from lock() without re-acquiring.
            Scheduler::current()->wake(next);
        } else {
            locked_ = false;
        }
    }

    bool is_locked() const noexcept { return locked_; }

private:
    bool               locked_ = false;
    std::deque<Fiber*> waiters_;
};

// RAII lock guard for fiber context.
class LockGuard {
public:
    explicit LockGuard(Mutex& m) noexcept : m_(m) { m_.lock(); }
    ~LockGuard() noexcept { m_.unlock(); }
    LockGuard(const LockGuard&)            = delete;
    LockGuard& operator=(const LockGuard&) = delete;
private:
    Mutex& m_;
};

} // namespace logos::reactor
