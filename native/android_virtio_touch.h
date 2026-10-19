/* MIT. Guest-only virtio-input events over an owned socket, never host /dev/input.
 * The VMM connects as the exact owned child. UI commands are already app-UID
 * authenticated by the display channel; raw event bytes are never exposed.
 */
#ifndef PLB_ANDROID_VIRTIO_TOUCH_H
#define PLB_ANDROID_VIRTIO_TOUCH_H
#include <stdint.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

struct plb_touch_event {
    uint16_t type, code;
    int32_t value;
};
_Static_assert(sizeof(struct plb_touch_event) == 8, "virtio_input_event wire size");
struct plb_touch_state {
    uint16_t active_mask;
};
#define PLB_TOUCH_MAX_POINTS 10
#define PLB_TOUCH_EVENT_CAPACITY 64

/* Validate an entire sample before committing any state or sending an event.
 * Each entry is slot/action/x/y; slots remain stable for a finger's lifetime.
 * One SYN_REPORT commits every moved finger together for pinch gestures.
 */
static int plb_touch_packet(struct plb_touch_state *state, uint32_t points, const uint32_t *values,
                            struct plb_touch_event events[PLB_TOUCH_EVENT_CAPACITY]) {
    if (!points || points > PLB_TOUCH_MAX_POINTS)
        return -1;
    struct plb_touch_state next = *state;
    uint16_t seen = 0;
    int count = 0;
#define PLB_TOUCH_EVENT(t, c, v)                                                                   \
    events[count++] = (struct plb_touch_event) {                                                   \
        t, c, (int32_t)(v)                                                                         \
    }
    for (uint32_t i = 0; i < points; i++) {
        uint32_t slot = values[i * 4], action = values[i * 4 + 1];
        uint32_t x = values[i * 4 + 2], y = values[i * 4 + 3];
        if (slot >= PLB_TOUCH_MAX_POINTS || action > 3 || x >= 480 || y >= 800 ||
            (seen & (1U << slot)))
            return -1;
        seen |= 1U << slot;
        int active = !!(next.active_mask & (1U << slot));
        if ((action == 0 && active) || (action == 2 && !active))
            return -1;
        if ((action == 1 || action == 3) && !active)
            continue;
        PLB_TOUCH_EVENT(3, 0x2f, slot); /* EV_ABS, ABS_MT_SLOT */
        if (action == 1 || action == 3) {
            PLB_TOUCH_EVENT(3, 0x39, -1); /* ABS_MT_TRACKING_ID */
            next.active_mask &= ~(1U << slot);
            if (!next.active_mask)
                PLB_TOUCH_EVENT(1, 0x14a, 0); /* BTN_TOUCH */
        } else {
            if (action == 0) {
                PLB_TOUCH_EVENT(3, 0x39, slot);
                if (!next.active_mask)
                    PLB_TOUCH_EVENT(1, 0x14a, 1);
                next.active_mask |= 1U << slot;
            }
            PLB_TOUCH_EVENT(3, 0x35, x); /* ABS_MT_POSITION_X */
            PLB_TOUCH_EVENT(3, 0x36, y); /* ABS_MT_POSITION_Y */
            if (slot == 0) {
                PLB_TOUCH_EVENT(3, 0x00, x); /* Legacy ABS_X / ABS_Y */
                PLB_TOUCH_EVENT(3, 0x01, y);
            }
        }
    }
    if (count)
        PLB_TOUCH_EVENT(0, 0, 0); /* EV_SYN, SYN_REPORT */
#undef PLB_TOUCH_EVENT
    *state = next;
    return count;
}

#ifndef PLB_TOUCH_PACKET_TEST
static int android_touch_listener = -1, android_touch_client = -1;
static pid_t android_touch_vmm;
static struct plb_touch_state android_touch_state;

static int android_touch_listen(void) {
    const char *path = BASE "/touch.sock";
    struct stat file;
    if (!lstat(path, &file)) {
        if (!S_ISSOCK(file.st_mode) || file.st_uid || (file.st_mode & 077) || unlink(path))
            return -1;
    } else if (errno != ENOENT)
        return -1;
    android_touch_listener = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    struct sockaddr_un address = {.sun_family = AF_UNIX};
    if (strlen(path) >= sizeof(address.sun_path) || android_touch_listener < 0)
        return -1;
    strcpy(address.sun_path, path);
    if (bind(android_touch_listener, (struct sockaddr *)&address, sizeof(address)) ||
        chmod(path, 0600) || listen(android_touch_listener, 1))
        return -1;
    return 0;
}

static int android_touch_send(uint32_t points, const uint32_t *values) {
    if (android_touch_client < 0) {
        int fd = accept4(android_touch_listener, NULL, NULL, SOCK_CLOEXEC | SOCK_NONBLOCK);
        struct ucred peer;
        socklen_t size = sizeof(peer);
        if (fd < 0)
            return -1;
        if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &peer, &size) || peer.uid ||
            peer.pid != android_touch_vmm) {
            close(fd);
            return -1;
        }
        android_touch_client = fd;
        fprintf(stderr, "ANDROID_VIRTIO_TOUCH_READY\n");
    }
    struct plb_touch_state next = android_touch_state;
    struct plb_touch_event events[PLB_TOUCH_EVENT_CAPACITY];
    int count = plb_touch_packet(&next, points, values, events);
    if (count < 0)
        return -1;
    size_t bytes = (size_t)count * sizeof(*events);
    if (bytes &&
        send(android_touch_client, events, bytes, MSG_NOSIGNAL | MSG_DONTWAIT) != (ssize_t)bytes) {
        // Never replay a partial packet or fabricate input delivery success.
        close(android_touch_client);
        android_touch_client = -1;
        android_touch_state.active_mask = 0;
        stopping = 1; /* End only this VM rather than leave a partially delivered press. */
        return -1;
    }
    android_touch_state = next;
    return 0;
}

static void android_touch_release(void) {
    if (android_touch_client >= 0 && android_touch_state.active_mask) {
        uint32_t values[PLB_TOUCH_MAX_POINTS * 4];
        for (uint32_t i = 0; i < PLB_TOUCH_MAX_POINTS; i++) {
            values[i * 4] = i;
            values[i * 4 + 1] = 3;
            values[i * 4 + 2] = values[i * 4 + 3] = 0;
        }
        (void)android_touch_send(PLB_TOUCH_MAX_POINTS, values);
    }
}

static void android_touch_close(void) {
    android_touch_release();
    if (android_touch_client >= 0)
        close(android_touch_client);
    if (android_touch_listener >= 0)
        close(android_touch_listener);
    android_touch_client = android_touch_listener = -1;
    struct stat file;
    if (!lstat(BASE "/touch.sock", &file) && S_ISSOCK(file.st_mode) && !file.st_uid)
        unlink(BASE "/touch.sock");
}
#endif
#endif
