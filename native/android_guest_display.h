/* MIT. App-UID authenticated, private Android guest display session.
 * Frame capture is deliberately bounded and slow in this experimental profile.
 * No claim of hardware acceleration, sound, guest networking or zero-copy.
 */
#ifndef PLB_ANDROID_PROBE
#include <pthread.h>
#include <stdatomic.h>
#include "android_guest_adb.h"
#include "android_guest_setup.generated.h"

#define ANDROID_DISPLAY_SOCKET "net.ryuya_dev.protectedlinux.android-display"
static int display_listener = -1;
static _Atomic int display_client = -1;
static uid_t display_uid;
static int android_identity_ready;
static pthread_t display_thread;

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
    int client = -1;
    for (int tries = 0; tries < 300 && !stopping; tries++) {
        client = accept4(display_listener, NULL, NULL, SOCK_CLOEXEC);
        if (client < 0) {
            usleep(100000);
            continue;
        }
        struct ucred peer;
        socklen_t length = sizeof(peer);
        if (getsockopt(client, SOL_SOCKET, SO_PEERCRED, &peer, &length) ||
            peer.uid != display_uid) {
            close(client);
            client = -1;
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
        char service[128];
        if (command == 'F') {
            strcpy(service, "exec:screencap -p");
        } else if (command == 'T') {
            uint32_t xy[2];
            if (android_io(client, xy, sizeof(xy), 0) || xy[0] >= 480 || xy[1] >= 800)
                break;
            snprintf(service, sizeof(service), "exec:cmd input tap %u %u", xy[0], xy[1]);
        } else if (command == 'K') {
            uint32_t key;
            if (android_io(client, &key, sizeof(key), 0) ||
                (key != 3 && key != 4 && key != 82 && key != 187 && key != 224))
                break;
            snprintf(service, sizeof(service), "exec:cmd input keyevent %u", key);
        } else if (command == 'U') {
            strcpy(service, "exec:cmd power wakeup; wm dismiss-keyguard; settings put system "
                            "screen_off_timeout 1800000");
        } else {
            break;
        }
        size_t length;
        int error = android_adb(service, output, &length);
        if (command == 'F') {
            static const unsigned char png[] = {137, 80, 78, 71, 13, 10, 26, 10};
            if (error || length < 24 || memcmp(output, png, sizeof(png)))
                length = 0;
            uint32_t size = (uint32_t)length;
            if (android_io(client, &size, sizeof(size), 1) ||
                (size && android_io(client, output, size, 1)))
                break;
        } else {
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
    display_uid = uid;
    display_listener = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    struct sockaddr_un address = {.sun_family = AF_UNIX};
    memcpy(address.sun_path + 1, ANDROID_DISPLAY_SOCKET, sizeof(ANDROID_DISPLAY_SOCKET) - 1);
    socklen_t size = offsetof(struct sockaddr_un, sun_path) + sizeof(ANDROID_DISPLAY_SOCKET);
    if (display_listener < 0 || bind(display_listener, (struct sockaddr *)&address, size) ||
        listen(display_listener, 1) ||
        pthread_create(&display_thread, NULL, android_display_worker, NULL))
        return -1;
    fprintf(stderr, "ANDROID_MANAGER_READY\n");
    return 0;
}

static void android_guest_shutdown(void) {
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
    pthread_join(display_thread, NULL);
    close(display_listener);
}
#endif
