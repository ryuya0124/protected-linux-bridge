/* MIT. Verified offline app-private copies for the explicit Android GPU profile.
 * Original files are only read; every new copy is read back before promotion. */
#ifndef PLB_ANDROID_GPU4G_FILES_H
#define PLB_ANDROID_GPU4G_FILES_H
static int private_file(const char *path) {
    struct stat file;
    return !lstat(path, &file) && S_ISREG(file.st_mode) && !file.st_uid && file.st_nlink == 1 &&
           !(file.st_mode & 077);
}

static int pinned_digest(const char *path, const char *expected) {
    int pipefd[2];
    if (pipe2(pipefd, O_CLOEXEC))
        return 0;
    pid_t pid = fork();
    if (!pid) {
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[0]);
        close(pipefd[1]);
        execl("/system/bin/toybox", "toybox", "sha256sum", path, (char *)NULL);
        _exit(126);
    }
    close(pipefd[1]);
    char result[256];
    size_t used = 0;
    ssize_t count;
    while (used < sizeof(result) &&
           (count = read(pipefd[0], result + used, sizeof(result) - used)) > 0)
        used += (size_t)count;
    close(pipefd[0]);
    int status;
    if (pid < 0)
        return 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR)
            return 0;
    }
    return WIFEXITED(status) && WEXITSTATUS(status) == 0 && used > 65 &&
           !memcmp(result, expected, 64) && result[64] == ' ';
}

static int owned_directory(const char *path, int create) {
    if (create && mkdir(path, 0700) && errno != EEXIST)
        return -1;
    struct stat info;
    return lstat(path, &info) || !S_ISDIR(info.st_mode) || info.st_uid || (info.st_mode & 077) ? -1
                                                                                               : 0;
}

static int owned_write_all(int fd, const unsigned char *data, size_t size) {
    size_t at = 0;
    while (at < size) {
        ssize_t written = write(fd, data + at, size - at);
        if (written < 0 && errno == EINTR)
            continue;
        if (written <= 0)
            return -1;
        at += (size_t)written;
    }
    return 0;
}

static int owned_copy(const char *source, const char *destination, mode_t mode, uid_t source_uid) {
    int input = open(source, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (input < 0)
        return -1;
    struct stat before = {0}, after = {0};
    int valid =
        !fstat(input, &before) && S_ISREG(before.st_mode) && before.st_uid == source_uid &&
        before.st_nlink == 1 &&
        !(before.st_mode & (!strcmp(source, "/apex/com.android.virt/bin/crosvm") ? 022 : 077));
    int output =
        valid ? open(destination, O_RDWR | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, mode) : -1;
    if (output < 0) {
        close(input);
        return -1;
    }
    unsigned char bytes[65536], comparison[65536];
    off_t offset = 0;
    int failed = 0;
    for (;;) {
        ssize_t count = pread(input, bytes, sizeof(bytes), offset);
        if (count < 0 && errno == EINTR)
            continue;
        if (count < 0) {
            failed = 1;
            break;
        }
        if (!count)
            break;
        if (owned_write_all(output, bytes, (size_t)count) ||
            pread(output, comparison, (size_t)count, offset) != count ||
            memcmp(bytes, comparison, (size_t)count)) {
            failed = 1;
            break;
        }
        offset += count;
    }
    failed |= fstat(input, &after) != 0;
    failed |= offset != before.st_size || before.st_size != after.st_size ||
              before.st_mtim.tv_sec != after.st_mtim.tv_sec ||
              before.st_mtim.tv_nsec != after.st_mtim.tv_nsec;
    failed |= fsync(output) != 0 || fchmod(output, mode) != 0;
    close(input);
    close(output);
    if (failed)
        unlink(destination); /* Only the new file created by this invocation. */
    return failed ? -1 : 0;
}

static int owned_install_pinned(const char *source, const char *destination, const char *digest,
                                mode_t mode, uid_t source_uid) {
    if (private_file(destination) && pinned_digest(destination, digest))
        return 0;
    char pending[256];
    snprintf(pending, sizeof(pending), "%s.pending", destination);
    struct stat info;
    if (!lstat(pending, &info)) {
        if (!private_file(pending) || unlink(pending))
            return -1;
    } else if (errno != ENOENT)
        return -1;
    if (owned_copy(source, pending, mode, source_uid) || !pinned_digest(pending, digest))
        return -1;
    if (!lstat(destination, &info) && !private_file(destination))
        return -1;
    return rename(pending, destination);
}

#endif
