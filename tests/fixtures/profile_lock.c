#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#define PLB_OWNER_LOCK_FILE getenv("PLB_TEST_LOCK")
#include "profile_lock.h"
int main(int argc, char **argv) {
    (void)argv;
    int fd = profile_lock();
    if (fd < 0)
        return 1;
    puts("LOCKED");
    fflush(stdout);
    if (argc > 1)
        getchar();
    close(fd);
    return 0;
}
