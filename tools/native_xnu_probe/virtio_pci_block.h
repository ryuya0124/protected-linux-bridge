/* MIT. Read-only modern VirtIO PCI transport for a bounded synthetic guest disk.
 * Protocol offsets follow VirtIO 1.2, sections 4.1 and 5.2. No host PCI access.
 * Parsing is independent of XNU so malformed capabilities can be tested offline.
 */
#ifndef PLB_NATIVE_VIRTIO_PCI_BLOCK_H
#define PLB_NATIVE_VIRTIO_PCI_BLOCK_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct plb_pci_window {
    uint64_t address;
    uint32_t bytes;
};
struct plb_vpci_caps {
    struct plb_pci_window common, notify, device;
    uint32_t notify_multiplier;
};

static uint16_t plb_le16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | (uint16_t)p[1] << 8);
}
static uint32_t plb_le32(const uint8_t *p) {
    return (uint32_t)plb_le16(p) | (uint32_t)plb_le16(p + 2) << 16;
}
static void plb_put16(volatile uint8_t *p, uint16_t value) {
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}
static void plb_put32(volatile uint8_t *p, uint32_t value) {
    plb_put16(p, (uint16_t)value);
    plb_put16(p + 2, (uint16_t)(value >> 16));
}
static void plb_put64(volatile uint8_t *p, uint64_t value) {
    plb_put32(p, (uint32_t)value);
    plb_put32(p + 4, (uint32_t)(value >> 32));
}
static uint16_t plb_volatile_le16(const volatile uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | (uint16_t)p[1] << 8);
}
static uint32_t plb_volatile_le32(const volatile uint8_t *p) {
    return (uint32_t)plb_volatile_le16(p) | (uint32_t)plb_volatile_le16(p + 2) << 16;
}

static bool plb_in_window(uint64_t address, uint64_t bytes, uint64_t base, uint64_t size) {
    return bytes && address >= base && address - base < size && bytes <= size - (address - base);
}

/* ml_io_unmap requires the original page-rounded kernel-map allocation, not
 * a capability's byte length or a permanent io_submap allocation. */
static bool plb_mapping_span(uint64_t address, uint64_t bytes, uint64_t page, uint64_t *base,
                             uint64_t *span) {
    if (!page || (page & (page - 1)) || !bytes || bytes > UINT64_MAX - address) {
        return false;
    }
    uint64_t offset = address & (page - 1);
    if (bytes > UINT64_MAX - offset || bytes + offset > UINT64_MAX - (page - 1)) {
        return false;
    }
    *base = address - offset;
    *span = (bytes + offset + page - 1) & ~(page - 1);
    return *span <= UINT64_MAX - *base;
}

/* Accept only a capability wholly inside its reviewed 32 KiB crosvm BAR and
 * the FDT's guest PCI aperture. Never size a BAR by writing all ones into it. */
static bool plb_vpci_window(const uint8_t config[256], unsigned cap, uint64_t aperture,
                            uint64_t aperture_bytes, struct plb_pci_window *window) {
    unsigned bar = config[cap + 4];
    if (bar >= 6) {
        return false;
    }
    for (unsigned previous = 0; previous < bar; previous++) {
        uint32_t value = plb_le32(config + 0x10 + 4 * previous);
        if (!(value & 1) && ((value >> 1) & 3) == 2) {
            if (previous + 1 == bar) {
                return false; /* The upper half of a 64-bit BAR is not a BAR. */
            }
            previous++;
        }
    }
    uint32_t low = plb_le32(config + 0x10 + 4 * bar);
    if ((low & 1) || ((low >> 1) & 3) == 1 || ((low >> 1) & 3) == 3) {
        return false;
    }
    uint64_t base = low & ~15U;
    if (((low >> 1) & 3) == 2) {
        if (bar == 5) {
            return false;
        }
        base |= (uint64_t)plb_le32(config + 0x14 + 4 * bar) << 32;
    }
    uint32_t offset = plb_le32(config + cap + 8), bytes = plb_le32(config + cap + 12);
    if (!base || offset > 32768 || !bytes || bytes > 32768 - offset || base > UINT64_MAX - offset ||
        !plb_in_window(base + offset, bytes, aperture, aperture_bytes)) {
        return false;
    }
    window->address = base + offset;
    window->bytes = bytes;
    return true;
}

