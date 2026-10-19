#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "android_vmm_log.h"

int main(int argc, char **argv) {
    assert(argc == 2);
    int frozen_ui[2], source[2];
    assert(!pipe(frozen_ui) && !pipe(source));
    int flags = fcntl(frozen_ui[1], F_GETFL);
    assert(!fcntl(frozen_ui[1], F_SETFL, flags | O_NONBLOCK));
    char bytes[8192];
    memset(bytes, 'x', sizeof(bytes));
    while (write(frozen_ui[1], bytes, sizeof(bytes)) > 0) {
    }
    assert(errno == EAGAIN);
    assert(!fcntl(frozen_ui[1], F_SETFL, flags));
    assert(dup2(frozen_ui[1], STDOUT_FILENO) >= 0);
    assert(dup2(frozen_ui[1], STDERR_FILENO) >= 0);
    struct android_vmm_log log;
    assert(!android_vmm_log_start(&log, source[0], argv[1]));
    for (int chunk = 0; chunk < 600; chunk++) {
        size_t at = 0;
        while (at < sizeof(bytes)) {
            ssize_t count = write(source[1], bytes + at, sizeof(bytes) - at);
            assert(count > 0);
            at += (size_t)count;
        }
    }
    assert(write(source[1], "PLB_GUNYAH_", 11) == 11);
    assert(write(source[1], "PAGE_FAULT", 10) == 10);
    close(source[1]);
    android_vmm_log_stop(&log);
    assert(atomic_load(&log.fault));
    struct stat info;
    assert(!stat(argv[1], &info));
    assert(info.st_size > 0 && info.st_size <= ANDROID_VMM_LOG_LIMIT);
    assert((info.st_mode & 0777) == 0600);
    return 0;
}
