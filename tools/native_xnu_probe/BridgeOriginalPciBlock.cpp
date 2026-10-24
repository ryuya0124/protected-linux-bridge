/* MIT. Isolated read-only PCI disk behind Apple's original storage family.
 * Only the separate 2 MiB pattern or pinned 10 MiB AppOS fixture is accepted.
 * No public-source storage-family implementation is linked into this variant.
 */
#include <IOKit/IOBSD.h>
#include <IOKit/IODeviceTreeSupport.h>
#include <IOKit/IOLib.h>
#include <IOKit/storage/IOBlockStorageDevice.h>
#include <kern/debug.h>
#include <libkern/c++/OSIterator.h>
#include <libkern/c++/OSData.h>
#include <libkern/c++/OSNumber.h>
#include <libkern/c++/OSString.h>
#include <pexpert/pexpert.h>
#include <sys/conf.h>
#include <sys/fcntl.h>
#include <sys/stat.h>
#include <sys/systm.h>
#include <sys/uio.h>
#include <sys/mount.h>
#include "BridgePciMediaDevice.h"
#include "pci_contract.generated.h"

// sys/systm.h declares this under BSD_KERNEL_PRIVATE, whereas this unit uses
// the actual IOKit compilation recipe. The kernel definition is const int.
extern "C" const int nchrdev;

namespace {
#ifndef PLB_PCI_DATA_BYTES
#define PLB_PCI_DATA_BYTES 2097152
#endif
constexpr uint64_t diskBytes = PLB_PCI_DATA_BYTES;
void receipt(const char *phase, uint64_t a = 0, uint64_t b = 0) {
    kprintf("PLB_XNU_ORIGINAL nonce=" PLB_PCI_NONCE " phase=%s a=%llu b=%llu\n", phase, a, b);
}
#if !defined(PLB_PCI_APFS_FIXTURE)
uint8_t expectedByte(uint64_t offset) {
    return static_cast<uint8_t>(offset * 17 + (offset >> 9) * 29 + plb_pci_seed[offset % 16]);
}
#endif
} // namespace

class BridgeOriginalPciBlock : public IOBlockStorageDevice {
    OSDeclareDefaultStructors(BridgeOriginalPciBlock);
    IOLock *gate = nullptr;
    uint8_t page[4096] = {};
    bool online = false;

