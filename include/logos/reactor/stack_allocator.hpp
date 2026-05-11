// Logos project — https://github.com/victor-smirnov/logos
//
// StackAllocator — raw memory for fiber stack segments.
//
// Allocates via mmap, rounded up to the system page size.  No guard pages —
// callers (StackPool) add those on top.
//
// Used by StackChain (green mode) and StackPool (classic mode).

#pragma once

#include <cstddef>

namespace logos::reactor {

struct StackSegment {
    void*  base = nullptr;
    size_t size = 0;
};

class StackAllocator {
public:
    // Default initial segment for green-mode fibers: 64 KB.
    // lld's split-stack relaxation pads frame-size checks by 0x4000 (16 KB)
    // for functions that call into non-split-stack libraries (liburing, libc).
    // A 64 KB initial segment avoids __morestack_non_split calls in the
    // common path.
    static constexpr size_t kDefaultSegmentSize = 64 * 1024;

    // Allocate at least min_size bytes.
    // Returns {ptr, actual_size} where actual_size >= min_size and is a
    // multiple of the system page size.
    static StackSegment allocate(size_t min_size = kDefaultSegmentSize) noexcept;

    // Return memory.  seg.size must match what allocate() returned.
    static void deallocate(StackSegment seg) noexcept;
};

} // namespace logos::reactor
