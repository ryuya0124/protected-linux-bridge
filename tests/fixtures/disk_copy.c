#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include "stream_relay.h"
#include "datagram_protocol.h"
static int corrupt;
static int flush(int fd) {
    if (corrupt && pwrite(fd, "bad", 3, 3) != 3)
        return -1;
    return fsync(fd);
}
#define fsync flush
#include "disk_copy.h"
int main(int argc, char **argv) {
    if (argc != 5)
        return 3;
    corrupt = atoi(argv[4]);
    int from = open(argv[1], O_RDONLY), to = open(argv[2], corrupt == 2 ? O_RDONLY : O_RDWR);
    if (from < 0 || to < 0)
        return 4;
    return disk_copy_verify(from, to, strtoull(argv[3], NULL, 10)) ? 1 : 0;
}
