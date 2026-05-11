
#include <logos/reactor/stack_chain.hpp>
#include <logos/verification/assert.hpp>

namespace logos::reactor {

namespace {
size_t next_pow2(size_t v) noexcept {
    if (v <= 1) return 1;
    --v;
    v |= v >> 1; v |= v >> 2; v |= v >> 4;
    v |= v >> 8; v |= v >> 16; v |= v >> 32;
    return v + 1;
}
} // namespace

StackChain::StackChain(size_t initial_size) noexcept {
    push_segment(initial_size);
}

StackChain::~StackChain() noexcept {
    while (header_) {
        SegmentHeader* prev = header_->prev;
        StackAllocator::deallocate({ header_->alloc_base, header_->alloc_size });
        header_ = prev;
    }
}

StackChain::StackChain(StackChain&& o) noexcept : header_(o.header_) {
    o.header_ = nullptr;
}

StackChain& StackChain::operator=(StackChain&& o) noexcept {
    if (this != &o) {
        this->~StackChain();
        header_ = o.header_;
        o.header_ = nullptr;
    }
    return *this;
}

void* StackChain::push_segment(size_t min_size) noexcept {
    // Growth: 2× current, or min_size, rounded to next power-of-2 pages.
    size_t cur    = header_ ? header_->alloc_size : 0;
    size_t target = (cur == 0) ? StackAllocator::kDefaultSegmentSize : cur * 2;
    if (target < min_size) target = min_size;
    size_t alloc_size = next_pow2(target);

    StackSegment seg = StackAllocator::allocate(alloc_size);
    LOGOS_ASSERT(seg.base != nullptr, "REACTOR-CHAIN-001",
                 "StackChain::push_segment allocation failed");

    // Place SegmentHeader at the high end of the allocation, 16-byte aligned.
    uintptr_t top = reinterpret_cast<uintptr_t>(seg.base) + seg.size;
    uintptr_t hdr = (top - sizeof(SegmentHeader)) & ~uintptr_t(15);
    auto* new_hdr = reinterpret_cast<SegmentHeader*>(hdr);
    new_hdr->prev       = header_;
    new_hdr->alloc_base = seg.base;
    new_hdr->alloc_size = seg.size;
    header_ = new_hdr;

    // Return the SegmentHeader address as the initial RSP.
    // Stack grows downward from here into the usable area below the header.
    return new_hdr;
}

bool StackChain::pop_segment() noexcept {
    if (!header_ || !header_->prev) return false;  // keep the root
    SegmentHeader* prev = header_->prev;
    StackAllocator::deallocate({ header_->alloc_base, header_->alloc_size });
    header_ = prev;
    return true;
}

void* StackChain::current_limit() const noexcept {
    return header_ ? header_->alloc_base : nullptr;
}

size_t StackChain::current_size() const noexcept {
    return header_ ? header_->alloc_size : 0;
}

} // namespace logos::reactor
