// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos

#include <logos/reactor/fiber.hpp>
#include <logos/reactor/scheduler.hpp>
#include <logos/verification/assert.hpp>

#include <sys/mman.h>
#include <cstring>
#include <format>

namespace logos::reactor {

uint64_t Fiber::next_id_ = 1;

// ---------------------------------------------------------------------------
// Fiber construction
// ---------------------------------------------------------------------------

Fiber::Fiber(std::move_only_function<void()> fn, std::string_view name, size_t stack_size)
    : stack_size_(stack_size)
    , fn_(std::move(fn))
    , name_(name)
    , id_(next_id_++)
{
    LOGOS_ASSERT(stack_size_ >= 16 * 1024, "REACTOR-FIBER-001",
                 "Stack size must be at least 16 KB, got {}", stack_size_);
    LOGOS_ASSERT(stack_size_ % 4096 == 0, "REACTOR-FIBER-002",
                 "Stack size must be a multiple of 4096, got {}", stack_size_);

    // Allocate stack with a guard page at the bottom (lowest address).
    // mmap gives zeroed memory; PROT_NONE guard page causes SIGSEGV on overflow.
    size_t total = stack_size_ + 4096;  // extra page for guard
    void* mem = mmap(nullptr, total,
                     PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK,
                     -1, 0);
    LOGOS_ASSERT(mem != MAP_FAILED, "REACTOR-FIBER-003",
                 "mmap failed for fiber '{}' stack: errno={}", name_, errno);

    // Protect the lowest page (guard page — catches stack overflow).
    int rc = mprotect(mem, 4096, PROT_NONE);
    LOGOS_ASSERT(rc == 0, "REACTOR-FIBER-004",
                 "mprotect guard page failed for fiber '{}': errno={}", name_, errno);

    stack_base_ = static_cast<uint8_t*>(mem);
    init_stack();
}

Fiber::~Fiber() {
    if (stack_base_) {
        size_t total = stack_size_ + 4096;
        munmap(stack_base_, total);
        stack_base_ = nullptr;
    }
}

// ---------------------------------------------------------------------------
// Stack initialisation
//
// Lays out a fake call frame so that fiber_switch restores into
// fiber_entry_trampoline, which will call entry(this).
//
// Stack layout (high → low address, stack grows downward):
//
//   [stack_base_ + stack_size_ + 4096]  ← top of allocation
//   ...
//   [stack top aligned]
//   [ return address = &fiber_entry_trampoline ]  ← rsp points here
//
// FiberRegs.r12 = this   (picked up by fiber_entry_trampoline → rdi)
// ---------------------------------------------------------------------------
void Fiber::init_stack() {
    // Stack top: base + 4096 (guard) + stack_size_.
    uint8_t* top = stack_base_ + 4096 + stack_size_;

    // SysV x86-64 ABI: at function entry rsp % 16 == 8
    // (the call instruction has pushed an 8-byte return address).
    //
    // Our trampoline is entered via `ret` (not `call`), so:
    //   rsp after ret = regs_.rsp + 8
    //   trampoline then does `callq Fiber::entry` → rsp -= 8
    //   inside Fiber::entry: rsp = regs_.rsp + 8 - 8 = regs_.rsp
    //
    // For rsp inside Fiber::entry to satisfy rsp % 16 == 8, we need
    // regs_.rsp % 16 == 8, i.e. push the trampoline address onto a
    // 16-byte-aligned address (top % 16 == 0 before the push).
    top = reinterpret_cast<uint8_t*>(
        reinterpret_cast<uintptr_t>(top) & ~uintptr_t(15)
    );  // top % 16 == 0

    // Push the trampoline address as the fake "return address" that ret jumps to.
    top -= 8;   // top % 16 == 8 after this push
    *reinterpret_cast<uint64_t*>(top) =
        reinterpret_cast<uint64_t>(fiber_entry_trampoline);

    regs_.rsp = reinterpret_cast<uint64_t>(top);
    regs_.r12 = reinterpret_cast<uint64_t>(this);   // passed to trampoline

    // Split-stack limit: the lowest usable byte of this fiber's stack
    // (immediately above the guard page).  fiber_switch stores this into
    // %fs:0x70 so jenny's stack-overflow prologue knows the fiber's boundary.
    regs_.stack_limit = reinterpret_cast<uint64_t>(stack_base_ + 4096);
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
[[noreturn]] void Fiber::finish(Fiber* self) noexcept {
    LOGOS_ASSERT(self != nullptr, "REACTOR-FIBER-020",
                 "Fiber::finish called with null Fiber*");
    self->scheduler_->fiber_done(self);
    // fiber_done() switches away from this fiber — we never return here.
    __builtin_unreachable();
}

} // namespace logos::reactor
