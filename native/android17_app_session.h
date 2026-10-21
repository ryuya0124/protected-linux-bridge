/* MIT. A single app-UID authenticated controller for the owned Android 17 guest.
 * Only fixed guest commands and validated virtio contacts cross this channel.
 * No screenshots, TCP listener, generic shell or global display registration. */
#ifndef PLB_ANDROID17_APP_SESSION_H
#define PLB_ANDROID17_APP_SESSION_H
#include <stddef.h>
#include <sys/socket.h>
#include <sys/un.h>
#include "android_guest_adb.h"
#include "android_guest_input.h"
#include "android_virtio_touch.h"
#include "android_guest_setup.generated.h"
#include "android_network_session.h"
static int app_listener = -1;
static _Atomic int app_client = -1;
static int app_identity_ready;
static pthread_t app_thread;
static int app_thread_started;

/* Refuse missing display consent before RAM pressure or host policy writes. */
static int app_capability_pipe(int *child_input) {
    unsigned char capability[40];
    size_t used = 0;
    long long deadline = plb_shmem_millis() + 5000;
    while (used < sizeof(capability) && plb_shmem_millis() < deadline) {
        struct pollfd ready = {.fd = STDIN_FILENO, .events = POLLIN};
        if (poll(&ready, 1, 100) <= 0)
            continue;
        ssize_t count = read(STDIN_FILENO, capability + used, sizeof(capability) - used);
        if (count <= 0)
            return -1;
        used += (size_t)count;
    }
    if (used != sizeof(capability) || memcmp(capability, "PLD1", 4) ||
        plb_sensor_read_be32(capability + 4) != PLB_VIEWER_UID)
        return -1;
    int pipefd[2];
    if (pipe2(pipefd, O_CLOEXEC))
        return -1;
    int okay = write(pipefd[1], capability, sizeof(capability)) == sizeof(capability);
    close(pipefd[1]);
    if (!okay) {
        close(pipefd[0]);
        return -1;
    }
    *child_input = pipefd[0];
    return 0;
}

static int app_session_listen(void) {
    struct sockaddr_un address = {.sun_family = AF_UNIX};
    const char name[] = "net.ryuya_dev.protectedlinux.android-display";
    memcpy(address.sun_path + 1, name, sizeof(name) - 1);
    app_listener = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (app_listener < 0 ||
        bind(app_listener, (struct sockaddr *)&address,
             offsetof(struct sockaddr_un, sun_path) + sizeof(name)) ||
        listen(app_listener, 1) || android_touch_listen())
        return -1;
    return 0;
}

static int app_bootstrap(unsigned char *output) {
    const char identity[] =
        "exec:sh -c 'getprop ro.product.device; getprop ro.hardware; "
        "getprop ro.build.version.incremental; getprop ro.build.version.sdk; id -u'";
    const char root[] = "vsoc_arm64_only\ncutf_cvm\n16373615\n37\n0\n";
    const char shell[] = "vsoc_arm64_only\ncutf_cvm\n16373615\n37\n2000\n";
    int setup = 0;
    long long deadline = plb_shmem_millis() + 110000;
    while (!stopping && plb_shmem_millis() < deadline) {
        size_t length;
        if (!android_adb(identity, output, &length)) {
            if (length == sizeof(root) - 1 && !memcmp(output, root, length)) {
                app_identity_ready = 1;
                if (!setup) {
                    if (android_adb(android_setup_service, output, &length) ||
                        !memmem(output, length, "PLB_ANDROID_GUEST_SETUP_READY", 29))
                        return -1;
                    setup = 1;
                }
                if (!android_adb("exec:getprop sys.boot_completed", output, &length) &&
                    length == 2 && !memcmp(output, "1\n", 2)) {
                    puts("ANDROID17_APP_GUEST_READY");
                    return 0;
                }
                usleep(500000);
                continue;
            }
            if (length != sizeof(shell) - 1 || memcmp(output, shell, length))
                return -1;
            android_adb("root:", output, &length);
        }
        usleep(500000);
    }
    return -1;
}

