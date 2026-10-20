/* No host hardware or VM. Exercise the real wire bytes and permission/clock gates. */
#include "android_guest_sensors.h"
#include <assert.h>

static int control(struct plb_guest_sensors *state, const char *text) {
    unsigned char reply[256];
    return plb_sensor_control(state, text, strlen(text), 1000000000, reply, sizeof(reply));
}
int main(void) {
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
