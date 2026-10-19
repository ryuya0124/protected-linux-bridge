/* MIT. An inherited controller lock excludes a new VM after its manager exits. */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
static const char *test_lock_path;
#define PLB_OWNER_LOCK_FILE test_lock_path
#include "diagnostic_owner_lock.h"

int main(int argc, char **argv) {
    if (argc != 2)
        return 1;
    test_lock_path = argv[1];
    int lock = profile_lock(), ready[2], release[2];
    if (lock < 3 || pipe(ready) || pipe(release))
        return 2;
    char descriptor[32];
    snprintf(descriptor, sizeof(descriptor), "%d", lock);
    if (setenv(PLB_DIAGNOSTIC_OWNER_ENV, descriptor, 1))
        return 3;
    pid_t child = fork();
    if (!child) {
        close(ready[0]);
        close(release[1]);
        int inherited = diagnostic_owner_lock();
        char byte;
        if (inherited != lock || !(fcntl(lock, F_GETFD) & FD_CLOEXEC) ||
            write(ready[1], "R", 1) != 1 || read(release[0], &byte, 1) != 1)
            _exit(4);
        close(inherited);
        _exit(0);
    }
    close(ready[1]);
    close(release[0]);
    close(lock);
    char byte;
    if (child < 0 || read(ready[0], &byte, 1) != 1)
        return 5;
    // The manager has closed its copy. The controller still owns the same lock.
    int contender = open(test_lock_path, O_RDWR | O_CLOEXEC);
    if (contender < 0 || flock(contender, LOCK_EX | LOCK_NB) != -1 ||
        (errno != EWOULDBLOCK && errno != EAGAIN))
        return 6;
    if (write(release[1], "X", 1) != 1)
        return 7;
    int status;
    if (waitpid(child, &status, 0) != child || !WIFEXITED(status) || WEXITSTATUS(status) ||
        flock(contender, LOCK_EX | LOCK_NB))
        return 8;
    close(contender);
    int unrelated = open("/dev/null", O_RDONLY);
    snprintf(descriptor, sizeof(descriptor), "%d", unrelated);
    setenv(PLB_DIAGNOSTIC_OWNER_ENV, descriptor, 1);
    if (diagnostic_owner_lock() != -1)
        return 9;
    close(unrelated);
    puts("PLB_NETWORK_OWNER_LIFETIME_OK");
    return 0;
}
