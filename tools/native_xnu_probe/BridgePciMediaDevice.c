/* MIT. Persistent read-only PCI transport for isolated storage diagnostics.
 * Host-visible descriptors/data remain entirely in the exclusive shared tail.
 */
#define PLB_PCI_RECEIPT_PREFIX "PLB_XNU_MEDIA"
#include "xnu_pci_support.h"
#include "BridgePciMediaDevice.h"

#ifndef PLB_PCI_DATA_BYTES
#define PLB_PCI_DATA_BYTES 2097152
#endif
#if PLB_PCI_DATA_BYTES != 2097152 && PLB_PCI_DATA_BYTES != 10485760
#error "Only the synthetic2MiB or pinned AppOS10MiB diagnostic is supported"
#endif

static struct {
    volatile uint8_t *config;
    struct bridge_mappings mappings;
    struct plb_vpci_queue queue;
    bool attempted;
    bool online;
    bool touched_queue;
    uint64_t requests;
} device;

bool plb_media_device_close(void) {
    device.online = false;
    bool reset = !device.touched_queue || plb_vpci_reset(&device.queue);
    if (!reset) {
        /* Do not recycle shared DMA or MMIO while the device may still use it.
         * The diagnostic will power off its own guest; its VMM owns teardown. */
        return false;
    }
    if (device.queue.dma) {
        unmap_region(device.queue.dma, device.queue.dma_bytes);
        device.queue.dma = NULL;
    }
    for (unsigned i = 0; i < 3; i++) {
        if (device.mappings.regions[i]) {
            unmap_region(device.mappings.regions[i], device.mappings.sizes[i]);
            device.mappings.regions[i] = NULL;
        }
    }
    if (device.config) {
        unmap_region(device.config, 0x10000);
        device.config = NULL;
    }
    device.touched_queue = false;
    return true;
}

bool plb_media_device_open(void) {
    if (device.attempted) {
        return false;
    }
    device.attempted = true;
    uint64_t cam[2], windows[4], shared[2];
    if (!dt_values("reg", cam, sizeof(cam)) ||
        !dt_values("pci-apertures", windows, sizeof(windows)) ||
        !dt_values("shared-probe", shared, sizeof(shared)) || cam[0] != 0x10000 ||
        cam[1] != 0x10000 || windows[0] != 0x02000000 || windows[1] != 0x02000000 ||
        windows[2] != 0xe0000000 || windows[3] != 0xff20000000ULL || shared[0] != 0x9fff0000 ||
        shared[1] != 8192) {
        return false;
    }
    device.config = map_region(cam[0], cam[1]);
    if (!device.config) {
        return false;
    }
    unsigned blocks = 0, selected = 0;
    struct plb_vpci_caps chosen = {0};
    for (unsigned function = 0; function < 256; function++) {
        uint8_t snapshot[256];
        for (unsigned i = 0; i < sizeof(snapshot); i++) {
            snapshot[i] = device.config[function * 256 + i];
        }
        if (plb_le16(snapshot) != 0x1af4 ||
            (plb_le16(snapshot + 2) != 0x1042 && plb_le16(snapshot + 2) != 0x1001)) {
            continue;
        }
        blocks++;
        struct plb_vpci_caps caps;
        if (!plb_vpci_parse(snapshot, windows[0], windows[1], &caps) &&
            !plb_vpci_parse(snapshot, windows[2], windows[3], &caps)) {
            goto fail;
        }
        struct bridge_mappings mapped = {0};
        struct plb_pci_window regions[3] = {caps.common, caps.notify, caps.device};
        bool valid = true;
        for (unsigned i = 0; i < 3; i++) {
            mapped.regions[i] = map_region(regions[i].address, regions[i].bytes);
            mapped.sizes[i] = regions[i].bytes;
            if (!mapped.regions[i]) {
                valid = false;
            }
        }
        uint64_t sectors = 0;
        struct plb_vpci_io io = {&mapped, region_read, region_write, elapsed_ms, io_barrier};
        valid = valid && plb_vpci_capacity(&io, &sectors) && !mapped.invalid;
        receipt("candidate", function, sectors);
        if (valid && sectors == PLB_PCI_DATA_BYTES / 512 && selected == 0) {
            selected++;
            device.mappings = mapped;
            chosen = caps;
        } else {
            for (unsigned i = 0; i < 3; i++) {
                if (mapped.regions[i]) {
                    unmap_region(mapped.regions[i], mapped.sizes[i]);
                }
            }
            if (!valid || sectors != 131072) {
                goto fail;
            }
        }
    }
    if (blocks != 2 || selected != 1) {
        goto fail;
    }
    device.queue.io =
        (struct plb_vpci_io){&device.mappings, region_read, region_write, elapsed_ms, io_barrier};
    device.queue.dma = map_region(shared[0], shared[1]);
    device.queue.dma_physical = shared[0];
    device.queue.dma_bytes = (uint32_t)shared[1];
    device.queue.notify_bytes = chosen.notify.bytes;
    device.queue.notify_multiplier = chosen.notify_multiplier;
    if (!device.queue.dma) {
        goto fail;
    }
    device.touched_queue = true;
    if (!plb_vpci_start(&device.queue) || device.mappings.invalid ||
        device.queue.capacity_sectors != PLB_PCI_DATA_BYTES / 512) {
        goto fail;
    }
    device.online = true;
    receipt("queue_ready", shared[0], shared[1]);
    return true;
fail:
    (void)plb_media_device_close();
    return false;
}

bool plb_media_device_read(uint64_t offset, uint8_t destination[4096]) {
    if (!device.online || !destination || (offset & 4095) || offset > PLB_PCI_DATA_BYTES - 4096) {
        return false;
    }
    device.requests++;
    if (!plb_vpci_read_page(&device.queue, offset / 512) || device.mappings.invalid) {
        device.online = false;
        return false;
    }
    for (unsigned i = 0; i < 4096; i++) {
        destination[i] = device.queue.dma[4096 + i];
    }
    return true;
}

uint64_t plb_media_device_requests(void) {
    return device.requests;
}
