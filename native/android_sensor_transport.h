/* MIT. Default-empty virtual sensor set for the Android 17 Cuttlefish guest.
 * The guest's matching HAL uses framed CONTROL on hvc18 and DATA on hvc19.
 * Explicit owner configuration can relay selected samples through this private
 * stream. Default callers expose no host sensor, physical device or network.
 */
#include <sys/socket.h>
#include <sys/un.h>
#include <stdint.h>
#include <time.h>
#include "android_guest_sensors.h"

struct sensor_endpoint {
    int listener;
    int client;
    size_t used;
    unsigned char buffer[4104];
    int relay_enabled;
    struct plb_guest_sensors relay;
};

static int sensor_listen(struct sensor_endpoint *endpoint, const char *path) {
    struct stat old;
    endpoint->listener = endpoint->client = -1;
    endpoint->used = 0;
    endpoint->relay_enabled = 0;
    plb_sensor_configure(&endpoint->relay, 0);
    if (!lstat(path, &old)) {
        if (!S_ISSOCK(old.st_mode) || old.st_uid || (old.st_mode & 077))
            return -1;
        if (unlink(path))
            return -1;
    } else if (errno != ENOENT) {
        return -1;
    }
    struct sockaddr_un address = {.sun_family = AF_UNIX};
    if (strlen(path) >= sizeof(address.sun_path))
        return -1;
    strcpy(address.sun_path, path);
    endpoint->listener = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (endpoint->listener < 0 ||
        bind(endpoint->listener, (struct sockaddr *)&address, sizeof(address)) ||
        listen(endpoint->listener, 1))
        return -1;
    return 0;
}

static void sensor_poll(struct sensor_endpoint *endpoint, int oemlock) {
    if (endpoint->client < 0) {
        endpoint->client = accept4(endpoint->listener, NULL, NULL, SOCK_CLOEXEC | SOCK_NONBLOCK);
        if (endpoint->client < 0)
            return;
        /* Only the child crosvm (same root identity) can reach this private path. */
        struct ucred peer;
        socklen_t size = sizeof(peer);
        if (getsockopt(endpoint->client, SOL_SOCKET, SO_PEERCRED, &peer, &size) || peer.uid) {
            close(endpoint->client);
            endpoint->client = -1;
            return;
        }
    }
    ssize_t count = recv(endpoint->client, endpoint->buffer + endpoint->used,
                         sizeof(endpoint->buffer) - endpoint->used, 0);
    if (count < 0 && (errno == EAGAIN || errno == EINTR))
        return;
    if (count <= 0)
        goto disconnect;
    endpoint->used += (size_t)count;
    while (endpoint->used >= 8) {
        uint32_t length;
        memcpy(&length, endpoint->buffer + 4, 4);
        if (length > 4096)
            goto disconnect;
        if (endpoint->used < 8 + length)
            return;
        uint32_t command;
        memcpy(&command, endpoint->buffer, 4);
        if (oemlock) {
            /* Guest-only OEM state: locked, unlocking denied. Never acknowledge
             * a request to enable unlocking. This is not a host security service. */
            if (command > 3 || length > 1 || (length && endpoint->buffer[8]))
                goto disconnect;
            unsigned char response[9] = {0};
            uint32_t response_command = command | 0x80000000U, response_length = 1;
            memcpy(response, &response_command, 4);
            memcpy(response + 4, &response_length, 4);
            response[8] = command == 3;
            if (send(endpoint->client, response, sizeof(response), MSG_NOSIGNAL) !=
                sizeof(response))
                goto disconnect;
        } else if (endpoint->relay_enabled) {
            struct timespec now;
            unsigned char response[256];
            if (command || clock_gettime(CLOCK_MONOTONIC, &now))
                goto disconnect;
#ifdef CLOCK_BOOTTIME
            if (clock_gettime(CLOCK_BOOTTIME, &now))
                goto disconnect;
#endif
            int result = plb_sensor_control(&endpoint->relay, endpoint->buffer + 8, length,
                                            (int64_t)now.tv_sec * 1000000000 + now.tv_nsec,
                                            response, sizeof(response));
            if (result < 0 || (result && send(endpoint->client, response, (size_t)result,
                                              MSG_NOSIGNAL) != result))
                goto disconnect;
        } else if (length == 12 && !memcmp(endpoint->buffer + 8, "list-sensors", 12)) {
            /* AOSP SharedFdChannel RawMessage response + decimal empty sensor mask. */
            const unsigned char response[] = {0, 0, 0, 128, 2, 0, 0, 0, '0', '\n'};
            if (send(endpoint->client, response, sizeof(response), MSG_NOSIGNAL) !=
                sizeof(response))
                goto disconnect;
            fprintf(stderr, "PLB Android guest: advertised empty virtual sensor set\n");
        }
        endpoint->used -= 8 + length;
        memmove(endpoint->buffer, endpoint->buffer + 8 + length, endpoint->used);
    }
    return;
disconnect:
    close(endpoint->client);
    endpoint->client = -1;
    endpoint->used = 0;
}

/* Configure before guest HAL discovery, from detected AND explicitly selected
 * app-side sensors. No caller currently enables this in the released runtime. */
static inline void sensor_enable_relay(struct sensor_endpoint *control, uint32_t selected) {
    plb_sensor_configure(&control->relay, selected);
    control->relay_enabled = 1;
}

static inline int sensor_forward_sample(struct sensor_endpoint *control,
                                        struct sensor_endpoint *data, unsigned id,
                                        int64_t host_timestamp_ns, const float *values,
                                        size_t count) {
    if (!control->relay_enabled || control->client < 0 || data->client < 0)
        return 0;
    unsigned char frames[256];
    size_t bytes = plb_sensor_sample(&control->relay, id, host_timestamp_ns, values, count, frames,
                                     sizeof(frames));
    if (!bytes)
        return 0;
    ssize_t sent = send(data->client, frames, bytes, MSG_NOSIGNAL | MSG_DONTWAIT);
    if (sent == (ssize_t)bytes)
        return 1;
    /* Never leave a partially framed stream live or queue unbounded samples. */
    close(data->client);
    data->client = -1;
    data->used = 0;
    plb_sensor_configure(&control->relay, 0);
    return -1;
}

static void sensor_close(struct sensor_endpoint *endpoint, const char *path) {
    if (endpoint->client >= 0)
        close(endpoint->client);
    if (endpoint->listener >= 0) {
        close(endpoint->listener);
        unlink(path);
    }
}
