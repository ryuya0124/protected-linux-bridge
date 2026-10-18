/* MIT. Opt-in clipboard helper, dropped to shell's existing clipboard permission. */
#ifndef PLB_CLIPBOARD_HOST_H
#define PLB_CLIPBOARD_HOST_H
#define CLIP_DIR "/data/local/tmp/net.ryuya_dev.protectedlinux.clipboard"
static pid_t clipboard_pid = -1;
static int clipboard_mode;
static int clipboard_start(uid_t module_uid, int mode) {
    if (mode < 0 || mode > 3)
        return -1;
    if (clipboard_pid > 0 && waitpid(clipboard_pid, NULL, WNOHANG) != 0) {
        clipboard_pid = -1;
        clipboard_mode = 0;
    }
    if (mode == clipboard_mode && (!mode || clipboard_pid > 0))
        return 0;
    if (clipboard_pid > 0) {
        kill(clipboard_pid, SIGTERM);
        wait_or_kill(clipboard_pid, 1000);
        clipboard_pid = -1;
    }
    clipboard_mode = 0;
    if (!mode)
        return 0;
    if (mkdir(CLIP_DIR, 0755) && errno != EEXIST)
        return -1;
    int directory = open(CLIP_DIR, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    struct stat st;
    if (directory < 0)
        return -1;
    if (fstat(directory, &st) || !S_ISDIR(st.st_mode) || st.st_uid || (st.st_mode & 022)) {
        close(directory);
        return -1;
    }
    if (fchmod(directory, 0755)) {
        close(directory);
        return -1;
    }
    int src = open("/data/user/0/net.ryuya_dev.protectedlinux/files/runtime/clipboard-worker.jar",
                   O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (src < 0) {
        close(directory);
        return -1;
    }
    if (fstat(src, &st) || !S_ISREG(st.st_mode) || st.st_uid != module_uid || st.st_size < 1024 ||
        st.st_size > 256 * 1024) {
        close(src);
        close(directory);
        return -1;
    }
    int dst = openat(directory, "worker.jar.pending",
                     O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (dst < 0) {
        close(src);
        close(directory);
        return -1;
    }
    unsigned char data[16384];
    ssize_t n;
    off_t copied = 0;
    int error = 0;
    while ((n = read(src, data, sizeof(data))) > 0) {
        if (plb_write_all(dst, data, (size_t)n)) {
            error = -1;
            break;
        }
        copied += n;
    }
    if (n < 0 || copied != st.st_size || fsync(dst) || fchmod(dst, 0644))
        error = -1;
    close(src);
    close(dst);
    if (!error && renameat(directory, "worker.jar.pending", directory, "worker.jar"))
        error = -1;
    close(directory);
    if (error)
        return -1;
    pid_t parent = getpid(), child = fork();
    if (child < 0)
        return -1;
    if (!child) {
        log_child(BASE "/clipboard-bridge.log");
        for (int fd = 3; fd < 1024; fd++)
            close(fd);
        if (setgroups(0, NULL) || setgid(2000) || setuid(2000) ||
            prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) || prctl(PR_SET_PDEATHSIG, SIGKILL) ||
            getppid() != parent)
            _exit(110);
        char uid[24], flags[8];
        snprintf(uid, sizeof(uid), "%u", module_uid);
        snprintf(flags, sizeof(flags), "%d", mode);
        setenv("CLASSPATH", CLIP_DIR "/worker.jar", 1);
        execl("/system/bin/app_process", "app_process", "/system/bin", "--nice-name=plb-clipboard",
              "net.ryuya_dev.protectedlinux.root.ClipboardWorker", uid, flags, (char *)NULL);
        _exit(111);
    }
    clipboard_pid = child;
    clipboard_mode = mode;
    return 0;
}
static int clipboard_client(int mode) {
    int fd = local_socket(0);
    if (fd < 0)
        return 1;
    struct timeval timeout = {4, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    unsigned char command[] = {'L', (unsigned char)mode}, result = 255;
    int error = plb_write_all(fd, command, 2) || read(fd, &result, 1) != 1 || result;
    close(fd);
    return !!error;
}
#endif
