// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos

#pragma once

#include <logos/reactor/features.hpp>

#if LOGOS_HAS_GREEN_STACKS
#  include <logos/reactor/stack_chain.hpp>
#  include <logos/reactor/green_fn.hpp>
#else
#  include <logos/reactor/stack_pool.hpp>
#endif

#include <cstdint>
#include <cstddef>
#include <functional>
#include <string>
#include <string_view>

namespace logos::reactor {

// ---------------------------------------------------------------------------
// FiberRegs — callee-saved registers saved/restored on context switch.
//
// x86-64 System V ABI callee-saved: rbp, rbx, r12-r15.
// rsp is saved explicitly (return address is on top of the restored stack).
// stack_limit mirrors %fs:0x70 — the GCC/LLVM split-stack low-water mark.
// Jenny (Clang 21 with [[clang::green]]) instruments function prologues to
// compare rsp against this TLS slot; if we switch stacks without swapping it,
// every call inside the fiber immediately triggers __morestack → crash.
// ---------------------------------------------------------------------------
struct FiberRegs {
    uint64_t rbp         = 0;  // offset  0
    uint64_t rbx         = 0;  // offset  8
    uint64_t r12         = 0;  // offset 16 — Fiber* for entry trampoline
    uint64_t r13         = 0;  // offset 24
    uint64_t r14         = 0;  // offset 32
    uint64_t r15         = 0;  // offset 40
    uint64_t rsp         = 0;  // offset 48 — return address is at *rsp
    uint64_t stack_limit = 0;  // offset 56 — saved/restored %fs:0x70
};

// ---------------------------------------------------------------------------
// FiberState
// ---------------------------------------------------------------------------
enum class FiberState : uint8_t {
    Ready,      // in scheduler run queue, waiting to run
    Running,    // currently executing
    Blocked,    // waiting for IO, channel, mutex, or join
    Done,       // function returned; joiners will be woken
};

class Scheduler;

// ---------------------------------------------------------------------------
// Fiber — a cooperatively-scheduled green fiber.
//
// Stack mode is selected at compile time via LOGOS_HAS_GREEN_STACKS:
//
//   Green mode (LOGOS_HAS_GREEN_STACKS=1, Jenny/Clang 21):
//     - Initial stack is 1 KB (grows via __morestack as needed).
//     - stack_size parameter to Fiber() and Scheduler::spawn() is ignored.
//     - StackChain member manages the segment linked list.
//     - tls_current_fiber (scheduler.cpp) is set during execution so that
//       green_bridge.cpp can locate the fiber on __morestack entry.
//
//   Classic mode (LOGOS_HAS_GREEN_STACKS=0):
//     - Fixed-size stack acquired from StackPool on construction,
//       returned to the pool on destruction (O(1) reuse, no per-fiber mmap).
//     - Stack size is set at ReactorEngine construction (engine-wide).
//     - guard page (PROT_NONE, 4 KB) at the bottom catches overflow.
//
// Ownership: the Scheduler owns all live Fibers.
// ---------------------------------------------------------------------------
class Fiber {
public:
    // Classic mode default — ignored in green mode.
    static constexpr size_t kDefaultStackSize = 256 * 1024;  // 256 KB

#if LOGOS_HAS_GREEN_STACKS
    // Green mode: fn is a GreenFn (type-erased green callable).
    // GreenFn's internal thunk is [[clang::green]], so the fiber body
    // can call any other green scheduler primitive (yield, block, join…).
    explicit Fiber(GreenFn fn, std::string_view name = "") noexcept;
#else
    // Classic mode: pool provides + recycles the stack.
    // pool must remain valid for the Fiber's lifetime (Scheduler outlives Fiber).
    explicit Fiber(std::move_only_function<void()> fn,
                   std::string_view                name = "",
                   StackPool*                      pool = nullptr) noexcept;
#endif

    ~Fiber() noexcept;

    // Non-copyable, non-movable (Scheduler holds raw pointers).
    Fiber(const Fiber&)            = delete;
    Fiber& operator=(const Fiber&) = delete;

    FiberState       state() const noexcept { return state_; }
    std::string_view name()  const noexcept { return name_; }
    uint64_t         id()    const noexcept { return id_; }
    int              result() const noexcept { return result_; }

#if LOGOS_HAS_GREEN_STACKS
    // Called from green_bridge.cpp via tls_current_fiber when __morestack fires.
    void* grow_stack(size_t needed) noexcept  { return stack_chain_.push_segment(needed); }
    void  shrink_stack()            noexcept  { stack_chain_.pop_segment(); }
    void* stack_limit()       const noexcept  { return stack_chain_.current_limit(); }
#endif

private:
    friend class Scheduler;
    friend class Reactor;

    // LOGOS_GREEN: root of the fiber's green-stack call tree.
    // Any [[clang::red]] calls from fn_() auto-switch to the system thread
    // stack (set up in Scheduler::switch_to before fiber_switch).
    LOGOS_GREEN static void entry(Fiber* self) noexcept;

    // LOGOS_GREEN: must stay on green stack to reach fiber_done → fiber_switch.
    [[noreturn]] LOGOS_GREEN static void finish(Fiber* self) noexcept;

    void init_stack() noexcept;

    FiberRegs regs_{};

#if LOGOS_HAS_GREEN_STACKS
    StackChain stack_chain_;
#else
    StackPool* pool_       = nullptr;
    uint8_t*   stack_base_ = nullptr;
    size_t     stack_size_ = kDefaultStackSize;
#endif

#if LOGOS_HAS_GREEN_STACKS
    GreenFn fn_;
#else
    std::move_only_function<void()> fn_;
#endif
    std::string           name_;
    uint64_t              id_;
    FiberState            state_  = FiberState::Ready;
    int                   result_ = 0;

    Scheduler* scheduler_   = nullptr;
    Fiber*     join_waiter_ = nullptr;

    static uint64_t next_id_;
};

// ---------------------------------------------------------------------------
// fiber_switch — assembly context switch (fiber_switch.S)
// ---------------------------------------------------------------------------
// fiber_switch is assembly — no compiler-generated prologue.  Declare it green
// so Jenny 19 does NOT wrap calls from green context with RSP save/restore
// (which would clobber the intentional stack switch).
// Red callers use the _red alias (same symbol, different declaration).
extern "C" LOGOS_GREEN void fiber_switch(FiberRegs* from, const FiberRegs* to) noexcept;
extern "C" void fiber_switch_red(FiberRegs* from, const FiberRegs* to) noexcept
    asm("fiber_switch");
extern "C" LOGOS_GREEN void fiber_entry_trampoline() noexcept;

} // namespace logos::reactor
