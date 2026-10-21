/* MIT. Exercise real reply delivery and conservative refusal of every live role. */
#include <assert.h>
#include <signal.h>
#include <stdio.h>
#include <sys/socket.h>
#include <unistd.h>
#include "stream_relay.h"
#include "idle_handoff.h"

static void request(int authorized, int role, int expected) {
    int pair[2];
    assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, pair));
    pid_t sessions[2] = {role == 3 ? 100 : 0, 0};
    pid_t workers[2] = {0, role == 6 ? 100 : 0};
    int stop =
        plb_idle_handoff(pair[0], authorized, role == 1 ? 100 : -1, role == 2 ? 100 : -1, sessions,
                         2, role == 4 ? 100 : -1, role == 5 ? 100 : -1, workers, 2);
    unsigned char reply = 255;
    assert(read(pair[1], &reply, 1) == 1 && reply == expected);
    assert(stop == (expected == 0));
    close(pair[0]);
    close(pair[1]);
}

int main(void) {
    (void)plb_relay;
    signal(SIGPIPE, SIG_IGN);
    request(1, 0, 0);
    request(0, 0, 3);
    for (int role = 1; role <= 6; role++)
        request(1, role, 2);
    int pair[2];
    assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, pair));
    close(pair[1]);
    assert(!plb_idle_handoff(pair[0], 1, -1, -1, NULL, 0, -1, -1, NULL, 0));
    close(pair[0]);
    puts("PLB_IDLE_HANDOFF_BOUNDARIES_PASS");
    return 0;
}
