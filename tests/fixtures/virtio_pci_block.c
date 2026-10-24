/* MIT. Host-side fake device exercises capability validation and split queues. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../tools/native_xnu_probe/virtio_pci_block.h"

static void capability(uint8_t *c, unsigned at, unsigned next, unsigned kind, unsigned offset,
                       unsigned bytes) {
    c[at] = 9;
    c[at + 1] = (uint8_t)next;
    c[at + 2] = kind == 2 ? 20 : 16;
    c[at + 3] = (uint8_t)kind;
    plb_put32(c + at + 8, offset);
    plb_put32(c + at + 12, bytes);
    if (kind == 2)
        plb_put32(c + at + 16, 4);
}
static void make_config(uint8_t c[256]) {
    memset(c, 0, 256);
    plb_put16(c, 0x1af4);
    plb_put16(c + 2, 0x1042);
    plb_put16(c + 4, 6);
    plb_put16(c + 6, 0x10);
    plb_put32(c + 0x10, 0x02000000);
    c[0x34] = 0x40;
    capability(c, 0x40, 0x50, 1, 0, 56);
    capability(c, 0x50, 0x64, 2, 0x3000, 4096);
    capability(c, 0x64, 0, 4, 0x2000, 4096);
}
static void parser_tests(void) {
    uint8_t c[256];
    struct plb_vpci_caps found;
    make_config(c);
    assert(plb_vpci_parse(c, 0x02000000, 0x02000000, &found));
    assert(found.common.address == 0x02000000 && found.notify_multiplier == 4);
    for (unsigned kind = 0; kind < 11; kind++) {
        make_config(c);
        switch (kind) {
        case 0:
            c[0x41] = 0x40;
            break; /* cycle */
        case 1:
            c[0x34] = 0xfd;
            break; /* invalid alignment */
        case 2:
            c[0x42] = 15;
            break; /* truncated vendor capability */
        case 3:
            c[0x64 + 3] = 1;
            break; /* duplicate common configuration */
        case 4:
            c[0x44] = 6;
            break; /* nonexistent BAR */
        case 5:
            plb_put32(c + 0x10, 0x02000001);
            break; /* port I/O BAR */
        case 6:
            plb_put32(c + 0x48, 32767);
            break; /* cap crosses BAR bound */
        case 7:
            plb_put32(c + 0x10, 0x90000000);
            break; /* outside aperture */
        case 8:
            c[0x52] = 16;
            break; /* missing notify multiplier */
        case 9:
            plb_put16(c + 4, 0);
            break; /* DMA disabled */
        case 10:
            c[0x65] = 0xff;
            break; /* bad next capability */
        }
        assert(!plb_vpci_parse(c, 0x02000000, 0x02000000, &found));
    }
    make_config(c);
    plb_put32(c + 0x10, 0xe0000004);
    plb_put32(c + 0x14, 0);
    assert(plb_vpci_parse(c, 0xe0000000, 0xff20000000ULL, &found));
    c[0x44] = 1;
    plb_put32(c + 0x14, 0x02000000);
    assert(!plb_vpci_window(c, 0x40, 0x02000000, 0x02000000, &found.common));
    assert(!plb_in_window(UINT64_MAX - 1, 8, UINT64_MAX - 4, 4));
    assert(!plb_in_window(0x9ffff000, 8192, 0x9e000000, 0x2000000));
    uint64_t base, span;
    assert(plb_mapping_span(0xfffffff02c010000ULL, 56, 4096, &base, &span));
    assert(base == 0xfffffff02c010000ULL && span == 4096);
    assert(plb_mapping_span(0x10023, 4096, 4096, &base, &span));
    assert(base == 0x10000 && span == 8192);
    assert(!plb_mapping_span(UINT64_MAX - 1, 16, 4096, &base, &span));
    assert(!plb_mapping_span(0x1000, 1, 3000, &base, &span));
}
struct fake {
    uint8_t registers[56], dma[8192];
    uint32_t feature_select;
    uint64_t ticks;
    unsigned writes, requests, error;
    bool readonly;
};
static uint64_t fake_read(void *p, unsigned region, unsigned offset, unsigned width) {
    struct fake *f = p;
    if (region == PLB_DEVICE)
        return offset == 0 ? 4096 : 0;
    if (offset == 4)
        return f->feature_select ? 3 : f->readonly ? 32 : 0;
    if (offset == 24)
        return 8;
    if (width == 1)
        return f->registers[offset];
    if (width == 2)
        return plb_le16(f->registers + offset);
    return plb_le32(f->registers + offset);
}
static void fake_write(void *p, unsigned region, unsigned offset, unsigned width, uint64_t v) {
    struct fake *f = p;
    f->writes++;
    if (region == PLB_NOTIFY) {
        assert(width == 2 && v == 0);
        assert(plb_le32(f->dma + 544) == 0); /* Never sends a write operation. */
        assert(plb_le32(f->dma + 8) == 16 && plb_le16(f->dma + 12) == 1);
        assert(plb_le16(f->dma + 28) == 3 && plb_le16(f->dma + 44) == 2);
        assert(plb_le32(f->dma + 24) == 4096 && plb_le32(f->dma + 40) == 1);
        assert(plb_le32(f->dma + 16) == 0x9fff1000);
        f->requests++;
        if (f->error == 1)
            return; /* no completion */
        uint32_t sector = plb_le32(f->dma + 552);
        for (unsigned n = 0; n < 4096; n++)
            f->dma[4096 + n] = (uint8_t)(sector * 512 + n);
        f->dma[512] = f->error == 2 ? 1 : 0;
        uint16_t available = plb_le16(f->dma + 130);
        unsigned used = 260 + (unsigned)((available - 1) % 8) * 8;
        plb_put32(f->dma + used, f->error == 3 ? 7 : 0);
        plb_put32(f->dma + used + 4, f->error == 4 ? 2 : 4097);
        plb_put16(f->dma + 258, available);
        return;
    }
    if (offset == 0)
        f->feature_select = (uint32_t)v;
    if (width == 1)
        f->registers[offset] = (uint8_t)v;
    else if (width == 2)
        plb_put16(f->registers + offset, (uint16_t)v);
    else
        plb_put32(f->registers + offset, (uint32_t)v);
    if (offset == 20 && !v)
        memset(f->registers, 0, sizeof(f->registers));
}
static uint64_t fake_time(void *p) {
    return ((struct fake *)p)->ticks++;
}
static void fake_barrier(void *p) {
    (void)p;
}
static struct plb_vpci_queue make_queue(struct fake *f) {
    struct plb_vpci_queue q = {0};
    q.io = (struct plb_vpci_io){f, fake_read, fake_write, fake_time, fake_barrier};
    q.dma = f->dma;
    q.dma_physical = 0x9fff0000;
    q.dma_bytes = sizeof(f->dma);
    q.notify_bytes = 4096;
    q.notify_multiplier = 4;
    return q;
}
int main(void) {
    parser_tests();
    struct fake f = {.readonly = true};
    struct plb_vpci_queue q = make_queue(&f);
    uint64_t capacity = 0;
    assert(plb_vpci_capacity(&q.io, &capacity) && capacity == 4096);
    assert(plb_vpci_start(&q));
    for (unsigned page = 0; page < 512; page++) {
        assert(plb_vpci_read_page(&q, page * 8));
        for (unsigned i = 0; i < 4096; i++)
            assert(f.dma[4096 + i] == (uint8_t)i);
    }
    assert(f.requests == 512 && !plb_vpci_read_page(&q, 4089));
    assert(plb_vpci_reset(&q) && !q.ready);
    for (unsigned fault = 1; fault <= 4; fault++) {
        memset(&f, 0, sizeof(f));
        f.readonly = true;
        q = make_queue(&f);
        assert(plb_vpci_start(&q));
        f.error = fault;
        assert(!plb_vpci_read_page(&q, 0));
        assert(!q.ready);
        unsigned requests = f.requests;
        assert(!plb_vpci_read_page(&q, 0) && f.requests == requests);
        assert(plb_vpci_reset(&q));
    }
    memset(&f, 0, sizeof(f));
    q = make_queue(&f);
    assert(!plb_vpci_start(&q)); /* Refuse a writable backend. */
    f.readonly = true;
    f.writes = 0;
    q.dma_physical = 0x84404000;
    assert(!plb_vpci_start(&q) && f.writes == 0); /* Never submit private guest RAM. */
    puts("virtio_pci_block: capability bounds, read-only DMA, ring wrap and fault controls passed");
}