  public:
    bool init(OSDictionary *properties = nullptr) override {
        if (!IOBlockStorageDevice::init(properties)) {
            return false;
        }
        gate = IOLockAlloc();
        return gate != nullptr;
    }
    bool start(IOService *provider) override {
        online = IOBlockStorageDevice::start(provider) && plb_media_device_open();
        return online &&
               setProperty(kIOBlockStorageDeviceTypeKey, kIOBlockStorageDeviceTypeGeneric);
    }
    void free() override {
        if (gate) {
            IOLockFree(gate);
        }
        IOBlockStorageDevice::free();
    }
    bool finish() {
        IOLockLock(gate);
        online = false;
        bool reset = plb_media_device_close();
        IOLockUnlock(gate);
        return reset;
    }
    IOReturn doEjectMedia() override {
        return kIOReturnUnsupported;
    }
    IOReturn doFormatMedia(UInt64) override {
        return kIOReturnNotWritable;
    }
    UInt32 doGetFormatCapacities(UInt64 *, UInt32) const override {
        return 0;
    }
    char *getVendorString() override {
        return const_cast<char *>("Bridge");
    }
    char *getProductString() override {
        return const_cast<char *>("Synthetic read-only PCI disk");
    }
    char *getRevisionString() override {
        return const_cast<char *>("1");
    }
    char *getAdditionalDeviceInfoString() override {
        return const_cast<char *>("isolated diagnostic");
    }
    IOReturn reportBlockSize(UInt64 *value) override {
        if (!value) {
            return kIOReturnBadArgument;
        }
        *value = 512;
        return kIOReturnSuccess;
    }
    IOReturn reportMaxValidBlock(UInt64 *value) override {
        if (!value) {
            return kIOReturnBadArgument;
        }
        *value = diskBytes / 512 - 1;
        return kIOReturnSuccess;
    }
    IOReturn reportEjectability(bool *value) override {
        if (!value) {
            return kIOReturnBadArgument;
        }
        *value = false;
        return kIOReturnSuccess;
    }
    IOReturn reportRemovability(bool *value) override {
        return reportEjectability(value);
    }
    IOReturn reportWriteProtection(bool *value) override {
        if (!value) {
            return kIOReturnBadArgument;
        }
        *value = true;
        return kIOReturnSuccess;
    }
    IOReturn reportMediaState(bool *present, bool *changed) override {
        if (!present) {
            return kIOReturnBadArgument;
        }
        *present = online;
        if (changed) {
            *changed = false;
        }
        return kIOReturnSuccess;
    }
    IOReturn getWriteCacheState(bool *value) override {
        return reportEjectability(value);
    }
    IOReturn setWriteCacheState(bool) override {
        return kIOReturnUnsupported;
    }
    IOReturn doSynchronizeCache() override {
        return online ? kIOReturnSuccess : kIOReturnNotReady;
    }
    IOReturn doSynchronize(UInt64, UInt64, IOOptionBits) override {
        return doSynchronizeCache();
    }
    IOReturn doAsyncReadWrite(IOMemoryDescriptor *buffer, UInt64 block, UInt64 count,
                              IOStorageAttributes *, IOStorageCompletion *completion) override {
        if (!buffer || !completion || !completion->action || !count || block >= diskBytes / 512 ||
            count > diskBytes / 512 - block || buffer->getLength() < count * 512) {
            return kIOReturnBadArgument;
        }
        if (buffer->getDirection() != kIODirectionIn) {
            return kIOReturnNotWritable;
        }
        uint64_t done = 0;
        IOReturn status = kIOReturnSuccess;
        IOLockLock(gate);
        if (!online) {
            status = kIOReturnNotReady;
        }
        while (status == kIOReturnSuccess && done < count * 512) {
            uint64_t offset = block * 512 + done;
            unsigned within = static_cast<unsigned>(offset & 4095);
            uint64_t bytes = count * 512 - done;
            if (bytes > 4096 - within) {
                bytes = 4096 - within;
            }
            if (!plb_media_device_read(offset & ~4095ULL, page)) {
                online = false;
                status = kIOReturnIOError;
                break;
            }
            if (buffer->writeBytes(done, page + within, bytes) != bytes) {
                status = kIOReturnVMError;
                break;
            }
            done += bytes;
        }
        IOLockUnlock(gate);
        // The ordinary IOStorage completion ABI was compiled from public headers.
        // Invoke its actual callback; do not import a guessed static helper address.
        completion->action(completion->target, completion->parameter, status, done);
        return kIOReturnSuccess;
    }
};
OSDefineMetaClassAndStructors(BridgeOriginalPciBlock, IOBlockStorageDevice);

