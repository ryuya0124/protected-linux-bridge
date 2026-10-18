/* MIT. Explicit diagnostic client: run as the installed Terminal UID, not root. */
#define _GNU_SOURCE
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <stdlib.h>
int main(int argc, char **argv) {
    if (getuid() == 0 || argc != 2) {
        fputs("Run as Terminal UID with a guest command\n", stderr);
        return 1;
    }
    alarm(600);
    const char name[] = "net.ryuya_dev.protectedlinux.console";
    struct sockaddr_un a = {.sun_family = AF_UNIX};
    memcpy(a.sun_path + 1, name, sizeof(name) - 1);
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0 ||
        connect(fd, (struct sockaddr *)&a, offsetof(struct sockaddr_un, sun_path) + sizeof(name))) {
        perror("connect");
        return 2;
    }
    struct ucred peer;
    socklen_t length = sizeof(peer);
    if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &peer, &length) || peer.uid != 0)
        return 3;
    write(fd, "R", 1);
    unsigned char status = 255;
    if (read(fd, &status, 1) != 1 || status) {
        fprintf(stderr, "VM_STATUS=%u\n", status);
        return 4;
    }
    printf("VM_CONSOLE_ACCEPTED uid=%d\n", getuid());
    fflush(stdout);
    write(fd, argv[1], strlen(argv[1]));
    write(fd, "\r", 1);
    char data[16384];
    ssize_t n;
    while ((n = read(fd, data, sizeof(data))) > 0) {
        ssize_t sent = 0;
        while (sent < n) {
            ssize_t part = write(1, data + sent, (size_t)(n - sent));
            if (part <= 0)
                return 5;
            sent += part;
        }
    }
    close(fd);
    return 0;
}
