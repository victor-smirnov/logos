// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos stdlib — fiber context switch (x86-64 System V ABI).
//
// This file is the ONLY native dependency of the Logos fiber runtime.
// Everything else (scheduler, spawn, yield, thread pool) is written in Logos.
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
// Per-thread scheduler and thread pointers — thread-local storage.
//
// Using initial-exec TLS model: two-instruction access via %fs,
// works for static libs loaded at program startup (our use case).
//
// logos_sched_get() -> *mut u8
// logos_sched_set(ptr: *mut u8)
// logos_thread_get() -> *mut u8
// logos_thread_set(ptr: *mut u8)
// ---------------------------------------------------------------------------
    .section .tbss,"awT",@nobits
    .align  8
    .globl  _logos_sched_tls
_logos_sched_tls:
    .zero   8

    .globl  _logos_thread_tls
_logos_thread_tls:
    .zero   8

    .text

    .globl  logos_sched_get
    .type   logos_sched_get, @function
logos_sched_get:
    movq    _logos_sched_tls@gottpoff(%rip), %rax
    movq    %fs:(%rax), %rax
    ret
    .size   logos_sched_get, .-logos_sched_get

    .globl  logos_sched_set
    .type   logos_sched_set, @function
logos_sched_set:
    movq    _logos_sched_tls@gottpoff(%rip), %rax
    movq    %rdi, %fs:(%rax)
    ret
    .size   logos_sched_set, .-logos_sched_set

    .globl  logos_thread_get
    .type   logos_thread_get, @function
logos_thread_get:
    movq    _logos_thread_tls@gottpoff(%rip), %rax
    movq    %fs:(%rax), %rax
    ret
    .size   logos_thread_get, .-logos_thread_get

    .globl  logos_thread_set
    .type   logos_thread_set, @function
logos_thread_set:
    movq    _logos_thread_tls@gottpoff(%rip), %rax
    movq    %rdi, %fs:(%rax)
    ret
    .size   logos_thread_set, .-logos_thread_set

// ---------------------------------------------------------------------------
// Atomic load/store for i64 — SPSC correctness on x86 TSO.
// Plain mov is already acquire-load / release-store on x86; the extern
// function boundary prevents LLVM from reordering across the call.
//
// logos_atomic_load64(ptr: *const i64) -> i64
// logos_atomic_store64(ptr: *mut i64, val: i64)
// ---------------------------------------------------------------------------
    .globl  logos_atomic_load64
    .type   logos_atomic_load64, @function
logos_atomic_load64:
    movq    (%rdi), %rax
    ret
    .size   logos_atomic_load64, .-logos_atomic_load64

    .globl  logos_atomic_store64
    .type   logos_atomic_store64, @function
logos_atomic_store64:
    movq    %rsi, (%rdi)
    ret
    .size   logos_atomic_store64, .-logos_atomic_store64

// ---------------------------------------------------------------------------
// i32 atomic load/store (plain mov is acquire/release on x86 TSO).
//
// logos_atomic_load32(ptr: *const i32) -> i32
// logos_atomic_store32(ptr: *mut i32, val: i32)
// ---------------------------------------------------------------------------
    .globl  logos_atomic_load32
    .type   logos_atomic_load32, @function
logos_atomic_load32:
    movl    (%rdi), %eax
    ret
    .size   logos_atomic_load32, .-logos_atomic_load32

    .globl  logos_atomic_store32
    .type   logos_atomic_store32, @function
logos_atomic_store32:
    movl    %esi, (%rdi)
    ret
    .size   logos_atomic_store32, .-logos_atomic_store32

// ---------------------------------------------------------------------------
// Atomic fetch-add: atomically adds val to *ptr, returns old value.
//
// logos_atomic_fetch_add64(ptr: *mut i64, val: i64) -> i64
// logos_atomic_fetch_add32(ptr: *mut i32, val: i32) -> i32
// ---------------------------------------------------------------------------
    .globl  logos_atomic_fetch_add64
    .type   logos_atomic_fetch_add64, @function
logos_atomic_fetch_add64:
    lock xaddq  %rsi, (%rdi)
    movq    %rsi, %rax
    ret
    .size   logos_atomic_fetch_add64, .-logos_atomic_fetch_add64

    .globl  logos_atomic_fetch_add32
    .type   logos_atomic_fetch_add32, @function
logos_atomic_fetch_add32:
    lock xaddl  %esi, (%rdi)
    movl    %esi, %eax
    ret
    .size   logos_atomic_fetch_add32, .-logos_atomic_fetch_add32

// ---------------------------------------------------------------------------
// Compare-and-swap: if *ptr == expected, store desired and return true.
// Returns false if *ptr != expected.
//
// logos_atomic_cas64(ptr: *mut i64, expected: i64, desired: i64) -> bool
// logos_atomic_cas32(ptr: *mut i32, expected: i32, desired: i32) -> bool
// ---------------------------------------------------------------------------
    .globl  logos_atomic_cas64
    .type   logos_atomic_cas64, @function
logos_atomic_cas64:
    movq    %rsi, %rax
    lock cmpxchgq %rdx, (%rdi)
    sete    %al
    movzbl  %al, %eax
    ret
    .size   logos_atomic_cas64, .-logos_atomic_cas64

    .globl  logos_atomic_cas32
    .type   logos_atomic_cas32, @function
logos_atomic_cas32:
    movl    %esi, %eax
    lock cmpxchgl %edx, (%rdi)
    sete    %al
    movzbl  %al, %eax
    ret
    .size   logos_atomic_cas32, .-logos_atomic_cas32
