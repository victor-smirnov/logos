// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos
//
// StackPool — per-Scheduler free list of fixed-size fiber stacks (classic mode).
//
// Each stack allocation consists of:
//   [ guard page (PROT_NONE, 4 KB) | usable stack (stack_size_ bytes) ]
//   low address ────────────────────────────────────── high address
//
// The guard page at the bottom catches stack overflows with SIGSEGV.
//
// Not thread-safe — accessed from one OS thread only (the Scheduler's thread).
//
// acquire(): pop from the free list, or mmap + mprotect a new stack.
// release(): push the allocation base back onto the free list.
// ~StackPool(): munmap all stacks in the free list.

#pragma once

#include <logos/reactor/stack_allocator.hpp>

#include <cstddef>
#include <vector>

namespace logos::reactor {

class StackPool {
public:
    static constexpr size_t kDefaultStackSize = 256 * 1024;  // 256 KB

    explicit StackPool(size_t stack_size = kDefaultStackSize) noexcept;
    ~StackPool() noexcept;

    StackPool(const StackPool&)            = delete;
    StackPool& operator=(const StackPool&) = delete;

    // Returns { alloc_base, stack_size_ }.
    // alloc_base = guard page base; usable stack bottom = alloc_base + 4096.
    StackSegment acquire() noexcept;

    // Return a stack to the pool.  seg.base must be the alloc_base from acquire().
    void release(StackSegment seg) noexcept;

    size_t stack_size() const noexcept { return stack_size_; }

private:
    size_t             stack_size_;
    std::vector<void*> free_;  // alloc_base values from acquire()
};

} // namespace logos::reactor
