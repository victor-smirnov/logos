// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos
//
// Fiber-aware bounded/unbounded channel (single-core cooperative).
//
// Channel<T> is a FIFO queue with blocking send/recv semantics:
//   - send(T) blocks if the channel is full (capacity > 0)
//   - recv()  blocks if the channel is empty
//
// capacity == 0 means unbounded (send never blocks).
//
// Usage:
//   Channel<int> ch(4);  // bounded, 4 slots
//   // Producer fiber:
//   ch.send(42);
//   // Consumer fiber:
//   int v = ch.recv();

#pragma once

#include <logos/reactor/scheduler.hpp>
#include <logos/verification/assert.hpp>

#include <deque>
#include <optional>

namespace logos::reactor {

template<typename T>
class Channel {
public:
    // capacity == 0 → unbounded (send() never blocks on fullness)
    explicit Channel(size_t capacity = 0) : capacity_(capacity) {}

    ~Channel() = default;

    Channel(const Channel&)            = delete;
    Channel& operator=(const Channel&) = delete;

    // -----------------------------------------------------------------------
    // Blocking send.
    // If the channel is full (bounded), blocks until a receiver drains a slot.
    // -----------------------------------------------------------------------
    void send(T value) {
        // Wait until there is room (for bounded channels).
        while (full()) {
            Scheduler* s = Scheduler::current();
            LOGOS_ASSERT(s && s->running(), "REACTOR-CHAN-001",
                         "Channel::send() called outside a fiber");
            sender_waiters_.push_back(s->running());
            s->block();
        }
        buffer_.push_back(std::move(value));
        wake_one_receiver();
    }

    // -----------------------------------------------------------------------
    // Blocking recv.
    // Blocks until a value is available.
    // -----------------------------------------------------------------------
    T recv() {
        while (buffer_.empty()) {
            Scheduler* s = Scheduler::current();
            LOGOS_ASSERT(s && s->running(), "REACTOR-CHAN-010",
                         "Channel::recv() called outside a fiber");
            receiver_waiters_.push_back(s->running());
            s->block();
        }
        T value = std::move(buffer_.front());
        buffer_.pop_front();
        wake_one_sender();
        return value;
    }

    // -----------------------------------------------------------------------
    // Non-blocking variants. Return false / nullopt on failure.
    // -----------------------------------------------------------------------
    bool try_send(T value) {
        if (full()) return false;
        buffer_.push_back(std::move(value));
        wake_one_receiver();
        return true;
    }

    std::optional<T> try_recv() {
        if (buffer_.empty()) return std::nullopt;
        T value = std::move(buffer_.front());
        buffer_.pop_front();
        wake_one_sender();
        return value;
    }

    // -----------------------------------------------------------------------
    // Inspection (non-blocking, advisory only).
    // -----------------------------------------------------------------------
    bool   empty()    const noexcept { return buffer_.empty(); }
    bool   full()     const noexcept { return capacity_ > 0 && buffer_.size() >= capacity_; }
    size_t size()     const noexcept { return buffer_.size(); }
    size_t capacity() const noexcept { return capacity_; }

private:
    void wake_one_receiver() {
        if (!receiver_waiters_.empty()) {
            Fiber* f = receiver_waiters_.front();
            receiver_waiters_.pop_front();
            Scheduler::current()->wake(f);
        }
    }

    void wake_one_sender() {
        if (!sender_waiters_.empty()) {
            Fiber* f = sender_waiters_.front();
            sender_waiters_.pop_front();
            Scheduler::current()->wake(f);
        }
    }

    size_t              capacity_;
    std::deque<T>       buffer_;
    std::deque<Fiber*>  sender_waiters_;
    std::deque<Fiber*>  receiver_waiters_;
};

} // namespace logos::reactor