#if defined(PLB_PCI_APFS_MOUNT)
namespace {
BridgeOriginalPciBlock *mountBlock = nullptr;
constexpr char appUuid[] = "C0DCFAA8-4E81-49CD-807D-BED3B9B6AE54";

bool selectRoot(IOService *parent, unsigned depth, unsigned &visited, unsigned &matches) {
    if (depth >= 8 || ++visited > 128) {
        return false;
    }
    OSString *identity = OSDynamicCast(OSString, parent->getProperty("UUID"));
    OSString *name = OSDynamicCast(OSString, parent->getProperty(kIOBSDNameKey));
    kprintf("PLB_XNU_REGISTRY nonce=" PLB_PCI_NONCE " depth=%u class=%s bsd=%s uuid=%s\n", depth,
            parent->getMetaClass()->getClassName(), name ? name->getCStringNoCopy() : "-",
            identity ? identity->getCStringNoCopy() : "-");
    if (identity && !strcmp(identity->getCStringNoCopy(), appUuid)) {
        if (!name || !parent->metaCast("IOMedia")) {
            return false;
        }
        matches++;
    }
    OSIterator *iterator = parent->getChildIterator(gIOServicePlane);
    if (!iterator) {
        return true;
    }
    bool pass = true;
    while (OSObject *child = iterator->getNextObject()) {
        IOService *service = OSDynamicCast(IOService, child);
        if (service && !selectRoot(service, depth + 1, visited, matches)) {
            pass = false;
            break;
        }
    }
    iterator->release();
    return pass;
}

bool configureRoot(BridgeOriginalPciBlock *block) {
    unsigned visited = 0, matches = 0;
    if (!selectRoot(block, 0, visited, matches) || matches != 1) {
        receipt("root_selection_failed", matches, visited);
        return false;
    }
    constexpr char matching[] =
        "<dict><key>IOProviderClass</key><string>IOMedia</string>"
        "<key>IOPropertyMatch</key><dict><key>UUID</key>"
        "<string>C0DCFAA8-4E81-49CD-807D-BED3B9B6AE54</string></dict></dict>";
    IORegistryEntry *chosen = IORegistryEntry::fromPath("/chosen", gIODTPlane);
    OSData *data = OSData::withBytes(matching, sizeof(matching));
    bool pass = chosen && data && chosen->setProperty("root-matching", data);
    if (data) {
        data->release();
    }
    if (chosen) {
        chosen->release();
    }
    if (pass) {
        receipt("root_selected", matches, visited);
    }
    return pass;
}

[[noreturn]] void finishMountProbe() {
    bool reset = mountBlock && mountBlock->finish();
    if (reset) {
        receipt("device_reset");
    }
    receipt(reset ? "mount_probe_complete" : "mount_probe_reset_failed");
    receipt("guest_shutdown");
    register uint64_t command __asm__("x0") = 0x84000008;
    __asm__ volatile("hvc #0" : "+r"(command) : : "memory");
    for (;;) {
        __asm__ volatile("wfe");
    }
}
} // namespace

extern "C" void plb_bridge_apfs_mount_result(int error);
extern "C" void plb_bridge_apfs_mount_result(int error) {
    receipt("vfs_mount_result", static_cast<uint64_t>(error), 0);
    if (error) {
        finishMountProbe();
    }
}

extern "C" void plb_bridge_apfs_auth_result(int error, mount_t mounted);
extern "C" void plb_bridge_apfs_auth_result(int error, mount_t mounted) {
    // Called after the real FSIOC_KERNEL_ROOTAUTH operation. Never substitute
    // a successful authentication result or allow user-space to continue.
    bool apfs = mounted && !strcmp(vfs_statfs(mounted)->f_fstypename, "apfs");
    uint64_t readonly = mounted && (vfs_flags(mounted) & MNT_RDONLY) != 0;
    receipt("vfs_apfs_readonly", apfs, readonly);
    receipt("apple_root_auth_result", static_cast<uint64_t>(error), 0);
    finishMountProbe();
}
#endif

