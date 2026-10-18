#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
/* Model root ownership only for resource-file checks on the unprivileged Mac. */
static int test_fstat(int fd, struct stat *st) {
    int r = fstat(fd, st);
    if (!r)
        st->st_uid = 0;
    return r;
}
static int injected_sync(int fd) {
    static int count;
    const char *failure = getenv("PLB_FAIL_SYNC");
    if (failure && ++count == atoi(failure)) {
        errno = EIO;
        return -1;
    }
    return fsync(fd);
}
static int injected_rename(const char *from, const char *to) {
    static int count;
    const char *failure = getenv("PLB_FAIL_RENAME");
    if (failure && ++count == atoi(failure)) {
        errno = EIO;
        return -1;
    }
    return rename(from, to);
}
#define fsync injected_sync
#define rename injected_rename
#define fstat test_fstat
#define BASE "PLB_TEST_BASE_PLACEHOLDER"
#define OVERLAY BASE "/current"
#include "stream_relay.h"
#include "resource_settings.h"
#include "disk_migration_state.h"
#include "storage_state.h"
int main(int argc, char **argv) {
    if (argc < 2)
        return 2;
    if (argv[1][0] == 'p')
        return storage_promote(2) ? 1 : 0;
    if (argv[1][0] == 'r')
        return storage_recover() ? 1 : 0;
    if (argv[1][0] == 's') {
        struct resource_settings r = {512, 1, 30, 4, 1, 1};
        return resources_save(&r);
    }
    return 2;
}
