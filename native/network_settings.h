/* MIT. Optional TUN networking is scoped to the experimental guest only. */
#ifndef PLB_NETWORK_SETTINGS_H
#define PLB_NETWORK_SETTINGS_H
static int network_flag_read(const char *path) {
    int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0)
        return errno == ENOENT ? 0 : -1;
    struct stat st;
    char value[3];
    ssize_t n = read(fd, value, sizeof(value));
    int ok = !fstat(fd, &st) && S_ISREG(st.st_mode) && st.st_uid == 0 && !(st.st_mode & 077) &&
             n == 2 && value[1] == '\n' && (value[0] == '0' || value[0] == '1');
    close(fd);
    return ok ? value[0] - '0' : -1;
}
static int network_flag_save(const char *path, const char *pending, int enabled) {
    if ((enabled != 0 && enabled != 1) || (enabled && strcmp(PROFILE_TAG, " stock") == 0))
        return 1;
    int fd = open(pending, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0)
        return 1;
    struct stat st;
    const char *value = enabled ? "1\n" : "0\n";
    int error = fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_uid != 0 || fchmod(fd, 0600) ||
                plb_write_all(fd, value, 2) || fsync(fd);
    close(fd);
    if (!error)
        error = rename(pending, path);
    if (error)
        unlink(pending);
    return !!error;
}
static int network_read(void) {
    return network_flag_read(BASE "/network-mode");
}
static int network_app(void) {
    return network_flag_read(BASE "/network-app");
}
static int network_save(int value) {
    return network_flag_save(BASE "/network-mode", BASE "/network-mode.pending", value);
}
static int network_app_save(int value) {
    return network_flag_save(BASE "/network-app", BASE "/network-app.pending", value);
}
#endif
