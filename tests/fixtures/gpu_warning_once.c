/* MIT. Isolated file/clock fixtures only; no /proc writes, VMM or kernel device. */
#define _GNU_SOURCE
#include <assert.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifndef CLOCK_BOOTTIME
#define CLOCK_BOOTTIME CLOCK_MONOTONIC
#endif
#define PLB_GPU_WARNING_ONCE 1
#define PLB_GPU_ONCE_TEST 1
#include "gpu_warning_once.h"

int main(void) {
    assert(plb_gpu_once_values(PLB_GPU_ONCE_BOOT_ID, 1000, 4640));
    assert(plb_gpu_once_values(PLB_GPU_ONCE_BOOT_ID, 300999, 4640));
    assert(!plb_gpu_once_values(PLB_GPU_ONCE_BOOT_ID, 301000, 4640));
    assert(!plb_gpu_once_values(PLB_GPU_ONCE_BOOT_ID, 999, 4640));
    assert(!plb_gpu_once_values("other boot", 1000, 4640));
    assert(!plb_gpu_once_values(NULL, 1000, 4640));
    assert(!plb_gpu_once_values(PLB_GPU_ONCE_BOOT_ID, 1000, 4640 | 128));
    assert(!plb_gpu_once_values(PLB_GPU_ONCE_BOOT_ID, 1000, 4608));
    assert(!plb_gpu_once_values(PLB_GPU_ONCE_BOOT_ID, 1000, 0));
    assert(plb_gpu_once_proof_fd() < 0);
    int fd = plb_gpu_once_claim_marker();
    assert(fd >= 3);
    assert(plb_gpu_once_proof_fd() == fd);
    assert(plb_gpu_once_claim_marker() < 0); /* O_EXCL: no second trial. */
    assert(!fchmod(fd, 0644));
    assert(plb_gpu_once_proof_fd() < 0);
    assert(!fchmod(fd, 0600));
    assert(plb_gpu_once_proof_fd() == fd);
    assert(!setenv(PLB_GPU_ONCE_FD_ENV, "-1", 1));
    assert(plb_gpu_once_proof_fd() < 0);
    assert(!setenv(PLB_GPU_ONCE_FD_ENV, "not-a-descriptor", 1));
    assert(plb_gpu_once_proof_fd() < 0);
    close(fd);
    return 0;
}
