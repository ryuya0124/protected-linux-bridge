/* Exercise the actual broker with synthetic peers, never a physical LED. */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include "stream_relay.h"
static int guest_lifetime = 10;
#include "guest_torch.h"
int main(int argc, char **argv) {
    signal(SIGPIPE, SIG_IGN);
    if (argc == 2 && !strcmp(argv[1], "--broker"))
        torch_broker();
    return torch_command(argc, argv);
}
