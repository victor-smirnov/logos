//
// Lock-free single-producer / single-consumer ring buffer.
//
// N must be a power of two.  Capacity is exactly N items.
// push() is safe to call from exactly one thread; pop() from exactly one
// (different) thread.  No other synchronisation is needed between them.
//
// Usage:
//   SpscQueue<int, 256> q;
//   // producer thread:
//   if (!q.push(42)) { /* full */ }
//   // consumer thread:
//   int v; if (q.pop(v)) { /* use v */ }

#pragma once

#include <array>
#include <atomic>
#include <cstddef>

namespace logos::reactor {

template<typename T, size_t N>
class SpscQueue {
    static_assert(N > 0 && (N & (N - 1)) == 0,
                  "SpscQueue capacity N must be a power of two");

    // Keep head and tail on separate cache lines to avoid false sharing.
    alignas(64) std::atomic<size_t> head_{0};   // consumer advances
    alignas(64) std::atomic<size_t> tail_{0};   // producer advances
    std::array<T, N> buf_{};

public:
    // Push one item.  Returns false if the queue is full.
    // Only one thread may call push() at a time.
    bool push(T val) noexcept {
        size_t t = tail_.load(std::memory_order_relaxed);
        if (t - head_.load(std::memory_order_acquire) == N)
            return false;
        buf_[t & (N - 1)] = std::move(val);
        tail_.store(t + 1, std::memory_order_release);
        return true;
    }

    // Pop one item into out.  Returns false if the queue is empty.
    // Only one thread may call pop() at a time.
    bool pop(T& out) noexcept {
        size_t h = head_.load(std::memory_order_relaxed);
        if (tail_.load(std::memory_order_acquire) == h)
            return false;
        out = std::move(buf_[h & (N - 1)]);
        head_.store(h + 1, std::memory_order_release);
        return true;
    }

    bool   empty()               const noexcept { return size() == 0; }
    size_t size()                const noexcept {
        return tail_.load(std::memory_order_acquire) -
               head_.load(std::memory_order_acquire);
    }
    static constexpr size_t capacity() noexcept { return N; }
};

} // namespace logos::reactor
