// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov

#include <logos/reactor/stack_allocator.hpp>
#include <logos/verification/assert.hpp>

#include <sys/mman.h>
#include <unistd.h>

namespace logos::reactor {

namespace {
size_t page_round_up(size_t n) noexcept {
    static const size_t kPage = static_cast<size_t>(::sysconf(_SC_PAGESIZE));
    return (n + kPage - 1) & ~(kPage - 1);
}
} // namespace

StackSegment StackAllocator::allocate(size_t min_size) noexcept {
    size_t size = page_round_up(min_size > 0 ? min_size : kDefaultSegmentSize);
    void* p = ::mmap(nullptr, size,
                     PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK,
                     -1, 0);
    LOGOS_ASSERT(p != MAP_FAILED, "REACTOR-STACKALLOC-001",
                 "StackAllocator::allocate mmap failed for size={}", size);
    return { p, size };
}

void StackAllocator::deallocate(StackSegment seg) noexcept {
    if (seg.base && seg.size > 0)
        ::munmap(seg.base, seg.size);
}

} // namespace logos::reactor
