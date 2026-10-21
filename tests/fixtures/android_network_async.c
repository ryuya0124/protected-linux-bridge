/* MIT. Pending network setup must never wait on the input worker. */
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#ifdef __APPLE__
#define PR_SET_PDEATHSIG 1
static int prctl(int option, ...) {
    (void)option;
    errno = ENOTSUP;
    return -1;
}
#else
#include <sys/prctl.h>
#endif
#define PLB_ANDROID4G_RUNTIME "/never-executed"
#include "android_network_session.h"

static void run(int reply) {
    int channel[2], release[2];
    assert(!pipe(channel) && !pipe(release));
    pid_t child = fork();
    assert(child >= 0);
    if (!child) {
        close(channel[0]);
        close(release[1]);
        char go;
        if (read(release[0], &go, 1) != 1)
            _exit(10);
        if (reply >= 0) {
            unsigned char result = (unsigned char)reply;
            if (write(channel[1], &result, 1) != 1)
                _exit(11);
        }
        close(channel[1]);
        for (;;)
            pause();
    }
    close(channel[1]);
    close(release[0]);
    android_network_child = child;
    android_network_result_fd = channel[0];
    android_network_result = 2;
    android_network_deadline = android_network_clock() + 30;
    // No reply exists yet. A blocking implementation hits the outer test deadline.
    assert(android_network_status() == 2);
    assert(fcntl(channel[0], F_GETFD) >= 0);
    assert(write(release[1], "R", 1) == 1);
    struct pollfd ready = {channel[0], POLLIN, 0};
    assert(poll(&ready, 1, 1000) == 1);
    assert(android_network_status() == (reply == 0 ? 0 : 1));
    assert(android_network_result_fd == -1);
    assert(fcntl(channel[0], F_GETFD) < 0 && errno == EBADF);
    if (reply == 0) {
        // A once-ready relay can die later. The UI must report failure without
        // reaping it early (cleanup still owns its unreusable child PID).
        assert(!kill(child, SIGTERM));
        int result = 0;
        for (int i = 0; i < 100 && result == 0; i++) {
            usleep(1000);
            result = android_network_status();
        }
        assert(result == 1);
        siginfo_t information = {0};
        assert(!waitid(P_PID, child, &information, WEXITED | WNOHANG | WNOWAIT));
        assert(information.si_pid == child);
    }
    android_network_stop();
    assert(android_network_child == -1);
    close(release[1]);
}

int main(void) {
    (void)android_network_start; /* Platform spawn is exercised on Android separately. */
    run(0);
    run(1);
    run(-1); /* EOF without a result must fail closed. */
    puts("PLB_NETWORK_ASYNC_OK");
    return 0;
}
