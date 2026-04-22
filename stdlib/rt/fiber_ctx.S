// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos stdlib — fiber context switch (x86-64 System V ABI).
//
// This file is the ONLY native dependency of the Logos fiber runtime.
// Everything else (scheduler, spawn, yield) is written in Logos itself.
//
// FiberCtx layout (must match struct FiberCtx in stdlib/fiber/fiber.logos):
//   offset  0 : rbp
//   offset  8 : rbx
//   offset 16 : r12   (new-fiber: entry fn ptr)
//   offset 24 : r13   (new-fiber: arg)
//   offset 32 : r14
//   offset 40 : r15
//   offset 48 : rsp   (return address on top of restored stack)

    .text

// ---------------------------------------------------------------------------
// logos_fiber_switch(from: *mut FiberCtx, to: *const FiberCtx)
//   rdi = from, rsi = to
//
// Saves callee-saved registers + rsp into *from.
// Restores from *to and returns into the 'to' fiber.
// ---------------------------------------------------------------------------
    .globl  logos_fiber_switch
    .type   logos_fiber_switch, @function
logos_fiber_switch:
    movq    %rbp,  0(%rdi)
    movq    %rbx,  8(%rdi)
    movq    %r12, 16(%rdi)
    movq    %r13, 24(%rdi)
    movq    %r14, 32(%rdi)
    movq    %r15, 40(%rdi)
    movq    %rsp, 48(%rdi)

    movq     0(%rsi), %rbp
    movq     8(%rsi), %rbx
    movq    16(%rsi), %r12
    movq    24(%rsi), %r13
    movq    32(%rsi), %r14
    movq    40(%rsi), %r15
    movq    48(%rsi), %rsp
    ret
    .size   logos_fiber_switch, .-logos_fiber_switch

// ---------------------------------------------------------------------------
// logos_fiber_entry — trampoline for newly spawned fibers.
//
// On entry:  r12 = fn ptr (fn(i64) -> ())
//            r13 = arg   (i64)
//
// Calls fn(arg), then calls logos_fiber_done() which never returns.
// ---------------------------------------------------------------------------
    .globl  logos_fiber_entry
    .type   logos_fiber_entry, @function
logos_fiber_entry:
    // On entry here (via ret in fiber_switch), rsp = stack_top (16-byte aligned).
    // The fake "return address" was popped by ret, so rsp is now 16-byte aligned.
    // The ABI requires rsp to be 16-byte aligned before a call instruction,
    // which is exactly the case here.
    movq    %r13, %rdi
    callq   *%r12
    // After fn returns, call the Logos-level done handler.
    callq   logos_fiber_done
    // logos_fiber_done() never returns.
    ud2
    .size   logos_fiber_entry, .-logos_fiber_entry

// ---------------------------------------------------------------------------
// logos_get_fiber_entry_addr() -> i64
//   Returns the address of logos_fiber_entry so Logos code can store it
//   into FiberCtx.rsp (as the fake return address for a new fiber).
// ---------------------------------------------------------------------------
    .globl  logos_get_fiber_entry_addr
    .type   logos_get_fiber_entry_addr, @function
logos_get_fiber_entry_addr:
    leaq    logos_fiber_entry(%rip), %rax
    ret
    .size   logos_get_fiber_entry_addr, .-logos_get_fiber_entry_addr

// ---------------------------------------------------------------------------
// Per-thread scheduler pointer (one global for now; thread-local later).
//
// logos_sched_get() -> *mut u8
// logos_sched_set(ptr: *mut u8)
// ---------------------------------------------------------------------------
    .data
    .align  8
    .globl  _logos_sched_g
_logos_sched_g:
    .quad   0

    .text

    .globl  logos_sched_get
    .type   logos_sched_get, @function
logos_sched_get:
    movq    _logos_sched_g(%rip), %rax
    ret
    .size   logos_sched_get, .-logos_sched_get

    .globl  logos_sched_set
    .type   logos_sched_set, @function
logos_sched_set:
    movq    %rdi, _logos_sched_g(%rip)
    ret
    .size   logos_sched_set, .-logos_sched_set