static void *app_session_worker(void *unused) {
    (void)unused;
    unsigned char *output = malloc(ANDROID_ADB_LIMIT + 1);
    int client = -1;
    if (!output)
        goto done;
    for (int i = 0; i < 300 && !stopping; i++) {
        struct pollfd ready = {.fd = app_listener, .events = POLLIN};
        if (poll(&ready, 1, 100) <= 0)
            continue;
        int candidate = accept4(app_listener, NULL, NULL, SOCK_CLOEXEC);
        struct ucred peer;
        socklen_t size = sizeof(peer);
        if (candidate < 0)
            continue;
        if (getsockopt(candidate, SOL_SOCKET, SO_PEERCRED, &peer, &size) ||
            peer.uid != PLB_VIEWER_UID) {
            close(candidate);
            continue;
        }
        client = candidate;
        atomic_store(&app_client, client);
        break;
    }
    if (client < 0)
        goto done;
    struct timeval timeout = {.tv_sec = 5};
    setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    uint32_t greeting[] = {0x41424c50, 1, 480, 800};
    if (android_io(client, greeting, sizeof(greeting), 1))
        goto done;
    uint32_t status = app_bootstrap(output) ? 1 : 0;
    if (android_io(client, &status, sizeof(status), 1) || status)
        goto done;
    while (!stopping) {
        unsigned char command;
        struct pollfd ready = {.fd = client, .events = POLLIN};
        if (poll(&ready, 1, 100) <= 0)
            continue;
        if (android_io(client, &command, 1, 0) || command == 'X')
            break;
        unsigned char reply = 0;
        if (command == 'N' || command == 'n') {
            reply =
                command == 'N' ? android_network_start(PLB_VIEWER_UID) : android_network_status();
        } else if (command == 'P') {
            reply = 'P';
        } else if (command == 'V') {
            uint32_t points, values[PLB_TOUCH_MAX_POINTS * 4];
            if (android_io(client, &points, sizeof(points), 0) || !points ||
                points > PLB_TOUCH_MAX_POINTS ||
                android_io(client, values, points * 4 * sizeof(*values), 0))
                break;
            reply = android_touch_send(points, values) ? 1 : 0;
        } else if (command == 'K' || command == 'U') {
            uint32_t values[1] = {0};
            char service[512];
            size_t length;
            if ((command == 'K' && android_io(client, values, sizeof(values), 0)) ||
                android_input_service(service, sizeof(service), command, values))
                break;
            reply = android_adb(service, output, &length) ||
                    !memmem(output, length, ANDROID_INPUT_OK, sizeof(ANDROID_INPUT_OK) - 1);
        } else {
            break; /* No arbitrary ADB service, capture or caller-selected network payload. */
        }
        if (android_io(client, &reply, 1, 1))
            break;
    }
done:
    android_touch_release();
    if (client >= 0) {
        atomic_store(&app_client, -1);
        close(client);
    }
    free(output);
    stopping = 1;
    return NULL;
}

static void app_session_shutdown(int child_alive) {
    int client = atomic_load(&app_client);
    if (client >= 0)
        shutdown(client, SHUT_RDWR);
    if (app_thread_started)
        pthread_join(app_thread, NULL);
    android_network_stop();
    if (app_identity_ready && child_alive) {
        unsigned char *output = malloc(ANDROID_ADB_LIMIT + 1);
        if (output) {
            size_t length;
            /* Only the pinned guest CID. Allow shutdown I/O after stopping input. */
            android_shutdown_io = 1;
            (void)android_adb("exec:svc power shutdown", output, &length);
            android_shutdown_io = 0;
            free(output);
        }
    }
    android_touch_close();
    if (app_listener >= 0)
        close(app_listener);
}
#endif
