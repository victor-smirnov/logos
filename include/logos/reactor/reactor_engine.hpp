// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
//
// ReactorEngine — fixed pool of Reactors with P2P lock-free SPSC queues.
//
// Creates N Reactors (one per OS thread) and wires them with N×N SPSC
// queues for cross-reactor task submission.  Each ordered pair (i, j) has
// a dedicated SpscQueue — producer i pushes, consumer j pops — so the
// data path is fully lock-free with zero contention between producers.
//
// Conditional wakeup: eventfd is written only when the target reactor is
// sleeping in io_uring_wait_cqe.  When the target is actively running
// fibers, pushed tasks are picked up on the next drain_p2p_() iteration
// with no syscall overhead.
//
// The old mutex-protected alien queue is retained for truly alien threads
// (non-reactor OS threads).  Reactor-to-reactor submission uses SPSC.
//
// Usage:
//
//   ReactorEngine engine(4);   // 4 reactors, IDs 0..3
//
//   // Spawn initial fibers before starting.
//   engine.reactor(0).spawn([] { ... });
//
//   // Start each reactor on its own OS thread.
//   std::vector<std::jthread> threads;
//   for (size_t i = 0; i < engine.size(); ++i)
//       threads.emplace_back([&engine, i] { engine.reactor(i).run(); });
//
//   // Inside a fiber on reactor 0:
//   auto result = submit_to(engine.reactor(1), [] -> logos::expected<int> {
//       return 42;
//   });

#pragma once

#include <logos/reactor/reactor.hpp>
#include <logos/reactor/spsc_queue.hpp>
#include <logos/verification/assert.hpp>

#include <cstddef>
#include <functional>
#include <memory>
#include <vector>

namespace logos::reactor {

class ReactorEngine {
public:
    static constexpr size_t kQueueCapacity = 256;

#if LOGOS_HAS_GREEN_STACKS
    using Task = GreenFn;
#else
    using Task = std::move_only_function<void()>;
#endif
    using TaskQueue = SpscQueue<Task, kQueueCapacity>;

    // Create num_reactors reactors, each with the given io_uring depth.
    // stack_size: classic mode only — sets the StackPool size for all fibers on
    //             every reactor in this engine.  Ignored in green mode.
    explicit ReactorEngine(size_t num_reactors,
                           unsigned ring_depth = Reactor::kRingDepth,
                           size_t   stack_size = Fiber::kDefaultStackSize) noexcept;
    ~ReactorEngine() noexcept;

    ReactorEngine(const ReactorEngine&)            = delete;
    ReactorEngine& operator=(const ReactorEngine&) = delete;

    size_t   size() const noexcept { return num_reactors_; }
    Reactor& reactor(size_t id) noexcept;

    // P2P queue from reactor `from` to reactor `to`.
    // Only from may push; only to may pop.
    TaskQueue& queue(size_t from, size_t to) noexcept {
        return queues_[from * num_reactors_ + to];
    }

private:
    size_t                            num_reactors_;
    std::vector<std::unique_ptr<Reactor>> reactors_;
    std::vector<TaskQueue>            queues_;   // flat N×N
};

} // namespace logos::reactor
