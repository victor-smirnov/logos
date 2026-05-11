
#include <logos/reactor/stack_pool.hpp>
#include <logos/verification/assert.hpp>

#include <sys/mman.h>

namespace logos::reactor {

StackPool::StackPool(size_t stack_size) noexcept : stack_size_(stack_size) {
    LOGOS_ASSERT(stack_size_ >= 16 * 1024, "REACTOR-POOL-001",
                 "StackPool stack_size must be >= 16 KB, got {}", stack_size_);
    LOGOS_ASSERT(stack_size_ % 4096 == 0, "REACTOR-POOL-002",
                 "StackPool stack_size must be a multiple of 4096, got {}", stack_size_);
}

StackPool::~StackPool() noexcept {
    const size_t total = stack_size_ + 4096;
    for (void* base : free_)
        ::munmap(base, total);
}

StackSegment StackPool::acquire() noexcept {
    if (!free_.empty()) {
        void* base = free_.back();
        free_.pop_back();
        return { base, stack_size_ };
    }
    const size_t total = stack_size_ + 4096;
    void* mem = ::mmap(nullptr, total,
                       PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK,
                       -1, 0);
    LOGOS_ASSERT(mem != MAP_FAILED, "REACTOR-POOL-003",
                 "StackPool::acquire mmap failed for total={}", total);
    int rc = ::mprotect(mem, 4096, PROT_NONE);
    LOGOS_ASSERT(rc == 0, "REACTOR-POOL-004",
                 "StackPool::acquire mprotect guard page failed");
    return { mem, stack_size_ };
}

void StackPool::release(StackSegment seg) noexcept {
    if (seg.base)
        free_.push_back(seg.base);
}

} // namespace logos::reactor
