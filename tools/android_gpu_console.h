/* MIT. Early swap in the owned userdebug guest through its existing console.
 * Diagnostic only. No host shell or physical serial device is used. */
#include <poll.h>
#include <time.h>

static int gpu_console_fd = -1;
static unsigned long gpu_console_bytes;
static int gpu_console_started;
#ifdef PLB_ANDROID4G_INTERACTIVE
static _Atomic int gpu_console_finish;
#endif
#include "android_gpu_early.generated.h"

static void gpu_console_poll(struct sensor_endpoint *console, pid_t child, int tick) {
    if (console->client < 0) {
        console->client = accept4(console->listener, NULL, NULL, SOCK_CLOEXEC | SOCK_NONBLOCK);
        if (console->client < 0)
            return;
        struct ucred peer;
        socklen_t size = sizeof(peer);
        if (getsockopt(console->client, SOL_SOCKET, SO_PEERCRED, &peer, &size) || peer.uid ||
            peer.pid != child) {
            close(console->client);
            console->client = -1;
            return;
        }
        gpu_console_fd = open(BASE "/android-console.log",
                              O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    }
    char bytes[8192];
    for (int n = 0; n < 32; n++) {
        ssize_t count = recv(console->client, bytes, sizeof(bytes), MSG_DONTWAIT);
        if (count < 0)
            break;
        if (!count) {
            close(console->client);
            console->client = -1;
            return;
        }
        if (gpu_console_fd >= 0 && gpu_console_bytes + (size_t)count <= 2 * 1024 * 1024)
            (void)write(gpu_console_fd, bytes, (size_t)count);
        gpu_console_bytes += (size_t)count;
        static const char marker[] = "started service 'console'";
        char window[sizeof(bytes) + sizeof(marker)];
        memcpy(window, console->buffer, console->used);
        memcpy(window + console->used, bytes, (size_t)count);
        size_t combined = console->used + (size_t)count;
        if (!gpu_console_started && memmem(window, combined, marker, sizeof(marker) - 1))
            gpu_console_started = tick + 500;
        console->used = combined < sizeof(marker) - 2 ? combined : sizeof(marker) - 2;
        memcpy(console->buffer, window + combined - console->used, console->used);
    }
    if (!stopping && gpu_console_started > 0 && tick >= gpu_console_started) {
        ssize_t sent = send(console->client, console_swap_command, sizeof(console_swap_command) - 1,
                            MSG_NOSIGNAL);
        fprintf(stderr, "ANDROID_GPU_EARLY_CONSOLE sent=%d\n",
                sent == sizeof(console_swap_command) - 1);
        gpu_console_started = -1; /* Fixed command once; no periodic injection. */
    }
}

struct gpu_console_context {
    struct sensor_endpoint *console;
    pid_t child;
};
static void *gpu_console_worker(void *opaque) {
    struct gpu_console_context *context = opaque;
    struct timespec start, now;
    clock_gettime(CLOCK_MONOTONIC, &start);
    while (
#ifdef PLB_ANDROID4G_INTERACTIVE
        !atomic_load(&gpu_console_finish)
#else
        !stopping
#endif
    ) {
        struct pollfd ready = {
            .fd = context->console->client >= 0 ? context->console->client
                                                : context->console->listener,
            .events = POLLIN,
        };
        poll(&ready, 1, 100);
        clock_gettime(CLOCK_MONOTONIC, &now);
        int elapsed =
            (int)((now.tv_sec - start.tv_sec) * 1000 + (now.tv_nsec - start.tv_nsec) / 1000000);
        gpu_console_poll(context->console, context->child, elapsed + 1);
    }
    return NULL;
}

#ifdef PLB_ANDROID4G_INTERACTIVE
static void gpu_console_finish_worker(pthread_t thread, int started,
                                      struct sensor_endpoint *console) {
    atomic_store(&gpu_console_finish, 1);
    if (started)
        pthread_join(thread, NULL);
    sensor_close(console, BASE "/early-console.sock");
    if (gpu_console_fd >= 0)
        close(gpu_console_fd);
}
#endif
