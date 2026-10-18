#include <stdlib.h>
#include "stream_relay.h"
#include "datagram_protocol.h"
#include "gpu_protocol.h"
int main(int argc, char **argv) {
    if (argc != 4)
        return 3;
    int a = atoi(argv[2]), b = atoi(argv[3]);
    if (argv[1][0] == 'g')
        return gpu_negotiate(a, b) ? 1 : 0;
    unsigned char data[PLB_DGRAM_MAX];
    size_t size;
    if (argv[1][0] == 'w')
        return plb_dgram_write(a, data, PLB_DGRAM_MAX + 1) ? 0 : 1;
    if (plb_dgram_read(a, data, argv[1][0] == 's' ? 8 : sizeof(data), &size))
        return 1;
    return plb_dgram_write(b, data, size) ? 2 : 0;
}