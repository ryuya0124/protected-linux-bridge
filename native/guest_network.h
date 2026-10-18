/* MIT. Explicit guest UDP forwarding and IPv4 ping through the unprivileged host. */
#ifndef PLB_GUEST_NETWORK_H
#define PLB_GUEST_NETWORK_H
#include <arpa/inet.h>
#include <stdint.h>
#include <time.h>
#include "datagram_protocol.h"
static int plb_number(const char *text, unsigned minimum, unsigned maximum, unsigned *result) {
    if (!text || !*text)
        return -1;
    unsigned value = 0;
    for (const char *p = text; *p; p++) {
        if (*p < '0' || *p > '9' || value > maximum / 10)
            return -1;
        value = value * 10 + (unsigned)(*p - '0');
        if (value > maximum)
            return -1;
    }
    if (value < minimum)
        return -1;
    *result = value;
    return 0;
}
static int plb_remote(const char *host, unsigned port, char mode,
                      unsigned char ready[PLB_DGRAM_READY_SIZE]) {
    size_t length = strlen(host);
    if (!plb_valid_host((const unsigned char *)host, length)) {
        fputs("Invalid destination. Use a hostname or IP literal, without URL syntax.\n", stderr);
        return -1;
    }
    int fd = vsock_connect(PROXY_PORT);
    if (fd < 0) {
        perror("VM network relay");
        return -1;
    }
    struct timeval timeout = {3, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    unsigned char head[6] = {0,
                             PLB_DGRAM_VERSION,
                             (unsigned char)mode,
                             (unsigned char)(port >> 8),
                             (unsigned char)port,
                             (unsigned char)length};
    if (plb_write_all(fd, head, sizeof(head)) || plb_write_all(fd, host, length) ||
        plb_read_exact(fd, ready, PLB_DGRAM_READY_SIZE)) {
        fputs("VM network handshake failed or timed out.\n", stderr);
        close(fd);
        return -1;
    }
    if (ready[0] || (ready[1] != 4 && ready[1] != 6)) {
        const char *reason = ready[0] == 2   ? "destination blocked"
                             : ready[0] == 3 ? "name resolution failed"
                             : ready[0] == 5 ? "Android denies the unprivileged ping socket"
                                             : "relay unavailable";
        fprintf(stderr, "VM network: %s (status %u).\n", reason, ready[0]);
        close(fd);
        return -1;
    }
    return fd;
}
static int plb_ping(int argc, char **argv) {
    unsigned count = 4, timeout = 2, size = 56;
    const char *host = NULL;
    for (int i = 1; i < argc; i++) {
        if ((!strcmp(argv[i], "-c") || !strcmp(argv[i], "-W") || !strcmp(argv[i], "-s")) &&
            i + 1 < argc) {
            char option = argv[i][1];
            unsigned value;
            if (plb_number(argv[++i], option == 's' ? 8 : 1,
                           option == 'c'   ? 64
                           : option == 'W' ? 10
                                           : 1392,
                           &value))
                goto usage;
            if (option == 'c')
                count = value;
            else if (option == 'W')
                timeout = value;
            else
                size = value;
        } else if (argv[i][0] == '-' || host)
            goto usage;
        else
            host = argv[i];
    }
    if (!host)
        goto usage;
    unsigned char ready[PLB_DGRAM_READY_SIZE], packet[1400], reply[1400];
    int fd = plb_remote(host, 0, 'I', ready);
    if (fd < 0)
        return 2;
    char address[INET6_ADDRSTRLEN];
    if (!inet_ntop(AF_INET, ready + 2, address, sizeof(address))) {
        close(fd);
        return 2;
    }
    printf("PING %s (%s), %u data bytes\nICMP via Android host relay; RTT includes VM transport.\n",
           host, address, size);
    fflush(stdout);
    unsigned char nonce[8];
    int random = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    if (random < 0 || plb_read_exact(random, nonce, sizeof(nonce))) {
        if (random >= 0)
            close(random);
        close(fd);
        return 2;
    }
    close(random);
    unsigned received = 0;
    double total = 0, minimum = 1e9, maximum = 0;
    for (unsigned sequence = 1; sequence <= count; sequence++) {
        memset(packet, 0, size + 8);
        packet[0] = 8;
        packet[4] = (unsigned char)(getpid() >> 8);
        packet[5] = (unsigned char)getpid();
        packet[6] = (unsigned char)(sequence >> 8);
        packet[7] = (unsigned char)sequence;
        memcpy(packet + 8, nonce, sizeof(nonce));
        for (unsigned i = 16; i < size + 8; i++)
            packet[i] = (unsigned char)i;
        uint16_t checksum = plb_icmp_checksum(packet, size + 8);
        packet[2] = (unsigned char)(checksum >> 8);
        packet[3] = (unsigned char)checksum;
        double started = plb_now();
        if (plb_dgram_write(fd, packet, size + 8)) {
            fputs("Ping relay disconnected.\n", stderr);
            close(fd);
            return 2;
        }
        int matched = 0;
        double deadline = started + timeout;
        while (plb_now() < deadline) {
            struct pollfd p = {fd, POLLIN, 0};
            int ms = (int)((deadline - plb_now()) * 1000);
            if (ms < 1)
                ms = 1;
            int result = poll(&p, 1, ms);
            if (result < 0 && errno == EINTR)
                continue;
            if (result <= 0)
                break;
            size_t n;
            if (plb_dgram_read(fd, reply, sizeof(reply), &n)) {
                fputs("Ping relay disconnected.\n", stderr);
                close(fd);
                return 2;
            }
            if (n != size + 8 || reply[0] || reply[1] || reply[6] != packet[6] ||
                reply[7] != packet[7] || memcmp(reply + 8, packet + 8, size))
                continue;
            double elapsed = (plb_now() - started) * 1000;
            received++;
            total += elapsed;
            if (elapsed < minimum)
                minimum = elapsed;
            if (elapsed > maximum)
                maximum = elapsed;
            printf("%zu bytes from %s: icmp_seq=%u relay_time=%.2f ms\n", n, address, sequence,
                   elapsed);
            matched = 1;
            break;
        }
        if (!matched)
            printf("Request timeout for icmp_seq=%u\n", sequence);
        fflush(stdout);
        double remaining = 1.0 - (plb_now() - started);
        if (sequence < count && remaining > 0) {
            struct timespec delay = {(time_t)remaining,
                                     (long)((remaining - (time_t)remaining) * 1e9)};
            nanosleep(&delay, NULL);
        }
    }
    close(fd);
    printf("--- %s relay ping statistics ---\n%u sent, %u received, %.0f%% packet loss\n", host,
           count, received, 100.0 * (count - received) / count);
    if (received)
        printf("relay RTT min/avg/max = %.2f/%.2f/%.2f ms\n", minimum, total / received, maximum);
    return received ? 0 : 1;
usage:
    fputs("Usage: ping [-c 1..64] [-W timeout_seconds_1..10] [-s data_bytes_8..1392] HOST\nIPv4 "
          "ICMP echo through Android's unprivileged relay; not a guest NIC.\n",
          stderr);
    return 2;
}
#define PLB_UDP_CLIENTS 16
struct plb_udp_client {
    int fd;
    struct sockaddr_in source;
    double used;
};
static int plb_udp_forward(int argc, char **argv) {
    unsigned local_port, remote_port;
    if (argc != 4 || plb_number(argv[1], 1, 65535, &local_port) ||
        plb_number(argv[3], 1, 65535, &remote_port))
        goto usage;
    if (!plb_valid_host((const unsigned char *)argv[2], strlen(argv[2])))
        goto usage;
    int local = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (local < 0) {
        perror("UDP localhost socket");
        return 2;
    }
    struct sockaddr_in bind_address = {.sin_family = AF_INET,
                                       .sin_port = htons((unsigned short)local_port),
                                       .sin_addr = {htonl(INADDR_LOOPBACK)}};
    if (bind(local, (struct sockaddr *)&bind_address, sizeof(bind_address))) {
        perror("UDP localhost bind");
        close(local);
        return 2;
    }
    struct plb_udp_client clients[PLB_UDP_CLIENTS];
    for (int i = 0; i < PLB_UDP_CLIENTS; i++)
        clients[i].fd = -1;
    printf("UDP 127.0.0.1:%u -> %s:%u via Android host relay\nUp to 16 local clients; associations "
           "expire after 60 seconds idle. Ctrl+C stops.\n",
           local_port, argv[2], remote_port);
    fflush(stdout);
    unsigned char data[PLB_DGRAM_MAX];
    double deadline = plb_now() + guest_lifetime;
    while (plb_now() < deadline) {
        struct pollfd p[1 + PLB_UDP_CLIENTS];
        p[0] = (struct pollfd){local, POLLIN, 0};
        for (int i = 0; i < PLB_UDP_CLIENTS; i++) {
            if (clients[i].fd >= 0 && plb_now() - clients[i].used > 60) {
                close(clients[i].fd);
                clients[i].fd = -1;
            }
            p[i + 1] = (struct pollfd){clients[i].fd, POLLIN, 0};
        }
        int result = poll(p, 1 + PLB_UDP_CLIENTS, 1000);
        if (result < 0) {
            if (errno == EINTR)
                continue;
            break;
        }
        if (p[0].revents & POLLIN) {
            struct sockaddr_in source = {0};
            struct iovec buffer = {data, sizeof(data)};
            struct msghdr message = {.msg_name = &source,
                                     .msg_namelen = sizeof(source),
                                     .msg_iov = &buffer,
                                     .msg_iovlen = 1};
            ssize_t n = recvmsg(local, &message, MSG_DONTWAIT | MSG_TRUNC);
            if (n < 0 || (size_t)n > sizeof(data) || source.sin_family != AF_INET ||
                source.sin_addr.s_addr != htonl(INADDR_LOOPBACK))
                continue;
            int slot = -1, free_slot = -1;
            for (int i = 0; i < PLB_UDP_CLIENTS; i++) {
                if (clients[i].fd < 0) {
                    if (free_slot < 0)
                        free_slot = i;
                    continue;
                }
                if (clients[i].source.sin_port == source.sin_port &&
                    clients[i].source.sin_addr.s_addr == source.sin_addr.s_addr) {
                    slot = i;
                    break;
                }
            }
            if (slot < 0) {
                slot = free_slot;
                if (slot < 0)
                    continue;
                unsigned char ready[PLB_DGRAM_READY_SIZE];
                int fd = plb_remote(argv[2], remote_port, 'U', ready);
                if (fd < 0)
                    continue;
                clients[slot] = (struct plb_udp_client){fd, source, plb_now()};
            }
            if (plb_dgram_write(clients[slot].fd, data, (size_t)n)) {
                close(clients[slot].fd);
                clients[slot].fd = -1;
            } else
                clients[slot].used = plb_now();
        }
        for (int i = 0; i < PLB_UDP_CLIENTS; i++)
            if (p[i + 1].revents & (POLLIN | POLLHUP | POLLERR)) {
                if (clients[i].fd != p[i + 1].fd)
                    continue;
                size_t n;
                if (plb_dgram_read(clients[i].fd, data, sizeof(data), &n)) {
                    close(clients[i].fd);
                    clients[i].fd = -1;
                    continue;
                }
                sendto(local, data, n, MSG_DONTWAIT, (struct sockaddr *)&clients[i].source,
                       sizeof(clients[i].source));
                clients[i].used = plb_now();
            }
    }
    for (int i = 0; i < PLB_UDP_CLIENTS; i++)
        if (clients[i].fd >= 0)
            close(clients[i].fd);
    close(local);
    return 0;
usage:
    fputs("Usage: plb-udp-forward LOCAL_PORT HOST REMOTE_PORT\nConfigure the guest app to use "
          "127.0.0.1:LOCAL_PORT. No transparent NIC, broadcast or incoming Internet listener.\n",
          stderr);
    return 2;
}
static int plb_network_command(int argc, char **argv) {
    const char *name = strrchr(argv[0], '/');
    name = name ? name + 1 : argv[0];
    if (!strcmp(name, "ping") || !strcmp(name, "plb-ping"))
        return plb_ping(argc, argv);
    if (!strcmp(name, "plb-udp-forward"))
        return plb_udp_forward(argc, argv);
    fputs("Guest agent must run as guest PID 1.\n", stderr);
    return 48;
}
#endif
