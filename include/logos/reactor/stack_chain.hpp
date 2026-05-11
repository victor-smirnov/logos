// Logos project — https://github.com/victor-smirnov/logos
//
// StackChain — linked list of stack segments for green-mode fibers.
//
// Each segment is a contiguous block of memory.  A SegmentHeader is placed
// at the HIGH address of each block (stack grows downward), storing the link
// to the previous segment and allocation metadata.
//
// Segment layout (low → high address):
//
//   [ allocation_base ]         ← current_limit() — lower bound for __morestack
//   [  usable stack space  ]
//   [ SegmentHeader (16-B aligned) ]  ← current_sp() — initial RSP on entry
//
// push_segment(min_size):
//   Allocate a new segment (2× growth or min_size, rounded to page).
//   Returns the new initial RSP (= SegmentHeader address).
//
// pop_segment():
//   Free the top segment.  Refuses to pop the last (root) segment.

#pragma once

#include <logos/reactor/stack_allocator.hpp>

namespace logos::reactor {

class StackChain {
public:
    explicit StackChain(size_t initial_size = StackAllocator::kDefaultSegmentSize) noexcept;
    ~StackChain() noexcept;

    StackChain(StackChain&&) noexcept;
    StackChain& operator=(StackChain&&) noexcept;
    StackChain(const StackChain&)            = delete;
    StackChain& operator=(const StackChain&) = delete;

    // Push a new segment of at least min_size bytes.
    // Returns the new initial RSP (SegmentHeader address).
    void* push_segment(size_t min_size = 0) noexcept;

    // Pop the top segment (free it, restore previous).
    // Returns false if this is the only segment (root is kept).
    bool pop_segment() noexcept;

    // Initial RSP for the current segment (= SegmentHeader address).
    void* current_sp()    const noexcept { return header_; }

    // Lower bound for __morestack check (= allocation_base of current segment).
    // This is what %fs:0x70 should hold for the current fiber.
    void* current_limit() const noexcept;

    size_t current_size() const noexcept;

private:
    struct SegmentHeader {
        SegmentHeader* prev;       // previous segment's header (nullptr = first)
        void*          alloc_base; // passed to StackAllocator::deallocate
        size_t         alloc_size;
    };

    SegmentHeader* header_ = nullptr;
};

} // namespace logos::reactor
