// Logos project — https://github.com/victor-smirnov/logos
//
// C bridge between __morestack (assembly) and StackChain (C++).
//
// green_fiber_allocate_segment — called by __morestack when a green function
//   runs out of stack space.  Pushes a new segment onto the current fiber's
//   StackChain and updates %fs:0x70 to the new segment's lower limit.
//   Returns the new RSP (SegmentHeader address = top of new segment).
//
// green_fiber_release_segment — called by __morestack_release_segments when a
//   function returns from an extended segment.  Pops the segment and restores
//   %fs:0x70.
//
// green_fiber_allocation_failed — called when allocation returns null; aborts.
//
// Only compiled when LOGOS_HAS_GREEN_STACKS=1.

#include <logos/reactor/features.hpp>

#if LOGOS_HAS_GREEN_STACKS

#include <logos/reactor/fiber.hpp>

#include <cstdio>
#include <cstdlib>
#include <cstdint>

// tls_current_fiber — defined in scheduler.cpp; tracks which Fiber is running
// so green_bridge.cpp can reach the fiber's StackChain on __morestack entry.
namespace logos::reactor {
    extern thread_local Fiber* tls_current_fiber;
}

// __green_fiber_system_stack — Jenny's TLS slot for the "red stack" pointer.
//
// Before every fiber_switch from scheduler→fiber, Scheduler::switch_to()
// saves the current (scheduler's) RSP here.  When a [[clang::green]] function
// calls a [[clang::red]] function, Jenny reads this slot and switches RSP to
// the saved system-thread stack so the red function runs there instead of on
// the tiny green fiber stack.
//
// Declared extern "C" to match Jenny's expected ABI symbol name.
extern "C" __thread void* __green_fiber_system_stack = nullptr;

namespace {
// Write limit to %fs:0x70 (the GCC/LLVM split-stack low-water mark TLS slot).
// Called after every segment push/pop so the compiler-generated prologue check
// `cmp %fs:0x70, %rsp` sees the correct boundary for the current segment.
void set_stack_limit(uintptr_t limit) noexcept {
    asm volatile("movq %0, %%fs:0x70" : : "r"(limit) : "memory");
}
} // namespace

extern "C" {

// green_fiber_allocate_segment(frame_size, arg_size, return_address) → new RSP
//
// return_address is passed through from __morestack but is not currently used
// (the compiler handles the retry logic via the prologue/epilogue mechanism).
void* green_fiber_allocate_segment(size_t frame_size,
                                   size_t arg_size,
                                   void*  /*return_address*/) noexcept
{
    logos::reactor::Fiber* fiber = logos::reactor::tls_current_fiber;
    if (!fiber) return nullptr;

    void* new_sp = fiber->grow_stack(frame_size + arg_size);
    if (!new_sp) return nullptr;

    // Update the split-stack limit TLS slot for the new segment.
    set_stack_limit(reinterpret_cast<uintptr_t>(fiber->stack_limit()));
    return new_sp;
}

// green_fiber_release_segment()
void green_fiber_release_segment() noexcept {
    logos::reactor::Fiber* fiber = logos::reactor::tls_current_fiber;
    if (!fiber) return;

    fiber->shrink_stack();

    // Restore the split-stack limit to the (now-current) segment's lower bound.
    set_stack_limit(reinterpret_cast<uintptr_t>(fiber->stack_limit()));
}

// green_fiber_allocation_failed() — called from __morestack on null return.
[[noreturn]] void green_fiber_allocation_failed() noexcept {
    std::fprintf(stderr,
        "logos: FATAL: green fiber stack segment allocation failed — aborting\n");
    std::abort();
}

} // extern "C"

#endif // LOGOS_HAS_GREEN_STACKS