static bool plb_vpci_parse(const uint8_t config[256], uint64_t aperture, uint64_t aperture_bytes,
                           struct plb_vpci_caps *out) {
    struct plb_vpci_caps found = {0};
    uint16_t device = plb_le16(config + 2);
    if (plb_le16(config) != 0x1af4 || (device != 0x1042 && device != 0x1001) ||
        !(plb_le16(config + 6) & 0x10) || (config[0x0e] & 0x7f) != 0 ||
        (plb_le16(config + 4) & 6) != 6) {
        return false;
    }
    bool visited[256] = {false};
    unsigned cap = config[0x34];
    for (unsigned count = 0; cap && count < 48; count++) {
        if (cap < 0x40 || cap > 0xfc || (cap & 3) || visited[cap]) {
            return false;
        }
        visited[cap] = true;
        if (config[cap] == 9) {
            unsigned length = config[cap + 2], kind = config[cap + 3];
            if (length < 16 || length > 256 - cap) {
                return false;
            }
            struct plb_pci_window *window = NULL;
            if (kind == 1) {
                window = &found.common;
            } else if (kind == 2) {
                window = &found.notify;
            } else if (kind == 4) {
                window = &found.device;
            }
            if (window && (window->bytes ||
                           !plb_vpci_window(config, cap, aperture, aperture_bytes, window))) {
                return false;
            }
            if (kind == 2) {
                if (length < 20) {
                    return false;
                }
                found.notify_multiplier = plb_le32(config + cap + 16);
            }
        }
        cap = config[cap + 1];
    }
    if (cap || found.common.bytes < 56 || found.notify.bytes < 2 || found.device.bytes < 8 ||
        (found.common.address & 3) || (found.device.address & 3) || (found.notify.address & 1)) {
        return false;
    }
    *out = found;
    return true;
}

/* All callbacks operate on guest virtual mappings, never host physical memory. */
struct plb_vpci_io {
    void *context;
    uint64_t (*read)(void *, unsigned region, unsigned offset, unsigned width);
    void (*write)(void *, unsigned region, unsigned offset, unsigned width, uint64_t);
    uint64_t (*milliseconds)(void *);
    void (*barrier)(void *);
};
struct plb_vpci_queue {
    struct plb_vpci_io io;
    volatile uint8_t *dma;
    uint64_t dma_physical;
    uint64_t capacity_sectors;
    uint32_t dma_bytes;
    uint32_t notify_bytes, notify_multiplier, notify_offset;
    uint16_t available;
    bool ready;
};
enum { PLB_COMMON = 0, PLB_NOTIFY = 1, PLB_DEVICE = 2 };

static bool plb_vpci_capacity(struct plb_vpci_io *io, uint64_t *sectors) {
    for (unsigned n = 0; n < 8; n++) {
        uint64_t before = io->read(io->context, PLB_COMMON, 21, 1);
        uint64_t low = io->read(io->context, PLB_DEVICE, 0, 4);
        uint64_t high = io->read(io->context, PLB_DEVICE, 4, 4);
        if (before == io->read(io->context, PLB_COMMON, 21, 1)) {
            *sectors = low | high << 32;
            return *sectors > 0 && *sectors <= UINT64_MAX / 512;
        }
    }
    return false;
}

static bool plb_vpci_reset(struct plb_vpci_queue *q) {
    q->ready = false;
    q->io.write(q->io.context, PLB_COMMON, 20, 1, 0);
    uint64_t start = q->io.milliseconds(q->io.context);
    for (unsigned polls = 0; polls < 1000000; polls++) {
        if (q->io.read(q->io.context, PLB_COMMON, 20, 1) == 0) {
            return true;
        }
        if (q->io.milliseconds(q->io.context) - start >= 100) {
            return false;
        }
    }
    return false;
}

