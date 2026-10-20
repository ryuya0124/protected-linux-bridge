/* MIT. Cuttlefish/Goldfish Android 17 sensor protocol, no physical capture.
 * The session owner supplies the intersection of detected and selected sensors.
 * Zero/default state advertises nothing. This is not a public command channel.
 */
#ifndef PLB_ANDROID_GUEST_SENSORS_H
#define PLB_ANDROID_GUEST_SENSORS_H
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <locale.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PLB_SENSOR_SUPPORTED_MASK 0x1f7U /* IDs 0,1,2,4,5,6,7,8; no synthetic orientation. */
struct plb_guest_sensors {
    uint32_t available, enabled;
    unsigned delay_ms;
    int clock_ready;
    int64_t guest_anchor_ns, host_anchor_ns, last_host_ns[9];
};

static inline const char *plb_sensor_control_name(unsigned id) {
    static const char *names[] = {"acceleration", "gyroscope",   "magnetic-field",
                                  NULL,           "temperature", "proximity",
                                  "light",        "pressure",    "humidity"};
    return id < 9 ? names[id] : NULL;
}
static inline void plb_sensor_configure(struct plb_guest_sensors *state, uint32_t selected) {
    memset(state, 0, sizeof(*state));
    state->available = selected & PLB_SENSOR_SUPPORTED_MASK;
    state->delay_ms = 50;
}
static inline int plb_sensor_integer(const char *text, int64_t *value) {
    if (!text || !*text)
        return 0;
    for (const char *at = text; *at; at++)
        if (*at < '0' || *at > '9')
            return 0;
    errno = 0;
    char *end;
    long long parsed = strtoll(text, &end, 10);
    if (errno || *end || parsed < 0)
        return 0;
    *value = parsed;
    return 1;
}
static inline void plb_sensor_u32(unsigned char *out, uint32_t value) {
    for (int i = 0; i < 4; i++)
        out[i] = (unsigned char)(value >> (8 * i));
}
static inline size_t plb_sensor_frame(unsigned char *out, size_t capacity, uint32_t command,
                                      const char *payload, size_t length) {
    if (length > 4096 || capacity < length + 8)
        return 0;
    plb_sensor_u32(out, command | 0x80000000U);
    plb_sensor_u32(out + 4, (uint32_t)length);
    memcpy(out + 8, payload, length);
    return length + 8;
}
/* Returns framed reply size, zero for a recognized one-way control, -1 for an
 * invalid request. Commands/lengths are checked by the outer framed transport. */
static inline int plb_sensor_control(struct plb_guest_sensors *state, const void *payload,
                                     size_t length, int64_t host_now_ns, unsigned char *reply,
                                     size_t capacity) {
    char text[128];
    if (!payload || !length || length >= sizeof(text) || memchr(payload, 0, length) ||
        host_now_ns < 0)
        return -1;
    memcpy(text, payload, length);
    text[length] = 0;
    if (!strcmp(text, "list-sensors")) {
        char mask[32];
        int count = snprintf(mask, sizeof(mask), "%" PRIu32 "\n", state->available);
        size_t size = plb_sensor_frame(reply, capacity, 0, mask, (size_t)count);
        return size ? (int)size : -1;
    }
    int64_t value;
    if (!strncmp(text, "time:", 5)) {
        if (!plb_sensor_integer(text + 5, &value))
            return -1;
        state->guest_anchor_ns = value;
        state->host_anchor_ns = host_now_ns;
        state->clock_ready = 1;
        memset(state->last_host_ns, 0, sizeof(state->last_host_ns));
        return 0;
    }
    if (!strncmp(text, "set-delay:", 10)) {
        if (!plb_sensor_integer(text + 10, &value))
            return -1;
        /* Physical collection stays at <=50 Hz, regardless of guest demand. */
        state->delay_ms = value < 20 ? 20 : value > 1000 ? 1000 : (unsigned)value;
        return 0;
    }
    for (unsigned id = 0; id < 9; id++) {
        const char *name = plb_sensor_control_name(id);
        if (!name)
            continue;
        char expected[96];
        for (unsigned enabled = 0; enabled < 2; enabled++) {
            snprintf(expected, sizeof(expected), "set:%s:%u", name, enabled);
            if (strcmp(text, expected))
                continue;
            if (!(state->available & (1U << id)))
                return -1;
            if (enabled)
                state->enabled |= 1U << id;
            else
                state->enabled &= ~(1U << id);
            state->last_host_ns[id] = 0;
            return 0;
        }
    }
    return -1;
}
/* Two framed DATA responses: guest-clock timestamp then actual sensor values.
 * The host natural-device axes and SI/Android units are retained. Proximity is
 * a normalized near/far value (0/1) because the guest advertises maxRange=1.
 */
static inline size_t plb_sensor_sample(struct plb_guest_sensors *state, unsigned id,
                                       int64_t host_timestamp_ns, const float *values, size_t count,
                                       unsigned char *out, size_t capacity) {
    const char *control = plb_sensor_control_name(id);
    if (!control || !values || !state->clock_ready || !(state->available & (1U << id)) ||
        !(state->enabled & (1U << id)) || count != (id <= 2 ? 3U : 1U) ||
        host_timestamp_ns < state->host_anchor_ns || host_timestamp_ns <= state->last_host_ns[id])
        return 0;
    for (size_t i = 0; i < count; i++)
        if (!isfinite(values[i]))
            return 0;
    if (state->last_host_ns[id] &&
        host_timestamp_ns - state->last_host_ns[id] < (int64_t)state->delay_ms * 1000000)
        return 0;
    int64_t elapsed = host_timestamp_ns - state->host_anchor_ns;
    if (elapsed > INT64_MAX - state->guest_anchor_ns)
        return 0;
    char timestamp[64], sample[128];
    int time_length = snprintf(timestamp, sizeof(timestamp), "guest-sync:%" PRId64 "\n",
                               (state->guest_anchor_ns + elapsed) / 1000);
    const char *data_name = id == 2 ? "magnetic" : control;
    locale_t numeric = newlocale(LC_NUMERIC_MASK, "C", (locale_t)0);
    if (!numeric)
        return 0;
    locale_t original = uselocale(numeric);
    int sample_length = count == 3
                            ? snprintf(sample, sizeof(sample), "%s:%.9g:%.9g:%.9g\n", data_name,
                                       values[0], values[1], values[2])
                            : snprintf(sample, sizeof(sample), "%s:%.9g\n", data_name, values[0]);
    uselocale(original);
    freelocale(numeric);
    if (time_length < 0 || (size_t)time_length >= sizeof(timestamp) || sample_length < 0 ||
        (size_t)sample_length >= sizeof(sample) ||
        capacity < 16 + (size_t)time_length + (size_t)sample_length)
        return 0;
    size_t first = plb_sensor_frame(out, capacity, 2, timestamp, (size_t)time_length);
    size_t second =
        plb_sensor_frame(out + first, capacity - first, 2, sample, (size_t)sample_length);
    if (!first || !second)
        return 0;
    state->last_host_ns[id] = host_timestamp_ns;
    return first + second;
}
#endif
