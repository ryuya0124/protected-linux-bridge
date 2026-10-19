/* MIT. One verified guest, one unprivileged relay, owned by its VM manager.
 * All payloads and guest commands are fixed at build time; no upload/exec API.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include "network_fd_boundary.h"
#include "stream_relay.h"
#define PLB_OWNER_LOCK_FILE "/data/user/0/net.ryuya_dev/protectedlinux/files/vm-owner.lock"
#include "../tools/diagnostic_owner_lock.h"
#define FILES "/data/user/0/net.ryuya_dev.protectedlinux/files"
static volatile sig_atomic_t stopping;
static void stop(int value) {
    (void)value;
    stopping = 1;
}
#include "android_guest_adb.h"
#include "android_network_inspect.generated.h"
extern pid_t plb_start_proxy(uid_t uid, int listener, int client, int lifetime);

static int guest_command(const char *command, unsigned char *reply) {
    size_t size;
    return android_adb(command, reply, &size) ||
           !memmem(reply, size, "PLB_NETWORK_INSPECT_STEP_OK", 27);
}
int main(int argc, char **argv) {
    (void)plb_relay;
    (void)plb_write_all;
    struct stat directory;
    if (getuid() || argc != 3 || strlen(argv[1]) != 5 || strspn(argv[1], "0123456789") != 5 ||
        lstat(FILES, &directory) || !S_ISDIR(directory.st_mode) || directory.st_uid < 10000 ||
        directory.st_uid > 19999 || strtoul(argv[1], NULL, 10) != directory.st_uid)
        return 20;
    // Keep the same open-file-description lock until guest cleanup completes.
    // Otherwise a dead manager could release its lock and admit a replacement
    // VM while this controller still addresses the old fixed guest CID.
    if (setenv(PLB_DIAGNOSTIC_OWNER_ENV, argv[2], 1))
        return 21;
    int owner_lock = diagnostic_owner_lock();
    if (owner_lock < 3 || plb_network_close_inherited(owner_lock))
        return 21;
    // plb_start_proxy closes everything except its listener before dropping
    // privilege, including this lock and any future controller descriptors.
    pid_t owner = getppid();
    signal(SIGTERM, stop);
    signal(SIGINT, stop);
    signal(SIGPIPE, SIG_IGN);
    if (owner <= 1 || prctl(PR_SET_PDEATHSIG, SIGTERM) || getppid() != owner)
        return 22;
    unsigned char *reply = malloc(ANDROID_ADB_LIMIT);
    if (!reply)
        return 23;
    pid_t proxy = plb_start_proxy(directory.st_uid, -1, -1, 1800);
    unsigned char result = proxy > 0 ? 0 : 1;
    int attempted = 0;
    for (size_t i = 0; !result && !stopping &&
                       i < sizeof(network_inspect_commands) / sizeof(*network_inspect_commands);
         i++) {
        attempted = 1;
        if (guest_command(network_inspect_commands[i], reply)) {
            fprintf(stderr, "PLB_ANDROID_NETWORK_SETUP_FAILED step=%zu\n", i);
            result = 1;
        }
    }
    if (stopping)
        result = 1;
    // This is setup, not a forged assertion of connectivity validation.
    if (write(STDOUT_FILENO, &result, 1) != 1)
        result = 1;
    close(STDOUT_FILENO);
    fprintf(stderr, "PLB_ANDROID_NETWORK_SETUP result=%u\n", result);
    double deadline = plb_now() + 1800;
    while (!result && !stopping && plb_now() < deadline) {
        // Do not reap the owned child here: retaining it prevents PID reuse
        // before the cleanup signal, including an unexpected early exit.
        siginfo_t information = {0};
        if (waitid(P_PID, proxy, &information, WEXITED | WNOHANG | WNOWAIT) || information.si_pid)
            break;
        usleep(100000);
    }
    if (attempted) {
        android_shutdown_io = 1;
        int cleanup = guest_command(network_trial_stop, reply);
        fprintf(stderr, "PLB_ANDROID_NETWORK_GUEST_STOP result=%d\n", cleanup);
        android_shutdown_io = 0;
    }
    free(reply);
    if (proxy > 0) {
        kill(proxy, SIGTERM);
        while (waitpid(proxy, NULL, 0) < 0 && errno == EINTR) {
        }
    }
    return result ? 24 : 0;
}
