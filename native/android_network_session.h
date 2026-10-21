/* MIT. Opt-in network controller for the separately pinned GPU4G runtime. */
static pid_t android_network_child = -1;
// Set once before the display worker starts; retain through guest cleanup.
static int android_network_owner_fd = -1;
static int android_network_result_fd = -1, android_network_result = 1;
static double android_network_deadline;
static double android_network_clock(void) {
    struct timespec value;
    return clock_gettime(CLOCK_MONOTONIC, &value) ? -1 : value.tv_sec + value.tv_nsec / 1e9;
}

static void android_network_stop_signal(int signal_number) {
    if (android_network_result_fd >= 0)
        close(android_network_result_fd);
    android_network_result_fd = -1;
    if (android_network_child <= 0)
        return;
    // The child is never reaped elsewhere, so this PID cannot have been reused.
    kill(android_network_child, signal_number);
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

static void android_network_stop(void) {
    android_network_stop_signal(SIGTERM);
}

static int android_network_start(uid_t uid) {
    if (android_network_owner_fd < 3 || android_network_result == 2)
        return 1;
    if (android_network_child > 0) {
#ifdef PLB_ANDROID17_APP_SESSION
        // Keep a healthy guest TUN/NetworkAgent across host-relay reconnects.
        android_network_stop_signal(SIGUSR1);
#else
        android_network_stop();
#endif
    }
    double now = android_network_clock();
    if (now < 0)
        return 1;
    int channel[2];
    if (pipe2(channel, O_CLOEXEC | O_NONBLOCK))
        return 1;
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
        execl(PLB_ANDROID4G_RUNTIME "/network-controller", "network-controller", identity,
              owner_descriptor, (char *)NULL);
        _exit(72);
    }
    close(channel[1]);
    if (android_network_child < 0) {
        close(channel[0]);
        return 1;
    }
    android_network_result_fd = channel[0];
    android_network_result = 2; // Pending: input and presentation remain usable.
#ifdef PLB_ANDROID17_APP_SESSION
    android_network_deadline = now + 90;
#else
    android_network_deadline = now + 30;
#endif
    return android_network_result;
}

static int android_network_status(void) {
    if (android_network_result_fd < 0) {
        if (android_network_result == 0 && android_network_child > 0) {
            siginfo_t child = {0};
            if (waitid(P_PID, android_network_child, &child, WEXITED | WNOHANG | WNOWAIT) ||
                child.si_pid)
                android_network_result = 1;
        }
        return android_network_result;
    }
    struct pollfd ready = {android_network_result_fd, POLLIN, 0};
    int available = poll(&ready, 1, 0);
    double now = android_network_clock();
    if (available > 0) {
        unsigned char result;
        android_network_result =
            read(android_network_result_fd, &result, 1) == 1 && result == 0 ? 0 : 1;
    } else if (now < 0 || now >= android_network_deadline || (available < 0 && errno != EINTR)) {
        android_network_result = 1;
        // The owned child retains the lock until its cleanup finishes. Do not
        // wait on the input worker; final shutdown joins it independently.
        kill(android_network_child, SIGTERM);
    }
    if (android_network_result != 2) {
        close(android_network_result_fd);
        android_network_result_fd = -1;
        siginfo_t information = {0};
        if (!waitid(P_PID, android_network_child, &information, WEXITED | WNOHANG | WNOWAIT) &&
            information.si_pid)
            fprintf(stderr, "ANDROID_NETWORK_CONTROLLER_EXIT code=%d status=%d\n",
                    information.si_code, information.si_status);
        fprintf(stderr, "ANDROID_NETWORK_SETUP result=%d\n", android_network_result);
    }
    return android_network_result;
}
