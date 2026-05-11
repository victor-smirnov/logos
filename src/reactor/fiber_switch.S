// Logos project — https://github.com/victor-smirnov/logos
//
// x86-64 System V ABI context switch for green fibers.
//
// struct FiberRegs {
//   uint64_t rbp;         // offset  0
//   uint64_t rbx;         // offset  8
//   uint64_t r12;         // offset 16  (new-fiber: holds Fiber* for trampoline)
//   uint64_t r13;         // offset 24
//   uint64_t r14;         // offset 32
//   uint64_t r15;         // offset 40
//   uint64_t rsp;         // offset 48  (return address is on top of restored stack)
// };

    .text

// ---------------------------------------------------------------------------
// fiber_switch(FiberRegs* from, const FiberRegs* to)
//   rdi = from, rsi = to
//
// Saves callee-saved registers + rsp into *from.
// Restores from *to, then returns into the 'to' fiber.
// The return address for the 'to' fiber is already on top of its saved stack.
// ---------------------------------------------------------------------------
    .globl  fiber_switch
    .type   fiber_switch, @function
fiber_switch:
    // Save caller's callee-saved registers to *from (rdi).
    movq    %rbp,  0(%rdi)
    movq    %rbx,  8(%rdi)
    movq    %r12, 16(%rdi)
    movq    %r13, 24(%rdi)
    movq    %r14, 32(%rdi)
    movq    %r15, 40(%rdi)
    movq    %rsp, 48(%rdi)   // rsp points at caller's return address

    // Restore 'to' fiber's registers from *rsi.
    movq     0(%rsi), %rbp
    movq     8(%rsi), %rbx
    movq    16(%rsi), %r12
    movq    24(%rsi), %r13
    movq    32(%rsi), %r14
    movq    40(%rsi), %r15
    movq    48(%rsi), %rsp   // restore 'to' fiber's stack

    // Return into the 'to' fiber. The return address (resume point or
    // fiber_entry_trampoline for new fibers) is on top of the stack.
    ret
    .size   fiber_switch, .-fiber_switch

// ---------------------------------------------------------------------------
// fiber_entry_trampoline()
//
// Entry point for newly-created fibers. The initial FiberRegs.rsp points
// here via a fake call frame set up in Fiber::init_stack().
//
// On entry:  r12 = Fiber*
// Calls:     Fiber::entry(Fiber*)    [C++ static method]
// Then:      Fiber::finish(Fiber*)   [noreturn — switches to scheduler]
// ---------------------------------------------------------------------------
    .globl  fiber_entry_trampoline
    .type   fiber_entry_trampoline, @function
fiber_entry_trampoline:
    // r12 holds the Fiber pointer (set in FiberRegs by init_stack).
    // Move it into rdi (first argument register) and call entry().
    movq    %r12, %rdi
    // Align stack to 16 bytes before call (ABI requirement).
    // On entry here the stack is already aligned (init_stack guarantees this).
    callq   _ZN5logos7reactor5Fiber5entryEPS1_    // Fiber::entry(Fiber*)

    // If entry() returns (normal fiber completion), call finish().
    movq    %r12, %rdi
    callq   _ZN5logos7reactor5Fiber6finishEPS1_   // Fiber::finish(Fiber*)

    // finish() is [[noreturn]] — this is unreachable.
    ud2
    .size   fiber_entry_trampoline, .-fiber_entry_trampoline
