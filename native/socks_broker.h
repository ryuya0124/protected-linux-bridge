/* MIT. SOCKS5 over CID-checked vsock, after host UID/capability drop. */
#ifndef PLB_SOCKS_BROKER_H
#define PLB_SOCKS_BROKER_H
#define PLB_SOCKS_FLOWS 8
static int socks_address(int fd, unsigned char type, char host[254], unsigned *port,
                         unsigned char wire[260], size_t *length) {
    size_t size = type == 1 ? 4 : type == 4 ? 16 : 0;
    wire[0] = type;
    if (type == 3) {
        unsigned char n;
        if (plb_read_exact(fd, &n, 1) || !n || n > 253)
            return -1;
        wire[1] = n;
        size = n;
        if (plb_read_exact(fd, wire + 2, size) || !plb_valid_host(wire + 2, size))
            return -1;
        memcpy(host, wire + 2, size);
        host[size] = 0;
        *length = size + 2;
    } else {
        if (!size || plb_read_exact(fd, wire + 1, size) ||
            !inet_ntop(type == 1 ? AF_INET : AF_INET6, wire + 1, host, 254))
            return -1;
        *length = size + 1;
    }
    if (plb_read_exact(fd, wire + *length, 2))
        return -1;
    *port = ((unsigned)wire[*length] << 8) | wire[*length + 1];
    *length += 2;
    return 0;
}
static int socks_reply(int client, unsigned char code) {
    unsigned char reply[10] = {5, code, 0, 1, 0, 0, 0, 0, 0, 0};
    return plb_write_all(client, reply, sizeof(reply));
}
struct socks_flow {
    int fd;
    unsigned char address[260];
    size_t length;
    double used;
};
static int socks_udp_open(const char *host, unsigned port) {
    char service[6];
    snprintf(service, sizeof(service), "%u", port);
    struct addrinfo hints = {.ai_family = AF_UNSPEC, .ai_socktype = SOCK_DGRAM}, *list = NULL;
    if (!port || getaddrinfo(host, service, &hints, &list))
        return -1;
    int fd = -1;
    for (struct addrinfo *a = list; a; a = a->ai_next) {
        if (forbidden_address(a->ai_addr))
            continue;
        fd = socket(a->ai_family, SOCK_DGRAM | SOCK_CLOEXEC, 0);
        if (fd < 0)
            continue;
        if (!connect(fd, a->ai_addr, a->ai_addrlen))
            break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(list);
    return fd;
}
static void socks_udp(int client) {
    struct socks_flow flows[PLB_SOCKS_FLOWS];
    for (int i = 0; i < PLB_SOCKS_FLOWS; i++)
        flows[i].fd = -1;
    unsigned char data[PLB_DGRAM_MAX], address[260];
    double until = plb_network_lifetime ? plb_now() + plb_network_lifetime : 0;
    double idle = plb_now() + 60;
    while ((!until || plb_now() < until) && plb_now() < idle) {
        struct pollfd p[PLB_SOCKS_FLOWS + 1];
        p[0] = (struct pollfd){client, POLLIN, 0};
        for (int i = 0; i < PLB_SOCKS_FLOWS; i++)
            p[i + 1] = (struct pollfd){flows[i].fd, POLLIN, 0};
        int ready = poll(p, PLB_SOCKS_FLOWS + 1, 1000);
        if (ready < 0) {
            if (errno == EINTR)
                continue;
            break;
        }
        if (p[0].revents & (POLLIN | POLLHUP | POLLERR)) {
            unsigned char head[4];
            char host[254];
            unsigned port;
            size_t length;
            if (plb_read_exact(client, head, 4) ||
                socks_address(client, head[3], host, &port, address, &length))
                break;
            size_t size = ((size_t)head[0] << 8) | head[1];
            if (head[2] != 3 + length || size > sizeof(data) ||
                plb_read_exact(client, data, size) || !port)
                break;
            int slot = -1;
            for (int i = 0; i < PLB_SOCKS_FLOWS; i++)
                if (flows[i].fd >= 0 && flows[i].length == length &&
                    !memcmp(flows[i].address, address, length)) {
                    slot = i;
                    break;
                }
            if (slot < 0) {
                for (int i = 0; i < PLB_SOCKS_FLOWS; i++)
                    if (flows[i].fd < 0 || plb_now() - flows[i].used > 30) {
                        slot = i;
                        break;
                    }
                if (slot < 0)
                    break;
                if (flows[slot].fd >= 0)
                    close(flows[slot].fd);
                flows[slot].fd = socks_udp_open(host, port);
                if (flows[slot].fd < 0)
                    break;
                memcpy(flows[slot].address, address, length);
                flows[slot].length = length;
            }
            if (send(flows[slot].fd, data, size, MSG_DONTWAIT) != (ssize_t)size)
                break;
            flows[slot].used = plb_now();
            idle = plb_now() + 60;
        }
        for (int i = 0; i < PLB_SOCKS_FLOWS; i++)
            if (flows[i].fd >= 0 && (p[i + 1].revents & (POLLIN | POLLERR | POLLHUP))) {
                ssize_t size = recv(flows[i].fd, data, sizeof(data), MSG_DONTWAIT | MSG_TRUNC);
                if (size < 0) {
                    if (errno != EINTR && errno != EAGAIN) {
                        close(flows[i].fd);
                        flows[i].fd = -1;
                    }
                    continue;
                }
                if ((size_t)size > sizeof(data))
                    continue;
                unsigned char head[3] = {(unsigned char)((unsigned)size >> 8), (unsigned char)size,
                                         (unsigned char)(3 + flows[i].length)};
                if (plb_write_all(client, head, 3) ||
                    plb_write_all(client, flows[i].address, flows[i].length) ||
                    plb_write_all(client, data, (size_t)size))
                    goto done;
                idle = plb_now() + 60;
                flows[i].used = plb_now();
            }
    }
done:
    for (int i = 0; i < PLB_SOCKS_FLOWS; i++)
        if (flows[i].fd >= 0)
            close(flows[i].fd);
}
static void socks_broker(int client) {
    alarm(15);
    unsigned char greeting[2], methods[255], selected[2] = {5, 255};
    if (plb_read_exact(client, greeting, 2) || greeting[0] != 5 || !greeting[1] ||
        plb_read_exact(client, methods, greeting[1]))
        return;
    for (unsigned i = 0; i < greeting[1]; i++)
        if (!methods[i])
            selected[1] = 0;
    if (plb_write_all(client, selected, 2) || selected[1])
        return;
    unsigned char head[4], address[260];
    size_t length;
    char host[254];
    unsigned port;
    if (plb_read_exact(client, head, 4) || head[0] != 5 || head[2] ||
        socks_address(client, head[3], host, &port, address, &length))
        return;
    struct timeval timeout = {3, 0};
    setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    fprintf(stderr, "SOCKS_ACCEPTED command=%u uid=%d\n", head[1], getuid());
    if (head[1] == 1) {
        char service[6];
        snprintf(service, sizeof(service), "%u", port);
        int remote = port ? tcp_connect(host, service) : -1;
        if (remote < 0) {
            socks_reply(client, 2);
            return;
        }
        if (!socks_reply(client, 0)) {
            alarm(0);
            plb_relay(client, remote, plb_network_lifetime, 0);
        }
        close(remote);
    } else if (head[1] == 5) {
        if (socks_reply(client, 0))
            return;
        alarm(0);
        socks_udp(client);
    } else
        socks_reply(client, 7);
}
#endif
