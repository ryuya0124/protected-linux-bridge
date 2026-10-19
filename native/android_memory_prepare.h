/* MIT. Bounded ordinary RAM preparation for the selected Android profile.
 * Run before VM creation and before any host policy change. The common owner
 * lock prevents another owned VM/allocation operation from running alongside.
 * No signals, OOM-score changes or reclamation controls target another process. */
#ifndef PLB_ANDROID_MEMORY_PREPARE_H
#define PLB_ANDROID_MEMORY_PREPARE_H
#include "../tools/allocation_probe_limits.h"

static unsigned long android_prepare_available(void) {
    FILE *input = fopen("/proc/meminfo", "re");
    if (!input)
        return 0;
    char line[128];
    unsigned long available = 0;
    while (fgets(line, sizeof(line), input))
        if (sscanf(line, "MemAvailable: %lu kB", &available) == 1)
            break;
    fclose(input);
    return available;
}

static uint64_t android_prepare_pattern(uint64_t value) {
    value ^= value << 13;
    value ^= value >> 7;
    value ^= value << 17;
    return value;
}

static int android_prepare_worker(void) {
    signal(SIGALRM, SIG_DFL);
    alarm(25);
    int score = open("/proc/self/oom_score_adj", O_WRONLY | O_CLOEXEC);
    if (score < 0 || write(score, "1000", 4) != 4)
        return 1;
    close(score);
    size_t cap = plb_pressure_limit("reclaim-6g");
    unsigned char *memory =
        mmap(NULL, cap, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (memory == MAP_FAILED)
        return 1;
    size_t touched = 0, errors = 0;
    while (touched < cap && android_prepare_available() >= PLB_PROBE_AVAILABLE_FLOOR_KIB) {
        size_t end = touched + plb_pressure_chunk(touched, cap);
        if (end <= touched)
            break;
        for (size_t page = touched; page < end; page += 4096) {
            uint64_t state = 0x9e3779b97f4a7c15ULL ^ (page / 4096);
            uint64_t *words = (uint64_t *)(memory + page);
            for (size_t word = 0; word < 4096 / sizeof(*words); word++) {
                state = android_prepare_pattern(state);
                words[word] = state;
            }
        }
        touched = end;
    }
    for (size_t page = 0; page < touched; page += 4096) {
        uint64_t expected = android_prepare_pattern(0x9e3779b97f4a7c15ULL ^ (page / 4096));
        errors += *(volatile uint64_t *)(memory + page) != expected;
    }
    munmap(memory, cap);
    dprintf(STDERR_FILENO, "ANDROID_MEMORY_PREPARED touched_mib=%zu errors=%zu\n",
            touched / PLB_PROBE_MIB, errors);
    return errors || !touched ? 1 : 0;
}

/* The app connects before preparation, so closing its viewer cancels promptly.
 * No display thread exists yet: both the worker and later guardian fork safely. */
static int android_prepare_memory(void) {
    fprintf(stderr, "ANDROID_MEMORY_PREPARING total_budget_mib=6144 floor_mib=1536\n");
    pid_t parent = getpid(), worker = fork();
    if (!worker) {
        if (prctl(PR_SET_PDEATHSIG, SIGKILL) || getppid() != parent)
            _exit(1);
        _exit(android_prepare_worker());
    }
    if (worker < 0)
        return -1;
    int status = 0;
    pid_t done = 0;
    for (int i = 0; i < 300 && !stopping; i++) {
        done = waitpid(worker, &status, WNOHANG);
        if (done < 0 && errno == EINTR)
            done = 0;
        if (done > 0 || (done < 0 && errno != EINTR))
            break;
        if (!android_display_client_alive()) {
            stopping = 1;
            break;
        }
        usleep(100000);
    }
    if (!done) {
        kill(worker, SIGKILL);
        while (waitpid(worker, &status, 0) < 0 && errno == EINTR) {
        }
        return -1;
    }
    return done == worker && WIFEXITED(status) && WEXITSTATUS(status) == 0 ? 0 : -1;
}
#endif
