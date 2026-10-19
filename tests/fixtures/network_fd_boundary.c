/* MIT. Execute in a disposable child: VM-like FDs close, the listener survives. */
#include <fcntl.h>
#include <stdio.h>
#include <sys/wait.h>
#include "network_fd_boundary.h"
int main(void) {
    int ram = open("/dev/null", O_RDONLY), disk = dup(ram), listener = dup(ram);
    if (ram < 3 || disk < 3 || listener < 3)
        return 1;
    pid_t child = fork();
    if (!child) {
        if (plb_network_close_inherited(listener)) {
#ifdef __APPLE__
            // macOS has no /proc: the production helper must refuse this path.
            _exit(errno == ENOENT ? 0 : 2);
#else
            _exit(2);
#endif
        }
        if (fcntl(ram, F_GETFD) != -1 || errno != EBADF || fcntl(disk, F_GETFD) != -1 ||
            errno != EBADF || fcntl(listener, F_GETFD) < 0)
            _exit(3);
        _exit(0);
    }
    int status;
    if (child < 0 || waitpid(child, &status, 0) != child || !WIFEXITED(status) ||
        WEXITSTATUS(status))
        return 4;
    if (fcntl(ram, F_GETFD) < 0 || fcntl(disk, F_GETFD) < 0 || fcntl(listener, F_GETFD) < 0)
        return 5;
    close(ram);
    close(disk);
    close(listener);
    puts("PLB_NETWORK_FD_BOUNDARY_OK");
    return 0;
}
