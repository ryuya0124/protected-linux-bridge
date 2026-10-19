/* MIT. Regular-file restoration and owned-controller death; never real sysfs. */
#define _GNU_SOURCE
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/file.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

static int mock_pipe2(int descriptors[2], int flags) {
    (void)flags;
    if (pipe(descriptors))
        return -1;
    return fcntl(descriptors[0], F_SETFD, FD_CLOEXEC) || fcntl(descriptors[1], F_SETFD, FD_CLOEXEC);
}
#define pipe2 mock_pipe2
#define PLB_SHMEM_GUARD_TIMEOUT_MS 250
#include "small_shmem_guard.h"

int main(int argc, char **argv) {
    assert(argc == 2);
    int owner = open(PLB_SHMEM_GUARD_TEST_PREFIX "/owner", O_RDWR | O_CREAT, 0600);
    assert(owner >= 0 && flock(owner, LOCK_EX | LOCK_NB) == 0);
    struct plb_shmem_guard guard = {.guardian = -1, .alive_write = -1};
    if (!strcmp(argv[1], "refuse-original")) {
        assert(plb_shmem_begin(&guard, owner) == -1);
        assert(guard.guardian == -1);
        return 0;
    }
    if (!strcmp(argv[1], "normal") || !strcmp(argv[1], "concurrent-change")) {
        assert(plb_shmem_begin(&guard, owner) == 0);
        for (size_t i = 0; i < 3; i++) {
            char mode[32];
            assert(plb_shmem_read(plb_shmem_paths[i], mode) == 0 && !strcmp(mode, "advise"));
        }
        if (!strcmp(argv[1], "concurrent-change")) {
            assert(plb_shmem_write(plb_shmem_paths[1], "always") == 0);
            assert(plb_shmem_finish(&guard) == -1);
            char mode[32];
            assert(plb_shmem_read(plb_shmem_paths[1], mode) == 0 && !strcmp(mode, "always"));
        } else {
            assert(plb_shmem_finish(&guard) == 0 && plb_shmem_original());
        }
        return 0;
    }
    assert(!strcmp(argv[1], "controller-death") || !strcmp(argv[1], "deadline"));
    pid_t controller = fork();
    assert(controller >= 0);
    if (!controller) {
        assert(plb_shmem_begin(&guard, owner) == 0);
        if (!strcmp(argv[1], "controller-death"))
            kill(getpid(), SIGKILL);
        sleep(3); /* The guardian must kill only this owned controller first. */
        _exit(3);
    }
    int status = 0;
    assert(waitpid(controller, &status, 0) == controller);
    assert(WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL);
    for (int i = 0; i < 100 && !plb_shmem_original(); i++)
        usleep(10000);
    assert(plb_shmem_original());
    close(owner);
    return 0;
}
