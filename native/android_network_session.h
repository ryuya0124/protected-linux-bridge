/* MIT. Opt-in network controller for the separately pinned GPU4G runtime. */
static pid_t android_network_child = -1;
// Set once before the display worker starts; retain through guest cleanup.
static int android_network_owner_fd = -1;

static void android_network_stop(void) {
    if (android_network_child <= 0)
        return;
    // The child is never reaped elsewhere, so this PID cannot have been reused.
    kill(android_network_child, SIGTERM);
    for (int i = 0; i < 120; i++) {
        if (waitpid(android_network_child, NULL, WNOHANG) == android_network_child) {
            android_network_child = -1;
            return;
        }
        usleep(100000);
    }
    kill(android_network_child, SIGKILL);
    while (waitpid(android_network_child, NULL, 0) < 0 && errno == EINTR) {
    }
    android_network_child = -1;
}

static int android_network_start(uid_t uid) {
    if (android_network_child > 0 || android_network_owner_fd < 3)
        return -1;
    int channel[2];
    if (pipe2(channel, O_CLOEXEC))
        return -1;
    char identity[16], owner_descriptor[16];
    snprintf(identity, sizeof(identity), "%u", uid);
    snprintf(owner_descriptor, sizeof(owner_descriptor), "%d", android_network_owner_fd);
    pid_t owner = getpid();
    android_network_child = fork();
    if (android_network_child == 0) {
        // Fork from the display worker: use only async-signal-safe operations
        // until exec; the controller closes other inherited descriptors itself.
        if (prctl(PR_SET_PDEATHSIG, SIGTERM) || getppid() != owner ||
            dup2(channel[1], STDOUT_FILENO) < 0 || fcntl(android_network_owner_fd, F_SETFD, 0))
            _exit(70);
        int log = open("/data/user/0/net.ryuya_dev.protectedlinux/files/android-network.log",
                       O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW, 0600);
        if (log < 0 || dup2(log, STDERR_FILENO) < 0)
            _exit(71);
        if (log > STDERR_FILENO)
            close(log);
        execl("/data/user/0/net.ryuya_dev.protectedlinux/files/android-gpu4g-runtime/"
              "network-controller",
              "network-controller", identity, owner_descriptor, (char *)NULL);
        _exit(72);
    }
    close(channel[1]);
    int result = -1;
    if (android_network_child > 0) {
        for (int i = 0; i < 300 && !stopping; i++) {
            struct pollfd ready = {channel[0], POLLIN, 0};
            if (poll(&ready, 1, 100) > 0) {
                unsigned char response;
                if (read(channel[0], &response, 1) == 1 && response == 0)
                    result = 0;
                break;
            }
        }
    }
    close(channel[0]);
    if (result)
        android_network_stop();
    return result;
}