namespace {
IOService *findMedia(IOService *parent, unsigned depth = 0) {
    if (depth >= 8) {
        return nullptr;
    }
    OSIterator *children = parent->getChildIterator(gIOServicePlane);
    if (!children) {
        return nullptr;
    }
    IOService *found = nullptr;
    while (OSObject *child = children->getNextObject()) {
        IOService *service = OSDynamicCast(IOService, child);
        if (!service) {
            continue;
        }
        if (service->metaCast("IOMedia") && service->getProperty(kIOBSDNameKey)) {
            service->retain();
            found = service;
            break;
        }
        found = findMedia(service, depth + 1);
        if (found) {
            break;
        }
    }
    children->release();
    return found;
}

bool verifyBsd(IOService *media) {
    OSString *name = OSDynamicCast(OSString, media->getProperty(kIOBSDNameKey));
    OSNumber *mjr = OSDynamicCast(OSNumber, media->getProperty(kIOBSDMajorKey));
    OSNumber *mnr = OSDynamicCast(OSNumber, media->getProperty(kIOBSDMinorKey));
    if (!name || !name->getLength() || !mjr || !mnr || mjr->unsigned64BitValue() > 255 ||
        mnr->unsigned64BitValue() > 0xffffff) {
        return false;
    }
    dev_t device = makedev(mjr->unsigned32BitValue(), mnr->unsigned32BitValue());
    unsigned index = mjr->unsigned32BitValue();
    if (index >= static_cast<unsigned>(nchrdev)) {
        return false;
    }
    // The major is published by the real media BSD client, not a guessed index.
    receipt("bsd_node", index, mnr->unsigned32BitValue());
    if (!cdevsw[index].d_open || !cdevsw[index].d_read || !cdevsw[index].d_close ||
        cdevsw[index].d_open(device, FREAD, S_IFCHR, nullptr)) {
        receipt("bsd_open_failed");
        return false;
    }
    uint8_t *bytes = static_cast<uint8_t *>(IOMalloc(4096));
    bool pass = bytes != nullptr;
    uint64_t digest = 14695981039346656037ULL;
    for (uint64_t offset = 0; pass && offset < diskBytes; offset += 4096) {
        uio_t request = uio_create(1, static_cast<off_t>(offset), UIO_SYSSPACE, UIO_READ);
        pass = request && uio_addiov(request, reinterpret_cast<user_addr_t>(bytes), 4096) == 0;
        int result = pass ? cdevsw[index].d_read(device, request, 0) : -1;
        pass = pass && result == 0 && uio_resid(request) == 0;
        if (request) {
            uio_free(request);
        }
#if defined(PLB_PCI_APFS_FIXTURE)
        if (pass && offset == 0) {
            pass = memcmp(bytes + 32, "NXSB", 4) == 0;
            if (pass) {
                receipt("apfs_container_header", diskBytes, 4096);
            }
        }
#endif
        for (unsigned i = 0; pass && i < 4096; i++) {
#if !defined(PLB_PCI_APFS_FIXTURE)
            pass = bytes[i] == expectedByte(offset + i);
#endif
            digest = (digest ^ bytes[i]) * 1099511628211ULL;
        }
        if (!pass) {
            receipt("bsd_read_failed", offset, static_cast<uint64_t>(result));
        }
    }
#if defined(PLB_PCI_APFS_FIXTURE)
    pass = pass && digest == PLB_PCI_EXPECTED_FNV;
#endif
    if (pass) {
        receipt("bsd_read_pass", diskBytes, digest);
    }
    if (bytes) {
        IOFree(bytes, 4096);
    }
    int closed = cdevsw[index].d_close(device, FREAD, S_IFCHR, nullptr);
    if (!closed) {
        receipt("bsd_closed");
    }
    return pass && closed == 0;
}
} // namespace

extern "C" void plb_bridge_pci_media_probe(void);
extern "C" void plb_bridge_pci_media_probe(void) {
    unsigned enabled = 0;
    if (!PE_parse_boot_argn("bridge-pci-probe", &enabled, sizeof(enabled)) || enabled != 1) {
        return;
    }
    receipt("entry");
    // Normal in-guest metaclass API: never inspect donated RAM from Android.
    unsigned actualSize = IOBlockStorageDevice::gMetaClass.getClassSize();
    receipt("base_class_size", actualSize, sizeof(IOBlockStorageDevice));
    bool pass = actualSize == sizeof(IOBlockStorageDevice) && actualSize == 0x90;
    IOService *root = IOService::getServiceRoot();
    BridgeOriginalPciBlock *block = pass ? new BridgeOriginalPciBlock : nullptr;
    bool initialized = root && block && block->init();
    pass = initialized && block->attach(root) && block->start(root);
    if (pass) {
        receipt("block_started");
        block->setName("Bridge original-family PCI fixture");
        block->registerService(kIOServiceSynchronous);
        IOService *media = findMedia(block);
        pass = media && verifyBsd(media);
        if (media) {
            media->release();
        }
        if (!pass) {
            receipt("media_missing_or_failed");
        }
    }
#if defined(PLB_PCI_APFS_MOUNT)
    if (pass && configureRoot(block)) {
        mountBlock = block;
        // Keep the real read-only device alive for XNU's ordinary mount path.
        // A separate BSD hook records its actual mount/auth result and exits.
        return;
    }
    pass = false;
#endif
    bool reset = initialized ? block->finish() : plb_media_device_close();
    if (reset) {
        receipt("device_reset");
    }
    receipt(pass && reset ? "pass" : "failed");
    receipt("guest_shutdown");
    register uint64_t command __asm__("x0") = 0x84000008;
    __asm__ volatile("hvc #0" : "+r"(command) : : "memory");
    for (;;) {
        __asm__ volatile("wfe");
    }
}
