/* MIT. App-UID authenticated, private Android guest display session.
 * Frame capture is bounded; raw sRGB skips PNG but still performs CPU readback.
 * Guest GPU rendering does not imply direct GPU display/zero-copy transfer.
 */
#if !defined(PLB_ANDROID_PROBE) || defined(PLB_ANDROID4G_INTERACTIVE)
#include <pthread.h>
#include <stdatomic.h>
#include "android_guest_adb.h"
#include "android_guest_input.h"
#include "android_guest_frame.h"
#include "android_guest_setup.generated.h"

#define ANDROID_DISPLAY_SOCKET "net.ryuya_dev.protectedlinux.android-display"
static int display_listener = -1;
static _Atomic int display_client = -1;
static uid_t display_uid;
static int android_identity_ready;
#ifdef PLB_ANDROID4G_INTERACTIVE
#include "android_network_session.h"
#endif
static pthread_t display_thread;
static int display_thread_started;

static int android_display_listen(uid_t uid) {
    display_uid = uid;
    display_listener = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    struct sockaddr_un address = {.sun_family = AF_UNIX};
    memcpy(address.sun_path + 1, ANDROID_DISPLAY_SOCKET, sizeof(ANDROID_DISPLAY_SOCKET) - 1);
    socklen_t size = offsetof(struct sockaddr_un, sun_path) + sizeof(ANDROID_DISPLAY_SOCKET);
    if (display_listener < 0 || bind(display_listener, (struct sockaddr *)&address, size) ||
        listen(display_listener, 1))
        return -1;
    return 0;
}

static int android_display_accept(void) {
    int client = accept4(display_listener, NULL, NULL, SOCK_CLOEXEC);
    if (client < 0)
        return -1;
    struct ucred peer;
    socklen_t length = sizeof(peer);
    if (getsockopt(client, SOL_SOCKET, SO_PEERCRED, &peer, &length) || peer.uid != display_uid) {
        close(client);
        return -1;
    }
    return client;
}

#ifdef PLB_ANDROID_LARGE_PROFILE
static int android_display_client_alive(void) {
    int client = atomic_load(&display_client);
    if (client < 0)
        return 0;
    unsigned char byte;
    ssize_t size = recv(client, &byte, 1, MSG_PEEK | MSG_DONTWAIT);
    return size > 0 || (size < 0 && (errno == EAGAIN || errno == EINTR));
}

static int android_display_wait_for_client(void) {
    for (int i = 0; i < 100 && !stopping; i++) {
        int client = android_display_accept();
        if (client >= 0) {
            atomic_store(&display_client, client);
            return 0;
        }
        usleep(100000);
    }
    return -1;
}
#endif

static int android_bootstrap(unsigned char *output) {
    size_t length;
    const char identity[] = "exec:sh -c 'getprop ro.product.device; getprop ro.hardware; "
                            "getprop ro.build.version.incremental; id -u'";
    for (unsigned attempt = 0; attempt < 40 && !stopping; attempt++) {
        unsigned char peek;
        int client = atomic_load(&display_client);
        if (client >= 0 && recv(client, &peek, 1, MSG_PEEK | MSG_DONTWAIT) == 0)
            return -1;
        if (!android_adb(identity, output, &length) && length < 256) {
            output[length] = 0;
            const char expected[] = "vsoc_arm64_only\ncutf_cvm\n16373615\n";
            if (strncmp((char *)output, expected, strlen(expected)))
                return -1;
            if (!strcmp((char *)output + strlen(expected), "0\n")) {
                android_identity_ready = 1;
                break;
            }
            if (strcmp((char *)output + strlen(expected), "2000\n"))
                return -1;
            /* Original factory userdebug root service; no host ADB setting. */
            android_adb("root:", output, &length);
        }
        if (attempt == 39)
            return -1;
        usleep(1000000);
    }
    if (stopping || android_adb(android_setup_service, output, &length) ||
        !memmem(output, length, "PLB_ANDROID_GUEST_SETUP_READY", 29))
        return -1;
    fprintf(stderr, "ANDROID_GUEST_READY\n");
    return 0;
}

