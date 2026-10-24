/* MIT. Guest-only mappings shared by the two isolated synthetic-disk diagnostics. */
#ifndef PLB_XNU_PCI_SUPPORT_H
#define PLB_XNU_PCI_SUPPORT_H
#ifndef PLB_PCI_RECEIPT_PREFIX
#define PLB_PCI_RECEIPT_PREFIX "PLB_XNU_PCI"
#endif
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
    kprintf(PLB_PCI_RECEIPT_PREFIX " nonce=" PLB_PCI_NONCE " phase=%s a=%llu b=%llu\n", phase, a,
            b);
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

#endif
