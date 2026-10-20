/* MIT. Guest-only LED broker. This fixed protocol never carries host commands.
 * One Bridge connection, one outstanding root-guest request, bounded deadlines.
 * Android CameraManager consent, routing and expiry remain in the host app. */
#ifndef PLB_GUEST_TORCH_H
#define PLB_GUEST_TORCH_H
#include <sys/un.h>
#include <sys/prctl.h>
#ifndef PLB_TORCH_SOCKET
#define PLB_TORCH_SOCKET "/run/plb/torch.sock"
#endif
static const unsigned char torch_unavailable[8] = {1, 2, 0, 0, 0, 0, 0, 0};

static int torch_read(int fd, void *buffer, size_t bytes) {
    unsigned char *at = buffer;
    while (bytes) {
        ssize_t count = recv(fd, at, bytes, 0);
        if (count < 0 && errno == EINTR)
            continue;
        if (count <= 0)
            return -1;
        at += count;
        bytes -= (size_t)count;
    }
    return 0;
}

static int torch_connect(void) {
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    struct sockaddr_un address = {.sun_family = AF_UNIX};
    strcpy(address.sun_path, PLB_TORCH_SOCKET);
    if (fd < 0)
        return -1;
    struct timeval timeout = {.tv_sec = 3};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    if (connect(fd, (struct sockaddr *)&address, sizeof(address))) {
        close(fd);
        return -1;
    }
    struct ucred peer;
    socklen_t size = sizeof(peer);
    if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &peer, &size) || peer.uid) {
        close(fd);
        return -1;
    }
    return fd;
}

static void torch_broker(void) {
    signal(SIGTERM, SIG_DFL);
    signal(SIGINT, SIG_DFL);
    int listener = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    struct sockaddr_un address = {.sun_family = AF_UNIX};
    strcpy(address.sun_path, PLB_TORCH_SOCKET);
    /* Do not unlink an arbitrary stale object. A fresh guest /run is expected. */
    if (listener < 0 || bind(listener, (struct sockaddr *)&address, sizeof(address)) ||
        chmod(PLB_TORCH_SOCKET, 0600) || listen(listener, 4))
        _exit(1);
    int bridge = -1;
    for (;;) {
        struct pollfd events[2] = {{listener, POLLIN, 0}, {bridge, POLLIN, 0}};
        if (poll(events, 2, -1) < 0) {
            if (errno == EINTR)
                continue;
            break;
        }
        if (bridge >= 0 && events[1].revents) {
            /* An unsolicited response or EOF invalidates the bridge channel. */
            close(bridge);
            bridge = -1;
        }
        if (!(events[0].revents & POLLIN))
            continue;
        int client = accept4(listener, NULL, NULL, SOCK_CLOEXEC);
        if (client < 0)
            continue;
        struct timeval timeout = {.tv_sec = 2};
        setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
        struct ucred peer;
        socklen_t size = sizeof(peer);
        unsigned char role;
        if (getsockopt(client, SOL_SOCKET, SO_PEERCRED, &peer, &size) || peer.uid ||
            torch_read(client, &role, 1)) {
            close(client);
            continue;
        }
        if (role == 'B') {
            unsigned char status = bridge >= 0 ? 3 : 0;
            if (!plb_write_all(client, &status, 1) && !status) {
                bridge = client;
                continue;
            }
        } else if (role == 'C') {
            unsigned char request[4], reply[8];
            memcpy(reply, torch_unavailable, sizeof(reply));
            if (!torch_read(client, request, sizeof(request)) && bridge >= 0) {
                if (plb_write_all(bridge, request, sizeof(request)) ||
                    torch_read(bridge, reply, sizeof(reply)) || reply[0] != 1) {
                    close(bridge);
                    bridge = -1;
                    memcpy(reply, torch_unavailable, sizeof(reply));
                }
            }
            (void)plb_write_all(client, reply, sizeof(reply));
        }
        close(client);
    }
    if (bridge >= 0)
        close(bridge);
    close(listener);
    _exit(0);
}

static void start_torch_broker(void) {
    pid_t parent = getpid(), child = fork();
    if (!child) {
        signal(SIGTERM, SIG_DFL);
        if (prctl(PR_SET_PDEATHSIG, SIGTERM) || getppid() != parent)
            _exit(1);
        torch_broker();
    }
}

static void torch_bridge_session(int client) {
    int broker = torch_connect();
    unsigned char status = 3;
    if (broker >= 0 && !plb_write_all(broker, "B", 1) && !torch_read(broker, &status, 1) &&
        !status) {
        plb_write_all(client, &status, 1);
        plb_relay(client, broker, guest_lifetime, 0);
    } else {
        plb_write_all(client, &status, 1);
    }
    if (broker >= 0)
        close(broker);
}

static int torch_number(const char *text, unsigned maximum, unsigned char *result) {
    char *end;
    errno = 0;
    unsigned long value = strtoul(text, &end, 10);
    if (!*text || *text < '0' || *text > '9' || *end || errno || value > maximum)
        return -1;
    *result = (unsigned char)value;
    return 0;
}

static int torch_command(int argc, char **argv) {
    const char *name = strrchr(argv[0], '/');
    if (strcmp(name ? name + 1 : argv[0], "plb-torch"))
        return -1;
    unsigned char request[5] = {'C', 1, 0, 0, 0}, reply[8];
    if (argc == 2 && !strcmp(argv[1], "status")) {
        request[2] = 0;
    } else if (argc == 2 && !strcmp(argv[1], "off")) {
        request[2] = 2;
    } else if (argc >= 2 && argc <= 4 && !strcmp(argv[1], "on")) {
        request[2] = 1;
        request[4] = 10;
        if ((argc >= 3 && torch_number(argv[2], 255, request + 3)) ||
            (argc == 4 && (torch_number(argv[3], 30, request + 4) || !request[4])))
            return 2;
    } else {
        fputs("Usage: plb-torch status | off | on [level [seconds:1..30]]\n", stderr);
        return 2;
    }
    int fd = torch_connect();
    if (fd < 0) {
        fputs("LED unavailable: open an opted-in Bridge Debian terminal or desktop.\n", stderr);
        return 3;
    }
    int failed =
        plb_write_all(fd, request, sizeof(request)) || torch_read(fd, reply, sizeof(reply));
    close(fd);
    if (failed || reply[0] != 1)
        return 3;
    printf("status=%u on=%u available=%u max_level=%u owned_level=%u seconds=%u owned=%u\n",
           reply[1], reply[2], reply[3], reply[4], reply[5], reply[6], reply[7]);
    return reply[1] ? 3 : 0;
}
#endif
