// Logos runtime — fiber stack provisioning (conventional-OS native impl).
//
// The lcm fiber core (stdlib/lcm/fiber/fiber.logos) allocates fiber stacks
// through logos_fiber_stack_alloc / logos_fiber_stack_free instead of naming
// mmap directly, so the lcm tier stays free of syscalls. This file is that
// hook's conventional-OS implementation: an anonymous mmap with a PROT_NONE
// guard page over the low (overflow) end, so a fiber that overruns its stack
// faults instead of silently smashing the next allocation.
//
// It lives in liblstdlib_rt.a — the native runtime glue every conventional
// build links, right beside the context-switch asm (fiber_ctx.S). Stack
// provisioning is a native-runtime primitive of the same kind, not app-tier
// logic. A true LCM target does not link this archive and supplies its own
// logos_fiber_stack_alloc/free (e.g. scratchpad allocation over virtio).

#include <stdint.h>
#include <sys/mman.h>

// Guard page over the low GUARD_SIZE bytes; `total` (from the lcm core) already
// includes it (STACK_SIZE + GUARD_SIZE). Kept in sync with fiber.logos.
#define LOGOS_FIBER_GUARD_SIZE 4096

void* logos_fiber_stack_alloc(int64_t total) {
    void* base = mmap(0, (size_t)total, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (base == MAP_FAILED) {
        return 0;
    }
    // Arm the guard page at the low end; ignore failure (stack still usable).
    (void)mprotect(base, LOGOS_FIBER_GUARD_SIZE, PROT_NONE);
    return base;
}

void logos_fiber_stack_free(void* base, int64_t total) {
    (void)munmap(base, (size_t)total);
}
