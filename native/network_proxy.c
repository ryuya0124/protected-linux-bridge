/* MIT - Copyright (c) 2026 ryuya0124. VM-only HTTP CONNECT/forward proxy. */
#define _GNU_SOURCE
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <linux/vm_sockets.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <ifaddrs.h>
#include <grp.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <strings.h>
#include <unistd.h>
#include <errno.h>
#include "stream_relay.h"
#define PLB_CID 39820
#define PLB_PROXY_PORT 50441
static int forbidden_address(const struct sockaddr *address) {
    if (address->sa_family == AF_INET) {
        unsigned ip = ntohl(((const struct sockaddr_in *)address)->sin_addr.s_addr);
        if ((ip >> 24) == 0 || (ip >> 24) == 127 || (ip >> 28) >= 14 || (ip >> 16) == 0xa9fe)
            return 1;
    } else if (address->sa_family == AF_INET6) {
        const struct in6_addr *ip = &((const struct sockaddr_in6 *)address)->sin6_addr;
        if (IN6_IS_ADDR_LOOPBACK(ip) || IN6_IS_ADDR_UNSPECIFIED(ip) || IN6_IS_ADDR_MULTICAST(ip) ||
            IN6_IS_ADDR_LINKLOCAL(ip) || IN6_IS_ADDR_V4MAPPED(ip))
            return 1;
    } else
        return 1;
    struct ifaddrs *interfaces = NULL;
    if (getifaddrs(&interfaces)) {
        fprintf(stderr, "PROXY_INTERFACE_CHECK errno=%d\n", errno);
        return 1;
    }
    int forbidden = 0;
    for (struct ifaddrs *i = interfaces; i; i = i->ifa_next) {
        if (!i->ifa_addr || i->ifa_addr->sa_family != address->sa_family)
            continue;
        if (address->sa_family == AF_INET &&
            !memcmp(&((const struct sockaddr_in *)address)->sin_addr,
                    &((const struct sockaddr_in *)i->ifa_addr)->sin_addr, sizeof(struct in_addr)))
            forbidden = 1;
        if (address->sa_family == AF_INET6 &&
            !memcmp(&((const struct sockaddr_in6 *)address)->sin6_addr,
                    &((const struct sockaddr_in6 *)i->ifa_addr)->sin6_addr,
                    sizeof(struct in6_addr)))
            forbidden = 1;
    }
    freeifaddrs(interfaces);
    return forbidden;
}
static int plb_network_lifetime = 1800;
#define PLB_NETWORK_LIFETIME plb_network_lifetime
#include "datagram_broker.h"
static int tcp_connect(const char *host, const char *port) {
    struct addrinfo hints = {.ai_family = AF_UNSPEC, .ai_socktype = SOCK_STREAM}, *addresses = NULL;
    int resolved = getaddrinfo(host, port, &hints, &addresses);
    if (resolved) {
        fprintf(stderr, "PROXY_DNS error=%d uid=%d\n", resolved, getuid());
        return -1;
    }
    int fd = -1;
    for (struct addrinfo *a = addresses; a; a = a->ai_next) {
        if (forbidden_address(a->ai_addr)) {
            fprintf(stderr, "PROXY_DESTINATION_REJECTED\n");
            continue;
        }
        fd = socket(a->ai_family, SOCK_STREAM | SOCK_CLOEXEC, 0);
        if (fd < 0) {
            fprintf(stderr, "PROXY_SOCKET errno=%d\n", errno);
            continue;
        }
        int flags = fcntl(fd, F_GETFL);
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
        int result = connect(fd, a->ai_addr, a->ai_addrlen);
        if (result && errno == EINPROGRESS) {
            struct pollfd p = {fd, POLLOUT, 0};
            int error = 1;
            socklen_t length = sizeof(error);
            if (poll(&p, 1, 8000) > 0 && !getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &length) &&
                !error)
                result = 0;
        }
        if (!result) {
            fcntl(fd, F_SETFL, flags);
            break;
        }
        fprintf(stderr, "PROXY_CONNECT_FAILED errno=%d\n", errno);
        close(fd);
        fd = -1;
    }
    freeaddrinfo(addresses);
    return fd;
}
#include "socks_broker.h"
static int authority(const char *input, char *host, size_t capacity, char port[6],
                     unsigned default_port) {
    size_t length = strlen(input);
    if (!length || length > 300)
        return -1;
    for (size_t i = 0; i < length; i++)
        if ((unsigned char)input[i] <= 32 || strchr("/@\\?#", input[i]))
            return -1;
    const char *start = input, *end = input + length, *port_start = NULL;
    if (*start == '[') {
        start++;
        end = strchr(start, ']');
        if (!end)
            return -1;
        if (end[1]) {
            if (end[1] != ':')
                return -1;
            port_start = end + 2;
        }
    } else {
        const char *colon = strrchr(start, ':');
        if (colon) {
            end = colon;
            port_start = colon + 1;
        }
    }
    size_t host_size = (size_t)(end - start);
    if (!host_size || host_size >= capacity)
        return -1;
    memcpy(host, start, host_size);
    host[host_size] = 0;
    unsigned long value = default_port;
    if (port_start) {
        if (!*port_start)
            return -1;
        for (const char *p = port_start; *p; p++)
            if (*p < '0' || *p > '9')
                return -1;
        char *tail;
        value = strtoul(port_start, &tail, 10);
        if (*tail || !value || value > 65535)
            return -1;
    }
    if (!value)
        return -1;
    snprintf(port, 6, "%lu", value);
    return 0;
}
static void proxy_failure_at(int fd, int line) {
    fprintf(stderr, "PROXY_HTTP_FAILURE line=%d\n", line);
    const char text[] =
        "HTTP/1.1 502 Bad Gateway\r\nConnection: close\r\nContent-Length: 0\r\n\r\n";
    plb_write_all(fd, text, sizeof(text) - 1);
}

