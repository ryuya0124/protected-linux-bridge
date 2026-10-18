/* MIT. Explicit bounded UTF-8 clipboard requests; never polls Android clipboard. */
#ifndef PLB_GUEST_CLIPBOARD_H
#define PLB_GUEST_CLIPBOARD_H
#define PLB_CLIP_MAX 65536
static void clip_header(int fd, unsigned size) {
    unsigned char h[] = {(unsigned char)(size >> 24), (unsigned char)(size >> 16),
                         (unsigned char)(size >> 8), (unsigned char)size};
    plb_write_all(fd, h, 4);
}
static void clipboard_environment(void) {
    configure_environment();
    if (!access("/run/user/0/wayland-plb", F_OK) && !access("/tmp/.X11-unix/X0", F_OK))
        setenv("DISPLAY", ":0", 1);
    char source[3] = {0};
    int fd = open("/run/plb/clipboard-source", O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd >= 0) {
        ssize_t n = read(fd, source, sizeof(source));
        close(fd);
        if (n == 2 && source[0] == ':' && (source[1] == '0' || source[1] == '1'))
            setenv("DISPLAY", source, 1);
    }
}
static int clip_tool(int client, int write_mode, int wayland, const char *display,
                     unsigned char *data, size_t *size) {
    int p[2];
    if (pipe2(p, O_CLOEXEC))
        return -1;
    pid_t child = fork();
    if (child == 0) {
        close(client);
        clipboard_environment();
        if (display)
            setenv("DISPLAY", display, 1);
        setenv("WAYLAND_DISPLAY", "wayland-plb", 1);
        int null = open("/dev/null", O_RDWR | O_CLOEXEC);
        if (null >= 0)
            dup2(null, STDERR_FILENO);
        if (write_mode) {
            dup2(p[0], STDIN_FILENO);
            if (null >= 0)
                dup2(null, STDOUT_FILENO);
        } else {
            dup2(p[1], STDOUT_FILENO);
            if (null >= 0)
                dup2(null, STDIN_FILENO);
        }
        close(p[0]);
        close(p[1]);
        if (null >= 0)
            close(null);
        if (wayland) {
            if (write_mode)
                execlp("wl-copy", "wl-copy", "--type", "text/plain;charset=utf-8", (char *)NULL);
            else
                execlp("wl-paste", "wl-paste", "--no-newline", "--type", "text", (char *)NULL);
        } else {
            if (write_mode)
                execlp("xclip", "xclip", "-selection", "clipboard", "-in", (char *)NULL);
            else
                execlp("xclip", "xclip", "-selection", "clipboard", "-out", (char *)NULL);
        }
        _exit(127);
    }
    if (child < 0) {
        close(p[0]);
        close(p[1]);
        return -1;
    }
    int result = 0;
    if (write_mode) {
        close(p[0]);
        result = plb_write_all(p[1], data, *size);
        close(p[1]);
    } else {
        close(p[1]);
        *size = 0;
        double until = plb_now() + 3;
        for (;;) {
            struct pollfd ready = {p[0], POLLIN, 0};
            int left = (int)((until - plb_now()) * 1000);
            if (left <= 0 || poll(&ready, 1, left) <= 0) {
                result = -1;
                break;
            }
            unsigned char extra;
            void *to = *size < PLB_CLIP_MAX ? data + *size : &extra;
            size_t capacity = *size < PLB_CLIP_MAX ? PLB_CLIP_MAX - *size : 1;
            ssize_t n = read(p[0], to, capacity);
            if (!n)
                break;
            if (n < 0 || *size == PLB_CLIP_MAX) {
                result = -1;
                break;
            }
            *size += (size_t)n;
        }
        close(p[0]);
    }
    int status = 0;
    double until = plb_now() + 3;
    pid_t done;
    do {
        done = waitpid(child, &status, WNOHANG);
        if (!done)
            usleep(10000);
    } while (!done && plb_now() < until);
    if (!done) {
        kill(child, SIGKILL);
        waitpid(child, &status, 0);
        result = -1;
    } else if (done < 0 || !WIFEXITED(status) || WEXITSTATUS(status))
        result = -1;
    return result;
}
static void clipboard_session(int client) {
    unsigned char request = 0, h[4], *data = malloc(PLB_CLIP_MAX);
    if (!data)
        return;
    alarm(10);
    if (plb_read_exact(client, &request, 1))
        goto done;
    int wayland = !access("/run/user/0/wayland-plb", F_OK);
    size_t size = 0;
    if (request == 'G') {
        int error = clip_tool(client, 0, 0, NULL, data, &size);
        if (error && wayland)
            error = clip_tool(client, 0, 1, NULL, data, &size);
        clip_header(client, error ? 0xffffffffU : (unsigned)size);
        if (!error)
            plb_write_all(client, data, size);
    } else if (request == 'P' && !plb_read_exact(client, h, 4)) {
        unsigned length =
            ((unsigned)h[0] << 24) | ((unsigned)h[1] << 16) | ((unsigned)h[2] << 8) | h[3];
        if (length > PLB_CLIP_MAX || plb_read_exact(client, data, length))
            goto done;
        size = length;
        int error = clip_tool(client, 1, 0, NULL, data, &size);
        if (error && wayland)
            error = clip_tool(client, 1, 1, NULL, data, &size);
        if (!error && !access("/tmp/.X11-unix/X0", F_OK)) {
            size = length;
            clip_tool(client, 1, 0, ":0", data, &size);
            size = length;
            clip_tool(client, 1, 0, ":1", data, &size);
        }
        unsigned char status = error ? 1 : 0;
        plb_write_all(client, &status, 1);
    }
done:
    free(data);
}
static void clipboard_track_start(void) {
    pid_t child = fork();
    if (!child) {
        configure_environment();
        int null = open("/dev/null", O_RDWR | O_CLOEXEC);
        if (null >= 0) {
            dup2(null, 0);
            dup2(null, 1);
            dup2(null, 2);
            if (null > 2)
                close(null);
        }
        execl("/usr/bin/python3", "python3", "/run/plb/clipboard-watch.py", "--track-only",
              (char *)NULL);
        _exit(127);
    }
}
static void clipboard_watch(int client) {
    int p[2];
    if (pipe2(p, O_CLOEXEC)) {
        plb_write_all(client, "\3", 1);
        return;
    }
    pid_t child = fork();
    if (!child) {
        setsid();
        close(client);
        clipboard_environment();
        dup2(p[1], STDOUT_FILENO);
        close(p[0]);
        close(p[1]);
        int null = open("/dev/null", O_RDWR | O_CLOEXEC);
        if (null >= 0) {
            dup2(null, STDIN_FILENO);
            dup2(null, STDERR_FILENO);
            close(null);
        }
        execl("/usr/bin/python3", "python3", "/run/plb/clipboard-watch.py", (char *)NULL);
        _exit(127);
    }
    close(p[1]);
    if (child < 0) {
        close(p[0]);
        plb_write_all(client, "\3", 1);
        return;
    }
    plb_write_all(client, "\0", 1);
    plb_relay(client, p[0], guest_lifetime, 1);
    close(p[0]);
    kill(-child, SIGTERM);
    int status;
    for (int i = 0; i < 20; i++) {
        if (waitpid(child, &status, WNOHANG) != 0)
            return;
        usleep(10000);
    }
    kill(-child, SIGKILL);
    waitpid(child, &status, 0);
}
#endif
