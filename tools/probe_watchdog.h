/* MIT. Wait only for the direct diagnostic child created by this caller.
 * Timeout never signals a process group or a PID discovered by scanning /proc.
 */
#ifndef PLB_PROBE_WATCHDOG_H
#define PLB_PROBE_WATCHDOG_H
#include <sys/wait.h>
#include <signal.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>

static long long plb_probe_millis(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return now.tv_sec * 1000LL + now.tv_nsec / 1000000;
}

static int plb_probe_wait(pid_t child, int timeout_ms) {
    if (child <= 0 || timeout_ms <= 0) {
        return 126;
    }
    long long deadline = plb_probe_millis() + timeout_ms;
    int status = 0;
    for (;;) {
        pid_t done = waitpid(child, &status, WNOHANG);
        if (done == child) {
            return WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
        }
        if (done < 0 && errno != EINTR) {
            return 126;
        }
        if (plb_probe_millis() >= deadline) {
            /* An unreaped direct child cannot be reused as somebody else's PID. */
            kill(child, SIGKILL);
            while (waitpid(child, &status, 0) < 0 && errno == EINTR) {
            }
            return 124;
        }
        struct timespec delay = {.tv_nsec = 20000000};
        nanosleep(&delay, NULL);
    }
}
#endif