static bool plb_vpci_start(struct plb_vpci_queue *q) {
    if (!q->dma || q->dma_bytes < 8192 || (q->dma_physical & 4095) ||
        !plb_in_window(q->dma_physical, q->dma_bytes, 0x9e000000, 0x2000000) ||
        !plb_vpci_reset(q)) {
        return false;
    }
    struct plb_vpci_io *io = &q->io;
    io->write(io->context, PLB_COMMON, 20, 1, 1);
    io->write(io->context, PLB_COMMON, 20, 1, 3);
    io->write(io->context, PLB_COMMON, 0, 4, 0);
    uint32_t low = (uint32_t)io->read(io->context, PLB_COMMON, 4, 4);
    io->write(io->context, PLB_COMMON, 0, 4, 1);
    uint32_t high = (uint32_t)io->read(io->context, PLB_COMMON, 4, 4);
    if (!(low & (1U << 5)) || !(high & 1)) {
        return false;
    }
    io->write(io->context, PLB_COMMON, 8, 4, 0);
    io->write(io->context, PLB_COMMON, 12, 4, 1U << 5);
    io->write(io->context, PLB_COMMON, 8, 4, 1);
    /* ACCESS_PLATFORM uses the established static shared DMA bus addresses. */
    io->write(io->context, PLB_COMMON, 12, 4, high & 3);
    io->write(io->context, PLB_COMMON, 20, 1, 11);
    if (!(io->read(io->context, PLB_COMMON, 20, 1) & 8)) {
        return false;
    }
    if (!plb_vpci_capacity(io, &q->capacity_sectors) || q->capacity_sectors < 8) {
        return false;
    }
    io->write(io->context, PLB_COMMON, 22, 2, 0);
    if (io->read(io->context, PLB_COMMON, 24, 2) < 8 ||
        io->read(io->context, PLB_COMMON, 28, 2) != 0) {
        return false;
    }
    uint64_t notify = io->read(io->context, PLB_COMMON, 30, 2) * q->notify_multiplier;
    if (notify & 1 || notify > q->notify_bytes || q->notify_bytes - notify < 2) {
        return false;
    }
    q->notify_offset = (uint32_t)notify;
    for (unsigned n = 0; n < q->dma_bytes; n++) {
        q->dma[n] = 0;
    }
    plb_put16(q->dma + 128, 1); /* VRING_AVAIL_F_NO_INTERRUPT. */
    io->write(io->context, PLB_COMMON, 16, 2, 0xffff);
    io->write(io->context, PLB_COMMON, 26, 2, 0xffff);
    io->write(io->context, PLB_COMMON, 24, 2, 8);
    for (unsigned n = 0; n < 3; n++) {
        uint64_t address = q->dma_physical + (n == 0 ? 0 : n == 1 ? 128 : 256);
        io->write(io->context, PLB_COMMON, 32 + n * 8, 4, (uint32_t)address);
        io->write(io->context, PLB_COMMON, 36 + n * 8, 4, address >> 32);
    }
    io->barrier(io->context);
    io->write(io->context, PLB_COMMON, 28, 2, 1);
    io->write(io->context, PLB_COMMON, 20, 1, 15);
    q->available = 0;
    q->ready = io->read(io->context, PLB_COMMON, 20, 1) == 15;
    return q->ready;
}

static bool plb_vpci_read(struct plb_vpci_queue *q, uint64_t sector, uint32_t bytes) {
    if (!q->ready || !bytes || bytes > 4096 || (bytes & 511) || q->capacity_sectors < bytes / 512 ||
        sector > q->capacity_sectors - bytes / 512) {
        return false;
    }
    volatile uint8_t *p = q->dma;
    plb_put32(p + 544, 0); /* VIRTIO_BLK_T_IN; there is no write-request API. */
    plb_put32(p + 548, 0);
    plb_put64(p + 552, sector);
    p[512] = 0xff;
    const uint64_t addresses[3] = {q->dma_physical + 544, q->dma_physical + 4096,
                                   q->dma_physical + 512};
    const uint32_t lengths[3] = {16, bytes, 1};
    for (unsigned n = 0; n < 3; n++) {
        plb_put64(p + 16 * n, addresses[n]);
        plb_put32(p + 16 * n + 8, lengths[n]);
        plb_put16(p + 16 * n + 12, (n == 0 ? 0 : 2) | (n == 2 ? 0 : 1));
        plb_put16(p + 16 * n + 14, (uint16_t)(n + 1));
    }
    plb_put16(p + 132 + (q->available % 8) * 2, 0);
    q->available++;
    q->io.barrier(q->io.context);
    plb_put16(p + 130, q->available);
    q->io.barrier(q->io.context);
    q->io.write(q->io.context, PLB_NOTIFY, q->notify_offset, 2, 0);
    uint64_t start = q->io.milliseconds(q->io.context);
    for (unsigned polls = 0; polls < 1000000; polls++) {
        if (plb_volatile_le16(p + 258) == q->available) {
            q->io.barrier(q->io.context);
            unsigned used = 260 + ((uint16_t)(q->available - 1) % 8) * 8;
            bool valid = plb_volatile_le32(p + used) == 0 &&
                         plb_volatile_le32(p + used + 4) == bytes + 1 && p[512] == 0;
            if (!valid) {
                q->ready = false;
            }
            return valid;
        }
        if (q->io.milliseconds(q->io.context) - start >= 1000) {
            break;
        }
    }
    q->ready = false;
    return false;
}

static bool plb_vpci_read_page(struct plb_vpci_queue *q, uint64_t sector) {
    return plb_vpci_read(q, sector, 4096);
}
#endif
