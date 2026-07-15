// Logos runtime — monotonic clock provisioning (conventional-OS native impl).
//
// The deem incremental engine (stdlib/std/deem/incr_rec.logos) reads a
// monotonic nanosecond clock through logos_clock_now instead of naming
// clock_gettime directly, so the reasoning engine stays free of the std.time
// OS-API surface and can sit in the mem tier (a lean CEP for microcontrollers).
// Trace ns-fields and the S2 wall-clock anytime-budget (ADR 0015 §3) both read
// this hook.
//
// It lives in liblstdlib_rt.a — the native runtime glue every conventional
// build links, beside the context-switch asm (fiber_ctx.S) and the fiber-stack
// provisioner (fiber_stack.c). A monotonic tick is a native-runtime primitive
// of the same kind (a cycle counter on bare metal, CLOCK_MONOTONIC here), not
// std-tier OS-API. A true LCM target does not link this archive and supplies
// its own logos_clock_now (e.g. the manycore tick source).

#include <stdint.h>
#include <time.h>

// Monotonic nanoseconds. Matches the old std.time instant_now().nanos_since_epoch()
// (CLOCK_MONOTONIC, sec*1e9 + nsec) so trace/budget numbers are unchanged.
int64_t logos_clock_now(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0;
    }
    return (int64_t)ts.tv_sec * 1000000000LL + (int64_t)ts.tv_nsec;
}
