/* MIT. Guest-only PCI/static-DMA readback. No filesystem, GPU or host-memory peek.
 * The only accepted payload is the new 2 MiB nonce-pattern read-only disk.
 */
#include <kern/clock.h>
#include <kern/debug.h>
#include <kern/startup.h>
#include <machine/machine_routines.h>
#include <vm/memory_types.h>
#include <mach/vm_param.h>
#include <pexpert/device_tree.h>
#include <pexpert/pexpert.h>
#include "virtio_pci_block.h"
#include "pci_contract.generated.h"

struct bridge_mappings {
    volatile uint8_t *regions[3];
    uint32_t sizes[3];
    bool invalid;
};

static volatile uint8_t *map_region(uint64_t physical, uint64_t bytes) {
    return (volatile uint8_t *)ml_io_map_unmappable(physical, bytes, VM_WIMG_IO);
}

static void unmap_region(volatile uint8_t *address, uint64_t bytes) {
    uint64_t base, span;
    if (plb_mapping_span((uintptr_t)address, bytes, PAGE_SIZE, &base, &span)) {
        ml_io_unmap((vm_offset_t)base, (vm_size_t)span);
    }
}

static uint64_t region_read(void *context, unsigned region, unsigned offset, unsigned width) {
    struct bridge_mappings *m = context;
    if ((width != 1 && width != 2 && width != 4) || region >= 3 || width > m->sizes[region] ||
        offset > m->sizes[region] - width || (offset & (width - 1))) {
        m->invalid = true;
        return UINT64_MAX;
    }
    uintptr_t at = (uintptr_t)m->regions[region] + offset;
    if (width == 1)
        return *(volatile uint8_t *)at;
    if (width == 2)
        return *(volatile uint16_t *)at;
    if (width == 4)
        return *(volatile uint32_t *)at;
    m->invalid = true;
    return UINT64_MAX;
}

static void region_write(void *context, unsigned region, unsigned offset, unsigned width,
                         uint64_t v) {
    struct bridge_mappings *m = context;
    if ((width != 1 && width != 2 && width != 4) || region >= 3 || width > m->sizes[region] ||
        offset > m->sizes[region] - width || (offset & (width - 1))) {
        m->invalid = true;
        return;
    }
    uintptr_t at = (uintptr_t)m->regions[region] + offset;
    if (width == 1)
        *(volatile uint8_t *)at = (uint8_t)v;
    else if (width == 2)
        *(volatile uint16_t *)at = (uint16_t)v;
    else if (width == 4)
        *(volatile uint32_t *)at = (uint32_t)v;
    else
        m->invalid = true;
    __asm__ volatile("dsb oshst" ::: "memory");
}

static uint64_t elapsed_ms(void *context) {
    (void)context;
    uint64_t nanos;
    absolutetime_to_nanoseconds(mach_absolute_time(), &nanos);
    return nanos / 1000000;
}
static void io_barrier(void *context) {
    (void)context;
    __asm__ volatile("dmb osh" ::: "memory");
}

static void receipt(const char *phase, uint64_t a, uint64_t b) {
    kprintf("PLB_XNU_PCI nonce=" PLB_PCI_NONCE " phase=%s a=%llu b=%llu\n", phase, a, b);
}

static bool dt_values(const char *name, void *values, unsigned bytes) {
    DTEntry node = NULL;
    const void *data = NULL;
    unsigned length = 0;
    if (SecureDTFindEntry("name", "bridge-pci", &node) != kSuccess ||
        SecureDTGetProperty(node, name, &data, &length) != kSuccess || length != bytes) {
        return false;
    }
    memcpy(values, data, bytes);
    return true;
}

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
