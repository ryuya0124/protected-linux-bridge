#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <arpa/inet.h>
#include <netdb.h>
#include "stream_relay.h"
#include "datagram_protocol.h"
static int plb_network_lifetime = 10;
static int forbidden_address(const struct sockaddr *a) {
    return a->sa_family != AF_INET ||
           ((const struct sockaddr_in *)a)->sin_addr.s_addr != htonl(0x7f000001);
}
static int tcp_connect(const char *host, const char *port) {
    struct sockaddr_in a = {.sin_family = AF_INET, .sin_port = htons(atoi(port))};
    if (inet_pton(AF_INET, host, &a.sin_addr) != 1 || forbidden_address((struct sockaddr *)&a))
        return -1;
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (connect(fd, (struct sockaddr *)&a, sizeof(a))) {
        close(fd);
        return -1;
    }
    return fd;
}
#ifndef SOCK_CLOEXEC
#define SOCK_CLOEXEC 0
#endif
#include "socks_broker.h"
int main(int n, char **v) {
    if (n != 2 && n != 3)
        return 2;
    if (n == 3)
        plb_network_lifetime = atoi(v[2]);
    signal(SIGPIPE, SIG_IGN);
    socks_broker(atoi(v[1]));
    return 0;
}
