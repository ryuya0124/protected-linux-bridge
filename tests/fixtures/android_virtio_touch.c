/* MIT. Validate real virtio event encoding and atomic multi-finger state changes. */
#include <assert.h>
#include <stdio.h>
#define PLB_TOUCH_PACKET_TEST 1
#include "android_virtio_touch.h"

static int touches(struct plb_touch_event *events, int count, int value) {
    int found = 0;
    for (int i = 0; i < count; i++)
        if (events[i].type == 1 && events[i].code == 0x14a && events[i].value == value)
            found++;
    return found;
}
int main(void) {
    struct plb_touch_state state = {0};
    struct plb_touch_event events[PLB_TOUCH_EVENT_CAPACITY];
    uint32_t first[] = {0, 0, 100, 200}, second[] = {1, 0, 300, 600};
    int count = plb_touch_packet(&state, 1, first, events);
    assert(count == 8 && state.active_mask == 1 && touches(events, count, 1) == 1);
    unsigned char *bytes = (unsigned char *)events;
    assert(bytes[0] == 3 && bytes[1] == 0 && bytes[2] == 0x2f && bytes[3] == 0);
    count = plb_touch_packet(&state, 1, second, events);
    assert(count > 0 && state.active_mask == 3 && !touches(events, count, 1));
    uint32_t move[] = {1, 2, 320, 620, 0, 2, 80, 180};
    count = plb_touch_packet(&state, 2, move, events);
    int syncs = 0;
    for (int i = 0; i < count; i++)
        if (events[i].type == 0)
            syncs++;
    assert(syncs == 1 && events[count - 1].type == 0 && state.active_mask == 3);
    uint32_t duplicate[] = {0, 1, 0, 0, 0, 1, 0, 0};
    assert(plb_touch_packet(&state, 2, duplicate, events) == -1 && state.active_mask == 3);
    uint32_t bad[] = {1, 2, 480, 20};
    assert(plb_touch_packet(&state, 1, bad, events) == -1 && state.active_mask == 3);
    assert(plb_touch_packet(&state, 0, first, events) == -1);
    assert(plb_touch_packet(&state, 11, first, events) == -1);
    uint32_t up1[] = {1, 1, 300, 600};
    count = plb_touch_packet(&state, 1, up1, events);
    assert(state.active_mask == 1 && !touches(events, count, 0));
    uint32_t cancel[] = {0, 3, 0, 0};
    count = plb_touch_packet(&state, 1, cancel, events);
    assert(state.active_mask == 0 && touches(events, count, 0) == 1);
    assert(plb_touch_packet(&state, 1, cancel, events) == 0);
    uint32_t all[40];
    for (uint32_t i = 0; i < 10; i++) {
        all[i * 4] = i;
        all[i * 4 + 1] = 0;
        all[i * 4 + 2] = i * 10;
        all[i * 4 + 3] = i * 20;
    }
    count = plb_touch_packet(&state, 10, all, events);
    assert(count > 0 && count <= PLB_TOUCH_EVENT_CAPACITY && state.active_mask == 1023);
    for (int i = 0; i < 10; i++)
        all[i * 4 + 1] = 3;
    count = plb_touch_packet(&state, 10, all, events);
    assert(count > 0 && state.active_mask == 0 && touches(events, count, 0) == 1);
    puts("PLB_VIRTIO_MULTITOUCH_OK");
    return 0;
}
