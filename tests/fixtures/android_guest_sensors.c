/* No host hardware or VM. Exercise the real wire bytes and permission/clock gates. */
#include "android_guest_sensors.h"
#include "android_sensor_pipe.h"
#include <assert.h>

static int control(struct plb_guest_sensors *state, const char *text) {
    unsigned char reply[256];
    return plb_sensor_control(state, text, strlen(text), 1000000000, reply, sizeof(reply));
}
int main(void) {
    unsigned char header[PLB_SENSOR_PIPE_HEADER_BYTES] = {0};
    memcpy(header, "PLD1", 4);
    header[6] = 0x29;
    header[7] = 0xab; /* fixed test UID 10667 */
    memcpy(header + 40, "PLS1", 4);
    header[47] = 1;
    uint32_t selected = 0;
    assert(plb_sensor_pipe_header(header, 10667, &selected) && selected == 1);
    assert(!plb_sensor_pipe_header(header, 10668, &selected));
    header[44] = 128;
    assert(!plb_sensor_pipe_header(header, 10667, &selected));
    unsigned char reading[PLB_SENSOR_PIPE_SAMPLE_BYTES] = {0};
    reading[7] = 3;
    reading[15] = 1;
    reading[16] = 0x3f;
    reading[17] = 0x80; /* 1.0f */
    struct plb_sensor_pipe_sample decoded;
    assert(plb_sensor_pipe_decode(reading, 1, &decoded));
    assert(decoded.id == 0 && decoded.count == 3 && decoded.timestamp_ns == 1 &&
           decoded.values[0] == 1.0f);
    assert(!plb_sensor_pipe_decode(reading, 0, &decoded));
    reading[31] = 1;
    assert(!plb_sensor_pipe_decode(reading, 1, &decoded));
    reading[31] = 0;
    reading[16] = 0x7f;
    reading[17] = 0xc0; /* NaN */
    assert(!plb_sensor_pipe_decode(reading, 1, &decoded));
    reading[16] = reading[17] = 0;
    reading[8] = 0x80;
    assert(!plb_sensor_pipe_decode(reading, 1, &decoded));
    struct plb_guest_sensors state;
    unsigned char frame[256];
    float xyz[] = {1, 2, 3};
    plb_sensor_configure(&state, 0);
    assert(control(&state, "set:acceleration:1") == -1);
    assert(!plb_sensor_sample(&state, 0, 1100000000, xyz, 3, frame, sizeof(frame)));
    plb_sensor_configure(&state, (1U << 0) | (1U << 2) | (1U << 31));
    assert(state.available == 5);
    int size = plb_sensor_control(&state, "list-sensors", 12, 1000000000, frame, sizeof(frame));
    assert(size == 10 && frame[3] == 128 && frame[4] == 2 && frame[8] == '5');
    assert(control(&state, "set:magnetic-field:1") == 0);
    assert(control(&state, "set:gyroscope:1") == -1);
    assert(control(&state, "set:magnetic:1") == -1);
    assert(!plb_sensor_sample(&state, 2, 1100000000, xyz, 3, frame, sizeof(frame)));
    assert(control(&state, "time:5000000000") == 0);
    assert(control(&state, "set-delay:0") == 0 && state.delay_ms == 20);
    size_t bytes = plb_sensor_sample(&state, 2, 1100000000, xyz, 3, frame, sizeof(frame));
    assert(bytes > 16 && frame[0] == 2 && frame[3] == 128);
    assert(!memcmp(frame + 8, "guest-sync:5100000\n", 19));
    size_t first = 8 + frame[4];
    assert(frame[first] == 2 && !memcmp(frame + first + 8, "magnetic:1:2:3\n", 15));
    assert(!plb_sensor_sample(&state, 2, 1100000000, xyz, 3, frame, sizeof(frame)));
    assert(!plb_sensor_sample(&state, 2, 1101000000, xyz, 3, frame, sizeof(frame)));
    assert(!plb_sensor_sample(&state, 2, 1090000000, xyz, 3, frame, sizeof(frame)));
    xyz[0] = NAN;
    assert(!plb_sensor_sample(&state, 2, 1200000000, xyz, 3, frame, sizeof(frame)));
    xyz[0] = 1;
    assert(!plb_sensor_sample(&state, 2, 1200000000, xyz, 3, frame, 8));
    assert(control(&state, "set:magnetic-field:0") == 0);
    assert(!plb_sensor_sample(&state, 2, 1300000000, xyz, 3, frame, sizeof(frame)));
    assert(control(&state, "time:9223372036854775808") == -1);
    assert(control(&state, "time:-1") == -1);
    assert(control(&state, "set-delay:5x") == -1);
    const char bad[] = "time:0\0hidden";
    assert(plb_sensor_control(&state, bad, sizeof(bad) - 1, 0, frame, sizeof(frame)) == -1);
    plb_sensor_configure(&state, 5);
    assert(!state.enabled && !state.clock_ready);
    return 0;
}
