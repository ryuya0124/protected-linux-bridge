/* Exercises real POSIX child isolation, including a blocked-alarm worker. */
#include "probe_watchdog.h"
#include <assert.h>
#include <string.h>

int main(int argc, char **argv) {
    assert(argc == 2);
    if (!strcmp(argv[1], "invalid")) {
        assert(plb_probe_wait(0, 20) == 126);
        assert(plb_probe_wait(-1, 20) == 126);
        return 0;
    }
    pid_t sibling = fork();
    assert(sibling >= 0);
    if (!sibling) {
        for (;;) {
            pause();
        }
    }
    pid_t worker = fork();
    assert(worker >= 0);
    if (!worker) {
        if (!strcmp(argv[1], "success")) {
            _exit(7);
        }
        if (!strcmp(argv[1], "signal")) {
            raise(SIGTERM);
            _exit(1);
        }
        sigset_t blocked;
        sigfillset(&blocked);
        sigprocmask(SIG_SETMASK, &blocked, NULL);
        for (;;) {
            pause();
        }
    }
    long long start = plb_probe_millis();
    int result = plb_probe_wait(worker, 100);
    if (!strcmp(argv[1], "success")) {
        assert(result == 7);
    } else if (!strcmp(argv[1], "signal")) {
        assert(result == 128 + SIGTERM);
    } else {
        assert(result == 124);
        assert(plb_probe_millis() - start < 2000);
    }
    assert(kill(sibling, 0) == 0);
    kill(sibling, SIGKILL);
    waitpid(sibling, NULL, 0);
    return 0;
}
