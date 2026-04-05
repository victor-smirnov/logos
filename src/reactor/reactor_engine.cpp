// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov

#include <logos/reactor/reactor_engine.hpp>
#include <logos/verification/assert.hpp>

namespace logos::reactor {

ReactorEngine::ReactorEngine(size_t num_reactors, unsigned ring_depth,
                             size_t stack_size) noexcept
    : num_reactors_(num_reactors),
      queues_(num_reactors * num_reactors)
{
    LOGOS_ASSERT(num_reactors > 0, "REACTOR-ENGINE-001",
                 "ReactorEngine requires at least 1 reactor");

    reactors_.reserve(num_reactors);
    for (size_t i = 0; i < num_reactors; ++i) {
        reactors_.push_back(std::make_unique<Reactor>(ring_depth, stack_size));
        reactors_[i]->id_     = i;
        reactors_[i]->engine_ = this;
    }
}

ReactorEngine::~ReactorEngine() noexcept = default;

Reactor& ReactorEngine::reactor(size_t id) noexcept {
    LOGOS_ASSERT(id < num_reactors_, "REACTOR-ENGINE-002",
                 "reactor ID {} out of range [0, {})", id, num_reactors_);
    return *reactors_[id];
}

} // namespace logos::reactor
