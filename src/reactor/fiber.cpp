// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos

#include <logos/reactor/fiber.hpp>
#include <logos/reactor/scheduler.hpp>
#include <logos/verification/assert.hpp>

#include <cstring>

#if !LOGOS_HAS_GREEN_STACKS
#  include <sys/mman.h>
#endif

namespace logos::reactor {

uint64_t Fiber::next_id_ = 1;

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

#if LOGOS_HAS_GREEN_STACKS

Fiber::Fiber(GreenFn fn, std::string_view name) noexcept
    : stack_chain_()  // 1 KB initial segment (StackAllocator::kDefaultSegmentSize)
    , fn_(std::move(fn))
    , name_(name)
    , id_(next_id_++)
{
    init_stack();
}

#else  // classic mode

Fiber::Fiber(std::move_only_function<void()> fn,
             std::string_view                name,
             StackPool*                      pool) noexcept
    : pool_(pool)
    , fn_(std::move(fn))
    , name_(name)
    , id_(next_id_++)
{
    if (pool_) {
        StackSegment seg = pool_->acquire();
        stack_base_ = static_cast<uint8_t*>(seg.base);
        stack_size_ = seg.size;
    } else {
        // Standalone fiber (no pool) — allocate directly.
        LOGOS_ASSERT(stack_size_ >= 16 * 1024, "REACTOR-FIBER-001",
                     "Stack size must be at least 16 KB, got {}", stack_size_);
        LOGOS_ASSERT(stack_size_ % 4096 == 0, "REACTOR-FIBER-002",
                     "Stack size must be a multiple of 4096, got {}", stack_size_);

        size_t total = stack_size_ + 4096;
        void* mem = ::mmap(nullptr, total,
                           PROT_READ | PROT_WRITE,
                           MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK,
                           -1, 0);
        LOGOS_ASSERT(mem != MAP_FAILED, "REACTOR-FIBER-003",
                     "mmap failed for fiber '{}' stack: errno={}", name_, errno);
        int rc = ::mprotect(mem, 4096, PROT_NONE);
        LOGOS_ASSERT(rc == 0, "REACTOR-FIBER-004",
                     "mprotect guard page failed for fiber '{}': errno={}", name_, errno);
        stack_base_ = static_cast<uint8_t*>(mem);
    }
    init_stack();
}

#endif

// ---------------------------------------------------------------------------
// Destruction
// ---------------------------------------------------------------------------

Fiber::~Fiber() noexcept {
#if !LOGOS_HAS_GREEN_STACKS
    if (stack_base_) {
        if (pool_) {
            pool_->release({ stack_base_, stack_size_ });
        } else {
            size_t total = stack_size_ + 4096;
            ::munmap(stack_base_, total);
        }
        stack_base_ = nullptr;
    }
#endif
    // Green mode: stack_chain_ destructor frees all segments automatically.
}

// ---------------------------------------------------------------------------
// init_stack — set up the initial FiberRegs for the entry trampoline.
//
// We lay a fake call frame so that fiber_switch() restores into
// fiber_entry_trampoline, which calls Fiber::entry(this).
//
// Stack layout after init (grows downward):
//
//   [ ... usable space ... ]
//   [ &fiber_entry_trampoline ]  ← regs_.rsp  (fake "return address")
//
// regs_.r12 = this   (picked up by fiber_entry_trampoline → rdi)
// regs_.stack_limit = lower bound of stack (→ %fs:0x70)
// ---------------------------------------------------------------------------
void Fiber::init_stack() noexcept {
#if LOGOS_HAS_GREEN_STACKS
    // Green mode: initial RSP = SegmentHeader address (top of first segment).
    auto* sp = static_cast<uint8_t*>(stack_chain_.current_sp());
    // The header is already 16-byte aligned; align down just in case.
    sp = reinterpret_cast<uint8_t*>(reinterpret_cast<uintptr_t>(sp) & ~uintptr_t(15));

    // Push trampoline address as fake "return address" (popped by ret in fiber_switch).
    sp -= 8;
    *reinterpret_cast<uint64_t*>(sp) = reinterpret_cast<uint64_t>(fiber_entry_trampoline);

    regs_.rsp         = reinterpret_cast<uint64_t>(sp);
    regs_.r12         = reinterpret_cast<uint64_t>(this);
    regs_.stack_limit = reinterpret_cast<uint64_t>(stack_chain_.current_limit());

#else
    // Classic mode: stack top = guard_base + 4096 + stack_size_.
    uint8_t* top = stack_base_ + 4096 + stack_size_;

    // SysV ABI: at function entry rsp % 16 == 8 (call has pushed the return addr).
    top = reinterpret_cast<uint8_t*>(reinterpret_cast<uintptr_t>(top) & ~uintptr_t(15));

    top -= 8;   // top % 16 == 8 after this push
    *reinterpret_cast<uint64_t*>(top) = reinterpret_cast<uint64_t>(fiber_entry_trampoline);

    regs_.rsp         = reinterpret_cast<uint64_t>(top);
    regs_.r12         = reinterpret_cast<uint64_t>(this);
    regs_.stack_limit = reinterpret_cast<uint64_t>(stack_base_ + 4096);  // above guard page
#endif
}

// ---------------------------------------------------------------------------
// Fiber::entry — called by fiber_entry_trampoline
// ---------------------------------------------------------------------------
void Fiber::entry(Fiber* self) noexcept {
    LOGOS_ASSERT(self != nullptr, "REACTOR-FIBER-010",
                 "fiber_entry_trampoline called with null Fiber*");
    LOGOS_ASSERT(self->state_ == FiberState::Running, "REACTOR-FIBER-011",
                 "Fiber '{}' entered in wrong state: {}",
                 self->name_, static_cast<int>(self->state_));
    self->fn_();
}

// ---------------------------------------------------------------------------
// Fiber::finish — called after entry() returns; switches back to scheduler
// ---------------------------------------------------------------------------
[[noreturn]] LOGOS_GREEN void Fiber::finish(Fiber* self) noexcept {
    LOGOS_ASSERT(self != nullptr, "REACTOR-FIBER-020",
                 "Fiber::finish called with null Fiber*");
    self->scheduler_->fiber_done(self);
    // __builtin_unreachable() not usable in [[clang::green]] context (Jenny 19).
    for (;;) {}  // fiber_done never returns
}

} // namespace logos::reactor
