// Logos project — https://github.com/victor-smirnov/logos

#pragma once

#include <logos/reactor/stack_pool.hpp>

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
// ---------------------------------------------------------------------------
struct FiberRegs {
    uint64_t rbp         = 0;  // offset  0
    uint64_t rbx         = 0;  // offset  8
    uint64_t r12         = 0;  // offset 16 — Fiber* for entry trampoline
    uint64_t r13         = 0;  // offset 24
    uint64_t r14         = 0;  // offset 32
    uint64_t r15         = 0;  // offset 40
    uint64_t rsp         = 0;  // offset 48 — return address is at *rsp
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
// Fiber — a cooperatively-scheduled fiber.
//
// Fixed-size stack acquired from StackPool on construction, returned to the
// pool on destruction (O(1) reuse, no per-fiber mmap).
// Guard page (PROT_NONE, 4 KB) at the bottom catches overflow.
//
// Ownership: the Scheduler owns all live Fibers.
// ---------------------------------------------------------------------------
class Fiber {
public:
    static constexpr size_t kDefaultStackSize = 256 * 1024;  // 256 KB

    explicit Fiber(std::move_only_function<void()> fn,
                   std::string_view                name = "",
                   StackPool*                      pool = nullptr) noexcept;

    ~Fiber() noexcept;

    // Non-copyable, non-movable (Scheduler holds raw pointers).
    Fiber(const Fiber&)            = delete;
    Fiber& operator=(const Fiber&) = delete;

    FiberState       state() const noexcept { return state_; }
    std::string_view name()  const noexcept { return name_; }
    uint64_t         id()    const noexcept { return id_; }
    int              result() const noexcept { return result_; }

private:
    friend class Scheduler;
    friend class Reactor;

    static void entry(Fiber* self) noexcept;
    [[noreturn]] static void finish(Fiber* self) noexcept;

    void init_stack() noexcept;

    FiberRegs regs_{};

    StackPool* pool_       = nullptr;
    uint8_t*   stack_base_ = nullptr;
    size_t     stack_size_ = kDefaultStackSize;

    std::move_only_function<void()> fn_;
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
extern "C" void fiber_switch(FiberRegs* from, const FiberRegs* to) noexcept;
extern "C" void fiber_entry_trampoline() noexcept;

} // namespace logos::reactor