#define proxy_failure(fd) proxy_failure_at((fd), __LINE__)
static void http_proxy(int client) {
    fprintf(stderr, "PROXY_ACCEPTED uid=%d\n", getuid());
    alarm(30);
    char headers[32768];
    size_t length = 0;
    while (length < sizeof(headers) - 1) {
        ssize_t n = read(client, headers + length, 1);
        if (n < 0 && errno == EINTR)
            continue;
        if (n != 1)
            return;
        length++;
        if (length >= 4 && !memcmp(headers + length - 4, "\r\n\r\n", 4))
            break;
    }
    if (length >= sizeof(headers) - 1) {
        proxy_failure(client);
        return;
    }
    headers[length] = 0;
    char *line = strstr(headers, "\r\n");
    if (!line) {
        proxy_failure(client);
        return;
    }
    *line = 0;
    char method[17], target[8192], version[16], extra;
    int fields = sscanf(headers, "%16s %8191s %15s %c", method, target, version, &extra);
    if (fields != 3 || (strcmp(version, "HTTP/1.1") && strcmp(version, "HTTP/1.0"))) {
        proxy_failure(client);
        return;
    }
    for (const char *m = method; *m; m++)
        if (*m < 'A' || *m > 'Z') {
            proxy_failure(client);
            return;
        }
    int tunnel = !strcmp(method, "CONNECT");
    char address[301], host[256], port[6];
    const char *path = "/";
    if (tunnel) {
        if (strlen(target) >= sizeof(address)) {
            proxy_failure(client);
            return;
        }
        strcpy(address, target);
    } else {
        if (strncmp(target, "http://", 7)) {
            proxy_failure(client);
            return;
        }
        const char *start = target + 7, *slash = strchr(start, '/'), *query = strchr(start, '?');
        const char *end = slash ? slash : target + strlen(target);
        if (query && query < end) {
            proxy_failure(client);
            return;
        }
        size_t n = (size_t)(end - start);
        if (n >= sizeof(address)) {
            proxy_failure(client);
            return;
        }
        memcpy(address, start, n);
        address[n] = 0;
        if (slash)
            path = slash;
    }
    if (authority(address, host, sizeof(host), port, tunnel ? 0 : 80)) {
        proxy_failure(client);
        return;
    }
    int remote = tcp_connect(host, port);
    if (remote < 0) {
        proxy_failure(client);
        return;
    }
    struct timeval timeout = {3, 0};
    setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    setsockopt(remote, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    if (tunnel) {
        const char ok[] = "HTTP/1.1 200 Connection Established\r\n\r\n";
        if (plb_write_all(client, ok, sizeof(ok) - 1)) {
            close(remote);
            return;
        }
    } else {
        char request[9000];
        int count =
            snprintf(request, sizeof(request),
                     "%s %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n", method, path, address);
        if (count < 0 || (size_t)count >= sizeof(request) ||
            plb_write_all(remote, request, (size_t)count)) {
            close(remote);
            return;
        }
        char *current = line + 2;
        while (*current) {
            char *next = strstr(current, "\r\n");
            if (!next)
                break;
            if (next == current)
                break;
            size_t n = (size_t)(next - current);
            if (!strncasecmp(current, "Connection:", 11) ||
                !strncasecmp(current, "Proxy-Connection:", 17) ||
                !strncasecmp(current, "Proxy-Authorization:", 20) ||
                !strncasecmp(current, "Host:", 5)) {
            } else {
                if (plb_write_all(remote, current, n) || plb_write_all(remote, "\r\n", 2)) {
                    close(remote);
                    return;
                }
            }
            current = next + 2;
        }
        if (plb_write_all(remote, "\r\n", 2)) {
            close(remote);
            return;
        }
    }
    alarm(0);
    plb_relay(client, remote, plb_network_lifetime, 0);
    close(remote);
}
pid_t plb_start_proxy(uid_t network_uid, int manager_listener, int startup_client, int lifetime) {
    plb_network_lifetime = lifetime;
    int listener = socket(AF_VSOCK, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (listener < 0)
        return -1;
    struct sockaddr_vm a = {
        .svm_family = AF_VSOCK, .svm_port = PLB_PROXY_PORT, .svm_cid = VMADDR_CID_ANY};
    if (bind(listener, (struct sockaddr *)&a, sizeof(a)) || listen(listener, 16)) {
        close(listener);
        return -1;
    }
    pid_t parent = getpid(), pid = fork();
    if (pid != 0) {
        close(listener);
        return pid;
    }
    // The unprivileged proxy must never retain the root manager's control listener.
    if (manager_listener >= 0)
        close(manager_listener);
    if (startup_client >= 0)
        close(startup_client);
    int diagnostic = open("/data/user/0/net.ryuya_dev.protectedlinux/files/vm/network-proxy.log",
                          O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (diagnostic >= 0) {
        dup2(diagnostic, STDERR_FILENO);
        close(diagnostic);
    }
    prctl(PR_SET_PDEATHSIG, SIGKILL);
    if (getppid() != parent)
        _exit(70);
    signal(SIGPIPE, SIG_IGN);
    signal(SIGTERM, SIG_DFL);
    signal(SIGINT, SIG_DFL);
    // INTERNET permission gives Android app processes the ordinary inet group.
    // Keep that group only; dropping it also prevents Android DNS resolution.
    struct group *inet = getgrnam("inet");
    if (!inet)
        _exit(74);
    gid_t groups[] = {inet->gr_gid};
    if (setgroups(1, groups) || setgid(network_uid) || setuid(network_uid) ||
        prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0))
        _exit(71);
    // Linux clears parent-death signals on credential changes; restore after dropping UID.
    if (prctl(PR_SET_PDEATHSIG, SIGKILL) || getppid() != parent)
        _exit(73);
    int active = 0;
    for (;;) {
        while (waitpid(-1, NULL, WNOHANG) > 0)
            if (active > 0)
                active--;
        struct pollfd p = {listener, POLLIN, 0};
        if (poll(&p, 1, 200) <= 0)
            continue;
        struct sockaddr_vm peer;
        socklen_t size = sizeof(peer);
        int client = accept4(listener, (struct sockaddr *)&peer, &size, SOCK_CLOEXEC);
        if (client < 0)
            continue;
        if (peer.svm_cid != PLB_CID || active >= 16) {
            close(client);
            continue;
        }
        pid_t owner = getpid(), child = fork();
        if (child == 0) {
            prctl(PR_SET_PDEATHSIG, SIGKILL);
            if (getppid() != owner)
                _exit(72);
            close(listener);
            alarm(15);
            unsigned char first = 255;
            ssize_t got = recv(client, &first, 1, MSG_PEEK);
            if (got == 1) {
                if (first == 0)
                    plb_datagram_broker(client);
                else if (first == 5)
                    socks_broker(client);
                else
                    http_proxy(client);
            }
            close(client);
            _exit(0);
        }
        if (child > 0)
            active++;
        close(client);
    }
}
#ifdef PLB_PROXY_STANDALONE
int main(int argc, char **argv) {
    if (getuid() != 0 || argc != 2)
        return 1;
    pid_t child = plb_start_proxy((uid_t)atoi(argv[1]), -1, -1, 1800);
    if (child < 0) {
        perror("proxy start");
        return 2;
    }
    printf("PROXY_PID=%d\n", child);
    fflush(stdout);
    int status;
    waitpid(child, &status, 0);
    return WIFEXITED(status) ? WEXITSTATUS(status) : 128;
}
#endif
