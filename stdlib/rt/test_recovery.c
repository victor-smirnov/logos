// Logos test harness — panic-recovery runtime.
//
// Provides setjmp/longjmp infrastructure so a #[test] runner can survive
// assertion failures. Each test runs inside `logos_run_with_recovery`:
// the call returns 0 on normal completion, 1 on panic. The last panic
// message is captured in a thread-local buffer for the runner to print.

#include <setjmp.h>
#include <stddef.h>
#include <string.h>

#define LOGOS_PANIC_MSG_CAP 1024

// Plain globals (not __thread): the test runner is single-threaded.
// Using TLS here would emit R_X86_64_TPOFF32 relocations that the metaprog
// ORC JIT's RuntimeDyld cannot resolve (same issue documented for
// fiber_ctx.S → liblstdlib_fibers.a), forcing this object into the
// JIT-excluded archive set.
static jmp_buf* g_logos_panic_jmp = NULL;
static char     g_logos_panic_msg[LOGOS_PANIC_MSG_CAP];
static int      g_logos_panic_msg_len = 0;

// Hook called by stdlib panic / __fmt_panic before abort. If a recovery
// jmp_buf is installed, copy the message into TLS and longjmp out.
// Otherwise it's a no-op — caller proceeds with abort().
void logos_panic_maybe_longjmp(const char* msg, long n) {
    if (g_logos_panic_jmp == NULL) return;
    int cap = LOGOS_PANIC_MSG_CAP - 1;
    int len = (int)((n < cap) ? n : cap);
    if (msg && len > 0) memcpy(g_logos_panic_msg, msg, (size_t)len);
    g_logos_panic_msg[len] = '\0';
    g_logos_panic_msg_len = len;
    longjmp(*g_logos_panic_jmp, 1);
}

// Run a no-arg function pointer under panic recovery. Returns:
//   0 — function returned normally
//   1 — function panicked (longjmp from logos_panic_maybe_longjmp)
int logos_run_with_recovery(void (*fn)(void)) {
    jmp_buf  buf;
    jmp_buf* saved = g_logos_panic_jmp;
    g_logos_panic_jmp = &buf;
    g_logos_panic_msg_len = 0;
    g_logos_panic_msg[0] = '\0';
    int rv = setjmp(buf);
    if (rv == 0) {
        fn();
        g_logos_panic_jmp = saved;
        return 0;
    }
    g_logos_panic_jmp = saved;
    return 1;
}

// Accessors for the captured panic message (read by the test runner
// after a recovery returned 1).
const char* logos_panic_last_msg(void)    { return g_logos_panic_msg; }
long        logos_panic_last_msg_len(void){ return (long)g_logos_panic_msg_len; }

// ── Test-runner print helpers ─────────────────────────────────────────
// Implemented in C so the synthesized test-main Logos source can stay
// minimal. All output goes to stderr (matches Rust's libtest).
#include <stdio.h>

void logos_test_print_start(const char* name, long n) {
    fputs("test ", stderr);
    if (name && n > 0) fwrite(name, 1, (size_t)n, stderr);
    fputs(" ... ", stderr);
}

void logos_test_print_ok(void) { fputs("ok\n", stderr); }

void logos_test_print_failed(void) {
    fputs("FAILED\n", stderr);
    if (g_logos_panic_msg_len > 0) {
        fputs("    panic: ", stderr);
        fwrite(g_logos_panic_msg, 1, (size_t)g_logos_panic_msg_len, stderr);
        fputc('\n', stderr);
    }
}

void logos_test_print_failed_no_panic(void) {
    fputs("FAILED (expected panic, none observed)\n", stderr);
}

void logos_test_print_ignored(void) { fputs("ignored\n", stderr); }

// Final summary line + exit code. Mirrors Rust's libtest format closely
// enough to be diff-friendly in CI logs.
int logos_test_print_summary(int passed, int failed, int ignored) {
    fputc('\n', stderr);
    fputs("test result: ", stderr);
    if (failed == 0) fputs("ok. ", stderr);
    else             fputs("FAILED. ", stderr);
    fprintf(stderr, "%d passed; %d failed; %d ignored\n",
            passed, failed, ignored);
    return failed == 0 ? 0 : 1;
}
