#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#define PLB_OWNER_LOCK_FILE getenv("PLB_TEST_LOCK")
#include "profile_lock.h"
int main(int argc, char **argv) {
    int shared_only = argc == 2 && !strcmp(argv[1], "shared-only");
    /* Reference the real owner gate as well as the maintenance lease. */
    if (!shared_only) {
        int owner = profile_lock();
        if (owner < 0)
            return 2;
        close(owner);
    }
    int fd = profile_maintenance_lock(argc > 1 && !shared_only ? LOCK_EX : LOCK_SH);
    if (fd < 0)
        return 1;
    puts("LOCKED");
    fflush(stdout);
    if (argc > 2)
        getchar();
    close(fd);
    return 0;
}
