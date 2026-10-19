/* MIT. Fail closed on unreadable state or loss of this diagnostic's RAM margin.
 * Observe only our still-unreaped child. Never change another task's OOM score.
 */
#ifndef PLB_ANDROID4G_RESOURCE_LIMITS_H
#define PLB_ANDROID4G_RESOURCE_LIMITS_H
#include <stdio.h>
#include <unistd.h>

static int plb_android4g_resource_limits(pid_t child) {
    char path[96], line[256];
    unsigned long long available = 0, resident = 0;
    int found_available = 0, found_resident = 0;
    FILE *input = fopen("/proc/meminfo", "re");
    if (!input)
        return 0;
    while (fgets(line, sizeof(line), input)) {
        if (sscanf(line, "MemAvailable: %llu kB", &available) == 1) {
            found_available = 1;
            break;
        }
    }
    fclose(input);
    snprintf(path, sizeof(path), "/proc/%ld/status", (long)child);
    input = fopen(path, "re");
    if (!input)
        return 0;
    while (fgets(line, sizeof(line), input)) {
        if (sscanf(line, "VmRSS: %llu kB", &resident) == 1) {
            found_resident = 1;
            break;
        }
    }
    fclose(input);
    /* Leave 256 MiB within the 6 GiB combined cap for the owned supervisor.
     * RSS is an additional stop condition, not a measurement of all GPU memory.
     */
    return found_available && found_resident && available >= 1536ULL * 1024 &&
           resident <= 5888ULL * 1024;
}
#endif
