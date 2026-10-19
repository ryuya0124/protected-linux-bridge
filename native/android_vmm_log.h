/* MIT. Drain owned VMM diagnostics independently of the app's cached UI reader.
 * Only the newly created private log is truncated. No VM disk limit is changed.
 */
#include <poll.h>
#include <pthread.h>
#include <stdatomic.h>

#define ANDROID_VMM_LOG_LIMIT (2U * 1024U * 1024U)

struct android_vmm_log {
    int reader, file;
    size_t used;
    _Atomic int finish, fault;
    pthread_t thread;
};

static void *android_vmm_log_worker(void *opaque) {
    struct android_vmm_log *log = opaque;
    unsigned char bytes[8192], window[8192 + 128];
    size_t retained = 0;
    for (;;) {
        struct pollfd ready = {.fd = log->reader, .events = POLLIN};
        int result = poll(&ready, 1, 100);
        if (result < 0 && errno == EINTR)
            continue;
        if (result <= 0) {
            if (atomic_load(&log->finish))
                break;
            continue;
        }
        ssize_t count = read(log->reader, bytes, sizeof(bytes));
        if (count < 0 && errno == EINTR)
            continue;
        if (count <= 0)
            break;
        memcpy(window + retained, bytes, (size_t)count);
        size_t combined = retained + (size_t)count;
        static const char fault[] = "PLB_GUNYAH_PAGE_FAULT";
        if (memmem(window, combined, fault, sizeof(fault) - 1))
            atomic_store(&log->fault, 1);
        retained = combined < 128 ? combined : 128;
        memmove(window, window + combined - retained, retained);
        if (log->file < 0)
            continue;
        if (log->used + (size_t)count > ANDROID_VMM_LOG_LIMIT) {
            if (ftruncate(log->file, 0) || lseek(log->file, 0, SEEK_SET) < 0) {
                close(log->file);
                log->file = -1;
                continue;
            }
            log->used = 0;
        }
        size_t at = 0;
        while (at < (size_t)count) {
            ssize_t written = write(log->file, bytes + at, (size_t)count - at);
            if (written < 0 && errno == EINTR)
                continue;
            if (written <= 0) {
                close(log->file);
                log->file = -1;
                break;
            }
            at += (size_t)written;
        }
        log->used += at;
    }
    return NULL;
}

static int android_vmm_log_start(struct android_vmm_log *log, int reader, const char *path) {
    memset(log, 0, sizeof(*log));
    log->reader = reader;
    log->file = open(path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (log->file < 0)
        return -1;
    if (pthread_create(&log->thread, NULL, android_vmm_log_worker, log)) {
        close(log->file);
        log->file = -1;
        return -1;
    }
    return 0;
}

static void android_vmm_log_stop(struct android_vmm_log *log) {
    atomic_store(&log->finish, 1);
    pthread_join(log->thread, NULL);
    close(log->reader);
    if (log->file >= 0)
        close(log->file);
}
