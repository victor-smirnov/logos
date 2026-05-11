// Logos project — https://github.com/victor-smirnov/logos

#include <logos/reactor/fiber.hpp>
#include <logos/reactor/scheduler.hpp>
#include <logos/verification/assert.hpp>

#include <cstring>
#include <sys/mman.h>

namespace logos::reactor {

uint64_t Fiber::next_id_ = 1;

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------
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

// ---------------------------------------------------------------------------
// Destruction
// ---------------------------------------------------------------------------
Fiber::~Fiber() noexcept {
    if (stack_base_) {
        if (pool_) {
            pool_->release({ stack_base_, stack_size_ });
        } else {
            size_t total = stack_size_ + 4096;
            ::munmap(stack_base_, total);
        }
        stack_base_ = nullptr;
    }
}

// ---------------------------------------------------------------------------
// init_stack — set up the initial FiberRegs for the entry trampoline.
// ---------------------------------------------------------------------------
void Fiber::init_stack() noexcept {
    uint8_t* top = stack_base_ + 4096 + stack_size_;

    // SysV ABI: at function entry rsp % 16 == 8 (call has pushed the return addr).
    top = reinterpret_cast<uint8_t*>(reinterpret_cast<uintptr_t>(top) & ~uintptr_t(15));

    top -= 8;   // top % 16 == 8 after this push
    *reinterpret_cast<uint64_t*>(top) = reinterpret_cast<uint64_t>(fiber_entry_trampoline);

    regs_.rsp = reinterpret_cast<uint64_t>(top);
    regs_.r12 = reinterpret_cast<uint64_t>(this);
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
    __builtin_unreachable();
}

} // namespace logos::reactor