static void *android_display_worker(void *unused) {
    (void)unused;
    unsigned char *output = malloc(ANDROID_ADB_LIMIT + 1);
    if (!output) {
        stopping = 1;
        return NULL;
    }
    int client = atomic_load(&display_client);
    for (int tries = 0; client < 0 && tries < 300 && !stopping; tries++) {
        client = android_display_accept();
        if (client < 0) {
            usleep(100000);
            continue;
        }
        break;
    }
    if (client < 0)
        goto done;
    atomic_store(&display_client, client);
    struct timeval timeout = {.tv_sec = 30};
    setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    uint32_t greeting[] = {0x41424c50, 1, 480, 800};
    if (android_io(client, greeting, sizeof(greeting), 1))
        goto done;
    uint32_t status = android_bootstrap(output) ? 1 : 0;
    if (android_io(client, &status, sizeof(status), 1) || status)
        goto done;
    while (!stopping) {
        unsigned char command;
        if (android_io(client, &command, 1, 0))
            break;
        if (command == 'X')
            break;
        if (command == 'P') {
            unsigned char reply = 'P';
            if (android_io(client, &reply, 1, 1))
                break;
            continue;
        }
#ifdef PLB_ANDROID4G_INTERACTIVE
        if (command == 'N') {
            unsigned char reply = android_network_start(display_uid) ? 1 : 0;
            fprintf(stderr, "ANDROID_NETWORK_SETUP result=%u\n", reply);
            if (android_io(client, &reply, 1, 1))
                break;
            continue;
        }
#endif
        char service[256];
        if (command == 'F') {
            strcpy(service, "exec:screencap -p");
        } else if (command == 'R') {
            strcpy(service, "exec:screencap");
        } else {
            uint32_t values[5] = {0};
            size_t count = command == 'T' ? 2 : command == 'S' ? 5 : command == 'K' ? 1 : 0;
            if ((count && android_io(client, values, count * sizeof(*values), 0)) ||
                android_input_service(service, sizeof(service), command, values))
                break;
        }
        size_t length;
        int error = android_adb(service, output, &length);
        if (command == 'F' || command == 'R') {
            static const unsigned char png[] = {137, 80, 78, 71, 13, 10, 26, 10};
            if (error || (command == 'R' ? !android_raw_frame_valid(output, length)
                                         : length < 24 || memcmp(output, png, sizeof(png))))
                length = 0;
            uint32_t size = (uint32_t)length;
            if (android_io(client, &size, sizeof(size), 1) ||
                (size && android_io(client, output, size, 1)))
                break;
        } else {
            if (!error && !memmem(output, length, ANDROID_INPUT_OK, sizeof(ANDROID_INPUT_OK) - 1))
                error = -1;
            unsigned char reply = error ? 1 : 0;
            if (android_io(client, &reply, 1, 1))
                break;
        }
    }
done:
    if (client >= 0) {
        atomic_store(&display_client, -1);
        close(client);
    }
    free(output);
    stopping = 1;
    return NULL;
}

static int android_display_start(uid_t uid) {
    if ((display_listener < 0 && android_display_listen(uid)) || display_uid != uid ||
        pthread_create(&display_thread, NULL, android_display_worker, NULL))
        return -1;
    display_thread_started = 1;
    fprintf(stderr, "ANDROID_MANAGER_READY\n");
    return 0;
}

static void android_guest_shutdown(void) {
#ifdef PLB_ANDROID4G_INTERACTIVE
    android_network_stop();
#endif
    if (!android_identity_ready)
        return;
    unsigned char *output = malloc(ANDROID_ADB_LIMIT);
    if (!output)
        return;
    size_t length;
    android_shutdown_io = 1;
    /* This fixed endpoint is the still-owned, verified guest, never host ADB. */
    android_adb("exec:sync; /system/bin/reboot -p", output, &length);
    android_shutdown_io = 0;
    free(output);
    fprintf(stderr, "ANDROID_GUEST_SHUTDOWN_REQUESTED\n");
}

static void android_display_stop(void) {
    int client = atomic_load(&display_client);
    if (client >= 0)
        shutdown(client, SHUT_RDWR);
    if (display_thread_started)
        pthread_join(display_thread, NULL);
    else if (client >= 0) {
        close(client);
        atomic_store(&display_client, -1);
    }
    if (display_listener >= 0)
        close(display_listener);
    display_listener = -1;
    display_thread_started = 0;
}
#endif
