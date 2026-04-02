// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos

#pragma once

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
// Fiber — a green fiber (cooperatively scheduled, segmented-stack-compatible).
//
// [[clang::green]] functions run on this fiber's segmented stack.
// Plain (red) functions run on the system thread stack and must not be called
// from green functions without a trampoline.
//
// Ownership: the Scheduler owns all live Fibers.
// ---------------------------------------------------------------------------
class Fiber {
public:
    static constexpr size_t kDefaultStackSize = 256 * 1024;  // 256 KB

    // Create a fiber with the given function and stack size.
    // The fiber is not started until the scheduler runs it.
    explicit Fiber(std::move_only_function<void()> fn,
                   std::string_view                name       = "",
                   size_t                          stack_size = kDefaultStackSize);

    ~Fiber();

    // Non-copyable, non-movable (Scheduler holds raw pointers).
    Fiber(const Fiber&)            = delete;
    Fiber& operator=(const Fiber&) = delete;

    FiberState  state() const noexcept { return state_; }
    std::string_view name()  const noexcept { return name_; }
    uint64_t    id()    const noexcept { return id_; }

    // Return value stored by the fiber (for join).
    int result() const noexcept { return result_; }

private:
    friend class Scheduler;
    friend class Reactor;

    // Called by fiber_switch.S trampoline to run the fiber function.
    static void entry(Fiber* self) noexcept;

    // Called after entry() returns (or throws) to mark the fiber Done
    // and switch back to the scheduler.
    [[noreturn]] static void finish(Fiber* self) noexcept;

    void init_stack();

    FiberRegs   regs_{};
    uint8_t*    stack_base_ = nullptr;
    size_t      stack_size_;

    std::move_only_function<void()> fn_;
    std::string           name_;
    uint64_t              id_;
    FiberState            state_  = FiberState::Ready;
    int                   result_ = 0;

    Scheduler* scheduler_ = nullptr;   // set by Scheduler::spawn()

    // Fibers waiting to join this one (woken when this fiber finishes).
    Fiber* join_waiter_ = nullptr;

    static uint64_t next_id_;
};

// ---------------------------------------------------------------------------
// fiber_switch — assembly context switch (fiber_switch.S)
//
// Saves callee-saved regs + rsp into *from, restores from *to, then returns
// into the 'to' fiber (ret pops the return address off 'to's restored stack).
// ---------------------------------------------------------------------------
extern "C" void fiber_switch(FiberRegs* from, const FiberRegs* to) noexcept;

// Entry trampoline — defined in fiber_switch.S.
// On entry r12 = Fiber*. Calls Fiber::entry(r12), then Fiber::finish(r12).
extern "C" void fiber_entry_trampoline() noexcept;

} // namespace logos::reactor
