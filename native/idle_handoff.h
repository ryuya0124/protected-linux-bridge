/* MIT. Called only by the serialized Debian manager control loop. */
#ifndef PLB_IDLE_HANDOFF_H
#define PLB_IDLE_HANDOFF_H
#include <stddef.h>
#include <sys/types.h>

static int plb_idle_handoff(int client, int authorized, pid_t vm, pid_t proxy,
                            const pid_t *sessions, size_t session_count, pid_t renderer,
                            pid_t gpu_server, const pid_t *gpu_workers, size_t worker_count) {
    int busy = vm > 0 || proxy > 0 || renderer > 0 || gpu_server > 0;
    for (size_t i = 0; i < session_count; i++)
        busy |= sessions[i] > 0;
    for (size_t i = 0; i < worker_count; i++)
        busy |= gpu_workers[i] > 0;
    unsigned char reply = !authorized ? 3 : busy ? 2 : 0;
    // A failed reply must not turn an abandoned request into a stop operation.
    return !plb_write_all(client, &reply, 1) && reply == 0;
}
#endif
