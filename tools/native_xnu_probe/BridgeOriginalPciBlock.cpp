/* MIT. Isolated read-only PCI disk behind Apple's original storage family.
 * Only separately pinned synthetic, AppOS or main restore OS disks are accepted.
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
#if defined(PLB_PCI_BLOCK_POLICY)
#include <libkern/crypto/sha2.h>
#include "verified_read.h"
#include "verified_hash.h"
#include "root_policy.generated.h"
extern "C" const uint8_t plb_root_hashes[];
extern "C" const uint8_t plb_root_hashes_end[];
#endif

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
#if defined(PLB_PCI_BLOCK_POLICY)
bool rawPolicyRead(void *, uint64_t offset, uint8_t data[4096]) {
    if (!plb_media_device_read(offset, data)) {
        return false;
    }
#if defined(PLB_PCI_BLOCK_FAULT)
    static bool injected = false;
    if (!injected) {
        // Negative control changes only the guest's completed private copy.
        // Never alter the backing image, shared mappings or another VM.
        data[0] ^= 1;
        injected = true;
        receipt("integrity_fault_injected", offset, 1);
    }
#endif
    return true;
}

bool qualifyPolicy() {
    static const uint8_t expectedAbc[32] = {0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea,
                                            0x41, 0x41, 0x40, 0xde, 0x5d, 0xae, 0x22, 0x23,
                                            0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c,
                                            0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad};
    uint8_t digest[32];
    plb_verified_sha256("abc", 3, digest);
    if (memcmp(digest, expectedAbc, sizeof(digest))) {
        return false;
    }
    receipt("sha256_kat", 1, 256);
    uintptr_t begin = reinterpret_cast<uintptr_t>(plb_root_hashes);
    uintptr_t end = reinterpret_cast<uintptr_t>(plb_root_hashes_end);
    if (end < begin || end - begin != PLB_ROOT_TABLE_BYTES || PLB_ROOT_IMAGE_BYTES != diskBytes) {
        return false;
    }
    static const uint8_t expectedTable[32] = PLB_ROOT_TABLE_SHA256_BYTES;
    plb_verified_sha256(plb_root_hashes, PLB_ROOT_TABLE_BYTES, digest);
    if (memcmp(digest, expectedTable, sizeof(digest))) {
        return false;
    }
    receipt("integrity_policy_ready", PLB_VERIFIED_CHUNK_BYTES, PLB_ROOT_TABLE_BYTES / 32);
    return true;
}
#endif
#if !defined(PLB_PCI_APFS_FIXTURE) && !defined(PLB_PCI_MAIN_ROOT)
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
#if defined(PLB_PCI_BLOCK_POLICY)
    plb_verified_state *integrity = nullptr;
    bool integrityReported = false;
#endif

  public:
    bool init(OSDictionary *properties = nullptr) override {
        if (!IOBlockStorageDevice::init(properties)) {
            return false;
        }
        gate = IOLockAlloc();
#if defined(PLB_PCI_BLOCK_POLICY)
        integrity = static_cast<plb_verified_state *>(IOMalloc(sizeof(*integrity)));
        if (!integrity) {
            return false;
        }
        bzero(integrity, sizeof(*integrity));
#endif
        return gate != nullptr;
    }
    bool start(IOService *provider) override {
        online = IOBlockStorageDevice::start(provider) && plb_media_device_open();
#if defined(PLB_PCI_BLOCK_POLICY)
        online = online && qualifyPolicy();
#endif
        return online &&
               setProperty(kIOBlockStorageDeviceTypeKey, kIOBlockStorageDeviceTypeGeneric);
    }
    void free() override {
#if defined(PLB_PCI_BLOCK_POLICY)
        if (integrity) {
            IOFree(integrity, sizeof(*integrity));
        }
#endif
        if (gate) {
            IOLockFree(gate);
        }
        IOBlockStorageDevice::free();
    }
    bool finish() {
        IOLockLock(gate);
        online = false;
#if defined(PLB_PCI_BLOCK_POLICY)
        receipt("integrity_stats", integrity->verified_chunks, integrity->delivered_pages);
        receipt("integrity_failures", integrity->failures, integrity->poisoned);
#endif
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
            bool read;
#if defined(PLB_PCI_BLOCK_POLICY)
            read = plb_verified_read(integrity, diskBytes, plb_root_hashes, PLB_ROOT_TABLE_BYTES,
                                     offset & ~4095ULL, page, rawPolicyRead, nullptr,
                                     plb_verified_hash_snapshot);
            if (!read && !integrityReported) {
                receipt("integrity_rejected", integrity->verified_chunks,
                        integrity->delivered_pages);
                integrityReported = true;
            }
#else
            read = plb_media_device_read(offset & ~4095ULL, page);
#endif
            if (!read) {
                online = false;
#if defined(PLB_PCI_BLOCK_POLICY)
                status = integrity->poisoned ? kIOReturnNotPrivileged : kIOReturnIOError;
#else
                status = kIOReturnIOError;
#endif
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
#if defined(PLB_PCI_MAIN_ROOT)
#define PLB_ROOT_UUID "04769D26-2CA0-4825-B443-E2E5F88300F2"
#else
#define PLB_ROOT_UUID "C0DCFAA8-4E81-49CD-807D-BED3B9B6AE54"
#endif
constexpr char appUuid[] = PLB_ROOT_UUID;

bool selectRoot(IOService *parent, unsigned depth, unsigned &visited, unsigned &matches) {
    // Main restore disks include GPT partition services above APFS. Keep a
    // finite traversal budget without assuming the shallower raw AppOS tree.
    if (depth >= 16 || ++visited > 128) {
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
    constexpr char matching[] = "<dict><key>IOProviderClass</key><string>IOMedia</string>"
                                "<key>IOPropertyMatch</key><dict><key>UUID</key>"
                                "<string>" PLB_ROOT_UUID "</string></dict></dict>";
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
#if defined(PLB_PCI_MAIN_ROOT)
    // Qualify real BSD I/O at both ends, including the 1536-byte final page.
    // The multi-GiB whole image was hashed independently before admission;
    // these bounded reads must never be labelled a whole-disk guest readback.
    constexpr uint64_t readLimit = 4096;
#else
    constexpr uint64_t readLimit = diskBytes;
#endif
    for (uint64_t offset = 0; pass && offset < readLimit; offset += 4096) {
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
#if !defined(PLB_PCI_APFS_FIXTURE) && !defined(PLB_PCI_MAIN_ROOT)
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
#if defined(PLB_PCI_MAIN_ROOT)
    pass = pass && digest == PLB_PCI_EXPECTED_PREFIX_FNV &&
           memcmp(bytes + 512, "EFI PART", 8) == 0 && bytes[510] == 0x55 && bytes[511] == 0xaa;
    if (pass) {
        receipt("bsd_gpt_prefix_pass", 4096, digest);
        constexpr uint64_t tailOffset = diskBytes & ~4095ULL;
        constexpr unsigned tailBytes = diskBytes - tailOffset;
        uio_t request = uio_create(1, static_cast<off_t>(tailOffset), UIO_SYSSPACE, UIO_READ);
        pass = request && uio_addiov(request, reinterpret_cast<user_addr_t>(bytes), tailBytes) == 0;
        int result = pass ? cdevsw[index].d_read(device, request, 0) : -1;
        pass = pass && result == 0 && uio_resid(request) == 0;
        if (request) {
            uio_free(request);
        }
        digest = 14695981039346656037ULL;
        for (unsigned i = 0; pass && i < tailBytes; i++) {
            digest = (digest ^ bytes[i]) * 1099511628211ULL;
        }
        pass = pass && digest == PLB_PCI_EXPECTED_TAIL_FNV;
        if (pass) {
            receipt("bsd_partial_tail_pass", tailBytes, digest);
        } else {
            receipt("bsd_tail_failed", tailOffset, static_cast<uint64_t>(result));
        }
    }
#else
    if (pass) {
        receipt("bsd_read_pass", diskBytes, digest);
    }
#endif
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
