// Logos stdlib — atomic load/store/fetch-add/CAS (x86-64).
//
// Split out of fiber_ctx.S so this TU has zero TLS relocations.
// ORC RuntimeDyld can load it cleanly into a meta-jit module without
// dragging in unsupported R_X86_64_GOTTPOFF relocations from the
// fiber/thread/reactor TLS getters.

    .text

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


.section .note.GNU-stack,"",%progbits
