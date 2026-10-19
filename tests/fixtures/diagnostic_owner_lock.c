/* MIT. Unit-test executable with a compiler-selected temporary lock path. */
#include "diagnostic_owner_lock.h"
#include <assert.h>
#include <string.h>

int main(int argc, char **argv) {
    assert(argc == 2);
    int fd = diagnostic_owner_lock();
    int expected = !strcmp(argv[1], "success");
    assert((fd >= 0) == expected);
    assert(!getenv(PLB_DIAGNOSTIC_OWNER_ENV));
    if (fd >= 0) {
        assert(fcntl(fd, F_GETFD) & FD_CLOEXEC);
        close(fd);
    }
    return 0;
}
