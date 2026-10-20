/* MIT. Fixed, session-private app -> controller sensor stream.
 * The inherited stdin pipe is not a socket or public API. The controller copies
 * the original display capability to its child; only it consumes sensor bytes.
 * No paths, commands, permissions or capture requests are accepted here.
 */
#ifndef PLB_ANDROID_SENSOR_PIPE_H
#define PLB_ANDROID_SENSOR_PIPE_H
#include <math.h>
#include <stdint.h>
#include <string.h>
#include "android_guest_sensors.h"

#define PLB_SENSOR_PIPE_HEADER_BYTES 48
#define PLB_SENSOR_PIPE_SAMPLE_BYTES 32
struct plb_sensor_pipe_sample {
    unsigned id, count;
    int64_t timestamp_ns;
    float values[3];
};
static inline uint32_t plb_sensor_read_be32(const unsigned char *at) {
    return (uint32_t)at[0] << 24 | (uint32_t)at[1] << 16 | (uint32_t)at[2] << 8 | at[3];
}
static inline int plb_sensor_pipe_header(const unsigned char *bytes, unsigned viewer_uid,
                                         uint32_t *mask) {
    if (memcmp(bytes, "PLD1", 4) || plb_sensor_read_be32(bytes + 4) != viewer_uid ||
        memcmp(bytes + 40, "PLS1", 4))
        return 0;
    *mask = plb_sensor_read_be32(bytes + 44);
    return *mask && !(*mask & ~PLB_SENSOR_SUPPORTED_MASK);
}
static inline int plb_sensor_pipe_decode(const unsigned char *bytes, uint32_t selected,
                                         struct plb_sensor_pipe_sample *sample) {
    sample->id = plb_sensor_read_be32(bytes);
    sample->count = plb_sensor_read_be32(bytes + 4);
    uint64_t timestamp =
        (uint64_t)plb_sensor_read_be32(bytes + 8) << 32 | plb_sensor_read_be32(bytes + 12);
    if (sample->id >= 9 || !(selected & (1U << sample->id)) ||
        sample->count != (sample->id <= 2 ? 3U : 1U) || timestamp > INT64_MAX ||
        plb_sensor_read_be32(bytes + 28))
        return 0;
    sample->timestamp_ns = (int64_t)timestamp;
    for (unsigned i = 0; i < 3; i++) {
        uint32_t bits = plb_sensor_read_be32(bytes + 16 + 4 * i);
        memcpy(&sample->values[i], &bits, 4);
        if (!isfinite(sample->values[i]) || (i >= sample->count && bits))
            return 0;
    }
    return 1;
}
#endif
