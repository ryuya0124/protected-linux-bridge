/* MIT. Bounded keepalive for the developer-only headless Vulkan trial.
 * Drops to Bridge's actual UID. No screenshots, shell/upload API or Surface.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <grp.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

static volatile sig_atomic_t stopped;
static void stop(int number) {
    (void)number;
    stopped = 1;
}

static int read_all(int fd, void *data, size_t size) {
    unsigned char *at = data;
    while (size) {
        ssize_t count = read(fd, at, size);
        if (count <= 0)
            return -1;
        at += count;
        size -= (size_t)count;
    }
    return 0;
}

int main(void) {
    alarm(230);
    setvbuf(stdout, NULL, _IONBF, 0);
    struct stat app;
    pid_t parent = getppid();
    if (getuid() || stat("/data/user/0/net.ryuya_dev.protectedlinux/files", &app) ||
        !S_ISDIR(app.st_mode) || app.st_uid < 10000 || app.st_uid > 19999 || setgroups(0, NULL) ||
        setresgid(app.st_uid, app.st_uid, app.st_uid) ||
        setresuid(app.st_uid, app.st_uid, app.st_uid) || prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) ||
        prctl(PR_SET_PDEATHSIG, SIGTERM) || getppid() != parent)
        return 20;
    signal(SIGTERM, stop);
    signal(SIGINT, stop);
    signal(SIGPIPE, SIG_IGN);
    const char name[] = "net.ryuya_dev.protectedlinux.android-display";
    struct sockaddr_un address = {.sun_family = AF_UNIX};
    memcpy(address.sun_path + 1, name, sizeof(name) - 1);
    int fd = -1;
    for (int i = 0; i < 600 && !stopped; i++) {
        fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
        if (fd < 0)
            return 21;
        if (!connect(fd, (struct sockaddr *)&address,
                     offsetof(struct sockaddr_un, sun_path) + sizeof(name)))
            break;
        close(fd);
        fd = -1;
        usleep(100000);
    }
    if (fd < 0)
        return 22;
    struct ucred peer;
    socklen_t size = sizeof(peer);
    if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &peer, &size) || peer.uid)
        return 23;
    struct timeval timeout = {.tv_sec = 90};
    if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)))
        return 24;
    uint32_t greeting[5];
    if (read_all(fd, greeting, sizeof(greeting)) || greeting[0] != 0x41424c50 || greeting[1] != 1 ||
        greeting[2] != 480 || greeting[3] != 800 || greeting[4])
        return 25;
    puts("PLB_VULKAN_CLIENT_READY_NO_CAPTURE");
    timeout.tv_sec = 3;
    if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) ||
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)))
        return 26;
    for (int i = 0; i < 120 && !stopped; i++) {
        unsigned char reply;
        if (write(fd, "P", 1) != 1 || read_all(fd, &reply, 1) || reply != 'P')
            return 27;
        sleep(1);
    }
    (void)write(fd, "X", 1);
    close(fd);
    puts("PLB_VULKAN_CLIENT_CLOSED");
    return 0;
}
