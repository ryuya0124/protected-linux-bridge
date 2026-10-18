/* MIT. Exercise the actual bounded packet reader on local socketpairs only. */
#include <sys/socket.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif
static int stopping;
#define PLB_ANDROID_PROTOCOL_TEST 1
#include "android_guest_adb.h"

int main(int argc, char **argv) {
    if (argc != 2)
        return 2;
    int mode = atoi(argv[1]), pair[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, pair))
        return 3;
    const unsigned char payload[] = {0, 255, 1, 13};
    uint32_t header[] = {ADB_WRTE, 2, 1, sizeof(payload), 269, ADB_WRTE ^ 0xffffffffU};
    if (mode == 1)
        header[5] ^= 1;
    if (mode == 2)
        header[3] = 1024 * 1024 + 1;
    if (mode == 3)
        header[4]++;
    if (mode == 6)
        header[4] = 0;
    if (mode == 7)
        stopping = 1;
    write(pair[1], header, mode == 4 ? 12 : sizeof(header));
    if (mode != 5 && mode != 4)
        write(pair[1], payload, sizeof(payload));
    shutdown(pair[1], SHUT_WR);
    unsigned char *body = malloc(1024 * 1024);
    uint32_t received[6];
    int result = android_adb_receive(pair[0], received, body);
    int valid = mode == 0 || mode == 6;
    int okay = valid ? !result && !memcmp(body, payload, sizeof(payload)) : result == -1;
    if (mode == 0 && !result) {
        stopping = 0;
        okay = !android_adb_send(pair[0], ADB_OKAY, 1, 2, NULL, 0);
        uint32_t ack[6];
        okay = okay && read(pair[1], ack, sizeof(ack)) == sizeof(ack) && ack[0] == ADB_OKAY &&
               ack[1] == 1 && ack[2] == 2;
    }
    free(body);
    close(pair[0]);
    close(pair[1]);
    return okay ? 0 : 1;
}
