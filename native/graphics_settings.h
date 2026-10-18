/* MIT. Fixed graphics settings only; no host command strings or guest file mounting. */
#ifndef PLB_GRAPHICS_SETTINGS_H
#define PLB_GRAPHICS_SETTINGS_H
#include "display_modes.h"
#define PLB_GRAPHICS_FILE BASE "/graphics-mode"
struct graphics_settings {
    int mode, resolution, fps;
};
static int graphics_read(struct graphics_settings *value) {
    *value = (struct graphics_settings){-1, 2, 30};
    int fd = open(PLB_GRAPHICS_FILE, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0)
        return errno == ENOENT ? 0 : -1;
    struct stat st;
    char data[64] = {0};
    ssize_t n = read(fd, data, sizeof(data) - 1);
    int ok = !fstat(fd, &st) && S_ISREG(st.st_mode) && st.st_uid == 0 && !(st.st_mode & 077) &&
             n > 0 && n < 63;
    close(fd);
    if (!ok)
        return -1;
    if (n == 2 && data[1] == '\n') {
        value->mode = data[0] - '0';
    } else {
        char tail, extra;
        if (sscanf(data, "%d %d %d%c%c", &value->mode, &value->resolution, &value->fps, &tail,
                   &extra) != 4 ||
            tail != '\n')
            return -1;
    }
    return (value->mode == 0 || value->mode == 1 || value->mode == 3) &&
                   plb_display_valid(value->resolution, value->fps)
               ? 0
               : -1;
}
static int graphics_save(int mode, int resolution, int fps) {
    if ((mode != 0 && mode != 1 && mode != 3) || !plb_display_valid(resolution, fps))
        return 1;
    int fd = open(PLB_GRAPHICS_FILE ".pending",
                  O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (fd < 0)
        return 1;
    struct stat st;
    char data[64];
    int size = snprintf(data, sizeof(data), "%d %d %d\n", mode, resolution, fps);
    int error = fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_uid != 0 || fchmod(fd, 0600) ||
                plb_write_all(fd, data, (size_t)size) || fsync(fd);
    close(fd);
    if (!error)
        error = rename(PLB_GRAPHICS_FILE ".pending", PLB_GRAPHICS_FILE);
    if (error)
        unlink(PLB_GRAPHICS_FILE ".pending");
    return !!error;
}
static int graphics_number(const char *text) {
    if (!text || !*text)
        return -1;
    int value = 0;
    for (; *text; text++) {
        if (*text < '0' || *text > '9' || value > 1000)
            return -1;
        value = value * 10 + *text - '0';
    }
    return value;
}
#endif
