/* MIT. Probe only an owned live mapping via the standard io_uring buffer API.
 * Register/unregister issues long-term pins, which can migrate pages. No custom
 * module, raw hypercall, submitted I/O, policy write or pin held through LEND.
 * Failure, including an unavailable API, refuses the requested diagnostic mode.
 */
#ifndef PLB_PREALLOCATED_PIN_H
#define PLB_PREALLOCATED_PIN_H
#include <linux/io_uring.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#include <time.h>
#include <unistd.h>

#ifndef __NR_io_uring_setup
#define __NR_io_uring_setup 425
#define __NR_io_uring_register 427
#endif

static int plb_preallocated_pin(void *mapping, size_t size) {
    const size_t batch = 512UL * 1024 * 1024;
    struct iovec regions[10];
    size_t count = (size + batch - 1) / batch;
    if (!size || size % 4096 || count > 10) {
        return -1;
    }
    struct io_uring_params parameters;
    memset(&parameters, 0, sizeof(parameters));
    int ring = (int)syscall(__NR_io_uring_setup, 1, &parameters);
    if (ring < 0) {
        printf("PLB_PREALLOCATED_PIN available=0 errno=%d\n", errno);
        return -1;
    }
    for (size_t i = 0; i < count; i++) {
        size_t offset = i * batch;
        regions[i].iov_base = (unsigned char *)mapping + offset;
        regions[i].iov_len = size - offset < batch ? size - offset : batch;
    }
    struct timespec before, after;
    clock_gettime(CLOCK_MONOTONIC, &before);
    errno = 0;
    long result = syscall(__NR_io_uring_register, ring, IORING_REGISTER_BUFFERS, regions, count);
    int saved = errno;
    clock_gettime(CLOCK_MONOTONIC, &after);
    long long micros =
        (after.tv_sec - before.tv_sec) * 1000000LL + (after.tv_nsec - before.tv_nsec) / 1000;
    int unregistered = result == 0 && syscall(__NR_io_uring_register, ring,
                                              IORING_UNREGISTER_BUFFERS, NULL, 0) == 0;
    close(ring); /* Closing also releases the ring's pins on an error path. */
    printf("PLB_PREALLOCATED_PIN available=1 bytes=%zu result=%ld errno=%d "
           "elapsed_us=%lld unregistered=%d\n",
           size, result, saved, micros, unregistered);
    return result == 0 && unregistered ? 0 : -1;
}
#endif
