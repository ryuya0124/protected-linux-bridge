/* MIT. Guest-only loopback SOCKS transport to the CID-filtered host relay. */
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <linux/vm_sockets.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/system_properties.h>
#include <sys/wait.h>
#include <unistd.h>
#include "../../native/stream_relay.h"

static int property_is(const char *name, const char *expected) {
    char value[PROP_VALUE_MAX];
    return __system_property_get(name, value) > 0 && !strcmp(value, expected);
}
int main(int argc, char **argv) {
    (void)argv;
    if (getuid() || argc != 1 || !property_is("ro.product.device", "vsoc_arm64_only") ||
        !property_is("ro.hardware", "cutf_cvm") ||
        !property_is("ro.build.version.incremental", "16373615"))
        return 20;
    (void)plb_write_all;
    signal(SIGPIPE, SIG_IGN);
    int server = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    // A stopped session may leave TCP TIME_WAIT sockets. Rebind only loopback;
    // this does not permit two active listeners or expose the proxy externally.
    int reuse = 1;
    if (server < 0 || setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)))
        return 21;
    struct sockaddr_in address = {
        .sin_family = AF_INET, .sin_port = htons(1080), .sin_addr.s_addr = htonl(INADDR_LOOPBACK)};
    if (server < 0 || bind(server, (struct sockaddr *)&address, sizeof(address)) ||
        listen(server, 16))
        return 21;
    pid_t parent = getppid();
    if (parent <= 1 || prctl(PR_SET_PDEATHSIG, SIGTERM) || getppid() != parent)
        return 24;
    int active = 0;
    puts("PLB_GUEST_LOOPBACK_READY");
    fflush(stdout);
    while (1) {
        while (waitpid(-1, NULL, WNOHANG) > 0)
            if (active > 0)
                active--;
        struct pollfd poller = {server, POLLIN, 0};
        if (poll(&poller, 1, 200) <= 0)
            continue;
        int client = accept4(server, NULL, NULL, SOCK_CLOEXEC);
        if (client < 0)
            continue;
        if (active >= 16) {
            close(client);
            continue;
        }
        pid_t owner = getpid(), child = fork();
        if (child == 0) {
            if (prctl(PR_SET_PDEATHSIG, SIGKILL) || getppid() != owner)
                _exit(22);
            close(server);
            alarm(10);
            int remote = socket(AF_VSOCK, SOCK_STREAM | SOCK_CLOEXEC, 0);
            struct sockaddr_vm peer = {.svm_family = AF_VSOCK, .svm_cid = 2, .svm_port = 50442};
            if (remote < 0 || connect(remote, (struct sockaddr *)&peer, sizeof(peer)))
                _exit(23);
            alarm(0);
            plb_relay(client, remote, 0, 0);
            _exit(0);
        }
        if (child > 0)
            active++;
        close(client);
    }
    close(server);
    return 0;
}
