/* MIT. A VMM exit code alone is not a guest boot/readback success. */
#ifndef PLB_CPU_PROBE_RESULT_H
#define PLB_CPU_PROBE_RESULT_H
#include <stdio.h>
#include <string.h>

static int plb_cpu_console_success_for_bytes(FILE *console, const char *nonce, int debian_required,
                                             size_t expected_bytes) {
    if (!console || !nonce || strlen(nonce) != 32 ||
        (expected_bytes != 16ULL * 1024 * 1024 && expected_bytes != 256ULL * 1024 * 1024 &&
         expected_bytes != 384ULL * 1024 * 1024 && expected_bytes != 768ULL * 1024 * 1024 &&
         expected_bytes != 1280ULL * 1024 * 1024))
        return 0;
    char line[1024], expected[128];
    snprintf(expected, sizeof(expected), "PLB_CPU_FINISHED nonce=%s result=0", nonce);
    int memory_ok = 0, debian_ok = 0, finished = 0, invalid = 0;
    size_t total = 0;
    while (fgets(line, sizeof(line), console)) {
        size_t length = strlen(line);
        total += length;
        if (total > 1024 * 1024)
            return 0;
        while (length && (line[length - 1] == '\r' || line[length - 1] == '\n'))
            line[--length] = 0;
        if (!strncmp(line, "PLB_CPU_GUEST ", 14)) {
            char observed[33], arch[16], release[128], extra;
            int result;
            size_t bytes;
            int fields = sscanf(
                line, "PLB_CPU_GUEST nonce=%32s result=%d arch=%15s release=%127s bytes=%zu%c",
                observed, &result, arch, release, &bytes, &extra);
            if (fields == 5 && !strcmp(observed, nonce)) {
                if (!result && !strcmp(arch, "aarch64") && bytes == expected_bytes)
                    memory_ok = 1;
                else
                    invalid = 1;
            }
        }
        if (!strcmp(line, "PLB_CPU_DEBIAN_READONLY result=0"))
            debian_ok = memory_ok;
        if (!strcmp(line, expected))
            finished = memory_ok && (!debian_required || debian_ok);
    }
    return !ferror(console) && !invalid && finished;
}
#endif
