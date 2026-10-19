/* MIT. Bounded client for the pinned, offline Cuttlefish userdebug guest's adbd.
 * Only connects to guest CID 39821, port 5555. Never connects to host Android ADB.
 * AUTH is rejected: supporting authenticated general-purpose ADB is out of scope.
 * No listening TCP socket, forwarding or arbitrary client command. The separate
 * development render probe can upload only its built-in signed guest test APK;
 * the ordinary app has no upload endpoint or caller-selected payload/path.
 */
#ifndef PLB_ANDROID_PROTOCOL_TEST
#include <linux/vm_sockets.h>
#endif
#include <poll.h>
#include <time.h>

static _Thread_local int android_shutdown_io;

#define ANDROID_ADB_LIMIT (2U * 1024U * 1024U)
#define ADB_CNXN 0x4e584e43U
#define ADB_OPEN 0x4e45504fU
#define ADB_OKAY 0x59414b4fU
#define ADB_CLSE 0x45534c43U
#define ADB_WRTE 0x45545257U

static int android_io(int fd, void *data, size_t size, int writing) {
    unsigned char *at = data;
    while (size) {
        ssize_t n = writing ? send(fd, at, size, MSG_NOSIGNAL) : recv(fd, at, size, 0);
        if (n < 0 && errno == EINTR && (!stopping || android_shutdown_io))
            continue;
        if (n <= 0 || (stopping && !android_shutdown_io))
            return -1;
        at += n;
        size -= (size_t)n;
    }
    return 0;
}

static int android_adb_send(int fd, uint32_t command, uint32_t a, uint32_t b, const void *payload,
                            uint32_t length) {
    uint32_t checksum = 0;
    const unsigned char *bytes = payload;
    for (uint32_t i = 0; i < length; i++)
        checksum += bytes[i];
    uint32_t header[] = {command, a, b, length, checksum, command ^ 0xffffffffU};
    return android_io(fd, header, sizeof(header), 1) ||
           (length && android_io(fd, (void *)payload, length, 1));
}

static int android_adb_receive(int fd, uint32_t header[6], unsigned char *body) {
    if (android_io(fd, header, 24, 0) || header[3] > 1024 * 1024 ||
        header[5] != (header[0] ^ 0xffffffffU) || (header[3] && android_io(fd, body, header[3], 0)))
        return -1;
    uint32_t checksum = 0;
    for (uint32_t i = 0; i < header[3]; i++)
        checksum += body[i];
    /* New ADB protocol versions may omit checksums. */
    return header[4] && checksum != header[4] ? -1 : 0;
}

#ifndef PLB_ANDROID_PROTOCOL_TEST
static int android_adb(const char *service, unsigned char *output, size_t *length) {
    *length = 0;
    int fd = socket(AF_VSOCK, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0)
        return -1;
    struct timeval timeout = {.tv_sec = 8};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    struct sockaddr_vm address = {.svm_family = AF_VSOCK, .svm_cid = 39821, .svm_port = 5555};
    unsigned char *body = malloc(1024 * 1024);
    int result = -1;
    if (!body || connect(fd, (struct sockaddr *)&address, sizeof(address)))
        goto done;
    const char identity[] = "host::";
    uint32_t header[6];
    if (android_adb_send(fd, ADB_CNXN, 0x01000001, 1024 * 1024, identity, sizeof(identity)) ||
        android_adb_receive(fd, header, body) || header[0] != ADB_CNXN)
        goto done;
    if (android_adb_send(fd, ADB_OPEN, 1, 0, service, (uint32_t)strlen(service) + 1))
        goto done;
    uint32_t remote = 0;
    for (unsigned packets = 0; packets < 1024 && (!stopping || android_shutdown_io); packets++) {
        if (android_adb_receive(fd, header, body))
            goto done;
        if (header[2] != 1)
            goto done;
        if (header[0] == ADB_OKAY) {
            remote = header[1];
        } else if (header[0] == ADB_WRTE) {
            if (!remote || header[1] != remote || header[3] > ANDROID_ADB_LIMIT - *length)
                goto done;
            memcpy(output + *length, body, header[3]);
            *length += header[3];
            if (android_adb_send(fd, ADB_OKAY, 1, remote, NULL, 0))
                goto done;
        } else if (header[0] == ADB_CLSE) {
            android_adb_send(fd, ADB_CLSE, 1, header[1], NULL, 0);
            result = 0;
            goto done;
        } else {
            goto done;
        }
    }
done:
    free(body);
    close(fd);
    return result;
}

#endif
