/* MIT. Empty virtual sensor set for the Android 17 Cuttlefish guest.
 * The guest's matching HAL uses framed CONTROL on hvc18 and DATA on hvc19.
 * No Android host sensor, physical device, or network endpoint is exposed.
 */
#include <sys/socket.h>
#include <sys/un.h>
#include <stdint.h>

struct sensor_endpoint {
    int listener;
    int client;
    size_t used;
    unsigned char buffer[4104];
};

static int sensor_listen(struct sensor_endpoint *endpoint, const char *path) {
    struct stat old;
    endpoint->listener = endpoint->client = -1;
    endpoint->used = 0;
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

static void sensor_close(struct sensor_endpoint *endpoint, const char *path) {
    if (endpoint->client >= 0)
        close(endpoint->client);
    if (endpoint->listener >= 0) {
        close(endpoint->listener);
        unlink(path);
    }
}
