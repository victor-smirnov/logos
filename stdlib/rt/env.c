// logos_argc / logos_argv — argc/argv capture for std.env.
//
// Reads /proc/self/cmdline before main() via a constructor function.
// A temporary SIGSEGV handler guards against sandbox environments that
// kill denied syscalls with SIGSEGV instead of returning -EPERM.

#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <signal.h>
#include <setjmp.h>

static int     g_argc = 0;
static char  **g_argv = NULL;
static char   *g_buf  = NULL;

static sigjmp_buf env_jmp_;

static void env_segv_(int sig)
{
    (void)sig;
    siglongjmp(env_jmp_, 1);
}

static void __attribute__((constructor)) logos_env_init(void)
{
    /* Guard against seccomp-SIGSEGV sandboxes. */
    struct sigaction sa, old_segv, old_bus;
    sa.sa_handler = env_segv_;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGSEGV, &sa, &old_segv);
    sigaction(SIGBUS,  &sa, &old_bus);

    if (sigsetjmp(env_jmp_, 1) != 0) {
        /* Took a signal — restore handlers and bail out gracefully. */
        sigaction(SIGSEGV, &old_segv, NULL);
        sigaction(SIGBUS,  &old_bus,  NULL);
        return;
    }

    int fd = openat(AT_FDCWD, "/proc/self/cmdline", O_RDONLY);
    if (fd < 0) goto done;

    char tmp[65536];
    int n = (int)read(fd, tmp, sizeof(tmp) - 1);
    close(fd);
    if (n <= 0) goto done;
    tmp[n] = '\0';

    int argc = 0;
    for (int i = 0; i < n; i++) if (tmp[i] == '\0') argc++;
    if (argc == 0) goto done;

    g_buf  = (char*)malloc((size_t)(n + 1));
    g_argv = (char**)malloc((size_t)(argc + 1) * sizeof(char*));
    if (!g_buf || !g_argv) { free(g_buf); free(g_argv); g_buf = NULL; g_argv = NULL; goto done; }

    memcpy(g_buf, tmp, (size_t)(n + 1));
    g_argv[0] = g_buf;
    int ai = 1;
    for (int i = 0; i < n && ai < argc; i++)
        if (g_buf[i] == '\0') g_argv[ai++] = g_buf + i + 1;
    g_argv[argc] = NULL;
    g_argc = argc;

done:
    sigaction(SIGSEGV, &old_segv, NULL);
    sigaction(SIGBUS,  &old_bus,  NULL);
}

int logos_argc(void)          { return g_argc; }
const char **logos_argv(void) { return (const char **)g_argv; }
