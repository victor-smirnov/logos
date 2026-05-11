// Logos project — https://github.com/victor-smirnov/logos
//
// __morestack — split-stack overflow handler for green fibers (x86-64).
//
// Called by the compiler-generated prologue when a [[clang::green]] function
// does not have enough stack space in the current segment.
//
// On entry (compiler convention):
//   %r10 = required frame size
//   %r11 = argument bytes on stack
//   return address on stack = address in prologue after "call __morestack"
//
// The handler:
//   1. Saves all caller-saved registers + XMM0-7.
//   2. Calls green_fiber_allocate_segment(frame_size, arg_size, return_addr)
//      which allocates a new segment and updates %fs:0x70.
//   3. Switches %rsp to the new segment.
//   4. Restores registers.
//   5. Returns — the caller (function prologue) executes its "retq", which
//      returns to the original call site; the function is retried on the
//      new stack with a freshly set %fs:0x70 limit.
//
// __morestack_release_segments — called by the compiler epilogue when a
// function returns while running on an extended (non-root) segment.
// Calls green_fiber_release_segment() which pops the segment and
// restores %fs:0x70.
//
// Only the active __morestack/__morestack_release_segments symbols are
// emitted when LOGOS_HAS_GREEN_STACKS=1.  The split-stack marker sections
// are always present (they are informational for the linker).

#include <logos/reactor/features.hpp>

#if LOGOS_HAS_GREEN_STACKS

    .text

// ---------------------------------------------------------------------------
// __morestack
// ---------------------------------------------------------------------------
    .globl  __morestack
    .type   __morestack, @function
    .align  16

__morestack:
    // Standard frame.
    push    %rbp
    mov     %rsp, %rbp

    // Save all caller-saved general-purpose registers.
    push    %rax
    push    %rcx
    push    %rdx
    push    %rsi
    push    %rdi
    push    %r8
    push    %r9
    push    %r10    // frame_size
    push    %r11    // arg_size

    // Save XMM0-7 (caller-saved, needed for floating-point arguments).
    sub     $128, %rsp
    movdqa  %xmm0,   0(%rsp)
    movdqa  %xmm1,  16(%rsp)
    movdqa  %xmm2,  32(%rsp)
    movdqa  %xmm3,  48(%rsp)
    movdqa  %xmm4,  64(%rsp)
    movdqa  %xmm5,  80(%rsp)
    movdqa  %xmm6,  96(%rsp)
    movdqa  %xmm7, 112(%rsp)

    // Call green_fiber_allocate_segment(frame_size, arg_size, return_addr).
    // SysV ABI: rdi=arg1, rsi=arg2, rdx=arg3.
    mov     %r10, %rdi      // frame_size
    mov     %r11, %rsi      // arg_size
    mov     8(%rbp), %rdx   // return address (after "call __morestack" in prologue)

    call    green_fiber_allocate_segment@PLT

    // rax = new RSP (top of new segment), or nullptr on failure.
    test    %rax, %rax
    jz      .Lallocation_failed

    // Restore XMM registers.
    movdqa   0(%rsp), %xmm0
    movdqa  16(%rsp), %xmm1
    movdqa  32(%rsp), %xmm2
    movdqa  48(%rsp), %xmm3
    movdqa  64(%rsp), %xmm4
    movdqa  80(%rsp), %xmm5
    movdqa  96(%rsp), %xmm6
    movdqa 112(%rsp), %xmm7
    add     $128, %rsp

    // Restore general-purpose registers (all except rax which we still need).
    pop     %r11
    pop     %r10
    pop     %r9
    pop     %r8
    pop     %rdi
    pop     %rsi
    pop     %rdx
    pop     %rcx

    // Stash new RSP in %r10; restore %rax.
    mov     %rax, %r10
    pop     %rax

    // Collect both return addresses from the saved frame:
    //   8(%rbp)  = ret_to_prologue_ret  (the "retq" after "call __morestack")
    //  16(%rbp)  = ret_to_caller        (pushed by the original "call func")
    mov     8(%rbp), %r11       // ret_to_prologue_ret
    mov    16(%rbp), %rcx       // ret_to_caller

    // Restore old frame pointer.
    mov     (%rbp), %rbp

    // Switch to the new stack segment.
    mov     %r10, %rsp

    // Push both return addresses on the new stack (caller deeper, ret on top).
    // Our "ret" pops ret_to_prologue_ret → jumps to the "retq" in the prologue.
    // That "retq" pops ret_to_caller → returns to the original caller.
    // The caller retries the call; %fs:0x70 is now set to the new segment
    // limit so the prologue stack check succeeds.
    push    %rcx                // ret_to_caller  (deeper)
    push    %r11                // ret_to_prologue_ret (on top)
    ret

.Lallocation_failed:
    call    green_fiber_allocation_failed@PLT
    ud2     // unreachable

    .size __morestack, .-__morestack


// ---------------------------------------------------------------------------
// __morestack_non_split — called by lld when a split-stack function calls a
// non-split-stack callee.  lld replaces `call __morestack` with
// `call __morestack_non_split` in adjusted prologues.
//
// For prologues where lld used "Pattern 1" relaxation (replaced the original
// `cmp %fs:0x70,%rsp` with `stc; nop`), the stack check ALWAYS fails — even
// after __morestack allocates a new segment.  Retrying the function would
// loop forever.
//
// Fix: increment the return address by 1.  The prologue layout is:
//      call __morestack_non_split   ; 5 bytes
//      ret                          ; 1 byte  ← original return address
//      push %rbp                    ; 1 byte  ← function body start
//
// Skipping the `ret` makes __morestack jump directly into the function body,
// bypassing the infinite-retry prologue.  The caller's return address is
// still placed on the new stack by __morestack so the function returns
// normally when done.
//
// We also add 0x4000 (16 KB) to %r10 (frame_size) because lld's threshold
// adjustment is only in the prologue check, not in %r10.
// ---------------------------------------------------------------------------
    .globl  __morestack_non_split
    .type   __morestack_non_split, @function
    .align  16

__morestack_non_split:
    // lld adjusts the caller's prologue threshold (+16 KB) but does not
    // change %r10 (requested frame_size).  Add the headroom here so the
    // new segment is large enough for the adjusted check to pass on retry.
    add     $0x4000, %r10
    jmp     __morestack

    .size   __morestack_non_split, .-__morestack_non_split


// ---------------------------------------------------------------------------
// __morestack_release_segments
// ---------------------------------------------------------------------------
    .globl  __morestack_release_segments
    .type   __morestack_release_segments, @function
    .align  16

__morestack_release_segments:
    push    %rbp
    mov     %rsp, %rbp

    push    %rax
    push    %rcx
    push    %rdx
    push    %rsi
    push    %rdi
    push    %r8
    push    %r9
    push    %r10
    push    %r11

    call    green_fiber_release_segment@PLT

    pop     %r11
    pop     %r10
    pop     %r9
    pop     %r8
    pop     %rdi
    pop     %rsi
    pop     %rdx
    pop     %rcx
    pop     %rax

    pop     %rbp
    ret

    .size __morestack_release_segments, .-__morestack_release_segments

#endif // LOGOS_HAS_GREEN_STACKS

// Split-stack linker marker sections (always emitted).
    .section .note.GNU-split-stack,"",@progbits
    .section .note.GNU-stack,"",@progbits
    .section .note.GNU-no-split-stack,"",@progbits
