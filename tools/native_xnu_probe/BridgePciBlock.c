/* MIT. Isolated full synthetic-disk readback; never an iOS filesystem boot. */
#include "xnu_pci_support.h"

static bool run_probe(void) {
    uint64_t cam[2], windows[4], shared[2];
    if (!dt_values("reg", cam, sizeof(cam)) ||
        !dt_values("pci-apertures", windows, sizeof(windows)) ||
        !dt_values("shared-probe", shared, sizeof(shared)) || cam[0] != 0x10000 ||
        cam[1] != 0x10000 || windows[0] != 0x02000000 || windows[1] != 0x02000000 ||
        windows[2] != 0xe0000000 || windows[3] != 0xff20000000ULL || shared[0] != 0x9fff0000 ||
        shared[1] != 8192) {
        receipt("bad_dt", 0, 0);
        return false;
    }
    volatile uint8_t *config = map_region(cam[0], cam[1]);
    if (config == NULL)
        return false;
    struct bridge_mappings selected = {0};
    struct plb_vpci_caps selected_caps = {0};
    unsigned found = 0, blocks = 0;
    bool success = false;
    for (unsigned function = 0; function < 256; function++) {
        uint8_t snapshot[256];
        for (unsigned i = 0; i < sizeof(snapshot); i++)
            snapshot[i] = config[function * 256 + i];
        if (plb_le16(snapshot) != 0x1af4 ||
            (plb_le16(snapshot + 2) != 0x1042 && plb_le16(snapshot + 2) != 0x1001))
            continue;
        blocks++;
        struct plb_vpci_caps caps;
        if (!plb_vpci_parse(snapshot, windows[0], windows[1], &caps) &&
            !plb_vpci_parse(snapshot, windows[2], windows[3], &caps)) {
            receipt("bad_caps", function, 0);
            goto done;
        }
        struct bridge_mappings mapped = {0};
        struct plb_pci_window regions[3] = {caps.common, caps.notify, caps.device};
        for (unsigned i = 0; i < 3; i++) {
            mapped.regions[i] = map_region(regions[i].address, regions[i].bytes);
            mapped.sizes[i] = regions[i].bytes;
            if (mapped.regions[i] == NULL) {
                for (unsigned j = 0; j < i; j++)
                    unmap_region(mapped.regions[j], mapped.sizes[j]);
                goto done;
            }
        }
        struct plb_vpci_io io = {&mapped, region_read, region_write, elapsed_ms, io_barrier};
        uint64_t sectors = 0;
        bool valid = plb_vpci_capacity(&io, &sectors) && !mapped.invalid;
        receipt("candidate", function, sectors);
        if (valid && sectors == 4096) {
            if (found++) {
                for (unsigned j = 0; j < 3; j++)
                    unmap_region(mapped.regions[j], mapped.sizes[j]);
                goto done;
            }
            selected = mapped;
            selected_caps = caps;
        } else {
            for (unsigned j = 0; j < 3; j++)
                unmap_region(mapped.regions[j], mapped.sizes[j]);
            if (!valid || sectors != 131072)
                goto done;
        }
    }
    if (blocks != 2 || found != 1) {
        receipt("bad_devices", blocks, found);
        goto done;
    }
    struct plb_vpci_queue queue = {0};
    queue.io = (struct plb_vpci_io){&selected, region_read, region_write, elapsed_ms, io_barrier};
    queue.dma = map_region(shared[0], shared[1]);
    queue.dma_physical = shared[0];
    queue.dma_bytes = (uint32_t)shared[1];
    queue.notify_bytes = selected_caps.notify.bytes;
    queue.notify_multiplier = selected_caps.notify_multiplier;
    if (queue.dma == NULL)
        goto done;
    bool ready = plb_vpci_start(&queue) && !selected.invalid;
    if (ready)
        receipt("queue_ready", queue.dma_physical, queue.dma_bytes);
    uint64_t digest = 14695981039346656037ULL;
    for (unsigned offset = 0; ready && offset < 2097152; offset += 4096) {
        if (!plb_vpci_read_page(&queue, offset / 512) || selected.invalid) {
            receipt("read_failed", offset, 0);
            ready = false;
            break;
        }
        for (unsigned i = 0; i < 4096; i++) {
            unsigned at = offset + i;
            uint8_t expected = (uint8_t)(at * 17 + (at >> 9) * 29 + plb_pci_seed[at % 16]);
            uint8_t actual = queue.dma[4096 + i];
            if (actual != expected) {
                receipt("mismatch", at, actual);
                ready = false;
                break;
            }
            digest = (digest ^ actual) * 1099511628211ULL;
        }
    }
    bool reset = plb_vpci_reset(&queue);
    if (ready && reset && !selected.invalid) {
        receipt("read_pass", 2097152, digest);
        success = true;
    } else {
        receipt("reset_or_read_failed", reset, selected.invalid);
    }
    /* On failed reset, do not recycle mappings while DMA might still be live. */
    if (reset)
        unmap_region(queue.dma, shared[1]);
done:
    for (unsigned i = 0; i < 3; i++) {
        if (selected.regions[i])
            unmap_region(selected.regions[i], selected.sizes[i]);
    }
    unmap_region(config, cam[1]);
    return success;
}

static void bridge_pci_probe(void) {
    unsigned enabled = 0;
    if (!PE_parse_boot_argn("bridge-pci-probe", &enabled, sizeof(enabled)) || enabled != 1)
        return;
    receipt("entry", 0, 0);
    bool pass = run_probe();
    receipt(pass ? "pass" : "failed", 0, 0);
    receipt("guest_shutdown", 0, 0);
    /* FDT declares HVC PSCI. This powers off only this guest, never Android. */
    register uint64_t command __asm__("x0") = 0x84000008;
    __asm__ volatile("hvc #0" : "+r"(command) : : "memory");
    for (;;)
        __asm__ volatile("wfe");
}
STARTUP(EARLY_BOOT, STARTUP_RANK_LAST, bridge_pci_probe);
