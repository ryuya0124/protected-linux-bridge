#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#define BASE "PLB_TEST_BASE_PLACEHOLDER"
#define OVERLAY BASE "/current"
#include "disk_migration_state.h"
int main(void) {
    return disk_recover() ? 1 : 0;
}
